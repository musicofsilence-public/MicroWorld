#include "TestSupport.h"

#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/ReceiveResult.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Messaging/ChannelInformation.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessagingResult.h>
#include <MicroWorld/Messaging/MessagingSystem.h>
#include <MicroWorld/Messaging/MessagingSystemInformation.h>
#include <MicroWorld/Transport/LoopbackNetwork.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{

using MicroWorld::Core::EDelegateResult;
using MicroWorld::Core::ETransportResult;
using MicroWorld::Core::FDeviceAddress;
using MicroWorld::Core::FReceiveResult;
using MicroWorld::Core::ITransportDevice;
using MicroWorld::Core::MakeLoopbackAddress;
using MicroWorld::Core::TimePointMilliseconds;
using MicroWorld::Core::TSpan;
using MicroWorld::Messaging::EMessagingResult;
using MicroWorld::Messaging::FChannelInformation;
using MicroWorld::Messaging::FDefaultMessagingTraits;
using MicroWorld::Messaging::FMessage;
using MicroWorld::Messaging::FMessagingSystemInformation;
using MicroWorld::Messaging::FNameId;
using MicroWorld::Messaging::MessageAcknowledgementNameId;
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
/** Motivation: Names the two-byte fixed reliable sequence sub-header independently of production constants. */
constexpr std::size_t SequenceNumberBytes = sizeof(std::uint16_t);
/** Motivation: Names the complete acknowledgement packet length: two ids followed by one acknowledged sequence number. */
constexpr std::size_t AcknowledgementFrameByteCount = WireHeaderBytes + SequenceNumberBytes;
/** Motivation: States the number of bytes used by the ordinary cross-system payload. */
constexpr std::size_t WirePayloadByteCount = 3;
/** Motivation: States the one-byte malformed reliable payload that omits part of its required sequence number. */
constexpr std::size_t ShortReliablePayloadByteCount = SequenceNumberBytes - 1;
/** Motivation: States the one-byte malformed acknowledgement payload that cannot contain one complete sequence number. */
constexpr std::size_t InvalidAcknowledgementPayloadByteCount = SequenceNumberBytes - 1;
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
/** Motivation: Names two expected synchronous or routed subscriber deliveries. */
constexpr std::size_t TwoDeliveries = OneDelivery + 1;
/** Motivation: Names the initial reliable send plus one retry attempt. */
constexpr std::size_t TwoReliableSendAttempts = 2;
/** Motivation: Names the expected absence of any queued device packet after a rejected wire send. */
constexpr std::size_t NoQueuedPackets = 0;
/** Motivation: Names one queued device packet after a single accepted wire send. */
constexpr std::size_t OneQueuedPacket = 1;
/** Motivation: Names the expected absence of dropped inbound frames. */
constexpr std::uint32_t NoDroppedFrames = 0;
/** Motivation: Names one dropped inbound frame after one malformed or unroutable receive. */
constexpr std::uint32_t OneDroppedFrame = 1;
/** Motivation: Names two cumulative dropped inbound observations of the retained oversized packet. */
constexpr std::uint32_t TwoDroppedFrames = 2;
/** Motivation: Names the first sequence a newly created reliable channel writes. */
constexpr std::uint16_t FirstSequenceNumber = 0;
/** Motivation: Names the sequence following the first reliable send from one channel. */
constexpr std::uint16_t SecondSequenceNumber = FirstSequenceNumber + 1;
/** Motivation: Sets the short deterministic interval used to force reliable retries without wall-clock time. */
constexpr TimePointMilliseconds ReliableRetryIntervalMilliseconds = 50;
/** Motivation: Offsets a post-advance turn to the last moment still inside the retry interval. */
constexpr TimePointMilliseconds BeforeReliableRetryOffsetMilliseconds = ReliableRetryIntervalMilliseconds - 1;
/** Motivation: Identifies the first post-advance turn at which one reliable retry is due. */
constexpr TimePointMilliseconds ReliableRetryTurnMilliseconds = ReliableRetryIntervalMilliseconds;
/** Motivation: Identifies the receiver turn that consumes a retried reliable frame. */
constexpr TimePointMilliseconds RetriedReceiveTurnMilliseconds = ReliableRetryTurnMilliseconds + 1;
/** Motivation: Identifies the sender turn that consumes the acknowledgement of a retried reliable frame. */
constexpr TimePointMilliseconds AcknowledgementReceiveTurnMilliseconds = RetriedReceiveTurnMilliseconds + 1;
/** Motivation: Identifies a post-advance turn far beyond the retry interval after an acknowledgement. */
constexpr TimePointMilliseconds FarAfterAcknowledgementTurnMilliseconds = ReliableRetryIntervalMilliseconds * 4;
/** Motivation: Identifies a supplied turn earlier than the first send's default time stamp. */
constexpr TimePointMilliseconds BackwardsReliableTurnMilliseconds = 0;
/** Motivation: Fixes the exact total sends a budget-exhaustion test permits. */
constexpr std::uint8_t ReliableAttemptBudget = 3;
/** Motivation: Names how many packets one exhausted attempt budget produces, which is both the mailbox depth and the expected peer count. */
constexpr std::size_t ReliableAttemptPacketCount = ReliableAttemptBudget;
/** Motivation: Names one dropped send, which forces exactly one retry. */
constexpr std::size_t OneDroppedSend = 1;
/** Motivation: Names one reliable message whose bounded retry policy gave up. */
constexpr std::uint32_t OneAbandonedReliableMessage = 1;
/** Motivation: Names a sequence far above any this file sends, so no pending frame can ever match it. */
constexpr std::uint16_t UnmatchedAcknowledgementSequenceNumber = 4242;

/** Motivation: Supplies known application bytes for the ordinary cross-system round-trip. */
constexpr std::uint8_t WirePayload[WirePayloadByteCount] = {11, 22, 33};
/** Motivation: Supplies an incomplete reliable sub-header for malformed-frame handling. */
constexpr std::uint8_t ShortReliablePayload[ShortReliablePayloadByteCount] = {71};
/** Motivation: Supplies an acknowledgement payload that is too short to name a sequence. */
constexpr std::uint8_t InvalidAcknowledgementPayload[InvalidAcknowledgementPayloadByteCount] = {72};
/** Motivation: Supplies a payload that exceeds the small test system's wire frame budget by one byte. */
constexpr std::uint8_t OversizedLocalPayload[OversizedLocalPayloadByteCount] = {41, 42, 43};
/** Motivation: Supplies a payload valid for Messaging but larger than the selected device packet capacity once framed. */
constexpr std::uint8_t DeviceRejectedPayload[DeviceRejectedPayloadByteCount] = {51, 52, 53};
/** Motivation: Supplies an incomplete raw frame that has no complete pair of encoded name ids. */
constexpr std::uint8_t TooShortFrame[TooShortFrameByteCount] = {};
/** Motivation: Supplies a raw packet the device accepts but the receiving Messaging frame buffer cannot hold. */
constexpr std::uint8_t OversizedInboundFrame[OversizedInboundFrameByteCount] = {};
/** Motivation: Supplies a complete but unmatched acknowledgement payload in little-endian sequence order. */
constexpr std::uint8_t UnmatchedAcknowledgementPayload[SequenceNumberBytes] = {
	static_cast<std::uint8_t>(UnmatchedAcknowledgementSequenceNumber),
	static_cast<std::uint8_t>(UnmatchedAcknowledgementSequenceNumber >> BitsPerByte)};

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

	/**
	 * Motivation: Lets acknowledgement tests decode raw packet fields without trusting Messaging's production decoder.
	 * Responsibilities: Read InByteCount least-significant-byte-first bytes from InSource into one unsigned 32-bit value.
	 */
	static std::uint32_t ReadUnsignedLittleEndian(const std::uint8_t* const InSource, const std::size_t InByteCount) noexcept
	{
		std::uint32_t Value = 0;
		for (std::size_t ByteIndex = 0; ByteIndex < InByteCount; ++ByteIndex)
		{
			Value |= static_cast<std::uint32_t>(InSource[ByteIndex]) << (ByteIndex * BitsPerByte);
		}

		return Value;
	}

	/**
	 * Motivation: Keeps raw-frame assertions readable when they inspect an encoded Messaging name id.
	 * Responsibilities: Decode one four-byte little-endian name id from InSource.
	 */
	static FNameId ReadNameId(const std::uint8_t* const InSource) noexcept { return FNameId{ReadUnsignedLittleEndian(InSource, NameIdBytes)}; }

	/**
	 * Motivation: Keeps reliable wire assertions readable when they inspect a fixed sequence sub-header.
	 * Responsibilities: Decode one two-byte little-endian sequence number from InSource.
	 */
	static std::uint16_t ReadSequenceNumber(const std::uint8_t* const InSource) noexcept
	{
		return static_cast<std::uint16_t>(ReadUnsignedLittleEndian(InSource, SequenceNumberBytes));
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
 * Motivation: Makes reliable pending-capacity behavior observable with one occupied slot.
 * Responsibilities: Override only reliable pending capacity while preserving every other default trait limit.
 * Example:
 *   TMessagingSystem<FSingleReliablePendingMessagingTraits> System;
 */
struct FSingleReliablePendingMessagingTraits : FDefaultMessagingTraits
{
	/** Motivation: Limits the test system to exactly one reliable frame awaiting acknowledgement. */
	static constexpr std::size_t MaxReliablePendingMessages = 1;
};

/**
 * Motivation: Simulates deterministic initial packet loss without adding a production-only transport device.
 * Responsibilities: Drop the chosen number of sends before forwarding all later device operations to the wrapped loopback port.
 * Example:
 *   FPacketDropDevice Device{Network.Port(SendingPort), 1};
 */
class FPacketDropDevice final : public ITransportDevice
{
public:
	/**
	 * Motivation: Binds packet-loss behavior to one existing transport device without taking ownership of it.
	 * Responsibilities: Retain the wrapped device and exact remaining send drops.
	 */
	explicit FPacketDropDevice(ITransportDevice& InDevice, const std::size_t InSendDropsRemaining) noexcept
		: Device(InDevice), SendDropsRemaining(InSendDropsRemaining)
	{
	}

	/**
	 * Motivation: Keeps the wrapper side-effect free on destruction.
	 * Responsibilities: Release no resource because the caller owns the wrapped device.
	 */
	~FPacketDropDevice() noexcept override = default;

	/**
	 * Motivation: Preserves the wrapped device's receive-side lifecycle behavior.
	 * Responsibilities: Forward the caller-supplied pre-advance time unchanged.
	 */
	void PreAdvance(TimePointMilliseconds InNowMilliseconds) noexcept override { Device.PreAdvance(InNowMilliseconds); }

	/**
	 * Motivation: Preserves the wrapped device's send-side lifecycle behavior.
	 * Responsibilities: Forward the caller-supplied post-advance time unchanged.
	 */
	void PostAdvance(TimePointMilliseconds InNowMilliseconds) noexcept override { Device.PostAdvance(InNowMilliseconds); }

	/**
	 * Motivation: Lets reliability tests model a successful device send whose packet disappears before its peer can receive it.
	 * Responsibilities: Count each attempt, consume one configured drop as Success, and otherwise forward the complete request unchanged.
	 */
	ETransportResult TrySend(const FDeviceAddress& InTo, const TSpan<const std::uint8_t> InPacket) noexcept override
	{
		++SendAttemptCount;
		if (SendDropsRemaining > 0)
		{
			--SendDropsRemaining;
			return ETransportResult::Success;
		}

		return Device.TrySend(InTo, InPacket);
	}

	/**
	 * Motivation: Lets inbound acknowledgement tests use the wrapped device transparently.
	 * Responsibilities: Forward the complete receive operation and preserve the wrapped device's result.
	 */
	ETransportResult TryReceive(FDeviceAddress& OutFrom, TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept override
	{
		return Device.TryReceive(OutFrom, InDestination, OutResult);
	}

	/**
	 * Motivation: Keeps frame-size validation identical to the wrapped transport.
	 * Responsibilities: Return the wrapped device's packet limit unchanged.
	 */
	std::size_t MaxPacketBytes() const noexcept override { return Device.MaxPacketBytes(); }

	/**
	 * Motivation: Lets tests assert exact initial and retry send attempts without inspecting private Messaging slots.
	 * Responsibilities: Return the cumulative attempt count.
	 */
	std::size_t GetSendAttemptCount() const noexcept { return SendAttemptCount; }

private:
	/** Motivation: References the caller-owned loopback port that receives all non-dropped operations. */
	ITransportDevice& Device;

	/** Motivation: Counts how many future sends disappear while still reporting device acceptance. */
	std::size_t SendDropsRemaining{0};

	/** Motivation: Records all send calls so bounded retry behavior stays externally observable. */
	std::size_t SendAttemptCount{0};
};

/** Motivation: Supplies a payload that exactly fills the small best-effort application budget. */
constexpr std::uint8_t SmallFramePayload[FSmallFrameMessagingTraits::MaxMessageBytes] = {81, 82};

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
 * Motivation: Proves reliable framing remains transparent to subscribers while its acknowledgement stays protocol-only.
 * Responsibilities: Send one reliable message between systems, inspect the raw acknowledgement, then consume it without delivery or an
 *   acknowledgement loop.
 */
MW_TEST_CASE(MessagingSystem_ReliableWireMessagesStripSequenceAndAcknowledgeWithoutEcho)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> Network;
	FDefaultMessagingSystem SendingSystem;
	FDefaultMessagingSystem ReceivingSystem;
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FChannelInformation SendingChannel{"Telemetry", true, &Network.Port(SendingPort), ReceivingAddress};
	const FChannelInformation ReceivingChannel{"Telemetry", true, &Network.Port(ReceivingPort), SendingAddress};
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
	std::uint8_t AcknowledgementBytes[FDefaultMessagingSystem::MaxFrameBytes]{};
	FDeviceAddress AcknowledgementSender;
	FReceiveResult AcknowledgementReceiveResult;

	// Act
	const EMessagingResult SendResult = SendingSystem.SendMessageToChannel(Message, "Telemetry");
	ReceivingSystem.PreAdvance(FirstReceiveTurnMilliseconds);
	const std::size_t QueuedAcknowledgements = Network.QueuedCount(SendingPort);
	const ETransportResult AcknowledgementReceiveStatus = Network.Port(SendingPort)
															  .TryReceive(
																  AcknowledgementSender,
																  TSpan<std::uint8_t>(AcknowledgementBytes, FDefaultMessagingSystem::MaxFrameBytes),
																  AcknowledgementReceiveResult);
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
	FDefaultMessagingSystem SendingSystem;
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FChannelInformation SendingChannel{"Telemetry", true, &Network.Port(SendingPort), ReceivingAddress};
	const EMessagingResult CreateResult = SendingSystem.CreateChannel(SendingChannel);
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	Message.SetPayload(TSpan<const std::uint8_t>(WirePayload, WirePayloadByteCount));
	std::uint8_t FirstFrameBytes[FDefaultMessagingSystem::MaxFrameBytes]{};
	std::uint8_t SecondFrameBytes[FDefaultMessagingSystem::MaxFrameBytes]{};
	FDeviceAddress FirstFrameSender;
	FDeviceAddress SecondFrameSender;
	FReceiveResult FirstFrameReceiveResult;
	FReceiveResult SecondFrameReceiveResult;

	// Act
	const EMessagingResult FirstSendResult = SendingSystem.SendMessageToChannel(Message, "Telemetry");
	const EMessagingResult SecondSendResult = SendingSystem.SendMessageToChannel(Message, "Telemetry");
	const ETransportResult FirstReceiveStatus =
		Network.Port(ReceivingPort)
			.TryReceive(FirstFrameSender, TSpan<std::uint8_t>(FirstFrameBytes, FDefaultMessagingSystem::MaxFrameBytes), FirstFrameReceiveResult);
	const ETransportResult SecondReceiveStatus =
		Network.Port(ReceivingPort)
			.TryReceive(SecondFrameSender, TSpan<std::uint8_t>(SecondFrameBytes, FDefaultMessagingSystem::MaxFrameBytes), SecondFrameReceiveResult);
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
	FDefaultMessagingSystem ReceivingSystem;
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FChannelInformation ReceivingChannel{"Telemetry", true, &Network.Port(ReceivingPort), SendingAddress};
	const EMessagingResult CreateResult = ReceivingSystem.CreateChannel(ReceivingChannel);
	std::size_t DeliveryCount = NoDeliveries;
	FDefaultSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	const EMessagingResult SubscribeResult = ReceivingSystem.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FRawWireFrame Frame;
	Frame.Set("Telemetry", "TemperatureUpdated", TSpan<const std::uint8_t>(ShortReliablePayload, ShortReliablePayloadByteCount));
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);

	// Act
	const ETransportResult RawSendResult = Network.Port(SendingPort).TrySend(ReceivingAddress, TSpan<const std::uint8_t>(Frame.Bytes, Frame.Size));
	ReceivingSystem.PreAdvance(FirstReceiveTurnMilliseconds);
	const std::size_t QueuedAcknowledgements = Network.QueuedCount(SendingPort);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The malformed reliable receiver channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The malformed reliable subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The malformed reliable subscriber should register");
	MW_EXPECT_EQ(Test, ETransportResult::Success, RawSendResult, "The short reliable frame should reach the receiver port");
	MW_EXPECT_EQ(Test, NoDeliveries, DeliveryCount, "A partial sequence number should not reach a subscriber");
	MW_EXPECT_EQ(Test, OneDroppedFrame, ReceivingSystem.GetDroppedFrameCount(), "A partial reliable sequence should count as dropped");
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
	FDefaultMessagingSystem ReceivingSystem;
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FChannelInformation ReceivingChannel{"Telemetry", false, &Network.Port(ReceivingPort), SendingAddress};
	const EMessagingResult CreateResult = ReceivingSystem.CreateChannel(ReceivingChannel);
	std::size_t DeliveryCount = NoDeliveries;
	FDefaultSubscriberDelegate Subscriber;
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

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The acknowledgement receiver channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The acknowledgement receiver subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The acknowledgement receiver subscriber should register");
	MW_EXPECT_EQ(Test, ETransportResult::Success, RawSendResult, "The malformed acknowledgement should reach the receiver port");
	MW_EXPECT_EQ(Test, NoDeliveries, DeliveryCount, "Acknowledgement control traffic should not reach a subscriber");
	MW_EXPECT_EQ(Test, OneDroppedFrame, ReceivingSystem.GetDroppedFrameCount(), "A malformed acknowledgement should count as dropped");
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
	FDefaultMessagingSystem System;
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FChannelInformation Channel{"Telemetry", false, &Network.Port(SendingPort), ReceivingAddress};
	const EMessagingResult CreateResult = System.CreateChannel(Channel);
	std::size_t DeliveryCount = NoDeliveries;
	FDefaultSubscriberDelegate Subscriber;
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
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> Network;
	TMessagingSystem<FSmallFrameMessagingTraits> System;
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FChannelInformation BestEffortChannel{"BestEffort", false, &Network.Port(SendingPort), ReceivingAddress};
	const FChannelInformation ReliableChannel{"Reliable", true, &Network.Port(SendingPort), ReceivingAddress};
	const EMessagingResult BestEffortCreateResult = System.CreateChannel(BestEffortChannel);
	const EMessagingResult ReliableCreateResult = System.CreateChannel(ReliableChannel);
	std::size_t ReliableDeliveryCount = NoDeliveries;
	TMessagingSystem<FSmallFrameMessagingTraits>::FSubscriberDelegate ReliableSubscriber;
	const EDelegateResult ReliableBindingResult =
		ReliableSubscriber.Bind([&ReliableDeliveryCount](const FMessage&) noexcept { ++ReliableDeliveryCount; });
	const EMessagingResult ReliableSubscribeResult = System.SubscribeToChannel("Reliable", std::move(ReliableSubscriber));
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	Message.SetPayload(TSpan<const std::uint8_t>(SmallFramePayload, FSmallFrameMessagingTraits::MaxMessageBytes));

	// Act
	const EMessagingResult BestEffortSendResult = System.SendMessageToChannel(Message, "BestEffort");
	const EMessagingResult ReliableSendResult = System.SendMessageToChannel(Message, "Reliable");
	const std::size_t QueuedAfterSends = Network.QueuedCount(ReceivingPort);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, BestEffortCreateResult, "The small best-effort channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReliableCreateResult, "The small reliable channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, ReliableBindingResult, "The small reliable subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReliableSubscribeResult, "The small reliable subscriber should register");
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
	FDefaultMessagingSystem SendingSystem{Information};
	FDefaultMessagingSystem ReceivingSystem{Information};
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FDeviceAddress SendingAddress = MakeLoopbackAddress(SendingPort);
	const FChannelInformation SendingChannel{"Telemetry", true, &DroppingDevice, ReceivingAddress};
	const FChannelInformation ReceivingChannel{"Telemetry", true, &Network.Port(ReceivingPort), SendingAddress};
	const EMessagingResult SendingCreateResult = SendingSystem.CreateChannel(SendingChannel);
	const EMessagingResult ReceivingCreateResult = ReceivingSystem.CreateChannel(ReceivingChannel);
	std::size_t ReceivingDeliveryCount = NoDeliveries;
	FDefaultSubscriberDelegate Subscriber;
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

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendingCreateResult, "The retry sender channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReceivingCreateResult, "The retry receiver channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The retry receiver subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The retry receiver subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The dropped first reliable send should report device acceptance");
	MW_EXPECT_EQ(Test, NoQueuedPackets, QueuedBeforeRetry, "The dropped first attempt should queue no peer packet");
	MW_EXPECT_EQ(Test, OneDelivery, ReceivingDeliveryCount, "The retried reliable frame should reach the peer once");
	MW_EXPECT_EQ(Test, OneQueuedPacket, QueuedAcknowledgements, "The peer should acknowledge the retried reliable frame");
	MW_EXPECT_EQ(Test, TwoReliableSendAttempts, DroppingDevice.GetSendAttemptCount(), "The sender should make the initial attempt and one retry");
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
	FDefaultMessagingSystem System{Information};
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
	FDefaultMessagingSystem SendingSystem{Information};
	FDefaultMessagingSystem ReceivingSystem{Information};
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

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendingCreateResult, "The acknowledgement sender channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReceivingCreateResult, "The acknowledgement receiver channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The acknowledged reliable message should send");
	MW_EXPECT_EQ(Test, NoQueuedPackets, QueuedAfterAcknowledgement, "An acknowledged reliable frame should not resend later");
	MW_EXPECT_EQ(Test, NoDroppedFrames, SendingSystem.GetDroppedFrameCount(), "A valid acknowledgement should not count as dropped");
}

/**
 * Motivation: Makes the maximum reliable attempt count and its visible abandonment outcome exact.
 * Responsibilities: With no receiver turn to acknowledge, verify exactly the configured packet count and one stable abandonment count.
 */
MW_TEST_CASE(MessagingSystem_AbandonsReliableMessagesAfterTheExactAttemptBudget)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, ReliableAttemptPacketCount, StandardPacketBytes> Network;
	FMessagingSystemInformation Information{};
	Information.ReliableRetryIntervalMilliseconds = ReliableRetryIntervalMilliseconds;
	Information.MaxReliableSendAttempts = ReliableAttemptBudget;
	FDefaultMessagingSystem System{Information};
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
 * Responsibilities: Fill one pending slot, prove a second reliable send remains local but returns Full and reaches no device.
 */
MW_TEST_CASE(MessagingSystem_ReturnsFullForReliableSendsWhenAllPendingSlotsAreOccupied)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, OneMailboxSlot, StandardPacketBytes> Network;
	TMessagingSystem<FSingleReliablePendingMessagingTraits> System;
	const FChannelInformation Channel{"Telemetry", true, &Network.Port(SendingPort), MakeLoopbackAddress(ReceivingPort)};
	const EMessagingResult CreateResult = System.CreateChannel(Channel);
	std::size_t LocalDeliveryCount = NoDeliveries;
	TMessagingSystem<FSingleReliablePendingMessagingTraits>::FSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&LocalDeliveryCount](const FMessage&) noexcept { ++LocalDeliveryCount; });
	const EMessagingResult SubscribeResult = System.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	Message.SetPayload(TSpan<const std::uint8_t>(WirePayload, WirePayloadByteCount));

	// Act
	const EMessagingResult FirstSendResult = System.SendMessageToChannel(Message, "Telemetry");
	const EMessagingResult SecondSendResult = System.SendMessageToChannel(Message, "Telemetry");
	const std::size_t QueuedAfterSecondSend = Network.QueuedCount(ReceivingPort);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The one-slot reliable channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The pending-capacity subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The pending-capacity subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstSendResult, "The first reliable frame should occupy the only slot");
	MW_EXPECT_EQ(Test, EMessagingResult::Full, SecondSendResult, "The untrackable second reliable frame should report Full");
	MW_EXPECT_EQ(Test, TwoDeliveries, LocalDeliveryCount, "Both reliable sends should still deliver locally");
	MW_EXPECT_EQ(Test, OneQueuedPacket, QueuedAfterSecondSend, "Only the trackable reliable frame should reach the device");
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
	FDefaultMessagingSystem System{Information};
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

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The unmatched-acknowledgement channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstSendResult, "The first pending reliable frame should send");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondSendResult, "The second pending reliable frame should send");
	MW_EXPECT_EQ(Test, ETransportResult::Success, AcknowledgementSendResult, "The unmatched acknowledgement should reach the sender device");
	MW_EXPECT_EQ(Test, NoDroppedFrames, System.GetDroppedFrameCount(), "An unmatched valid acknowledgement should be consumed without a drop");
	MW_EXPECT_EQ(Test, TwoMailboxSlots, QueuedRetries, "The unmatched acknowledgement should release neither pending reliable frame");
}

/**
 * Motivation: Preserves best-effort availability when reliable retry storage is completely occupied.
 * Responsibilities: Fill one reliable slot, then send a best-effort frame on another channel and verify it still reaches the device.
 */
MW_TEST_CASE(MessagingSystem_BestEffortSendsDoNotConsumeReliablePendingSlots)
{
	// Arrange
	TLoopbackNetwork<TwoPorts, TwoMailboxSlots, StandardPacketBytes> Network;
	TMessagingSystem<FSingleReliablePendingMessagingTraits> System;
	const FDeviceAddress ReceivingAddress = MakeLoopbackAddress(ReceivingPort);
	const FChannelInformation ReliableChannel{"Reliable", true, &Network.Port(SendingPort), ReceivingAddress};
	const FChannelInformation BestEffortChannel{"BestEffort", false, &Network.Port(SendingPort), ReceivingAddress};
	const EMessagingResult ReliableCreateResult = System.CreateChannel(ReliableChannel);
	const EMessagingResult BestEffortCreateResult = System.CreateChannel(BestEffortChannel);
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	Message.SetPayload(TSpan<const std::uint8_t>(WirePayload, WirePayloadByteCount));

	// Act
	const EMessagingResult ReliableSendResult = System.SendMessageToChannel(Message, "Reliable");
	const EMessagingResult BestEffortSendResult = System.SendMessageToChannel(Message, "BestEffort");
	const std::size_t QueuedAfterSends = Network.QueuedCount(ReceivingPort);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReliableCreateResult, "The reliable pending-capacity channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, BestEffortCreateResult, "The best-effort pending-capacity channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReliableSendResult, "The reliable frame should fill the only pending slot");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, BestEffortSendResult, "A best-effort frame should not need a reliable pending slot");
	MW_EXPECT_EQ(Test, TwoMailboxSlots, QueuedAfterSends, "Both reliable and best-effort frames should reach the device");
}

} // namespace
