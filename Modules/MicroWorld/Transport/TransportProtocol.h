#pragma once

#include <MicroWorld/Core/ByteCodecConstants.h>
#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Transport/ByteReader.h>
#include <MicroWorld/Transport/ByteWriter.h>
#include <MicroWorld/Transport/TransportResult.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace MicroWorld::Transport
{

/** Motivation: Fixes the byte length of the message header that precedes every payload. */
constexpr std::size_t MessageHeaderBytes = 4;

/** Motivation: Reserves the channel value for session-control messages so channels 1..255 stay application-defined. */
constexpr std::uint8_t ControlChannel = 0;

/** Motivation: Sizes the largest control payload so the Welcome fields fit in one fixed array. */
constexpr std::size_t MaxControlPayloadBytes = 4;

/** Motivation: Locates the channel field within a framed message header. */
constexpr std::size_t MessageChannelByteIndex = 0;

/** Motivation: Locates the flags field within a framed message header. */
constexpr std::size_t MessageFlagsByteIndex = 1;

/** Motivation: Locates the little-endian payload length field within a framed message header. */
constexpr std::size_t MessagePayloadLengthByteIndex = 2;

/** Motivation: Fixes the encoded size of the payload length field so readers advance the exact span. */
constexpr std::size_t MessagePayloadLengthFieldBytes = 2;

/** Motivation: Pins the reserved flags value so a valid message always transmits zero and the reader rejects any other. */
constexpr std::uint8_t MessageReservedFlags = 0;

/** Motivation: Fixes the payload length the per-type control validators expect for Hello. */
constexpr std::size_t HelloControlPayloadBytes = 2;

/** Motivation: Fixes the payload length the per-type control validators expect for Welcome. */
constexpr std::size_t WelcomeControlPayloadBytes = 4;

/** Motivation: Fixes the payload length the per-type control validators expect for Heartbeat and Bye. */
constexpr std::size_t HeartbeatControlPayloadBytes = 1;

/** Motivation: Locates the type byte within every channel-0 control payload. */
constexpr std::size_t ControlTypeByteIndex = 0;

/** Motivation: Locates the protocol version byte in a Hello control payload. */
constexpr std::size_t HelloProtocolVersionByteIndex = 1;

/** Motivation: Locates the protocol version byte in a Welcome control payload. */
constexpr std::size_t WelcomeProtocolVersionByteIndex = 1;

/** Motivation: Locates the assigned peer index byte in a Welcome control payload. */
constexpr std::size_t WelcomePeerIndexByteIndex = 2;

/** Motivation: Locates the assigned peer generation byte in a Welcome control payload. */
constexpr std::size_t WelcomePeerGenerationByteIndex = 3;

/**
 * Motivation: Gives a parser one parsed view of a message header after validation so callers avoid re-reading offsets.
 * Responsibilities: Carry the validated channel, flags, and payload length, which the reader guarantees hold Flags == 0
 *   and PayloadBytes matching the trailing payload size.
 * Example:
 *   FMessageHeader Header;
 *   Core::TSpan<const std::uint8_t> Payload;
 *   if (ReadMessage(Message, Header, Payload) == ETransportResult::Success) { Dispatch(Header.Channel, Payload); }
 */
struct FMessageHeader
{
	/** Motivation: Carries the channel from offset 0; 0 is session control, 1..255 are application-defined. */
	std::uint8_t Channel{0};

	/** Motivation: Carries the flags from offset 1, always 0 for a valid message since nonzero is rejected. */
	std::uint8_t Flags{0};

	/** Motivation: Carries the little-endian payload length from offsets 2..3, equal to the trailing payload size. */
	std::uint16_t PayloadBytes{0};
};

/**
 * Motivation: Names the control message types a channel-0 payload may carry so the host routes each by intent.
 * Responsibilities: Distinguish the Hello, Welcome, Heartbeat, and Bye control shapes from application channels.
 * Example:
 *   if (Message.Type == EControlMessageType::Welcome) { Admit(Message.PeerIndex); }
 */
enum class EControlMessageType : std::uint8_t
{
	Hello = 1, ///< Motivation: Client-to-server greeting carrying the caller's protocol version.

	Welcome = 2, ///< Motivation: Server-to-client admission carrying the assigned peer index and generation.

	Heartbeat = 3, ///< Motivation: Keepalive exchanged in both directions on a configured interval.

	Bye = 4, ///< Motivation: Disconnect notice exchanged in both directions.
};

/**
 * Motivation: Gives the host one decoded view of a channel-0 control message after length validation.
 * Responsibilities: Carry only the type and the per-type fields (Hello uses ProtocolVersion; Welcome uses ProtocolVersion,
 *   PeerIndex, and PeerGeneration; Heartbeat and Bye use none), which the decoder populates only after enforcing the exact
 *   per-type length.
 * Example:
 *   FControlMessage Control;
 *   if (ReadControlMessage(Payload, Control) == ETransportResult::Success) { Handle(Control); }
 */
struct FControlMessage
{
	/** Motivation: Carries the control message type from the first payload byte. */
	EControlMessageType Type{EControlMessageType::Heartbeat};

	/** Motivation: Carries the protocol version used by Hello and Welcome. */
	std::uint8_t ProtocolVersion{0};

	/** Motivation: Carries the assigned peer index used by Welcome. */
	std::uint8_t PeerIndex{0};

	/** Motivation: Carries the assigned peer generation used by Welcome. */
	std::uint8_t PeerGeneration{0};
};

/**
 * Motivation: Frames one message transactionally so a Full or Invalid result leaves the writer cursor and accepted bytes intact.
 * Responsibilities: Validate the payload length and total required capacity up front, treat a payload over Uint16Max as Invalid,
 *   write only the four-byte header for a zero-length payload, and write header plus payload only on Success.
 */
inline ETransportResult WriteMessage(FByteWriter& InWriter, std::uint8_t InChannel, Core::TSpan<const std::uint8_t> InPayload) noexcept
{
	const std::size_t PayloadSize = InPayload.Size();
	if (PayloadSize > Core::Uint16Max)
	{
		// A u16 length field cannot represent a payload this large; reject before any write.
		return ETransportResult::Invalid;
	}
	const std::size_t RequiredBytes = MessageHeaderBytes + PayloadSize;
	// Pre-check the whole requirement before the first WriteByte so a Full leaves the cursor at zero.
	if (InWriter.Remaining() < RequiredBytes)
	{
		return ETransportResult::Full;
	}
	const std::uint16_t PayloadBytes = static_cast<std::uint16_t>(PayloadSize);
	(void)InWriter.WriteByte(InChannel);
	(void)InWriter.WriteByte(MessageReservedFlags); // Flags is reserved and always transmitted as zero.
	// The message header length is little-endian to match this layer's byte I/O convention (D6).
	std::uint8_t PayloadLengthBytes[MessagePayloadLengthFieldBytes];
	WriteUint16LittleEndian(PayloadBytes, PayloadLengthBytes);
	(void)InWriter.Write(Core::TSpan<const std::uint8_t>(PayloadLengthBytes, MessagePayloadLengthFieldBytes));
	if (PayloadSize > 0)
	{
		(void)InWriter.Write(InPayload);
	}
	return ETransportResult::Success;
}

/**
 * Motivation: Parses one whole framed message transactionally so a malformed header never yields a half-valid output.
 * Responsibilities: Return Invalid and leave OutHeader and OutPayload unchanged for a too-short message, a nonzero Flags
 *   byte, or a payload-size mismatch; write outputs only on Success.
 */
inline ETransportResult ReadMessage(
	Core::TSpan<const std::uint8_t> InMessage, FMessageHeader& OutHeader, Core::TSpan<const std::uint8_t>& OutPayload) noexcept
{
	if (InMessage.Size() < MessageHeaderBytes)
	{
		// Not even a header is present; nothing can be parsed.
		return ETransportResult::Invalid;
	}
	const std::uint8_t Flags = InMessage[MessageFlagsByteIndex];
	if (Flags != MessageReservedFlags)
	{
		// Flags is reserved; a nonzero value is a malformed or unknown-framing message.
		return ETransportResult::Invalid;
	}
	// The message header length is little-endian to match this layer's byte I/O convention (D6).
	const std::uint16_t PayloadBytes = ReadUint16LittleEndian(&InMessage[MessagePayloadLengthByteIndex]);
	if (InMessage.Size() - MessageHeaderBytes != PayloadBytes)
	{
		// The declared length disagrees with the actual trailing payload: truncation or corruption.
		return ETransportResult::Invalid;
	}
	OutHeader.Channel = InMessage[MessageChannelByteIndex];
	OutHeader.Flags = MessageReservedFlags;
	OutHeader.PayloadBytes = PayloadBytes;
	OutPayload = Core::TSpan<const std::uint8_t>(InMessage.Data() + MessageHeaderBytes, PayloadBytes);
	return ETransportResult::Success;
}

/**
 * Motivation: Encodes one channel-0 control message by reusing WriteMessage so framing stays consistent.
 * Responsibilities: Build the per-type control payload in a fixed local array, frame it on the control channel, and return
 *   Invalid without touching the writer for an unknown type.
 */
inline ETransportResult WriteControlMessage(FByteWriter& InWriter, const FControlMessage& InMessage) noexcept
{
	std::array<std::uint8_t, MaxControlPayloadBytes> Payload{};
	Payload[ControlTypeByteIndex] = static_cast<std::uint8_t>(InMessage.Type);
	std::size_t PayloadLength = HeartbeatControlPayloadBytes;
	switch (InMessage.Type)
	{
		case EControlMessageType::Hello:
			Payload[HelloProtocolVersionByteIndex] = InMessage.ProtocolVersion;
			PayloadLength = HelloControlPayloadBytes;
			break;
		case EControlMessageType::Welcome:
			Payload[HelloProtocolVersionByteIndex] = InMessage.ProtocolVersion;
			Payload[WelcomePeerIndexByteIndex] = InMessage.PeerIndex;
			Payload[WelcomePeerGenerationByteIndex] = InMessage.PeerGeneration;
			PayloadLength = WelcomeControlPayloadBytes;
			break;
		case EControlMessageType::Heartbeat:
		case EControlMessageType::Bye:
			PayloadLength = HeartbeatControlPayloadBytes;
			break;
		default:
			// An unknown type has no defined encoding; reject before any write.
			return ETransportResult::Invalid;
	}
	return WriteMessage(InWriter, ControlChannel, Core::TSpan<const std::uint8_t>(Payload.data(), PayloadLength));
}

/**
 * Motivation: Guards control decoding against a type byte its length does not match so a malformed payload is rejected early.
 * Responsibilities: Return Success only when the type byte is known and the payload size matches its expected length,
 *   otherwise Invalid.
 */
inline ETransportResult ValidateControlPayloadLength(const std::uint8_t InTypeByte, const std::size_t InPayloadSize) noexcept
{
	switch (InTypeByte)
	{
		case static_cast<std::uint8_t>(EControlMessageType::Hello):
			if (InPayloadSize != HelloControlPayloadBytes)
			{
				return ETransportResult::Invalid;
			}
			return ETransportResult::Success;
		case static_cast<std::uint8_t>(EControlMessageType::Welcome):
			if (InPayloadSize != WelcomeControlPayloadBytes)
			{
				return ETransportResult::Invalid;
			}
			return ETransportResult::Success;
		case static_cast<std::uint8_t>(EControlMessageType::Heartbeat):
		case static_cast<std::uint8_t>(EControlMessageType::Bye):
			if (InPayloadSize != HeartbeatControlPayloadBytes)
			{
				return ETransportResult::Invalid;
			}
			return ETransportResult::Success;
		default:
			// The type byte names no known control message; the caller drops it.
			return ETransportResult::Invalid;
	}
}

/**
 * Motivation: Reads the per-type control fields from a reader positioned past the type byte so one length-checked payload decodes.
 * Responsibilities: Populate OutMessage with the fields its validated type carries and leave the others default.
 */
inline void DecodeControlFields(FByteReader& InReader, const EControlMessageType InType, FControlMessage& OutMessage) noexcept
{
	FControlMessage Decoded{};
	Decoded.Type = InType;
	if (InType == EControlMessageType::Hello || InType == EControlMessageType::Welcome)
	{
		(void)InReader.ReadByte(Decoded.ProtocolVersion);
	}
	if (InType == EControlMessageType::Welcome)
	{
		(void)InReader.ReadByte(Decoded.PeerIndex);
		(void)InReader.ReadByte(Decoded.PeerGeneration);
	}
	OutMessage = Decoded;
}

/**
 * Motivation: Decodes one channel-0 control payload transactionally so a malformed payload never yields a partial control message.
 * Responsibilities: Read the type byte through a local FByteReader, validate it against Hello, Welcome, Heartbeat, and Bye,
 *   enforce the exact per-type length before any field read, and write OutMessage only on Success.
 */
inline ETransportResult ReadControlMessage(Core::TSpan<const std::uint8_t> InPayload, FControlMessage& OutMessage) noexcept
{
	FByteReader Reader(InPayload);
	std::uint8_t TypeByte = 0;
	if (Reader.ReadByte(TypeByte) != ETransportResult::Success)
	{
		// An empty control payload carries no type byte at all.
		return ETransportResult::Invalid;
	}
	const ETransportResult LengthResult = ValidateControlPayloadLength(TypeByte, InPayload.Size());
	if (LengthResult != ETransportResult::Success)
	{
		return LengthResult;
	}
	DecodeControlFields(Reader, static_cast<EControlMessageType>(TypeByte), OutMessage);
	return ETransportResult::Success;
}

} // namespace MicroWorld::Transport
