#include "TestSupport.h"
#include "MessagingWireTestHelpers.h"

#include <MicroWorld/Messaging/Message.h>

#include <cstddef>
#include <utility>

namespace
{

using namespace ::MicroWorld::Tests;

/**
 * Motivation: Proves end-to-end routing preserves message bytes, sender identity, explicit receiver timing, and local-only origin delivery.
 * Responsibilities: Send through two loopback ports, assert no early or echoed delivery, then verify both systems' subscribers observe their
 *   required results.
 */
MW_TEST_CASE(MessagingSystem_RoutesWireMessagesAfterReceiverPreAdvanceWithoutEcho)
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

	// Act
	const EMessagingResult SendResult = SendingSystem.SendMessageToChannel(Message, "Telemetry");
	const std::size_t ReceivingDeliveriesBeforePreAdvance = ReceivingRecorder.DeliveryCount;
	ReceivingSystem.PreAdvance(FirstReceiveTurnMilliseconds);
	SendingSystem.PreAdvance(SenderReceiveTurnMilliseconds);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendingCreateResult, "The sending device-backed channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReceivingCreateResult, "The receiving device-backed channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SendingBindingResult, "The sending subscriber should bind within inline storage");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, ReceivingBindingResult, "The receiving subscriber should bind within inline storage");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendingSubscribeResult, "The sending subscriber should register successfully");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReceivingSubscribeResult, "The receiving subscriber should register successfully");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The device-backed send should be accepted");
	MW_EXPECT_EQ(Test, OneDelivery, SendingRecorder.DeliveryCount, "The sender should receive its local delivery exactly once");
	MW_EXPECT_EQ(Test, NoDeliveries, ReceivingDeliveriesBeforePreAdvance, "The receiver should not deliver before its explicit pre-advance turn");
	MW_EXPECT_EQ(Test, OneDelivery, ReceivingRecorder.DeliveryCount, "The receiver should deliver the queued frame on pre-advance");
	MW_EXPECT_EQ(Test, FNameId{"TemperatureUpdated"}, ReceivingRecorder.MessageNameId, "The receiver should observe the wire message name");
	MW_EXPECT_EQ(Test, WirePayloadByteCount, ReceivingRecorder.PayloadSize, "The receiver should observe the exact wire payload length");
	MW_EXPECT_EQ(Test, WirePayload[0], ReceivingRecorder.PayloadBytes[0], "The receiver should observe the first wire payload byte");
	MW_EXPECT_EQ(Test, WirePayload[1], ReceivingRecorder.PayloadBytes[1], "The receiver should observe the second wire payload byte");
	MW_EXPECT_EQ(Test, WirePayload[2], ReceivingRecorder.PayloadBytes[2], "The receiver should observe the third wire payload byte");
	const bool bReceiverObservedSendingPort = ReceivingRecorder.Sender == SendingAddress;
	MW_EXPECT_EQ(Test, true, bReceiverObservedSendingPort, "The receiver should observe the sending port as the message sender");
	MW_EXPECT_EQ(Test, OneDelivery, SendingRecorder.DeliveryCount, "The sender pre-advance should not echo the loopback message to itself");
}

/**
 * Motivation: Keeps message-name subscriptions meaningful after the message crosses the transport boundary.
 * Responsibilities: Route one raw frame to the matching receiver subscriber while a different name filter remains silent.
 */
MW_TEST_CASE(MessagingSystem_AppliesMessageNameFiltersToWireMessages)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> Network;
	FMessagingSystem ReceivingSystem;
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FChannelInformation ReceivingChannel{"Telemetry", false, &Network.Port(ReceivingPort), SendingAddress};
	const EMessagingResult CreateResult = ReceivingSystem.CreateChannel(ReceivingChannel);
	std::size_t MatchingDeliveryCount = NoDeliveries;
	std::size_t MismatchedDeliveryCount = NoDeliveries;
	FSubscriberDelegate MatchingSubscriber;
	FSubscriberDelegate MismatchedSubscriber;
	const EDelegateResult MatchingBindingResult =
		MatchingSubscriber.Bind([&MatchingDeliveryCount](const FMessage&) noexcept { ++MatchingDeliveryCount; });
	const EDelegateResult MismatchedBindingResult =
		MismatchedSubscriber.Bind([&MismatchedDeliveryCount](const FMessage&) noexcept { ++MismatchedDeliveryCount; });
	const EMessagingResult MatchingSubscribeResult =
		ReceivingSystem.SubscribeToChannel("Telemetry", "TemperatureUpdated", std::move(MatchingSubscriber));
	const EMessagingResult MismatchedSubscribeResult =
		ReceivingSystem.SubscribeToChannel("Telemetry", "CommandReceived", std::move(MismatchedSubscriber));
	FRawWireFrame Frame;
	Frame.Set("Telemetry", "TemperatureUpdated", TSpan<const std::uint8_t>(nullptr, ZeroPayloadByteCount));
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);

	// Act
	const ETransportResult RawSendResult = Network.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(Frame.Bytes, Frame.Size));
	ReceivingSystem.PreAdvance(FirstReceiveTurnMilliseconds);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The filtered receiving channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, MatchingBindingResult, "The matching wire subscriber should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, MismatchedBindingResult, "The mismatched wire subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, MatchingSubscribeResult, "The matching wire filter should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, MismatchedSubscribeResult, "The mismatched wire filter should register");
	MW_EXPECT_EQ(Test, ETransportResult::Success, RawSendResult, "The raw matching frame should reach the receiver port");
	MW_EXPECT_EQ(Test, OneDelivery, MatchingDeliveryCount, "The matching filter should receive the wire message");
	MW_EXPECT_EQ(Test, NoDeliveries, MismatchedDeliveryCount, "The different filter should not receive the wire message");
}

/**
 * Motivation: Lets several named channels share one radio, with every frame reaching the channel its own encoded id names.
 * Responsibilities: Queue frames for two encoded channel ids on one shared device, run one receiver turn, and verify each subscriber observes only
 *   its own frame.
 */
MW_TEST_CASE(MessagingSystem_RoutesEachEncodedChannelFromOneSharedDevice)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, TwoMailboxSlots, StandardPacketBytes> Network;
	FMessagingSystem ReceivingSystem;
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FChannelInformation FirstReceivingChannel{"Telemetry", false, &Network.Port(ReceivingPort), SendingAddress};
	const FChannelInformation SecondReceivingChannel{"Commands", false, &Network.Port(ReceivingPort), SendingAddress};
	const EMessagingResult FirstCreateResult = ReceivingSystem.CreateChannel(FirstReceivingChannel);
	const EMessagingResult SecondCreateResult = ReceivingSystem.CreateChannel(SecondReceivingChannel);
	std::size_t FirstDeliveryCount = NoDeliveries;
	std::size_t SecondDeliveryCount = NoDeliveries;
	FSubscriberDelegate FirstSubscriber;
	FSubscriberDelegate SecondSubscriber;
	const EDelegateResult FirstBindingResult = FirstSubscriber.Bind([&FirstDeliveryCount](const FMessage&) noexcept { ++FirstDeliveryCount; });
	const EDelegateResult SecondBindingResult = SecondSubscriber.Bind([&SecondDeliveryCount](const FMessage&) noexcept { ++SecondDeliveryCount; });
	const EMessagingResult FirstSubscribeResult = ReceivingSystem.SubscribeToChannel("Telemetry", std::move(FirstSubscriber));
	const EMessagingResult SecondSubscribeResult = ReceivingSystem.SubscribeToChannel("Commands", std::move(SecondSubscriber));
	FRawWireFrame FirstFrame;
	FRawWireFrame SecondFrame;
	FirstFrame.Set("Telemetry", "TemperatureUpdated", TSpan<const std::uint8_t>(nullptr, ZeroPayloadByteCount));
	SecondFrame.Set("Commands", "CommandReceived", TSpan<const std::uint8_t>(nullptr, ZeroPayloadByteCount));
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);

	// Act
	const ETransportResult FirstRawSendResult =
		Network.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(FirstFrame.Bytes, FirstFrame.Size));
	const ETransportResult SecondRawSendResult =
		Network.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(SecondFrame.Bytes, SecondFrame.Size));
	ReceivingSystem.PreAdvance(FirstReceiveTurnMilliseconds);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstCreateResult, "The first shared-device channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondCreateResult, "The second shared-device channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, FirstBindingResult, "The first shared-device subscriber should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SecondBindingResult, "The second shared-device subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstSubscribeResult, "The first shared-device subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondSubscribeResult, "The second shared-device subscriber should register");
	MW_EXPECT_EQ(Test, ETransportResult::Success, FirstRawSendResult, "The first encoded channel frame should queue");
	MW_EXPECT_EQ(Test, ETransportResult::Success, SecondRawSendResult, "The second encoded channel frame should queue");
	MW_EXPECT_EQ(Test, OneDelivery, FirstDeliveryCount, "The first encoded channel should reach only its subscriber");
	MW_EXPECT_EQ(Test, OneDelivery, SecondDeliveryCount, "The second encoded channel should reach only its subscriber");
}

/**
 * Motivation: Keeps incoming traffic for nonexistent channels observable without leaking it to live subscribers.
 * Responsibilities: Deliver one raw frame with an unknown channel id and verify the receiver counts it as dropped.
 */
MW_TEST_CASE(MessagingSystem_CountsUnknownWireChannelsAsDroppedFrames)
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
	Frame.Set("Unknown", "TemperatureUpdated", TSpan<const std::uint8_t>(nullptr, ZeroPayloadByteCount));
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);

	// Act
	const ETransportResult RawSendResult = Network.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(Frame.Bytes, Frame.Size));
	ReceivingSystem.PreAdvance(FirstReceiveTurnMilliseconds);
	const std::uint32_t DroppedFrameCount = ReceivingSystem.GetDroppedFrameCount();

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The live receiver channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The live receiver subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The live receiver subscriber should register");
	MW_EXPECT_EQ(Test, ETransportResult::Success, RawSendResult, "The unknown-channel frame should reach the receiver device");
	MW_EXPECT_EQ(Test, NoDeliveries, DeliveryCount, "An unknown encoded channel should deliver to no live subscriber");
	MW_EXPECT_EQ(Test, OneDroppedFrame, DroppedFrameCount, "An unknown encoded channel should increment the dropped frame count");
}

/**
 * Motivation: Makes truncated device data safe and diagnosable rather than a decoder crash.
 * Responsibilities: Send raw bytes shorter than the wire header directly through loopback and verify one receiver drop.
 */
MW_TEST_CASE(MessagingSystem_CountsTooShortWireFramesAsDropped)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> Network;
	FMessagingSystem ReceivingSystem;
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FChannelInformation ReceivingChannel{"Telemetry", false, &Network.Port(ReceivingPort), SendingAddress};
	const EMessagingResult CreateResult = ReceivingSystem.CreateChannel(ReceivingChannel);
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);

	// Act
	const ETransportResult RawSendResult =
		Network.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(TooShortFrame, TooShortFrameByteCount));
	ReceivingSystem.PreAdvance(FirstReceiveTurnMilliseconds);
	const std::uint32_t DroppedFrameCount = ReceivingSystem.GetDroppedFrameCount();

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The malformed-frame receiver channel should be created");
	MW_EXPECT_EQ(Test, ETransportResult::Success, RawSendResult, "The raw short frame should reach the receiver device");
	MW_EXPECT_EQ(Test, OneDroppedFrame, DroppedFrameCount, "A frame shorter than the header should be counted as dropped");
}

/**
 * Motivation: Pins the concrete best-effort application payload boundary at the largest accepted message.
 * Responsibilities: Send exactly MaxMessageBytes and verify synchronous local delivery plus one complete device packet.
 */
MW_TEST_CASE(MessagingSystem_AcceptsExactMaximumBestEffortPayload)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> Network;
	FMessagingSystem SendingSystem;
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FChannelInformation SendingChannel{"Telemetry", false, &Network.Port(SendingPort), ReceivingAddress};
	const EMessagingResult CreateResult = SendingSystem.CreateChannel(SendingChannel);
	FWireMessageRecorder Recorder;
	FSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&Recorder](const FMessage& InMessage) noexcept { Recorder.Record(InMessage); });
	const EMessagingResult SubscribeResult = SendingSystem.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	Message.SetPayload(TSpan<const std::uint8_t>(MaximumPayload.data(), MaximumPayload.size()));
	std::uint8_t FrameBytes[FMessagingSystem::MaxFrameBytes]{};
	FDeviceAddress FrameSender;
	FReceiveResult FrameReceiveResult;

	// Act
	const EMessagingResult SendResult = SendingSystem.SendMessageToChannel(Message, "Telemetry");
	const std::size_t QueuedAfterSend = Network.QueuedCount(ReceivingPort);
	const ETransportResult FrameReceiveStatus =
		Network.Port(ReceivingPort).TryReceive(FrameSender, TSpan<std::uint8_t>(FrameBytes, FMessagingSystem::MaxFrameBytes), FrameReceiveResult);
	bool bWirePayloadMatches = true;
	for (std::size_t PayloadByteIndex = 0; PayloadByteIndex < MaximumPayloadByteCount; ++PayloadByteIndex)
	{
		if (FrameBytes[WireHeaderBytes + PayloadByteIndex] != MaximumPayload[PayloadByteIndex])
		{
			bWirePayloadMatches = false;
			break;
		}
	}
	const std::uint8_t FinalPayloadByte = FrameBytes[WireHeaderBytes + MaximumPayloadByteCount - 1];

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The maximum-payload channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The maximum-payload local subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The maximum-payload local subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "A payload exactly MaxMessageBytes should send successfully");
	MW_EXPECT_EQ(Test, OneDelivery, Recorder.DeliveryCount, "The exact maximum payload should deliver locally once");
	MW_EXPECT_EQ(Test, MaximumPayloadByteCount, Recorder.PayloadSize, "The local subscriber should receive the exact maximum payload size");
	MW_EXPECT_EQ(Test, OneQueuedPacket, QueuedAfterSend, "The exact maximum payload should reach the device once");
	MW_EXPECT_EQ(Test, ETransportResult::Success, FrameReceiveStatus, "The exact maximum frame should be available for wire inspection");
	MW_EXPECT_EQ(Test, FMessagingSystem::MaxFrameBytes, FrameReceiveResult.BytesReceived, "The exact maximum payload should fill the wire frame");
	MW_EXPECT_TRUE(Test, bWirePayloadMatches, "The wire frame should contain every maximum-payload byte");
	MW_EXPECT_EQ(Test, MaximumPayloadFinalByte, FinalPayloadByte, "The wire frame should retain the maximum payload's final sentinel byte");
}

/**
 * Motivation: Guarantees local composition remains reliable to subscribers when the payload is one byte beyond the concrete frame budget.
 * Responsibilities: Send MaxMessageBytes plus one and verify local delivery, Full, and no device packet.
 */
MW_TEST_CASE(MessagingSystem_DeliversLocallyWhenPayloadExceedsMessagingFrameCapacity)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> Network;
	FMessagingSystem SendingSystem;
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FChannelInformation SendingChannel{"Telemetry", false, &Network.Port(SendingPort), ReceivingAddress};
	const EMessagingResult CreateResult = SendingSystem.CreateChannel(SendingChannel);
	FWireMessageRecorder Recorder;
	FSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&Recorder](const FMessage& InMessage) noexcept { Recorder.Record(InMessage); });
	const EMessagingResult SubscribeResult = SendingSystem.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	Message.SetPayload(TSpan<const std::uint8_t>(OversizedLocalPayload, OversizedLocalPayloadByteCount));

	// Act
	const EMessagingResult SendResult = SendingSystem.SendMessageToChannel(Message, "Telemetry");
	const std::size_t QueuedAfterSend = Network.QueuedCount(ReceivingPort);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The payload-boundary sending channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The payload-boundary local subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The payload-boundary local subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Full, SendResult, "A payload one byte beyond MaxMessageBytes should report Full");
	MW_EXPECT_EQ(Test, OneDelivery, Recorder.DeliveryCount, "The oversized wire payload should still deliver locally");
	MW_EXPECT_EQ(Test, OversizedLocalPayloadByteCount, Recorder.PayloadSize, "The local subscriber should receive every oversized payload byte");
	MW_EXPECT_EQ(Test, NoQueuedPackets, QueuedAfterSend, "A frame beyond MaxFrameBytes should not reach the device");
}

/**
 * Motivation: Maps a device's packet ceiling into the Messaging Full result without losing the synchronous local delivery.
 * Responsibilities: Send a frame valid for Messaging but too large for loopback and verify Full with no queued device packet.
 */
MW_TEST_CASE(MessagingSystem_ReturnsFullWhenDevicePacketCapacityIsSmallerThanFrame)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, SmallerThanFramePacketBytes> Network;
	FMessagingSystem SendingSystem;
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FChannelInformation SendingChannel{"Telemetry", false, &Network.Port(SendingPort), ReceivingAddress};
	const EMessagingResult CreateResult = SendingSystem.CreateChannel(SendingChannel);
	std::size_t LocalDeliveryCount = NoDeliveries;
	FSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&LocalDeliveryCount](const FMessage&) noexcept { ++LocalDeliveryCount; });
	const EMessagingResult SubscribeResult = SendingSystem.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	Message.SetPayload(TSpan<const std::uint8_t>(DeviceRejectedPayload, DeviceRejectedPayloadByteCount));

	// Act
	const EMessagingResult SendResult = SendingSystem.SendMessageToChannel(Message, "Telemetry");
	const std::size_t QueuedAfterSend = Network.QueuedCount(ReceivingPort);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The small-device sending channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The small-device local subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The small-device local subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Full, SendResult, "A device that cannot carry the accepted frame should report Full");
	MW_EXPECT_EQ(Test, OneDelivery, LocalDeliveryCount, "The device capacity failure should not undo local delivery");
	MW_EXPECT_EQ(Test, NoQueuedPackets, QueuedAfterSend, "A device capacity failure should queue no packet");
}

/**
 * Motivation: Keeps a persistent oversized device packet observable on every turn until its producer corrects the frame size.
 * Responsibilities: Send an oversized raw packet directly through a permissive loopback device and verify each receiver turn counts the retained
 *   head.
 */
MW_TEST_CASE(MessagingSystem_RecountsOversizedInboundPacketOnEachPreAdvance)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, LargerThanFramePacketBytes> Network;
	FMessagingSystem ReceivingSystem;
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FChannelInformation ReceivingChannel{"Telemetry", false, &Network.Port(ReceivingPort), SendingAddress};
	const EMessagingResult CreateResult = ReceivingSystem.CreateChannel(ReceivingChannel);
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);

	// Act
	const ETransportResult RawSendResult =
		Network.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(OversizedInboundFrame, OversizedInboundFrameByteCount));
	ReceivingSystem.PreAdvance(FirstReceiveTurnMilliseconds);
	const std::uint32_t DroppedAfterFirstTurn = ReceivingSystem.GetDroppedFrameCount();
	ReceivingSystem.PreAdvance(SecondReceiveTurnMilliseconds);
	const std::uint32_t DroppedAfterSecondTurn = ReceivingSystem.GetDroppedFrameCount();

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The oversized-frame receiver channel should be created");
	MW_EXPECT_EQ(Test, ETransportResult::Success, RawSendResult, "The permissive device should accept the oversized raw packet");
	MW_EXPECT_EQ(Test, OneDroppedFrame, DroppedAfterFirstTurn, "The first too-large receive attempt should count one dropped frame");
	MW_EXPECT_EQ(Test, TwoDroppedFrames, DroppedAfterSecondTurn, "The retained packet should be counted again on the next receiver turn");
}

/**
 * Motivation: Preserves local-only Messaging as a complete composition when no transport device is linked to a channel.
 * Responsibilities: Send locally on a null-device channel and verify the unrelated loopback stays empty while pre-advance drops nothing.
 */
MW_TEST_CASE(MessagingSystem_DeliversLocallyWithoutADeviceAndDropsNothingOnPreAdvance)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> Network;
	FMessagingSystem System;
	const FChannelInformation LocalChannel{"Telemetry", false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(LocalChannel);
	std::size_t DeliveryCount = NoDeliveries;
	FSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	const EMessagingResult SubscribeResult = System.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");

	// Act
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, "Telemetry");
	const std::size_t QueuedAfterSend = Network.QueuedCount(ReceivingPort);
	System.PreAdvance(FirstReceiveTurnMilliseconds);
	const std::uint32_t DroppedFrameCount = System.GetDroppedFrameCount();

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The local-only channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The local-only subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The local-only subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "A local-only message should succeed");
	MW_EXPECT_EQ(Test, OneDelivery, DeliveryCount, "A local-only channel should deliver to its subscriber");
	MW_EXPECT_EQ(Test, NoQueuedPackets, QueuedAfterSend, "A channel with no device should reach no loopback port");
	MW_EXPECT_EQ(Test, NoDroppedFrames, DroppedFrameCount, "Pre-advance with no devices should drop nothing");
}

} // namespace
