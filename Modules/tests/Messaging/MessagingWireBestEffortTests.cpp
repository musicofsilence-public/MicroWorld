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
	MW_EXPECT_TRUE(Test, ReceivingRecorder.SenderRoute.LinkId.IsValid(), "The receiver should preserve the registered source link");
	MW_EXPECT_EQ(Test, SendingAddress, ReceivingRecorder.SenderRoute.Address, "The receiver should preserve the complete sender route address");
	MW_EXPECT_EQ(Test, OneDelivery, SendingRecorder.DeliveryCount, "The sender pre-advance should not echo the loopback message to itself");
}

/**
 * Motivation: Prevents one device backlog from consuming unbounded receive work in a single Messaging turn.
 * Responsibilities: With a one-frame budget, deliver two queued frames across two pre-advance turns while retaining the deferred frame in FIFO.
 */
MW_TEST_CASE(MessagingSystem_DefersFramesBeyondPerDeviceReceiveBudget)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, TwoMailboxSlots, StandardPacketBytes> Network;
	FMessagingSystemInformation Information{};
	Information.MaxReceiveFramesPerDevicePerAdvance = 1;
	FMessagingSystem ReceivingSystem{Information};
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FChannelInformation ReceivingChannel{"Telemetry", false, &Network.Port(ReceivingPort), SendingAddress};
	const EMessagingResult CreateResult = ReceivingSystem.CreateChannel(ReceivingChannel);
	FWireMessageRecorder Recorder;
	FSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&Recorder](const FMessage& InMessage) noexcept { Recorder.Record(InMessage); });
	const EMessagingResult SubscribeResult = ReceivingSystem.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FRawWireFrame FirstFrame;
	FRawWireFrame SecondFrame;
	FirstFrame.Set("Telemetry", "TemperatureUpdated", TSpan<const std::uint8_t>(nullptr, ZeroPayloadByteCount));
	SecondFrame.Set("Telemetry", "CommandReceived", TSpan<const std::uint8_t>(nullptr, ZeroPayloadByteCount));
	const ETransportResult FirstRawSendResult =
		Network.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(FirstFrame.Bytes, FirstFrame.Size));
	const ETransportResult SecondRawSendResult =
		Network.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(SecondFrame.Bytes, SecondFrame.Size));

	// Act
	ReceivingSystem.PreAdvance(FirstReceiveTurnMilliseconds);
	const std::size_t DeliveriesAfterFirstTurn = Recorder.DeliveryCount;
	const FNameId FirstDeliveredMessageNameId = Recorder.MessageNameId;
	const std::size_t QueuedAfterFirstTurn = Network.QueuedCount(ReceivingPort);
	ReceivingSystem.PreAdvance(SecondReceiveTurnMilliseconds);
	const std::size_t DeliveriesAfterSecondTurn = Recorder.DeliveryCount;
	const FNameId SecondDeliveredMessageNameId = Recorder.MessageNameId;
	const std::size_t QueuedAfterSecondTurn = Network.QueuedCount(ReceivingPort);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The backlog receiver channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The backlog receiver subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The backlog receiver subscriber should register");
	MW_EXPECT_EQ(Test, ETransportResult::Success, FirstRawSendResult, "The first backlog frame should queue");
	MW_EXPECT_EQ(Test, ETransportResult::Success, SecondRawSendResult, "The second backlog frame should queue");
	MW_EXPECT_EQ(Test, OneDelivery, DeliveriesAfterFirstTurn, "The first turn should deliver only one frame from the device");
	MW_EXPECT_EQ(Test, FNameId{"TemperatureUpdated"}, FirstDeliveredMessageNameId, "The first turn should preserve the first queued frame");
	MW_EXPECT_EQ(Test, OneQueuedPacket, QueuedAfterFirstTurn, "The first turn should retain one deferred frame in the device queue");
	MW_EXPECT_EQ(Test, TwoDeliveries, DeliveriesAfterSecondTurn, "The second turn should deliver the retained frame");
	MW_EXPECT_EQ(Test, FNameId{"CommandReceived"}, SecondDeliveredMessageNameId, "The second turn should deliver the deferred second frame");
	MW_EXPECT_EQ(Test, NoQueuedPackets, QueuedAfterSecondTurn, "The second turn should leave the device queue empty");
}

/**
 * Motivation: Gives independent devices separate bounded opportunities to deliver traffic during the same Messaging turn.
 * Responsibilities: Queue two frames on each of two devices, apply a one-frame budget, and verify each delivers one while retaining one.
 */
MW_TEST_CASE(MessagingSystem_AppliesReceiveBudgetIndependentlyPerDevice)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, TwoMailboxSlots, StandardPacketBytes> FirstNetwork;
	TLoopbackNetwork<TwoPorts, TwoMailboxSlots, StandardPacketBytes> SecondNetwork;
	FMessagingSystemInformation Information{};
	Information.MaxReceiveFramesPerDevicePerAdvance = 1;
	FMessagingSystem ReceivingSystem{Information};
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FChannelInformation FirstChannel{"Telemetry", false, &FirstNetwork.Port(ReceivingPort), SendingAddress};
	const FChannelInformation SecondChannel{"Commands", false, &SecondNetwork.Port(ReceivingPort), SendingAddress};
	const EMessagingResult FirstCreateResult = ReceivingSystem.CreateChannel(FirstChannel);
	const EMessagingResult SecondCreateResult = ReceivingSystem.CreateChannel(SecondChannel);
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
	const ETransportResult FirstDeviceFirstSendResult =
		FirstNetwork.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(FirstFrame.Bytes, FirstFrame.Size));
	const ETransportResult FirstDeviceSecondSendResult =
		FirstNetwork.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(FirstFrame.Bytes, FirstFrame.Size));
	const ETransportResult SecondDeviceFirstSendResult =
		SecondNetwork.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(SecondFrame.Bytes, SecondFrame.Size));
	const ETransportResult SecondDeviceSecondSendResult =
		SecondNetwork.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(SecondFrame.Bytes, SecondFrame.Size));

	// Act
	ReceivingSystem.PreAdvance(FirstReceiveTurnMilliseconds);
	const std::size_t FirstQueuedAfterTurn = FirstNetwork.QueuedCount(ReceivingPort);
	const std::size_t SecondQueuedAfterTurn = SecondNetwork.QueuedCount(ReceivingPort);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstCreateResult, "The first independent-device channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondCreateResult, "The second independent-device channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, FirstBindingResult, "The first independent-device subscriber should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SecondBindingResult, "The second independent-device subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstSubscribeResult, "The first independent-device subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondSubscribeResult, "The second independent-device subscriber should register");
	MW_EXPECT_EQ(Test, ETransportResult::Success, FirstDeviceFirstSendResult, "The first frame for the first device should queue");
	MW_EXPECT_EQ(Test, ETransportResult::Success, FirstDeviceSecondSendResult, "The second frame for the first device should queue");
	MW_EXPECT_EQ(Test, ETransportResult::Success, SecondDeviceFirstSendResult, "The first frame for the second device should queue");
	MW_EXPECT_EQ(Test, ETransportResult::Success, SecondDeviceSecondSendResult, "The second frame for the second device should queue");
	MW_EXPECT_EQ(Test, OneDelivery, FirstDeliveryCount, "The first device should deliver its independent one-frame budget");
	MW_EXPECT_EQ(Test, OneDelivery, SecondDeliveryCount, "The second device should deliver its independent one-frame budget");
	MW_EXPECT_EQ(Test, OneQueuedPacket, FirstQueuedAfterTurn, "The first device should retain one frame after its budget is consumed");
	MW_EXPECT_EQ(Test, OneQueuedPacket, SecondQueuedAfterTurn, "The second device should retain one frame after its budget is consumed");
}

/**
 * Motivation: Makes zero an explicit opt-out from inbound device work rather than an implicit minimum receive allowance.
 * Responsibilities: Queue one valid frame, run a zero-budget pre-advance, and verify no delivery while the device retains the frame.
 */
MW_TEST_CASE(MessagingSystem_ZeroReceiveBudgetLeavesWireFramesQueued)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> Network;
	FMessagingSystemInformation Information{};
	Information.MaxReceiveFramesPerDevicePerAdvance = 0;
	FMessagingSystem ReceivingSystem{Information};
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FChannelInformation ReceivingChannel{"Telemetry", false, &Network.Port(ReceivingPort), SendingAddress};
	const EMessagingResult CreateResult = ReceivingSystem.CreateChannel(ReceivingChannel);
	std::size_t DeliveryCount = NoDeliveries;
	FSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	const EMessagingResult SubscribeResult = ReceivingSystem.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FRawWireFrame Frame;
	Frame.Set("Telemetry", "TemperatureUpdated", TSpan<const std::uint8_t>(nullptr, ZeroPayloadByteCount));
	const ETransportResult RawSendResult = Network.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(Frame.Bytes, Frame.Size));
	const std::size_t QueuedBeforeTurn = Network.QueuedCount(ReceivingPort);

	// Act
	ReceivingSystem.PreAdvance(FirstReceiveTurnMilliseconds);
	const std::size_t QueuedAfterTurn = Network.QueuedCount(ReceivingPort);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The zero-budget receiver channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The zero-budget receiver subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The zero-budget receiver subscriber should register");
	MW_EXPECT_EQ(Test, ETransportResult::Success, RawSendResult, "The zero-budget frame should queue before pre-advance");
	MW_EXPECT_EQ(Test, OneQueuedPacket, QueuedBeforeTurn, "One frame should be waiting before the zero-budget turn");
	MW_EXPECT_EQ(Test, NoDeliveries, DeliveryCount, "A zero receive budget should deliver no wire frame");
	MW_EXPECT_EQ(Test, OneQueuedPacket, QueuedAfterTurn, "A zero receive budget should leave the wire frame queued");
}

/**
 * Motivation: Prevents invalid traffic from bypassing the per-device receive-work limit.
 * Responsibilities: With a one-frame budget, consume an unknown-channel frame first, retain a valid frame, then deliver it next turn.
 */
MW_TEST_CASE(MessagingSystem_InvalidFramesConsumePerDeviceReceiveBudget)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, TwoMailboxSlots, StandardPacketBytes> Network;
	FMessagingSystemInformation Information{};
	Information.MaxReceiveFramesPerDevicePerAdvance = 1;
	FMessagingSystem ReceivingSystem{Information};
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FChannelInformation ReceivingChannel{"Telemetry", false, &Network.Port(ReceivingPort), SendingAddress};
	const EMessagingResult CreateResult = ReceivingSystem.CreateChannel(ReceivingChannel);
	std::size_t DeliveryCount = NoDeliveries;
	FSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	const EMessagingResult SubscribeResult = ReceivingSystem.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FRawWireFrame UnknownChannelFrame;
	FRawWireFrame ValidFrame;
	UnknownChannelFrame.Set("Unknown", "Ignored", TSpan<const std::uint8_t>(nullptr, ZeroPayloadByteCount));
	ValidFrame.Set("Telemetry", "TemperatureUpdated", TSpan<const std::uint8_t>(nullptr, ZeroPayloadByteCount));
	const ETransportResult UnknownChannelSendResult =
		Network.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(UnknownChannelFrame.Bytes, UnknownChannelFrame.Size));
	const ETransportResult ValidSendResult =
		Network.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(ValidFrame.Bytes, ValidFrame.Size));

	// Act
	ReceivingSystem.PreAdvance(FirstReceiveTurnMilliseconds);
	const std::size_t DeliveriesAfterFirstTurn = DeliveryCount;
	const std::size_t QueuedAfterFirstTurn = Network.QueuedCount(ReceivingPort);
	const std::uint32_t DroppedFramesAfterFirstTurn = ReceivingSystem.GetDroppedFrameCount();
	ReceivingSystem.PreAdvance(SecondReceiveTurnMilliseconds);
	const std::size_t DeliveriesAfterSecondTurn = DeliveryCount;
	const std::size_t QueuedAfterSecondTurn = Network.QueuedCount(ReceivingPort);
	const std::uint32_t DroppedFramesAfterSecondTurn = ReceivingSystem.GetDroppedFrameCount();

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The invalid-frame budget receiver channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The invalid-frame budget subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The invalid-frame budget subscriber should register");
	MW_EXPECT_EQ(Test, ETransportResult::Success, UnknownChannelSendResult, "The unknown-channel frame should queue first");
	MW_EXPECT_EQ(Test, ETransportResult::Success, ValidSendResult, "The valid frame should queue second");
	MW_EXPECT_EQ(Test, NoDeliveries, DeliveriesAfterFirstTurn, "The invalid first frame should produce no delivery");
	MW_EXPECT_EQ(Test, OneQueuedPacket, QueuedAfterFirstTurn, "The invalid frame should consume the first turn's receive budget");
	MW_EXPECT_EQ(Test, OneDroppedFrame, DroppedFramesAfterFirstTurn, "The invalid frame should count as dropped");
	MW_EXPECT_EQ(Test, OneDelivery, DeliveriesAfterSecondTurn, "The valid deferred frame should deliver on the second turn");
	MW_EXPECT_EQ(Test, NoQueuedPackets, QueuedAfterSecondTurn, "The second turn should empty the device queue");
	MW_EXPECT_EQ(Test, OneDroppedFrame, DroppedFramesAfterSecondTurn, "Delivering the valid frame should not add another drop");
}

/**
 * Motivation: Pins the receive budget to an exact device-call bound without allowing an extra empty probe.
 * Responsibilities: Queue exactly four valid frames, run a four-frame turn, and verify four deliveries, an empty queue, and exactly four receives.
 */
MW_TEST_CASE(MessagingSystem_MakesNoExtraReceiveProbeAfterExactBudget)
{
	// Arrange
	constexpr std::uint8_t ExactReceiveFrameCount = 4;
	TLoopbackNetwork<TwoPorts, ExactReceiveFrameCount, StandardPacketBytes> Network;
	FReceiveCountingDevice CountingDevice{Network.Port(ReceivingPort)};
	FMessagingSystemInformation Information{};
	Information.MaxReceiveFramesPerDevicePerAdvance = ExactReceiveFrameCount;
	FMessagingSystem ReceivingSystem{Information};
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FChannelInformation ReceivingChannel{"Telemetry", false, &CountingDevice, SendingAddress};
	const EMessagingResult CreateResult = ReceivingSystem.CreateChannel(ReceivingChannel);
	std::size_t DeliveryCount = NoDeliveries;
	FSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	const EMessagingResult SubscribeResult = ReceivingSystem.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FRawWireFrame Frame;
	Frame.Set("Telemetry", "TemperatureUpdated", TSpan<const std::uint8_t>(nullptr, ZeroPayloadByteCount));
	const ETransportResult FirstRawSendResult =
		Network.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(Frame.Bytes, Frame.Size));
	const ETransportResult SecondRawSendResult =
		Network.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(Frame.Bytes, Frame.Size));
	const ETransportResult ThirdRawSendResult =
		Network.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(Frame.Bytes, Frame.Size));
	const ETransportResult FourthRawSendResult =
		Network.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(Frame.Bytes, Frame.Size));

	// Act
	ReceivingSystem.PreAdvance(FirstReceiveTurnMilliseconds);
	const std::size_t QueuedAfterTurn = Network.QueuedCount(ReceivingPort);
	const std::size_t ReceiveCallCount = CountingDevice.GetReceiveCallCount();

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The exact-budget receiver channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The exact-budget receiver subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The exact-budget receiver subscriber should register");
	MW_EXPECT_EQ(Test, ETransportResult::Success, FirstRawSendResult, "The first exact-budget frame should queue");
	MW_EXPECT_EQ(Test, ETransportResult::Success, SecondRawSendResult, "The second exact-budget frame should queue");
	MW_EXPECT_EQ(Test, ETransportResult::Success, ThirdRawSendResult, "The third exact-budget frame should queue");
	MW_EXPECT_EQ(Test, ETransportResult::Success, FourthRawSendResult, "The fourth exact-budget frame should queue");
	MW_EXPECT_EQ(
		Test, static_cast<std::size_t>(ExactReceiveFrameCount), DeliveryCount, "The exact four-frame budget should deliver all four queued frames");
	MW_EXPECT_EQ(Test, NoQueuedPackets, QueuedAfterTurn, "The exact four-frame budget should leave the device queue empty");
	MW_EXPECT_EQ(
		Test,
		static_cast<std::size_t>(ExactReceiveFrameCount),
		ReceiveCallCount,
		"The exact four-frame budget should make four receive calls and no empty probe");
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
 * Motivation: Lets several named channels share one radio without multiplying that device's bounded receive work.
 * Responsibilities: Queue frames for two channel ids on one shared device with a one-frame budget, then verify routing across two turns and one
 *   combined device allowance per turn.
 */
MW_TEST_CASE(MessagingSystem_RoutesSharedDeviceChannelsWithinOneCombinedReceiveBudget)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, TwoMailboxSlots, StandardPacketBytes> Network;
	FMessagingSystemInformation Information{};
	Information.MaxReceiveFramesPerDevicePerAdvance = 1;
	FMessagingSystem ReceivingSystem{Information};
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
	const std::size_t FirstDeliveriesAfterFirstTurn = FirstDeliveryCount;
	const std::size_t SecondDeliveriesAfterFirstTurn = SecondDeliveryCount;
	const std::size_t QueuedAfterFirstTurn = Network.QueuedCount(ReceivingPort);
	ReceivingSystem.PreAdvance(SecondReceiveTurnMilliseconds);
	const std::size_t QueuedAfterSecondTurn = Network.QueuedCount(ReceivingPort);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstCreateResult, "The first shared-device channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondCreateResult, "The second shared-device channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, FirstBindingResult, "The first shared-device subscriber should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SecondBindingResult, "The second shared-device subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstSubscribeResult, "The first shared-device subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondSubscribeResult, "The second shared-device subscriber should register");
	MW_EXPECT_EQ(Test, ETransportResult::Success, FirstRawSendResult, "The first encoded channel frame should queue");
	MW_EXPECT_EQ(Test, ETransportResult::Success, SecondRawSendResult, "The second encoded channel frame should queue");
	MW_EXPECT_EQ(Test, OneDelivery, FirstDeliveriesAfterFirstTurn, "The first queued channel should consume the shared device's first turn budget");
	MW_EXPECT_EQ(Test, NoDeliveries, SecondDeliveriesAfterFirstTurn, "The second queued channel should remain deferred after the shared budget");
	MW_EXPECT_EQ(Test, OneQueuedPacket, QueuedAfterFirstTurn, "The shared device should retain one frame after its combined budget is consumed");
	MW_EXPECT_EQ(Test, OneDelivery, FirstDeliveryCount, "The first encoded channel should reach only its subscriber");
	MW_EXPECT_EQ(Test, OneDelivery, SecondDeliveryCount, "The second encoded channel should reach only its subscriber on the next turn");
	MW_EXPECT_EQ(Test, NoQueuedPackets, QueuedAfterSecondTurn, "The second shared-device turn should empty the retained queue");
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
