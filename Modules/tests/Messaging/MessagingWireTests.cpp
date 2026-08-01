#include "TestSupport.h"

#include <MicroWorld/Messaging/MessagingSystem.h>
#include <MicroWorld/Transport/LoopbackNetwork.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{

using MicroWorld::Core::EDelegateResult;
using MicroWorld::Core::ETransportResult;
using MicroWorld::Core::FDeviceAddress;
using MicroWorld::Core::MakeLoopbackAddress;
using MicroWorld::Core::TimePointMilliseconds;
using MicroWorld::Core::TSpan;
using MicroWorld::Messaging::EMessagingResult;
using MicroWorld::Messaging::FChannelInformation;
using MicroWorld::Messaging::FDefaultMessagingTraits;
using MicroWorld::Messaging::FMessage;
using MicroWorld::Messaging::FNameId;
using MicroWorld::Messaging::TMessagingSystem;
using MicroWorld::Transport::TLoopbackNetwork;

/** Motivation: Names the standard system whose frame limit the wire-path cases exercise. */
using FDefaultMessagingSystem = TMessagingSystem<>;

/** Motivation: Names the standard bounded subscriber delegate without repeating its system-qualified declaration. */
using FDefaultSubscriberDelegate = FDefaultMessagingSystem::FSubscriberDelegate;

/** Motivation: Identifies the port that originates cross-system test traffic. */
constexpr std::uint8_t SendingPort = 0;
/** Motivation: Identifies the port whose Messaging system receives cross-system test traffic. */
constexpr std::uint8_t ReceivingPort = 1;
/** Motivation: Supplies the smallest loopback network that can carry traffic between two systems. */
constexpr std::size_t TwoPorts = 2;
/** Motivation: Retains one packet while a receiver waits for its explicit pre-advance turn. */
constexpr std::size_t OneMailboxSlot = 1;
/** Motivation: Retains two raw frames for shared-device draining in one receiver turn. */
constexpr std::size_t TwoMailboxSlots = 2;
/** Motivation: Gives ordinary wire cases enough capacity for a complete default Messaging frame. */
constexpr std::size_t StandardPacketBytes = FDefaultMessagingSystem::MaxFrameBytes;
/** Motivation: Makes a device reject a valid default Messaging frame that is larger than this transport packet limit. */
constexpr std::size_t SmallerThanFramePacketBytes = FDefaultMessagingSystem::FrameHeaderBytes + 1;
/** Motivation: Makes direct inbound traffic exceed the receiving default Messaging frame buffer by one byte. */
constexpr std::size_t LargerThanFramePacketBytes = FDefaultMessagingSystem::MaxFrameBytes + 1;
// The frame layout below is restated here on purpose, independently of the production constants. A test that imported the encoder's own
// offsets would agree with any layout the encoder happened to produce, including a wrong one.
/** Motivation: Names the number of bytes used by one 32-bit encoded name id. */
constexpr std::size_t NameIdBytes = sizeof(std::uint32_t);
/** Motivation: Names the bit width of one wire byte for explicit little-endian frame setup. */
constexpr std::size_t BitsPerByte = 8;
/** Motivation: Marks the first encoded name id at the front of every raw test frame. */
constexpr std::size_t ChannelNameIdByteIndex = 0;
/** Motivation: Marks the message name id immediately after the encoded channel name id. */
constexpr std::size_t MessageNameIdByteIndex = ChannelNameIdByteIndex + NameIdBytes;
/** Motivation: Names the complete encoded wire header used by malformed-frame and raw-frame cases. */
constexpr std::size_t WireHeaderBytes = MessageNameIdByteIndex + NameIdBytes;
/** Motivation: States the number of bytes used by the ordinary cross-system payload. */
constexpr std::size_t WirePayloadByteCount = 3;
/** Motivation: States the number of bytes delivered locally before an oversized wire frame is rejected. */
constexpr std::size_t OversizedLocalPayloadByteCount = 3;
/** Motivation: States the number of bytes accepted locally before a small device rejects the wire frame. */
constexpr std::size_t DeviceRejectedPayloadByteCount = 3;
/** Motivation: States the number of bytes in a raw packet that cannot fit the receiver's frame buffer. */
constexpr std::size_t OversizedInboundFrameByteCount = LargerThanFramePacketBytes;
/** Motivation: States the number of bytes in a raw packet shorter than the required wire header. */
constexpr std::size_t TooShortFrameByteCount = WireHeaderBytes - 1;
/** Motivation: Names the empty application payload used to prove header-only frame delivery. */
constexpr std::size_t ZeroPayloadByteCount = 0;
/** Motivation: Names the first deterministic inbound Messaging turn. */
constexpr TimePointMilliseconds FirstReceiveTurnMilliseconds = 100;
/** Motivation: Names the second deterministic inbound Messaging turn used to prove retained oversized packets. */
constexpr TimePointMilliseconds SecondReceiveTurnMilliseconds = 200;
/** Motivation: Names the sender-side turn that proves loopback traffic does not echo to its origin port. */
constexpr TimePointMilliseconds SenderReceiveTurnMilliseconds = 300;
/** Motivation: Names the expected absence of subscriber delivery before a receiver pre-advance turn. */
constexpr std::size_t NoDeliveries = 0;
/** Motivation: Names one expected synchronous or routed subscriber delivery. */
constexpr std::size_t OneDelivery = 1;
/** Motivation: Names the expected absence of any queued device packet after a rejected wire send. */
constexpr std::size_t NoQueuedPackets = 0;
/** Motivation: Names the expected absence of dropped inbound frames. */
constexpr std::uint32_t NoDroppedFrames = 0;
/** Motivation: Names one dropped inbound frame after one malformed or unroutable receive. */
constexpr std::uint32_t OneDroppedFrame = 1;
/** Motivation: Names two cumulative dropped inbound observations of the retained oversized packet. */
constexpr std::uint32_t TwoDroppedFrames = 2;

/** Motivation: Supplies known application bytes for the ordinary cross-system round-trip. */
constexpr std::uint8_t WirePayload[WirePayloadByteCount] = {11, 22, 33};
/** Motivation: Supplies a payload that exceeds the small test system's wire frame budget by one byte. */
constexpr std::uint8_t OversizedLocalPayload[OversizedLocalPayloadByteCount] = {41, 42, 43};
/** Motivation: Supplies a payload valid for Messaging but larger than the selected device packet capacity once framed. */
constexpr std::uint8_t DeviceRejectedPayload[DeviceRejectedPayloadByteCount] = {51, 52, 53};
/** Motivation: Supplies an incomplete raw frame that has no complete pair of encoded name ids. */
constexpr std::uint8_t TooShortFrame[TooShortFrameByteCount] = {};
/** Motivation: Supplies a raw packet the device accepts but the receiving Messaging frame buffer cannot hold. */
constexpr std::uint8_t OversizedInboundFrame[OversizedInboundFrameByteCount] = {};

/**
 * Motivation: Captures delivered wire-message facts after Messaging releases its transient inbound payload view.
 * Responsibilities: Count deliveries and copy the message identity, sender, and bounded application bytes for later assertions.
 * Example:
 *   FWireMessageRecorder Recorder;
 *   Recorder.Record(Message);
 */
struct FWireMessageRecorder final
{
	/** Motivation: Bounds copied application bytes to the largest payload used by this test source. */
	static constexpr std::size_t MaxRecordedPayloadBytes = OversizedLocalPayloadByteCount;

	/**
	 * Motivation: Makes one synchronous subscriber callback observable after its borrowed payload bytes expire.
	 * Responsibilities: Count the delivery and copy its name, sender, payload size, and bounded payload bytes.
	 */
	void Record(const FMessage& InMessage) noexcept
	{
		++DeliveryCount;
		MessageNameId = InMessage.GetMessageNameId();
		Sender = InMessage.GetSender();
		PayloadSize = InMessage.GetPayload().Size();

		const std::size_t CopiedByteCount = PayloadSize < MaxRecordedPayloadBytes ? PayloadSize : MaxRecordedPayloadBytes;
		for (std::size_t ByteIndex = 0; ByteIndex < CopiedByteCount; ++ByteIndex)
		{
			PayloadBytes[ByteIndex] = InMessage.GetPayload()[ByteIndex];
		}
	}

	/** Motivation: Counts synchronous local and routed wire deliveries observed by this subscriber. */
	std::size_t DeliveryCount{0};

	/** Motivation: Preserves the most recently delivered message name for routing assertions. */
	FNameId MessageNameId{};

	/** Motivation: Preserves the source device address supplied by an inbound wire frame. */
	FDeviceAddress Sender{};

	/** Motivation: Preserves the byte length of the most recently delivered application payload. */
	std::size_t PayloadSize{0};

	/** Motivation: Retains copied payload bytes after Messaging reuses its inbound frame buffer. */
	std::uint8_t PayloadBytes[MaxRecordedPayloadBytes]{};
};

/**
 * Motivation: Provides a bounded raw frame builder for inbound malformed and routing tests without using Messaging's send path.
 * Responsibilities: Encode the two name ids in the documented little-endian wire order and retain an optional bounded application payload.
 * Example:
 *   FRawWireFrame Frame; Frame.Set("Telemetry", "Updated", TSpan<const std::uint8_t>(nullptr, 0));
 */
struct FRawWireFrame final
{
	/**
	 * Motivation: Lets raw-frame tests address a live or unknown channel without exercising Messaging's encoder.
	 * Responsibilities: Write the supplied channel and message ids followed by every supplied payload byte and record the complete frame size; the
	 *   caller keeps the header plus its payload within the fixed frame buffer.
	 */
	void Set(const FNameId InChannelNameId, const FNameId InMessageNameId, const TSpan<const std::uint8_t> InPayload) noexcept
	{
		WriteNameId(&Bytes[ChannelNameIdByteIndex], InChannelNameId);
		WriteNameId(&Bytes[MessageNameIdByteIndex], InMessageNameId);
		for (std::size_t PayloadByteIndex = 0; PayloadByteIndex < InPayload.Size(); ++PayloadByteIndex)
		{
			Bytes[WireHeaderBytes + PayloadByteIndex] = InPayload[PayloadByteIndex];
		}
		Size = WireHeaderBytes + InPayload.Size();
	}

	/**
	 * Motivation: Makes the test's raw bytes match the public wire contract on every host byte order.
	 * Responsibilities: Write one 32-bit Messaging name id least-significant byte first at OutDestination.
	 */
	static void WriteNameId(std::uint8_t* const OutDestination, const FNameId InNameId) noexcept
	{
		for (std::size_t ByteIndex = 0; ByteIndex < NameIdBytes; ++ByteIndex)
		{
			OutDestination[ByteIndex] = static_cast<std::uint8_t>(InNameId.Value >> (ByteIndex * BitsPerByte));
		}
	}

	/** Motivation: Retains the full default-sized frame buffer for direct loopback sends. */
	std::uint8_t Bytes[FDefaultMessagingSystem::MaxFrameBytes]{};

	/** Motivation: Records how many leading frame bytes are valid for the direct loopback send. */
	std::size_t Size{0};
};

/**
 * Motivation: Shrinks the Messaging frame limit so the local-before-wire-failure contract has a compact boundary input.
 * Responsibilities: Override only the message payload capacity while inheriting every future default trait member unchanged.
 * Example:
 *   TMessagingSystem<FSmallFrameMessagingTraits> System;
 */
struct FSmallFrameMessagingTraits : FDefaultMessagingTraits
{
	/** Motivation: Limits the test system's application payload to two bytes before the wire header is added. */
	static constexpr std::size_t MaxMessageBytes = 2;
};

/**
 * Motivation: Proves end-to-end routing preserves message bytes, sender identity, explicit receiver timing, and local-only origin delivery.
 * Responsibilities: Send through two loopback ports, assert no early or echoed delivery, then verify both systems' subscribers observe their
 *   required results.
 */
MW_TEST_CASE(MessagingSystem_RoutesWireMessagesAfterReceiverPreAdvanceWithoutEcho)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> Network;
	FDefaultMessagingSystem SendingSystem;
	FDefaultMessagingSystem ReceivingSystem;
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FChannelInformation SendingChannel{"Telemetry", false, &Network.Port(SendingPort), ReceivingAddress};
	const FChannelInformation ReceivingChannel{"Telemetry", false, &Network.Port(ReceivingPort), SendingAddress};
	const EMessagingResult SendingCreateResult = SendingSystem.CreateChannel(SendingChannel);
	const EMessagingResult ReceivingCreateResult = ReceivingSystem.CreateChannel(ReceivingChannel);
	FWireMessageRecorder SendingRecorder;
	FWireMessageRecorder ReceivingRecorder;
	FDefaultSubscriberDelegate SendingSubscriber;
	FDefaultSubscriberDelegate ReceivingSubscriber;
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
	FDefaultMessagingSystem ReceivingSystem;
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FChannelInformation ReceivingChannel{"Telemetry", false, &Network.Port(ReceivingPort), SendingAddress};
	const EMessagingResult CreateResult = ReceivingSystem.CreateChannel(ReceivingChannel);
	std::size_t MatchingDeliveryCount = NoDeliveries;
	std::size_t MismatchedDeliveryCount = NoDeliveries;
	FDefaultSubscriberDelegate MatchingSubscriber;
	FDefaultSubscriberDelegate MismatchedSubscriber;
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
	FDefaultMessagingSystem ReceivingSystem;
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FChannelInformation FirstReceivingChannel{"Telemetry", false, &Network.Port(ReceivingPort), SendingAddress};
	const FChannelInformation SecondReceivingChannel{"Commands", false, &Network.Port(ReceivingPort), SendingAddress};
	const EMessagingResult FirstCreateResult = ReceivingSystem.CreateChannel(FirstReceivingChannel);
	const EMessagingResult SecondCreateResult = ReceivingSystem.CreateChannel(SecondReceivingChannel);
	std::size_t FirstDeliveryCount = NoDeliveries;
	std::size_t SecondDeliveryCount = NoDeliveries;
	FDefaultSubscriberDelegate FirstSubscriber;
	FDefaultSubscriberDelegate SecondSubscriber;
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
	FDefaultMessagingSystem ReceivingSystem;
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FChannelInformation ReceivingChannel{"Telemetry", false, &Network.Port(ReceivingPort), SendingAddress};
	const EMessagingResult CreateResult = ReceivingSystem.CreateChannel(ReceivingChannel);
	std::size_t DeliveryCount = NoDeliveries;
	FDefaultSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	const EMessagingResult SubscribeResult = ReceivingSystem.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FRawWireFrame Frame;
	Frame.Set("Unknown", "TemperatureUpdated", TSpan<const std::uint8_t>(nullptr, ZeroPayloadByteCount));
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);

	// Act
	const ETransportResult RawSendResult = Network.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(Frame.Bytes, Frame.Size));
	ReceivingSystem.PreAdvance(FirstReceiveTurnMilliseconds);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The live receiver channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The live receiver subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The live receiver subscriber should register");
	MW_EXPECT_EQ(Test, ETransportResult::Success, RawSendResult, "The unknown-channel frame should reach the receiver device");
	MW_EXPECT_EQ(Test, NoDeliveries, DeliveryCount, "An unknown encoded channel should deliver to no live subscriber");
	MW_EXPECT_EQ(
		Test, OneDroppedFrame, ReceivingSystem.GetDroppedFrameCount(), "An unknown encoded channel should increment the dropped frame count");
}

/**
 * Motivation: Makes truncated device data safe and diagnosable rather than a decoder crash.
 * Responsibilities: Send raw bytes shorter than the wire header directly through loopback and verify one receiver drop.
 */
MW_TEST_CASE(MessagingSystem_CountsTooShortWireFramesAsDropped)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> Network;
	FDefaultMessagingSystem ReceivingSystem;
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FChannelInformation ReceivingChannel{"Telemetry", false, &Network.Port(ReceivingPort), SendingAddress};
	const EMessagingResult CreateResult = ReceivingSystem.CreateChannel(ReceivingChannel);
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);

	// Act
	const ETransportResult RawSendResult =
		Network.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(TooShortFrame, TooShortFrameByteCount));
	ReceivingSystem.PreAdvance(FirstReceiveTurnMilliseconds);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The malformed-frame receiver channel should be created");
	MW_EXPECT_EQ(Test, ETransportResult::Success, RawSendResult, "The raw short frame should reach the receiver device");
	MW_EXPECT_EQ(Test, OneDroppedFrame, ReceivingSystem.GetDroppedFrameCount(), "A frame shorter than the header should be counted as dropped");
}

/**
 * Motivation: Guarantees local composition remains reliable to subscribers even when the outbound wire frame cannot fit Messaging's own buffer.
 * Responsibilities: Send a payload one byte beyond a small system's frame budget and verify local delivery, Full, and no device packet.
 */
MW_TEST_CASE(MessagingSystem_DeliversLocallyWhenPayloadExceedsMessagingFrameCapacity)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> Network;
	TMessagingSystem<FSmallFrameMessagingTraits> SendingSystem;
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FChannelInformation SendingChannel{"Telemetry", false, &Network.Port(SendingPort), ReceivingAddress};
	const EMessagingResult CreateResult = SendingSystem.CreateChannel(SendingChannel);
	FWireMessageRecorder Recorder;
	TMessagingSystem<FSmallFrameMessagingTraits>::FSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&Recorder](const FMessage& InMessage) noexcept { Recorder.Record(InMessage); });
	const EMessagingResult SubscribeResult = SendingSystem.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	Message.SetPayload(TSpan<const std::uint8_t>(OversizedLocalPayload, OversizedLocalPayloadByteCount));

	// Act
	const EMessagingResult SendResult = SendingSystem.SendMessageToChannel(Message, "Telemetry");
	const std::size_t QueuedAfterSend = Network.QueuedCount(ReceivingPort);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The small-frame sending channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The small-frame local subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The small-frame local subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Full, SendResult, "A payload beyond MaxFrameBytes should report Full");
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
	FDefaultMessagingSystem SendingSystem;
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FChannelInformation SendingChannel{"Telemetry", false, &Network.Port(SendingPort), ReceivingAddress};
	const EMessagingResult CreateResult = SendingSystem.CreateChannel(SendingChannel);
	std::size_t LocalDeliveryCount = NoDeliveries;
	FDefaultSubscriberDelegate Subscriber;
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
 * head.
 */
MW_TEST_CASE(MessagingSystem_RecountsOversizedInboundPacketOnEachPreAdvance)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, LargerThanFramePacketBytes> Network;
	FDefaultMessagingSystem ReceivingSystem;
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
	FDefaultMessagingSystem System;
	const FChannelInformation LocalChannel{"Telemetry", false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(LocalChannel);
	std::size_t DeliveryCount = NoDeliveries;
	FDefaultSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	const EMessagingResult SubscribeResult = System.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");

	// Act
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, "Telemetry");
	const std::size_t QueuedAfterSend = Network.QueuedCount(ReceivingPort);
	System.PreAdvance(FirstReceiveTurnMilliseconds);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The local-only channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The local-only subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The local-only subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "A local-only message should succeed");
	MW_EXPECT_EQ(Test, OneDelivery, DeliveryCount, "A local-only channel should deliver to its subscriber");
	MW_EXPECT_EQ(Test, NoQueuedPackets, QueuedAfterSend, "A channel with no device should reach no loopback port");
	MW_EXPECT_EQ(Test, NoDroppedFrames, System.GetDroppedFrameCount(), "Pre-advance with no devices should drop nothing");
}

/**
 * Motivation: Ensures the header-only wire representation preserves a valid named message with no application bytes.
 * Responsibilities: Send a zero-length payload through two systems and verify the receiver sees the name and an empty payload.
 */
MW_TEST_CASE(MessagingSystem_RoundTripsZeroLengthWirePayload)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> Network;
	FDefaultMessagingSystem SendingSystem;
	FDefaultMessagingSystem ReceivingSystem;
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FChannelInformation SendingChannel{"Telemetry", false, &Network.Port(SendingPort), ReceivingAddress};
	const FChannelInformation ReceivingChannel{"Telemetry", false, &Network.Port(ReceivingPort), SendingAddress};
	const EMessagingResult SendingCreateResult = SendingSystem.CreateChannel(SendingChannel);
	const EMessagingResult ReceivingCreateResult = ReceivingSystem.CreateChannel(ReceivingChannel);
	FWireMessageRecorder ReceivingRecorder;
	FDefaultSubscriberDelegate ReceivingSubscriber;
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
