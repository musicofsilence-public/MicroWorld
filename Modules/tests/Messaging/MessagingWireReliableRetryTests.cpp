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

} // namespace
