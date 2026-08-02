#pragma once

#include <MicroWorld/Core/ByteCodecConstants.h>
#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Transport/ByteReader.h>
#include <MicroWorld/Transport/ByteWriter.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Transport
{

/** Motivation: Fixes the byte length of the message header that precedes every payload. */
constexpr std::size_t MessageHeaderBytes = 4;

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

/**
 * Motivation: Gives a parser one parsed view of a message header after validation so callers avoid re-reading offsets.
 * Responsibilities: Carry the validated channel, flags, and payload length, which the reader guarantees hold Flags == 0
 *   and PayloadBytes matching the trailing payload size.
 * Example:
 *   FMessageHeader Header;
 *   Core::TSpan<const std::uint8_t> Payload;
 *   if (ReadMessage(Message, Header, Payload) == Core::ETransportResult::Success) { Dispatch(Header.Channel, Payload); }
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
 * Motivation: Frames one message transactionally so a Full or Invalid result leaves the writer cursor and accepted bytes intact.
 * Responsibilities: Validate the payload length and total required capacity up front, treat a payload over Uint16Max as Invalid,
 *   write only the four-byte header for a zero-length payload, and write header plus payload only on Success.
 */
inline Core::ETransportResult WriteMessage(FByteWriter& InWriter, std::uint8_t InChannel, Core::TSpan<const std::uint8_t> InPayload) noexcept
{
	const std::size_t PayloadSize = InPayload.Size();
	if (PayloadSize > Core::Uint16Max)
	{
		// A u16 length field cannot represent a payload this large; reject before any write.
		return Core::ETransportResult::Invalid;
	}
	const std::size_t RequiredBytes = MessageHeaderBytes + PayloadSize;
	// Pre-check the whole requirement before the first WriteByte so a Full leaves the cursor at zero.
	if (InWriter.Remaining() < RequiredBytes)
	{
		return Core::ETransportResult::Full;
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
	return Core::ETransportResult::Success;
}

/**
 * Motivation: Parses one whole framed message transactionally so a malformed header never yields a half-valid output.
 * Responsibilities: Return Invalid and leave OutHeader and OutPayload unchanged for a too-short message, a nonzero Flags
 *   byte, or a payload-size mismatch; write outputs only on Success.
 */
inline Core::ETransportResult ReadMessage(
	Core::TSpan<const std::uint8_t> InMessage, FMessageHeader& OutHeader, Core::TSpan<const std::uint8_t>& OutPayload) noexcept
{
	if (InMessage.Size() < MessageHeaderBytes)
	{
		// Not even a header is present; nothing can be parsed.
		return Core::ETransportResult::Invalid;
	}
	const std::uint8_t Flags = InMessage[MessageFlagsByteIndex];
	if (Flags != MessageReservedFlags)
	{
		// Flags is reserved; a nonzero value is a malformed or unknown-framing message.
		return Core::ETransportResult::Invalid;
	}
	// The message header length is little-endian to match this layer's byte I/O convention (D6).
	const std::uint16_t PayloadBytes = ReadUint16LittleEndian(&InMessage[MessagePayloadLengthByteIndex]);
	if (InMessage.Size() - MessageHeaderBytes != PayloadBytes)
	{
		// The declared length disagrees with the actual trailing payload: truncation or corruption.
		return Core::ETransportResult::Invalid;
	}
	OutHeader.Channel = InMessage[MessageChannelByteIndex];
	OutHeader.Flags = MessageReservedFlags;
	OutHeader.PayloadBytes = PayloadBytes;
	OutPayload = Core::TSpan<const std::uint8_t>(InMessage.Data() + MessageHeaderBytes, PayloadBytes);
	return Core::ETransportResult::Success;
}

} // namespace MicroWorld::Transport
