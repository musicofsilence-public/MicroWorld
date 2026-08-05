#include "TestSupport.h"
#include "MessagingWireTestHelpers.h"

#include <MicroWorld/Messaging/Message.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{

using namespace ::MicroWorld::Tests;

/**
 * Motivation: Proves a dropped first reliable packet is retried from stored frame bytes and reaches its peer.
 * Responsibilities: Drop the first send, advance exactly one retry interval, and verify remote delivery plus one acknowledgement.
 */
MW_TEST_CASE(MessagingSystem_RetriesDroppedReliableMessagesAfterTheConfiguredInterval)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> Network;
	FPacketDropDevice DroppingDevice{Network.Port(SendingPort), OneDroppedSend};
	FMessagingSystemInformation Information{};
	Information.ReliableRetryIntervalMilliseconds = ReliableRetryIntervalMilliseconds;
	FMessagingSystem SendingSystem{Information};
	FMessagingSystem ReceivingSystem{Information};
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FChannelInformation SendingChannel{"Telemetry", true, &DroppingDevice, ReceivingAddress};
	const FChannelInformation ReceivingChannel{"Telemetry", true, &Network.Port(ReceivingPort), SendingAddress};
	const EMessagingResult SendingCreateResult = SendingSystem.CreateChannel(SendingChannel);
	const EMessagingResult ReceivingCreateResult = ReceivingSystem.CreateChannel(ReceivingChannel);
	std::size_t ReceivingDeliveryCount = NoDeliveries;
	FSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&ReceivingDeliveryCount](const FMessage&) noexcept { ++ReceivingDeliveryCount; });
	const EMessagingResult SubscribeResult = ReceivingSystem.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	Message.SetPayload(TSpan<const std::uint8_t>(WirePayload, WirePayloadByteCount));

	// Act
	const EMessagingResult SendResult = SendingSystem.SendMessageToChannel(Message, "Telemetry");
	const std::size_t QueuedBeforeRetry = Network.QueuedCount(ReceivingPort);
	SendingSystem.PostAdvance(ReliableRetryTurnMilliseconds);
	ReceivingSystem.PreAdvance(RetriedReceiveTurnMilliseconds);
	const std::size_t QueuedAcknowledgements = Network.QueuedCount(SendingPort);
	const std::size_t SendAttemptCount = DroppingDevice.GetSendAttemptCount();

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendingCreateResult, "The retry sender channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReceivingCreateResult, "The retry receiver channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The retry receiver subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The retry receiver subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The dropped first reliable send should report device acceptance");
	MW_EXPECT_EQ(Test, NoQueuedPackets, QueuedBeforeRetry, "The dropped first attempt should queue no peer packet");
	MW_EXPECT_EQ(Test, OneDelivery, ReceivingDeliveryCount, "The retried reliable frame should reach the peer once");
	MW_EXPECT_EQ(Test, OneQueuedPacket, QueuedAcknowledgements, "The peer should acknowledge the retried reliable frame");
	MW_EXPECT_EQ(Test, TwoReliableSendAttempts, SendAttemptCount, "The sender should make the initial attempt and one retry");
}

/**
 * Motivation: Prevents premature or non-monotonic post-advance calls from creating spurious reliable sends.
 * Responsibilities: Verify a turn inside the interval and an earlier turn both leave the peer mailbox unchanged.
 */
MW_TEST_CASE(MessagingSystem_DoesNotRetryBeforeItsIntervalOrWhenTimeMovesBackwards)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> Network;
	FMessagingSystemInformation Information{};
	Information.ReliableRetryIntervalMilliseconds = ReliableRetryIntervalMilliseconds;
	FMessagingSystem System{Information};
	const FChannelInformation Channel{"Telemetry", true, &Network.Port(SendingPort), MakeLoopbackAddress(ReceivingPort)};
	const EMessagingResult CreateResult = System.CreateChannel(Channel);
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	Message.SetPayload(TSpan<const std::uint8_t>(WirePayload, WirePayloadByteCount));

	// Act
	System.PreAdvance(FirstReceiveTurnMilliseconds);
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, "Telemetry");
	Network.Drain(ReceivingPort);
	System.PostAdvance(FirstReceiveTurnMilliseconds + BeforeReliableRetryOffsetMilliseconds);
	const std::size_t QueuedBeforeInterval = Network.QueuedCount(ReceivingPort);
	System.PostAdvance(BackwardsReliableTurnMilliseconds);
	const std::size_t QueuedAfterBackwardsTime = Network.QueuedCount(ReceivingPort);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The retry-guard channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The retry-guard reliable message should send initially");
	MW_EXPECT_EQ(Test, NoQueuedPackets, QueuedBeforeInterval, "A turn inside the retry interval should not queue a resend");
	MW_EXPECT_EQ(Test, NoQueuedPackets, QueuedAfterBackwardsTime, "Earlier unsigned time should not trigger a resend");
}

/**
 * Motivation: Proves acknowledgement consumption releases a reliable slot before any later retry is due.
 * Responsibilities: Deliver one reliable frame, consume its acknowledgement, then advance far beyond the interval without another send.
 */
MW_TEST_CASE(MessagingSystem_DoesNotRetryAcknowledgedReliableMessages)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> Network;
	FMessagingSystemInformation Information{};
	Information.ReliableRetryIntervalMilliseconds = ReliableRetryIntervalMilliseconds;
	FMessagingSystem SendingSystem{Information};
	FMessagingSystem ReceivingSystem{Information};
	const FChannelInformation SendingChannel{"Telemetry", true, &Network.Port(SendingPort), MakeLoopbackAddress(ReceivingPort)};
	const FChannelInformation ReceivingChannel{"Telemetry", true, &Network.Port(ReceivingPort), MakeLoopbackAddress(SendingPort)};
	const EMessagingResult SendingCreateResult = SendingSystem.CreateChannel(SendingChannel);
	const EMessagingResult ReceivingCreateResult = ReceivingSystem.CreateChannel(ReceivingChannel);
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	Message.SetPayload(TSpan<const std::uint8_t>(WirePayload, WirePayloadByteCount));

	// Act
	const EMessagingResult SendResult = SendingSystem.SendMessageToChannel(Message, "Telemetry");
	ReceivingSystem.PreAdvance(FirstReceiveTurnMilliseconds);
	SendingSystem.PreAdvance(AcknowledgementReceiveTurnMilliseconds);
	SendingSystem.PostAdvance(FarAfterAcknowledgementTurnMilliseconds);
	const std::size_t QueuedAfterAcknowledgement = Network.QueuedCount(ReceivingPort);
	const std::uint32_t DroppedFrameCount = SendingSystem.GetDroppedFrameCount();

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendingCreateResult, "The acknowledgement sender channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReceivingCreateResult, "The acknowledgement receiver channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The acknowledged reliable message should send");
	MW_EXPECT_EQ(Test, NoQueuedPackets, QueuedAfterAcknowledgement, "An acknowledged reliable frame should not resend later");
	MW_EXPECT_EQ(Test, NoDroppedFrames, DroppedFrameCount, "A valid acknowledgement should not count as dropped");
}

/**
 * Motivation: Proves a receive-budget-delayed acknowledgement permits one at-least-once retry without keeping the reliable frame pending.
 * Responsibilities: Consume an unmatched acknowledgement first, retry once, then consume the deferred matching acknowledgement and stop retrying.
 */
MW_TEST_CASE(MessagingSystem_ConsumesAcknowledgementDeferredByReceiveBudget)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, TwoMailboxSlots, StandardPacketBytes> Network;
	FMessagingSystemInformation Information{};
	Information.ReliableRetryIntervalMilliseconds = ReliableRetryIntervalMilliseconds;
	Information.MaxReceiveFramesPerDevicePerAdvance = 1;
	FMessagingSystem SendingSystem{Information};
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FChannelInformation SendingChannel{"Telemetry", true, &Network.Port(SendingPort), MakeLoopbackAddress(ReceivingPort)};
	const EMessagingResult SendingCreateResult = SendingSystem.CreateChannel(SendingChannel);
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	Message.SetPayload(TSpan<const std::uint8_t>(WirePayload, WirePayloadByteCount));
	FRawWireFrame UnmatchedAcknowledgementFrame;
	UnmatchedAcknowledgementFrame.Set(
		"Telemetry", MessageAcknowledgementNameId, TSpan<const std::uint8_t>(UnmatchedAcknowledgementPayload, SequenceNumberBytes));
	FRawWireFrame ExpectedAcknowledgementFrame;
	ExpectedAcknowledgementFrame.Set(
		"Telemetry", MessageAcknowledgementNameId, TSpan<const std::uint8_t>(FirstSequenceAcknowledgementPayload, SequenceNumberBytes));
	constexpr TimePointMilliseconds InitialReliableSendTurnMilliseconds = 0;

	// Act
	const EMessagingResult SendResult = SendingSystem.SendMessageToChannel(Message, "Telemetry");
	const std::size_t QueuedInitialReliableFrameCount = Network.QueuedCount(ReceivingPort);
	Network.Drain(ReceivingPort);
	const ETransportResult UnmatchedAcknowledgementSendResult =
		Network.Port(ReceivingPort)
			.TrySend(SendingAddress, TSpan<const std::uint8_t>(UnmatchedAcknowledgementFrame.Bytes, UnmatchedAcknowledgementFrame.Size));
	const ETransportResult ExpectedAcknowledgementSendResult =
		Network.Port(ReceivingPort)
			.TrySend(SendingAddress, TSpan<const std::uint8_t>(ExpectedAcknowledgementFrame.Bytes, ExpectedAcknowledgementFrame.Size));
	const std::size_t QueuedAcknowledgementCountBeforeReceive = Network.QueuedCount(SendingPort);
	SendingSystem.PreAdvance(InitialReliableSendTurnMilliseconds);
	const std::size_t QueuedAcknowledgementCountAfterFirstReceive = Network.QueuedCount(SendingPort);
	SendingSystem.PostAdvance(ReliableRetryTurnMilliseconds);
	const std::size_t QueuedRetryCountAfterDeferredAcknowledgement = Network.QueuedCount(ReceivingPort);
	Network.Drain(ReceivingPort);
	SendingSystem.PreAdvance(RetriedReceiveTurnMilliseconds);
	const std::size_t QueuedAcknowledgementCountAfterSecondReceive = Network.QueuedCount(SendingPort);
	SendingSystem.PostAdvance(FarAfterAcknowledgementTurnMilliseconds);
	const std::size_t QueuedRetryCountAfterDeferredAcknowledgementRelease = Network.QueuedCount(ReceivingPort);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendingCreateResult, "The receive-budget acknowledgement channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The receive-budget reliable frame should send initially");
	MW_EXPECT_EQ(Test, OneQueuedPacket, QueuedInitialReliableFrameCount, "The initial reliable frame should reach the peer mailbox");
	MW_EXPECT_EQ(
		Test, ETransportResult::Success, UnmatchedAcknowledgementSendResult, "The unmatched acknowledgement should enter the sender mailbox first");
	MW_EXPECT_EQ(
		Test, ETransportResult::Success, ExpectedAcknowledgementSendResult, "The expected acknowledgement should enter the sender mailbox second");
	MW_EXPECT_EQ(Test, TwoMailboxSlots, QueuedAcknowledgementCountBeforeReceive, "Both acknowledgements should be queued in FIFO order");
	MW_EXPECT_EQ(
		Test, OneQueuedPacket, QueuedAcknowledgementCountAfterFirstReceive, "A receive budget of one should defer the expected acknowledgement");
	MW_EXPECT_EQ(
		Test,
		OneQueuedPacket,
		QueuedRetryCountAfterDeferredAcknowledgement,
		"The deferred acknowledgement should permit exactly one at-least-once retry");
	MW_EXPECT_EQ(
		Test,
		NoQueuedPackets,
		QueuedAcknowledgementCountAfterSecondReceive,
		"The next receive turn should consume the deferred expected acknowledgement");
	MW_EXPECT_EQ(
		Test,
		NoQueuedPackets,
		QueuedRetryCountAfterDeferredAcknowledgementRelease,
		"The consumed deferred acknowledgement should prevent any later retry");
}

/**
 * Motivation: Makes the maximum reliable attempt count and its visible abandonment outcome exact.
 * Responsibilities: With no receiver turn to acknowledge, verify exactly the configured packet count and one stable abandonment count.
 */
MW_TEST_CASE(MessagingSystem_AbandonsReliableMessagesAfterTheExactAttemptBudget)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, ReliableAttemptPacketCount + OneMailboxSlot, StandardPacketBytes> Network;
	FMessagingSystemInformation Information{};
	Information.ReliableRetryIntervalMilliseconds = ReliableRetryIntervalMilliseconds;
	Information.MaxReliableSendAttempts = ReliableAttemptBudget;
	FMessagingSystem System{Information};
	const FChannelInformation Channel{"Telemetry", true, &Network.Port(SendingPort), MakeLoopbackAddress(ReceivingPort)};
	const EMessagingResult CreateResult = System.CreateChannel(Channel);
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	Message.SetPayload(TSpan<const std::uint8_t>(WirePayload, WirePayloadByteCount));

	// Act
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, "Telemetry");
	System.PostAdvance(ReliableRetryTurnMilliseconds);
	System.PostAdvance(ReliableRetryTurnMilliseconds * 2);
	System.PostAdvance(ReliableRetryTurnMilliseconds * 3);
	const std::size_t QueuedAtBudget = Network.QueuedCount(ReceivingPort);
	const std::uint32_t AbandonedAtBudget = System.GetAbandonedReliableMessageCount();
	System.PostAdvance(ReliableRetryTurnMilliseconds * 4);
	const std::size_t QueuedAfterAbandonment = Network.QueuedCount(ReceivingPort);
	const std::uint32_t AbandonedAfterAbandonment = System.GetAbandonedReliableMessageCount();

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The attempt-budget channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The attempt-budget reliable message should send");
	MW_EXPECT_EQ(Test, ReliableAttemptPacketCount, QueuedAtBudget, "The peer mailbox should observe exactly the configured total attempts");
	MW_EXPECT_EQ(Test, OneAbandonedReliableMessage, AbandonedAtBudget, "The exhausted reliable frame should increment abandonment once");
	MW_EXPECT_EQ(Test, ReliableAttemptPacketCount, QueuedAfterAbandonment, "No packet should follow reliable abandonment");
	MW_EXPECT_EQ(Test, OneAbandonedReliableMessage, AbandonedAfterAbandonment, "The abandonment count should stay stable after release");
}

/**
 * Motivation: Prevents a reliable wire promise when every fixed pending slot is already occupied.
 * Responsibilities: Fill every concrete pending slot, then prove one additional reliable send remains local but returns Full and reaches no device.
 */
MW_TEST_CASE(MessagingSystem_ReturnsFullForReliableSendsWhenAllPendingSlotsAreOccupied)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, FMessagingSystem::MaxReliablePendingMessages + OneMailboxSlot, StandardPacketBytes> Network;
	FMessagingSystem System;
	const FChannelInformation Channel{"Telemetry", true, &Network.Port(SendingPort), MakeLoopbackAddress(ReceivingPort)};
	const EMessagingResult CreateResult = System.CreateChannel(Channel);
	std::size_t LocalDeliveryCount = NoDeliveries;
	FSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&LocalDeliveryCount](const FMessage&) noexcept { ++LocalDeliveryCount; });
	const EMessagingResult SubscribeResult = System.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	Message.SetPayload(TSpan<const std::uint8_t>(WirePayload, WirePayloadByteCount));
	const EMessagingResult FillResult = FillReliablePendingSlots(System, Message, "Telemetry");
	const std::size_t ExpectedLocalDeliveryCount = FMessagingSystem::MaxReliablePendingMessages + OneDelivery;

	// Act
	const EMessagingResult OverflowSendResult = System.SendMessageToChannel(Message, "Telemetry");
	const std::size_t QueuedAfterOverflow = Network.QueuedCount(ReceivingPort);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The pending-capacity reliable channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The pending-capacity subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The pending-capacity subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FillResult, "Every concrete reliable pending slot should accept one frame");
	MW_EXPECT_EQ(Test, EMessagingResult::Full, OverflowSendResult, "A reliable frame beyond concrete pending capacity should report Full");
	MW_EXPECT_EQ(Test, ExpectedLocalDeliveryCount, LocalDeliveryCount, "Every accepted and overflow reliable send should still deliver locally");
	MW_EXPECT_EQ(
		Test, FMessagingSystem::MaxReliablePendingMessages, QueuedAfterOverflow, "Only reliable frames with pending slots should reach the device");
}

/**
 * Motivation: Returns concrete reliable capacity as soon as an acknowledgement releases one pending frame.
 * Responsibilities: Fill every pending slot, acknowledge the first sequence, and verify one later reliable send succeeds through the freed slot.
 */
MW_TEST_CASE(MessagingSystem_ReusesReliablePendingSlotAfterAcknowledgement)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, FMessagingSystem::MaxReliablePendingMessages, StandardPacketBytes> Network;
	FMessagingSystem System;
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FChannelInformation Channel{"Telemetry", true, &Network.Port(SendingPort), ReceivingAddress};
	const EMessagingResult CreateResult = System.CreateChannel(Channel);
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	Message.SetPayload(TSpan<const std::uint8_t>(WirePayload, WirePayloadByteCount));
	const EMessagingResult FillResult = FillReliablePendingSlots(System, Message, "Telemetry");
	Network.Drain(ReceivingPort);
	FRawWireFrame AcknowledgementFrame;
	AcknowledgementFrame.Set(
		"Telemetry", MessageAcknowledgementNameId, TSpan<const std::uint8_t>(FirstSequenceAcknowledgementPayload, SequenceNumberBytes));

	// Act
	const ETransportResult AcknowledgementSendResult =
		Network.Port(ReceivingPort).TrySend(SendingAddress, TSpan<const std::uint8_t>(AcknowledgementFrame.Bytes, AcknowledgementFrame.Size));
	System.PreAdvance(FirstReceiveTurnMilliseconds);
	const EMessagingResult ReusedSlotSendResult = System.SendMessageToChannel(Message, "Telemetry");
	const std::size_t QueuedAfterReuse = Network.QueuedCount(ReceivingPort);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The reusable-pending channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FillResult, "Every concrete reliable pending slot should be occupied");
	MW_EXPECT_EQ(Test, ETransportResult::Success, AcknowledgementSendResult, "The first pending sequence acknowledgement should reach the sender");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReusedSlotSendResult, "A released reliable pending slot should accept the next frame");
	MW_EXPECT_EQ(Test, OneQueuedPacket, QueuedAfterReuse, "The frame using the released pending slot should reach the device");
}

/**
 * Motivation: Keeps duplicate or late acknowledgement control traffic harmless while another reliable frame is still pending.
 * Responsibilities: Consume an unmatched acknowledgement, preserve zero drops, and verify both pending messages later retry.
 */
MW_TEST_CASE(MessagingSystem_ConsumesUnmatchedAcknowledgementsWithoutDisturbingPendingFrames)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, TwoMailboxSlots, StandardPacketBytes> Network;
	FMessagingSystemInformation Information{};
	Information.ReliableRetryIntervalMilliseconds = ReliableRetryIntervalMilliseconds;
	FMessagingSystem System{Information};
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FChannelInformation Channel{"Telemetry", true, &Network.Port(SendingPort), ReceivingAddress};
	const EMessagingResult CreateResult = System.CreateChannel(Channel);
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	Message.SetPayload(TSpan<const std::uint8_t>(WirePayload, WirePayloadByteCount));
	FRawWireFrame AcknowledgementFrame;
	AcknowledgementFrame.Set(
		"Telemetry", MessageAcknowledgementNameId, TSpan<const std::uint8_t>(UnmatchedAcknowledgementPayload, SequenceNumberBytes));

	// Act
	const EMessagingResult FirstSendResult = System.SendMessageToChannel(Message, "Telemetry");
	const EMessagingResult SecondSendResult = System.SendMessageToChannel(Message, "Telemetry");
	Network.Drain(ReceivingPort);
	const ETransportResult AcknowledgementSendResult =
		Network.Port(ReceivingPort).TrySend(SendingAddress, TSpan<const std::uint8_t>(AcknowledgementFrame.Bytes, AcknowledgementFrame.Size));
	System.PreAdvance(FirstReceiveTurnMilliseconds);
	System.PostAdvance(FarAfterAcknowledgementTurnMilliseconds);
	const std::size_t QueuedRetries = Network.QueuedCount(ReceivingPort);
	const std::uint32_t DroppedFrameCount = System.GetDroppedFrameCount();

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The unmatched-acknowledgement channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstSendResult, "The first pending reliable frame should send");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondSendResult, "The second pending reliable frame should send");
	MW_EXPECT_EQ(Test, ETransportResult::Success, AcknowledgementSendResult, "The unmatched acknowledgement should reach the sender device");
	MW_EXPECT_EQ(Test, NoDroppedFrames, DroppedFrameCount, "An unmatched valid acknowledgement should be consumed without a drop");
	MW_EXPECT_EQ(Test, TwoMailboxSlots, QueuedRetries, "The unmatched acknowledgement should release neither pending reliable frame");
}

/**
 * Motivation: Preserves best-effort availability when reliable retry storage is completely occupied.
 * Responsibilities: Fill every reliable pending slot, then send a best-effort frame on another channel and verify it still reaches the device.
 */
MW_TEST_CASE(MessagingSystem_BestEffortSendsDoNotConsumeReliablePendingSlots)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, FMessagingSystem::MaxReliablePendingMessages + OneMailboxSlot, StandardPacketBytes> Network;
	FMessagingSystem System;
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FChannelInformation ReliableChannel{"Reliable", true, &Network.Port(SendingPort), ReceivingAddress};
	const FChannelInformation BestEffortChannel{"BestEffort", false, &Network.Port(SendingPort), ReceivingAddress};
	const EMessagingResult ReliableCreateResult = System.CreateChannel(ReliableChannel);
	const EMessagingResult BestEffortCreateResult = System.CreateChannel(BestEffortChannel);
	const std::size_t ExpectedQueuedPacketCount = FMessagingSystem::MaxReliablePendingMessages + OneQueuedPacket;
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	Message.SetPayload(TSpan<const std::uint8_t>(WirePayload, WirePayloadByteCount));

	// Act
	const EMessagingResult ReliableFillResult = FillReliablePendingSlots(System, Message, "Reliable");
	const EMessagingResult BestEffortSendResult = System.SendMessageToChannel(Message, "BestEffort");
	const std::size_t QueuedAfterSends = Network.QueuedCount(ReceivingPort);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReliableCreateResult, "The reliable pending-capacity channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, BestEffortCreateResult, "The best-effort pending-capacity channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReliableFillResult, "Reliable frames should fill every concrete pending slot");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, BestEffortSendResult, "A best-effort frame should not need a reliable pending slot");
	MW_EXPECT_EQ(Test, ExpectedQueuedPacketCount, QueuedAfterSends, "Every reliable frame and the best-effort frame should reach the device");
}

/**
 * Motivation: Prevents a valid acknowledgement received through another registered device from releasing a route-owned retry frame.
 * Responsibilities: Consume a wrong-link acknowledgement and verify the original route still receives its scheduled retry.
 */
MW_TEST_CASE(MessagingSystem_IgnoresReliableAcknowledgementsFromAnotherRoute)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> MessageNetwork;
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> AcknowledgementNetwork;
	FMessagingSystemInformation Information{};
	Information.ReliableRetryIntervalMilliseconds = ReliableRetryIntervalMilliseconds;
	FMessagingSystem System{Information};
	MicroWorld::Messaging::FMessagingLinkId AlternateLinkId;
	const FChannelInformation Channel{"Telemetry", true, &MessageNetwork.Port(SendingPort), MakeLoopbackAddress(ReceivingPort)};
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	FRawWireFrame AcknowledgementFrame;
	AcknowledgementFrame.Set(
		"Telemetry", MessageAcknowledgementNameId, TSpan<const std::uint8_t>(FirstSequenceAcknowledgementPayload, SequenceNumberBytes));

	// Act
	const EMessagingResult CreateResult = System.CreateChannel(Channel);
	const EMessagingResult AlternateRegisterResult = System.RegisterLink(AcknowledgementNetwork.Port(SendingPort), AlternateLinkId);
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, "Telemetry");
	MessageNetwork.Drain(ReceivingPort);
	const ETransportResult WrongRouteAcknowledgementResult =
		AcknowledgementNetwork.Port(ReceivingPort)
			.TrySend(MakeLoopbackAddress(SendingPort), TSpan<const std::uint8_t>(AcknowledgementFrame.Bytes, AcknowledgementFrame.Size));
	System.PreAdvance(FirstReceiveTurnMilliseconds);
	System.PostAdvance(SecondReceiveTurnMilliseconds + ReliableRetryIntervalMilliseconds);
	const std::size_t QueuedRetryCount = MessageNetwork.QueuedCount(ReceivingPort);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The reliable default-route channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, AlternateRegisterResult, "The alternate acknowledgement device should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The initial reliable frame should send");
	MW_EXPECT_EQ(Test, ETransportResult::Success, WrongRouteAcknowledgementResult, "The wrong-route acknowledgement should reach Messaging");
	MW_EXPECT_EQ(Test, OneQueuedPacket, QueuedRetryCount, "A wrong-route acknowledgement should leave the original route pending");
}

/**
 * Motivation: Prevents a channel's lifecycle from resetting system-lifetime reliable identity or allowing an old acknowledgement to release new work.
 * Responsibilities: Acknowledge the first frame, recreate its channel, then prove the next id differs and a delayed old acknowledgement leaves it
 * retrying.
 */
MW_TEST_CASE(MessagingSystem_ChannelRecreationDoesNotReuseReliableIdsOrAcceptDelayedOldAcknowledgements)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, TwoMailboxSlots, StandardPacketBytes> Network;
	FMessagingSystemInformation Information{};
	Information.ReliableRetryIntervalMilliseconds = ReliableRetryIntervalMilliseconds;
	FMessagingSystem System{Information};
	const FChannelInformation Channel{"Telemetry", true, &Network.Port(SendingPort), MakeLoopbackAddress(ReceivingPort)};
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	FRawWireFrame FirstAcknowledgementFrame;
	FirstAcknowledgementFrame.Set(
		"Telemetry", MessageAcknowledgementNameId, TSpan<const std::uint8_t>(FirstSequenceAcknowledgementPayload, SequenceNumberBytes));

	// Act
	const EMessagingResult FirstCreateResult = System.CreateChannel(Channel);
	const EMessagingResult FirstSendResult = System.SendMessageToChannel(Message, "Telemetry");
	Network.Drain(ReceivingPort);
	const ETransportResult FirstAcknowledgementResult =
		Network.Port(ReceivingPort)
			.TrySend(MakeLoopbackAddress(SendingPort), TSpan<const std::uint8_t>(FirstAcknowledgementFrame.Bytes, FirstAcknowledgementFrame.Size));
	System.PreAdvance(FirstReceiveTurnMilliseconds);
	const EMessagingResult DestroyResult = System.DestroyChannel("Telemetry");
	const EMessagingResult RecreateResult = System.CreateChannel(Channel);
	const EMessagingResult SecondSendResult = System.SendMessageToChannel(Message, "Telemetry");
	std::uint8_t SecondFrameBytes[FMessagingSystem::MaxFrameBytes]{};
	FDeviceAddress SecondFrameSender;
	FReceiveResult SecondFrameReceiveResult;
	const ETransportResult SecondFrameReceiveStatus =
		Network.Port(ReceivingPort)
			.TryReceive(SecondFrameSender, TSpan<std::uint8_t>(SecondFrameBytes, FMessagingSystem::MaxFrameBytes), SecondFrameReceiveResult);
	const std::uint64_t RecreatedChannelReliableMessageId = FRawWireFrame::ReadSequenceNumber(&SecondFrameBytes[WireHeaderBytes]);
	const ETransportResult DelayedAcknowledgementResult =
		Network.Port(ReceivingPort)
			.TrySend(MakeLoopbackAddress(SendingPort), TSpan<const std::uint8_t>(FirstAcknowledgementFrame.Bytes, FirstAcknowledgementFrame.Size));
	System.PreAdvance(SecondReceiveTurnMilliseconds);
	System.PostAdvance(SecondReceiveTurnMilliseconds + ReliableRetryIntervalMilliseconds);
	const std::size_t QueuedRetryCount = Network.QueuedCount(ReceivingPort);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstCreateResult, "The original reliable channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstSendResult, "The original reliable frame should send");
	MW_EXPECT_EQ(Test, ETransportResult::Success, FirstAcknowledgementResult, "The original acknowledgement should reach Messaging");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, DestroyResult, "An acknowledged reliable channel should be destroyable");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, RecreateResult, "The same channel name should be recreatable");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondSendResult, "The recreated channel should send reliably");
	MW_EXPECT_EQ(Test, ETransportResult::Success, SecondFrameReceiveStatus, "The recreated reliable frame should reach the peer");
	MW_EXPECT_EQ(Test, SecondSequenceNumber, RecreatedChannelReliableMessageId, "A recreated channel should not reuse its first reliable id");
	MW_EXPECT_EQ(Test, ETransportResult::Success, DelayedAcknowledgementResult, "The delayed old acknowledgement should reach Messaging");
	MW_EXPECT_EQ(Test, OneQueuedPacket, QueuedRetryCount, "A delayed old acknowledgement should not release the new reliable frame");
}

} // namespace
