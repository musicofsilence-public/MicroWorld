#include "TestSupport.h"
#include "MessagingWireTestHelpers.h"

#include <MicroWorld/Messaging/Message.h>

#include <cstddef>
#include <utility>

namespace
{

using namespace ::MicroWorld::Tests;

/**
 * Motivation: Proves reliable framing remains transparent to subscribers while its acknowledgement stays protocol-only.
 * Responsibilities: Send one reliable message between systems, inspect the raw acknowledgement, then consume it without delivery or an
 *   acknowledgement loop.
 */
MW_TEST_CASE(MessagingSystem_ReliableWireMessagesStripSequenceAndAcknowledgeWithoutEcho)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> Network;
	FMessagingSystem SendingSystem;
	FMessagingSystem ReceivingSystem;
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FChannelInformation SendingChannel{"Telemetry", true, &Network.Port(SendingPort), ReceivingAddress};
	const FChannelInformation ReceivingChannel{"Telemetry", true, &Network.Port(ReceivingPort), SendingAddress};
	const EMessagingResult SendingCreateResult = SendingSystem.CreateChannel(SendingChannel);
	const EMessagingResult ReceivingCreateResult = ReceivingSystem.CreateChannel(ReceivingChannel);
	FWireMessageRecorder SendingRecorder;
	FWireMessageRecorder ReceivingRecorder;
	FSubscriberDelegate SendingSubscriber;
	FSubscriberDelegate ReceivingSubscriber;
	const EDelegateResult SendingBindingResult =
		SendingSubscriber.Bind([&SendingRecorder](const FMessage& InMessage) noexcept { SendingRecorder.Record(InMessage); });
	const EDelegateResult ReceivingBindingResult =
		ReceivingSubscriber.Bind([&ReceivingRecorder](const FMessage& InMessage) noexcept { ReceivingRecorder.Record(InMessage); });
	const EMessagingResult SendingSubscribeResult = SendingSystem.SubscribeToChannel("Telemetry", std::move(SendingSubscriber));
	const EMessagingResult ReceivingSubscribeResult = ReceivingSystem.SubscribeToChannel("Telemetry", std::move(ReceivingSubscriber));
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	Message.SetPayload(TSpan<const std::uint8_t>(WirePayload, WirePayloadByteCount));
	std::uint8_t AcknowledgementBytes[FMessagingSystem::MaxFrameBytes]{};
	FDeviceAddress AcknowledgementSender;
	FReceiveResult AcknowledgementReceiveResult;

	// Act
	const EMessagingResult SendResult = SendingSystem.SendMessageToChannel(Message, "Telemetry");
	ReceivingSystem.PreAdvance(FirstReceiveTurnMilliseconds);
	const std::size_t QueuedAcknowledgements = Network.QueuedCount(SendingPort);
	const ETransportResult AcknowledgementReceiveStatus =
		Network.Port(SendingPort)
			.TryReceive(
				AcknowledgementSender, TSpan<std::uint8_t>(AcknowledgementBytes, FMessagingSystem::MaxFrameBytes), AcknowledgementReceiveResult);
	const FNameId AcknowledgementChannelNameId = FRawWireFrame::ReadNameId(&AcknowledgementBytes[ChannelNameIdByteIndex]);
	const FNameId AcknowledgementMessageNameId = FRawWireFrame::ReadNameId(&AcknowledgementBytes[MessageNameIdByteIndex]);
	const std::uint16_t AcknowledgedSequenceNumber = FRawWireFrame::ReadSequenceNumber(&AcknowledgementBytes[WireHeaderBytes]);
	const ETransportResult RequeueAcknowledgementResult =
		Network.Port(ReceivingPort)
			.TrySend(SendingAddress, TSpan<const std::uint8_t>(AcknowledgementBytes, AcknowledgementReceiveResult.BytesReceived));
	SendingSystem.PreAdvance(SenderReceiveTurnMilliseconds);
	const std::size_t QueuedAfterAcknowledgementConsumption = Network.QueuedCount(ReceivingPort);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendingCreateResult, "The reliable sending channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReceivingCreateResult, "The reliable receiving channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SendingBindingResult, "The reliable sending subscriber should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, ReceivingBindingResult, "The reliable receiving subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendingSubscribeResult, "The reliable sending subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReceivingSubscribeResult, "The reliable receiving subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The reliable message should send");
	MW_EXPECT_EQ(Test, OneDelivery, SendingRecorder.DeliveryCount, "The sender should receive only its synchronous local delivery");
	MW_EXPECT_EQ(Test, OneDelivery, ReceivingRecorder.DeliveryCount, "The receiver should deliver the reliable message once");
	MW_EXPECT_EQ(Test, WirePayloadByteCount, ReceivingRecorder.PayloadSize, "The receiver should not observe the sequence sub-header");
	MW_EXPECT_EQ(Test, WirePayload[0], ReceivingRecorder.PayloadBytes[0], "The receiver should preserve the first application byte");
	MW_EXPECT_EQ(Test, WirePayload[1], ReceivingRecorder.PayloadBytes[1], "The receiver should preserve the second application byte");
	MW_EXPECT_EQ(Test, WirePayload[2], ReceivingRecorder.PayloadBytes[2], "The receiver should preserve the third application byte");
	MW_EXPECT_EQ(Test, OneQueuedPacket, QueuedAcknowledgements, "The receiver should send one acknowledgement");
	MW_EXPECT_EQ(Test, ETransportResult::Success, AcknowledgementReceiveStatus, "The sender port should receive the acknowledgement packet");
	MW_EXPECT_EQ(
		Test,
		AcknowledgementFrameByteCount,
		AcknowledgementReceiveResult.BytesReceived,
		"The acknowledgement payload should contain exactly one sequence number");
	MW_EXPECT_EQ(Test, FNameId{"Telemetry"}, AcknowledgementChannelNameId, "The acknowledgement should preserve the channel id");
	MW_EXPECT_EQ(Test, MessageAcknowledgementNameId, AcknowledgementMessageNameId, "The acknowledgement should use the reserved message name");
	MW_EXPECT_EQ(Test, FirstSequenceNumber, AcknowledgedSequenceNumber, "The acknowledgement should name the first reliable sequence");
	MW_EXPECT_EQ(Test, ETransportResult::Success, RequeueAcknowledgementResult, "The captured acknowledgement should requeue for sender consumption");
	MW_EXPECT_EQ(Test, OneDelivery, SendingRecorder.DeliveryCount, "The sender subscriber should not observe the acknowledgement");
	MW_EXPECT_EQ(Test, NoQueuedPackets, QueuedAfterAcknowledgementConsumption, "An acknowledgement should not trigger another acknowledgement");
}

/**
 * Motivation: Makes each reliable channel's outgoing sequence progression observable on its raw packets.
 * Responsibilities: Send two reliable messages in order and decode consecutive sequence numbers from the receiving port's queued frames.
 */
MW_TEST_CASE(MessagingSystem_ReliableWireMessagesUseConsecutiveSequenceNumbers)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, TwoMailboxSlots, StandardPacketBytes> Network;
	FMessagingSystem SendingSystem;
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FChannelInformation SendingChannel{"Telemetry", true, &Network.Port(SendingPort), ReceivingAddress};
	const EMessagingResult CreateResult = SendingSystem.CreateChannel(SendingChannel);
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	Message.SetPayload(TSpan<const std::uint8_t>(WirePayload, WirePayloadByteCount));
	std::uint8_t FirstFrameBytes[FMessagingSystem::MaxFrameBytes]{};
	std::uint8_t SecondFrameBytes[FMessagingSystem::MaxFrameBytes]{};
	FDeviceAddress FirstFrameSender;
	FDeviceAddress SecondFrameSender;
	FReceiveResult FirstFrameReceiveResult;
	FReceiveResult SecondFrameReceiveResult;

	// Act
	const EMessagingResult FirstSendResult = SendingSystem.SendMessageToChannel(Message, "Telemetry");
	const EMessagingResult SecondSendResult = SendingSystem.SendMessageToChannel(Message, "Telemetry");
	const ETransportResult FirstReceiveStatus =
		Network.Port(ReceivingPort)
			.TryReceive(FirstFrameSender, TSpan<std::uint8_t>(FirstFrameBytes, FMessagingSystem::MaxFrameBytes), FirstFrameReceiveResult);
	const ETransportResult SecondReceiveStatus =
		Network.Port(ReceivingPort)
			.TryReceive(SecondFrameSender, TSpan<std::uint8_t>(SecondFrameBytes, FMessagingSystem::MaxFrameBytes), SecondFrameReceiveResult);
	const std::uint16_t FirstFrameSequenceNumber = FRawWireFrame::ReadSequenceNumber(&FirstFrameBytes[WireHeaderBytes]);
	const std::uint16_t SecondFrameSequenceNumber = FRawWireFrame::ReadSequenceNumber(&SecondFrameBytes[WireHeaderBytes]);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The reliable sequence channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstSendResult, "The first reliable message should send");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondSendResult, "The second reliable message should send");
	MW_EXPECT_EQ(Test, ETransportResult::Success, FirstReceiveStatus, "The receiver should expose the first reliable packet");
	MW_EXPECT_EQ(Test, ETransportResult::Success, SecondReceiveStatus, "The receiver should expose the second reliable packet");
	MW_EXPECT_EQ(Test, FirstSequenceNumber, FirstFrameSequenceNumber, "The first reliable message should use the initial sequence");
	MW_EXPECT_EQ(Test, SecondSequenceNumber, SecondFrameSequenceNumber, "The second reliable message should use the next sequence");
}

/**
 * Motivation: Makes malformed reliable data visible without delivering a partial protocol header to subscribers.
 * Responsibilities: Send a reliable-channel frame shorter than one sequence number and verify one drop, no local delivery, and no acknowledgement.
 */
MW_TEST_CASE(MessagingSystem_DropsReliableFramesWithoutCompleteSequenceNumbers)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> Network;
	FMessagingSystem ReceivingSystem;
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FChannelInformation ReceivingChannel{"Telemetry", true, &Network.Port(ReceivingPort), SendingAddress};
	const EMessagingResult CreateResult = ReceivingSystem.CreateChannel(ReceivingChannel);
	std::size_t DeliveryCount = NoDeliveries;
	FSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	const EMessagingResult SubscribeResult = ReceivingSystem.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FRawWireFrame Frame;
	Frame.Set("Telemetry", "TemperatureUpdated", TSpan<const std::uint8_t>(ShortReliablePayload, ShortReliablePayloadByteCount));
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);

	// Act
	const ETransportResult RawSendResult = Network.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(Frame.Bytes, Frame.Size));
	ReceivingSystem.PreAdvance(FirstReceiveTurnMilliseconds);
	const std::size_t QueuedAcknowledgements = Network.QueuedCount(SendingPort);
	const std::uint32_t DroppedFrameCount = ReceivingSystem.GetDroppedFrameCount();

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The malformed reliable receiver channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The malformed reliable subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The malformed reliable subscriber should register");
	MW_EXPECT_EQ(Test, ETransportResult::Success, RawSendResult, "The short reliable frame should reach the receiver port");
	MW_EXPECT_EQ(Test, NoDeliveries, DeliveryCount, "A partial sequence number should not reach a subscriber");
	MW_EXPECT_EQ(Test, OneDroppedFrame, DroppedFrameCount, "A partial reliable sequence should count as dropped");
	MW_EXPECT_EQ(Test, NoQueuedPackets, QueuedAcknowledgements, "A malformed reliable frame should not receive an acknowledgement");
}

/**
 * Motivation: Prevents malformed acknowledgement traffic from disappearing silently on any channel kind.
 * Responsibilities: Send an undersized acknowledgement on a best-effort channel and verify it is counted and never delivered or answered.
 */
MW_TEST_CASE(MessagingSystem_DropsAcknowledgementsWithoutCompleteSequenceNumbers)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> Network;
	FMessagingSystem ReceivingSystem;
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FChannelInformation ReceivingChannel{"Telemetry", false, &Network.Port(ReceivingPort), SendingAddress};
	const EMessagingResult CreateResult = ReceivingSystem.CreateChannel(ReceivingChannel);
	std::size_t DeliveryCount = NoDeliveries;
	FSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	const EMessagingResult SubscribeResult = ReceivingSystem.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FRawWireFrame Frame;
	Frame.Set(
		"Telemetry", MessageAcknowledgementNameId, TSpan<const std::uint8_t>(InvalidAcknowledgementPayload, InvalidAcknowledgementPayloadByteCount));
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);

	// Act
	const ETransportResult RawSendResult = Network.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(Frame.Bytes, Frame.Size));
	ReceivingSystem.PreAdvance(FirstReceiveTurnMilliseconds);
	const std::size_t QueuedReplies = Network.QueuedCount(SendingPort);
	const std::uint32_t DroppedFrameCount = ReceivingSystem.GetDroppedFrameCount();

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The acknowledgement receiver channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The acknowledgement receiver subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The acknowledgement receiver subscriber should register");
	MW_EXPECT_EQ(Test, ETransportResult::Success, RawSendResult, "The malformed acknowledgement should reach the receiver port");
	MW_EXPECT_EQ(Test, NoDeliveries, DeliveryCount, "Acknowledgement control traffic should not reach a subscriber");
	MW_EXPECT_EQ(Test, OneDroppedFrame, DroppedFrameCount, "A malformed acknowledgement should count as dropped");
	MW_EXPECT_EQ(Test, NoQueuedPackets, QueuedReplies, "An acknowledgement should never receive a reply acknowledgement");
}

/**
 * Motivation: Keeps the acknowledgement name reserved to Messaging so an application cannot forge reliability control traffic.
 * Responsibilities: Attempt a local and device-backed send using the reserved name and verify Invalid, no local delivery, and no wire packet.
 */
MW_TEST_CASE(MessagingSystem_RejectsApplicationAcknowledgementMessages)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> Network;
	FMessagingSystem System;
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FChannelInformation Channel{"Telemetry", false, &Network.Port(SendingPort), ReceivingAddress};
	const EMessagingResult CreateResult = System.CreateChannel(Channel);
	std::size_t DeliveryCount = NoDeliveries;
	FSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	const EMessagingResult SubscribeResult = System.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FMessage Message;
	Message.SetMessageNameId(MessageAcknowledgementNameId);

	// Act
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, "Telemetry");
	const std::size_t QueuedAfterSend = Network.QueuedCount(ReceivingPort);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The acknowledgement rejection channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The acknowledgement rejection subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The acknowledgement rejection subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Invalid, SendResult, "An application acknowledgement message should be rejected");
	MW_EXPECT_EQ(Test, NoDeliveries, DeliveryCount, "A rejected acknowledgement message should not deliver locally");
	MW_EXPECT_EQ(Test, NoQueuedPackets, QueuedAfterSend, "A rejected acknowledgement message should not reach the device");
}

/**
 * Motivation: Makes the reliable sequence sub-header's two-byte cost visible at the payload capacity boundary.
 * Responsibilities: Send one exact-budget best-effort message and the same reliable message, then verify only the reliable wire send is Full while
 *   its local delivery remains synchronous.
 */
MW_TEST_CASE(MessagingSystem_ReliablePayloadBudgetReservesSequenceBytes)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, TwoMailboxSlots, ReliableBudgetProbePacketBytes> Network;
	FMessagingSystem System;
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FChannelInformation BestEffortChannel{"BestEffort", false, &Network.Port(SendingPort), ReceivingAddress};
	const FChannelInformation ReliableChannel{"Reliable", true, &Network.Port(SendingPort), ReceivingAddress};
	const EMessagingResult BestEffortCreateResult = System.CreateChannel(BestEffortChannel);
	const EMessagingResult ReliableCreateResult = System.CreateChannel(ReliableChannel);
	std::size_t ReliableDeliveryCount = NoDeliveries;
	FSubscriberDelegate ReliableSubscriber;
	const EDelegateResult ReliableBindingResult =
		ReliableSubscriber.Bind([&ReliableDeliveryCount](const FMessage&) noexcept { ++ReliableDeliveryCount; });
	const EMessagingResult ReliableSubscribeResult = System.SubscribeToChannel("Reliable", std::move(ReliableSubscriber));
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	Message.SetPayload(TSpan<const std::uint8_t>(MaximumPayload.data(), MaximumPayload.size()));

	// Act
	const EMessagingResult BestEffortSendResult = System.SendMessageToChannel(Message, "BestEffort");
	const EMessagingResult ReliableSendResult = System.SendMessageToChannel(Message, "Reliable");
	const std::size_t QueuedAfterSends = Network.QueuedCount(ReceivingPort);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, BestEffortCreateResult, "The payload-boundary best-effort channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReliableCreateResult, "The payload-boundary reliable channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, ReliableBindingResult, "The payload-boundary reliable subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReliableSubscribeResult, "The payload-boundary reliable subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, BestEffortSendResult, "The exact best-effort payload budget should send");
	MW_EXPECT_EQ(Test, EMessagingResult::Full, ReliableSendResult, "The same payload should exceed the reliable wire budget by its sequence header");
	MW_EXPECT_EQ(Test, OneDelivery, ReliableDeliveryCount, "The reliable capacity failure should not undo local delivery");
	MW_EXPECT_EQ(Test, OneQueuedPacket, QueuedAfterSends, "Only the best-effort packet should reach the device");
}

/**
 * Motivation: Ensures the header-only wire representation preserves a valid named message with no application bytes.
 * Responsibilities: Send a zero-length payload through two systems and verify the receiver sees the name and an empty payload.
 */
MW_TEST_CASE(MessagingSystem_RoundTripsZeroLengthWirePayload)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> Network;
	FMessagingSystem SendingSystem;
	FMessagingSystem ReceivingSystem;
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FChannelInformation SendingChannel{"Telemetry", false, &Network.Port(SendingPort), ReceivingAddress};
	const FChannelInformation ReceivingChannel{"Telemetry", false, &Network.Port(ReceivingPort), SendingAddress};
	const EMessagingResult SendingCreateResult = SendingSystem.CreateChannel(SendingChannel);
	const EMessagingResult ReceivingCreateResult = ReceivingSystem.CreateChannel(ReceivingChannel);
	FWireMessageRecorder ReceivingRecorder;
	FSubscriberDelegate ReceivingSubscriber;
	const EDelegateResult ReceivingBindingResult =
		ReceivingSubscriber.Bind([&ReceivingRecorder](const FMessage& InMessage) noexcept { ReceivingRecorder.Record(InMessage); });
	const EMessagingResult ReceivingSubscribeResult = ReceivingSystem.SubscribeToChannel("Telemetry", std::move(ReceivingSubscriber));
	FMessage Message;
	Message.SetMessageNameId("Heartbeat");
	Message.SetPayload(TSpan<const std::uint8_t>(nullptr, ZeroPayloadByteCount));

	// Act
	const EMessagingResult SendResult = SendingSystem.SendMessageToChannel(Message, "Telemetry");
	ReceivingSystem.PreAdvance(FirstReceiveTurnMilliseconds);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendingCreateResult, "The zero-payload sending channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReceivingCreateResult, "The zero-payload receiving channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, ReceivingBindingResult, "The zero-payload receiver subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReceivingSubscribeResult, "The zero-payload receiver subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "A header-only wire frame should send successfully");
	MW_EXPECT_EQ(Test, OneDelivery, ReceivingRecorder.DeliveryCount, "The header-only wire frame should deliver once");
	MW_EXPECT_EQ(Test, FNameId{"Heartbeat"}, ReceivingRecorder.MessageNameId, "The header-only wire frame should preserve its message name");
	MW_EXPECT_EQ(Test, ZeroPayloadByteCount, ReceivingRecorder.PayloadSize, "The header-only wire frame should preserve an empty payload");
}

} // namespace
