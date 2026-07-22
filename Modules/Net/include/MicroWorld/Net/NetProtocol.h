#pragma once

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Net/ByteReader.h>
#include <MicroWorld/Net/ByteWriter.h>
#include <MicroWorld/Net/NetResult.h>

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
 * Writes one framed message (header plus payload) into `Writer`.
 *
 * Validates the payload length and the total required capacity up front so a `Full` or
 * `Invalid` result leaves the writer cursor and accepted bytes unchanged. A zero-length
 * payload writes only the four-byte header.
 */
inline ENetResult WriteMessage(FByteWriter& Writer, std::uint8_t Channel, TSpan<const std::uint8_t> Payload) noexcept
{
	const std::size_t PayloadSize = Payload.Size();
	if (PayloadSize > 0xFFFF)
	{
		// A u16 length field cannot represent a payload this large; reject before any write.
		return ENetResult::Invalid;
	}
	const std::size_t RequiredBytes = MessageHeaderBytes + PayloadSize;
	// Pre-check the whole requirement before the first WriteByte so a Full leaves the cursor at zero.
	if (Writer.Remaining() < RequiredBytes)
	{
		return ENetResult::Full;
	}
	const std::uint16_t PayloadBytes = static_cast<std::uint16_t>(PayloadSize);
	(void)Writer.WriteByte(Channel);
	(void)Writer.WriteByte(std::uint8_t{0}); // Flags is reserved and always transmitted as zero.
	// The message header length is little-endian to match this layer's byte I/O convention (D6).
	std::uint8_t PayloadLengthBytes[2];
	WriteUint16LittleEndian(PayloadBytes, PayloadLengthBytes);
	(void)Writer.Write(TSpan<const std::uint8_t>(PayloadLengthBytes, 2));
	if (PayloadSize > 0)
	{
		(void)Writer.Write(Payload);
	}
	return ENetResult::Success;
}

/**
 * Parses one whole framed message from `Message`.
 *
 * Outputs are written only on `Success`: a too-short message, a nonzero Flags byte, or a
 * payload-size mismatch all return `Invalid` and leave `OutHeader` and `OutPayload` unchanged.
 */
inline ENetResult ReadMessage(TSpan<const std::uint8_t> Message, FMessageHeader& OutHeader, TSpan<const std::uint8_t>& OutPayload) noexcept
{
	if (Message.Size() < MessageHeaderBytes)
	{
		// Not even a header is present; nothing can be parsed.
		return ENetResult::Invalid;
	}
	const std::uint8_t Flags = Message[1];
	if (Flags != 0)
	{
		// Flags is reserved; a nonzero value is a malformed or unknown-framing message.
		return ENetResult::Invalid;
	}
	// The message header length is little-endian to match this layer's byte I/O convention (D6).
	const std::uint16_t PayloadBytes = ReadUint16LittleEndian(&Message[2]);
	if (Message.Size() - MessageHeaderBytes != PayloadBytes)
	{
		// The declared length disagrees with the actual trailing payload: truncation or corruption.
		return ENetResult::Invalid;
	}
	OutHeader.Channel = Message[0];
	OutHeader.Flags = std::uint8_t{0};
	OutHeader.PayloadBytes = PayloadBytes;
	OutPayload = TSpan<const std::uint8_t>(Message.Data() + MessageHeaderBytes, PayloadBytes);
	return ENetResult::Success;
}

/**
 * Encodes one channel-0 control message into `Writer` via `WriteMessage`.
 *
 * Builds the per-type control payload in a fixed local array, then frames it on the control
 * channel so the result and transactional contract are exactly `WriteMessage`'s. An unknown
 * `Type` returns `Invalid` without touching the writer.
 */
inline ENetResult WriteControlMessage(FByteWriter& Writer, const FControlMessage& Message) noexcept
{
	std::array<std::uint8_t, MaxControlPayloadBytes> Payload{};
	Payload[0] = static_cast<std::uint8_t>(Message.Type);
	std::size_t PayloadLength = 1;
	switch (Message.Type)
	{
		case EControlMessageType::Hello:
			Payload[1] = Message.ProtocolVersion;
			PayloadLength = 2;
			break;
		case EControlMessageType::Welcome:
			Payload[1] = Message.ProtocolVersion;
			Payload[2] = Message.PeerIndex;
			Payload[3] = Message.PeerGeneration;
			PayloadLength = 4;
			break;
		case EControlMessageType::Heartbeat:
		case EControlMessageType::Bye:
			PayloadLength = 1;
			break;
		default:
			// An unknown type has no defined encoding; reject before any write.
			return ENetResult::Invalid;
	}
	return WriteMessage(Writer, ControlChannel, TSpan<const std::uint8_t>(Payload.data(), PayloadLength));
}

namespace Detail
{

	/**
	 * Validates the control type byte and its exact per-type payload length.
	 *
	 * @param TypeByte First payload byte naming the control message type.
	 * @param PayloadSize Total control payload size in bytes.
	 * @return Success when the type is known and the length matches; otherwise Invalid.
	 */
	inline ENetResult ValidateControlPayloadLength(const std::uint8_t TypeByte, const std::size_t PayloadSize) noexcept
	{
		switch (TypeByte)
		{
			case static_cast<std::uint8_t>(EControlMessageType::Hello):
				if (PayloadSize != 2)
				{
					return ENetResult::Invalid;
				}
				return ENetResult::Success;
			case static_cast<std::uint8_t>(EControlMessageType::Welcome):
				if (PayloadSize != 4)
				{
					return ENetResult::Invalid;
				}
				return ENetResult::Success;
			case static_cast<std::uint8_t>(EControlMessageType::Heartbeat):
			case static_cast<std::uint8_t>(EControlMessageType::Bye):
				if (PayloadSize != 1)
				{
					return ENetResult::Invalid;
				}
				return ENetResult::Success;
			default:
				// The type byte names no known control message; the caller drops it.
				return ENetResult::Invalid;
		}
	}

	/**
	 * Reads the per-type control fields from a reader already positioned past the type byte.
	 *
	 * @param Reader Byte reader positioned immediately after the type byte.
	 * @param Type Validated control message type.
	 * @param OutMessage Populated with the decoded fields.
	 */
	inline void DecodeControlFields(FByteReader& Reader, const EControlMessageType Type, FControlMessage& OutMessage) noexcept
	{
		FControlMessage Decoded{};
		Decoded.Type = Type;
		if (Type == EControlMessageType::Hello || Type == EControlMessageType::Welcome)
		{
			(void)Reader.ReadByte(Decoded.ProtocolVersion);
		}
		if (Type == EControlMessageType::Welcome)
		{
			(void)Reader.ReadByte(Decoded.PeerIndex);
			(void)Reader.ReadByte(Decoded.PeerGeneration);
		}
		OutMessage = Decoded;
	}

} // namespace Detail

/**
 * Decodes one channel-0 control payload into `OutMessage`.
 *
 * Reads the type byte via a local `FByteReader`, validates it against {Hello, Welcome,
 * Heartbeat, Bye}, and enforces the exact per-type payload length before reading any field.
 * Outputs are written only on `Success`; a malformed payload returns `Invalid` and leaves
 * `OutMessage` unchanged.
 */
inline ENetResult ReadControlMessage(TSpan<const std::uint8_t> Payload, FControlMessage& OutMessage) noexcept
{
	FByteReader Reader(Payload);
	std::uint8_t TypeByte = 0;
	if (Reader.ReadByte(TypeByte) != ENetResult::Success)
	{
		// An empty control payload carries no type byte at all.
		return ENetResult::Invalid;
	}
	const ENetResult LengthResult = Detail::ValidateControlPayloadLength(TypeByte, Payload.Size());
	if (LengthResult != ENetResult::Success)
	{
		return LengthResult;
	}
	Detail::DecodeControlFields(Reader, static_cast<EControlMessageType>(TypeByte), OutMessage);
	return ENetResult::Success;
}

} // namespace MicroWorld
