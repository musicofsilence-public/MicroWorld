#pragma once

#include <MicroWorld/Core/ByteCodecConstants.h>
#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Transport/ByteReader.h>
#include <MicroWorld/Transport/ByteWriter.h>
#include <MicroWorld/Transport/TransportResult.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace MicroWorld
{

/** Fixed length of the message header that precedes every payload, in bytes. */
constexpr std::size_t MessageHeaderBytes = 4;

/** Channel value reserved for session-control messages; channels 1..255 are application-defined. */
constexpr std::uint8_t ControlChannel = 0;

/** Largest control payload, sized for `Welcome` (type byte plus three fields). */
constexpr std::size_t MaxControlPayloadBytes = 4;

/** Byte offset of the channel field within a framed message header. */
constexpr std::size_t MessageChannelByteIndex = 0;

/** Byte offset of the flags field within a framed message header. */
constexpr std::size_t MessageFlagsByteIndex = 1;

/** Byte offset of the little-endian payload length field within a framed message header. */
constexpr std::size_t MessagePayloadLengthByteIndex = 2;

/** Encoded size of the payload length field, in bytes. */
constexpr std::size_t MessagePayloadLengthFieldBytes = 2;

/** Reserved flags value; a valid message always transmits zero and the reader rejects any other. */
constexpr std::uint8_t MessageReservedFlags = 0;

/** Payload length the per-type control validators expect for `Hello`. */
constexpr std::size_t HelloControlPayloadBytes = 2;

/** Payload length the per-type control validators expect for `Welcome`. */
constexpr std::size_t WelcomeControlPayloadBytes = 4;

/** Payload length the per-type control validators expect for `Heartbeat` and `Bye`. */
constexpr std::size_t HeartbeatControlPayloadBytes = 1;

/** Index of the type byte within every channel-0 control payload. */
constexpr std::size_t ControlTypeByteIndex = 0;

/** Index of the protocol version byte in a `Hello` control payload. */
constexpr std::size_t HelloProtocolVersionByteIndex = 1;

/** Index of the protocol version byte in a `Welcome` control payload. */
constexpr std::size_t WelcomeProtocolVersionByteIndex = 1;

/** Index of the assigned peer index byte in a `Welcome` control payload. */
constexpr std::size_t WelcomePeerIndexByteIndex = 2;

/** Index of the assigned peer generation byte in a `Welcome` control payload. */
constexpr std::size_t WelcomePeerGenerationByteIndex = 3;

/**
 * Parsed view of one message header.
 *
 * The reader validates `Flags == 0` and `PayloadBytes == actual payload size` before
 * producing this struct, so a caller holding a populated `FMessageHeader` can trust both.
 */
struct FMessageHeader
{
	/** Channel from offset 0; 0 is session control, 1..255 are application-defined. */
	std::uint8_t Channel{0};

	/** Flags from offset 1; always 0 for a valid message (nonzero is rejected). */
	std::uint8_t Flags{0};

	/** Payload length from offsets 2..3 little-endian; equals the trailing payload size. */
	std::uint16_t PayloadBytes{0};
};

/** Type byte carried in the first payload byte of a channel-0 session-control message. */
enum class EControlMessageType : std::uint8_t
{
	/** Client-to-server greeting carrying the caller's protocol version. */
	Hello = 1,

	/** Server-to-client admission carrying the assigned peer index and generation. */
	Welcome = 2,

	/** Keepalive exchanged in both directions on a configured interval. */
	Heartbeat = 3,

	/** Disconnect notice exchanged in both directions. */
	Bye = 4,
};

/**
 * Decoded channel-0 control message.
 *
 * Only the fields a given `Type` carries are meaningful: `Hello` uses `ProtocolVersion`;
 * `Welcome` uses `ProtocolVersion`, `PeerIndex`, and `PeerGeneration`; `Heartbeat` and `Bye`
 * use none. The decoder enforces the exact per-type length before populating this struct.
 */
struct FControlMessage
{
	/** Control message type from the first payload byte. */
	EControlMessageType Type{EControlMessageType::Heartbeat};

	/** Protocol version carried by `Hello` and `Welcome`. */
	std::uint8_t ProtocolVersion{0};

	/** Assigned peer index carried by `Welcome`. */
	std::uint8_t PeerIndex{0};

	/** Assigned peer generation carried by `Welcome`. */
	std::uint8_t PeerGeneration{0};
};

/**
 * Writes one framed message (header plus payload) into `InWriter`.
 *
 * Validates the payload length and the total required capacity up front so a `Full` or
 * `Invalid` result leaves the writer cursor and accepted bytes unchanged. A zero-length
 * payload writes only the four-byte header.
 */
inline ETransportResult WriteMessage(FByteWriter& InWriter, std::uint8_t InChannel, TSpan<const std::uint8_t> InPayload) noexcept
{
	const std::size_t PayloadSize = InPayload.Size();
	if (PayloadSize > Uint16Max)
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
	(void)InWriter.Write(TSpan<const std::uint8_t>(PayloadLengthBytes, MessagePayloadLengthFieldBytes));
	if (PayloadSize > 0)
	{
		(void)InWriter.Write(InPayload);
	}
	return ETransportResult::Success;
}

/**
 * Parses one whole framed message from `InMessage`.
 *
 * Outputs are written only on `Success`: a too-short message, a nonzero Flags byte, or a
 * payload-size mismatch all return `Invalid` and leave `OutHeader` and `OutPayload` unchanged.
 */
inline ETransportResult ReadMessage(TSpan<const std::uint8_t> InMessage, FMessageHeader& OutHeader, TSpan<const std::uint8_t>& OutPayload) noexcept
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
	OutPayload = TSpan<const std::uint8_t>(InMessage.Data() + MessageHeaderBytes, PayloadBytes);
	return ETransportResult::Success;
}

/**
 * Encodes one channel-0 control message into `InWriter` via `WriteMessage`.
 *
 * Builds the per-type control payload in a fixed local array, then frames it on the control
 * channel so the result and transactional contract are exactly `WriteMessage`'s. An unknown
 * `Type` returns `Invalid` without touching the writer.
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
	return WriteMessage(InWriter, ControlChannel, TSpan<const std::uint8_t>(Payload.data(), PayloadLength));
}

/**
 * Validates the control type byte and its exact per-type payload length.
 *
 * @param InTypeByte First payload byte naming the control message type.
 * @param InPayloadSize Total control payload size in bytes.
 * @return Success when the type is known and the length matches; otherwise Invalid.
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
 * Reads the per-type control fields from a reader already positioned past the type byte.
 *
 * @param InReader Byte reader positioned immediately after the type byte.
 * @param InType Validated control message type.
 * @param OutMessage Populated with the decoded fields.
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
 * Decodes one channel-0 control payload into `OutMessage`.
 *
 * Reads the type byte via a local `FByteReader`, validates it against {Hello, Welcome,
 * Heartbeat, Bye}, and enforces the exact per-type payload length before reading any field.
 * Outputs are written only on `Success`; a malformed payload returns `Invalid` and leaves
 * `OutMessage` unchanged.
 */
inline ETransportResult ReadControlMessage(TSpan<const std::uint8_t> InPayload, FControlMessage& OutMessage) noexcept
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

} // namespace MicroWorld
