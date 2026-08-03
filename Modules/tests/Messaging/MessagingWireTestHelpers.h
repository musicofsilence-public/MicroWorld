#pragma once

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

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace MicroWorld::Tests
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
using MicroWorld::Messaging::FMessage;
using MicroWorld::Messaging::FMessagingSystemInformation;
using MicroWorld::Messaging::FNameId;
using MicroWorld::Messaging::MessageAcknowledgementNameId;
using MicroWorld::Transport::TLoopbackNetwork;

/** Motivation: Names the concrete system whose fixed frame limit the wire-path cases exercise. */
using FMessagingSystem = MicroWorld::Messaging::FMessagingSystem;

/** Motivation: Names the concrete bounded subscriber delegate without repeating its system-qualified declaration. */
using FSubscriberDelegate = FMessagingSystem::FSubscriberDelegate;

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
/** Motivation: Gives ordinary wire cases enough capacity for a complete concrete Messaging frame. */
constexpr std::size_t StandardPacketBytes = FMessagingSystem::MaxFrameBytes;
/** Motivation: Makes a device reject a valid concrete Messaging frame that is larger than this transport packet limit. */
constexpr std::size_t SmallerThanFramePacketBytes = FMessagingSystem::FrameHeaderBytes + 1;
/** Motivation: Makes direct inbound traffic exceed the receiving concrete Messaging frame buffer by one byte. */
constexpr std::size_t LargerThanFramePacketBytes = FMessagingSystem::MaxFrameBytes + 1;
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
/** Motivation: Keeps the reliable payload-budget device ceiling above both candidate Messaging frame sizes. */
constexpr std::size_t ReliableBudgetProbePacketBytes = FMessagingSystem::MaxFrameBytes + SequenceNumberBytes + 1;
/** Motivation: Names the complete acknowledgement packet length: two ids followed by one acknowledged sequence number. */
constexpr std::size_t AcknowledgementFrameByteCount = WireHeaderBytes + SequenceNumberBytes;
/** Motivation: States the number of bytes used by the ordinary cross-system payload. */
constexpr std::size_t WirePayloadByteCount = 3;
/** Motivation: States the one-byte malformed reliable payload that omits part of its required sequence number. */
constexpr std::size_t ShortReliablePayloadByteCount = SequenceNumberBytes - 1;
/** Motivation: States the one-byte malformed acknowledgement payload that cannot contain one complete sequence number. */
constexpr std::size_t InvalidAcknowledgementPayloadByteCount = SequenceNumberBytes - 1;
/** Motivation: States the exact concrete application payload boundary accepted by best-effort framing. */
constexpr std::size_t MaximumPayloadByteCount = FMessagingSystem::MaxMessageBytes;
/** Motivation: States the one-byte-over concrete payload rejected before device transmission. */
constexpr std::size_t OversizedLocalPayloadByteCount = MaximumPayloadByteCount + 1;
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
/** Motivation: Names the exact peer packet count produced before one reliable attempt budget is exhausted. */
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
/** Motivation: Makes the final maximum-payload byte observable in a complete captured wire frame. */
constexpr std::uint8_t MaximumPayloadFinalByte = 82;

/**
 * Motivation: Supplies a maximum-sized payload whose final byte proves the encoder copied the complete application boundary.
 * Responsibilities: Return a zero-filled maximum payload with MaximumPayloadFinalByte in its final position.
 */
constexpr std::array<std::uint8_t, MaximumPayloadByteCount> MakeMaximumPayload() noexcept
{
	std::array<std::uint8_t, MaximumPayloadByteCount> Payload{};
	Payload[MaximumPayloadByteCount - 1] = MaximumPayloadFinalByte;
	return Payload;
}

/** Motivation: Supplies a payload that exactly fills the concrete best-effort application budget. */
constexpr std::array<std::uint8_t, MaximumPayloadByteCount> MaximumPayload = MakeMaximumPayload();
/** Motivation: Supplies a payload that exceeds the concrete Messaging frame budget by one byte. */
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
/** Motivation: Supplies the first reliable sequence in documented little-endian acknowledgement order. */
constexpr std::uint8_t FirstSequenceAcknowledgementPayload[SequenceNumberBytes] = {
	static_cast<std::uint8_t>(FirstSequenceNumber), static_cast<std::uint8_t>(FirstSequenceNumber >> BitsPerByte)};

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

	/** Motivation: Retains the full concrete-sized frame buffer for direct loopback sends. */
	std::uint8_t Bytes[FMessagingSystem::MaxFrameBytes]{};

	/** Motivation: Records how many leading frame bytes are valid for the direct loopback send. */
	std::size_t Size{0};
};

/**
 * Motivation: Reaches the concrete reliable-pending boundary without inspecting private retry storage.
 * Responsibilities: On a new reliable channel with empty pending storage and sufficient device capacity, send exactly the pending limit.
 */
inline EMessagingResult FillReliablePendingSlots(FMessagingSystem& InSystem, const FMessage& InMessage, const FNameId InChannelNameId) noexcept
{
	for (std::size_t PendingIndex = 0; PendingIndex < FMessagingSystem::MaxReliablePendingMessages; ++PendingIndex)
	{
		const EMessagingResult SendResult = InSystem.SendMessageToChannel(InMessage, InChannelNameId);
		if (SendResult != EMessagingResult::Success)
		{
			return SendResult;
		}
	}

	return EMessagingResult::Success;
}

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

} // namespace MicroWorld::Tests
