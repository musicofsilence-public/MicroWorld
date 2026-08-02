#pragma once

#include <MicroWorld/Core/ByteCodecConstants.h>
#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Transport/ByteWriter.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace MicroWorld::Transport::FrameCodec
{

/** Motivation: Marks the start of every framed message on the wire so a decoder can resync after corruption. */
constexpr std::uint8_t FrameMagicByte = 0xA5;

/** Motivation: Fixes the framing cost (magic, source id, two length bytes, two CRC bytes) so capacity math is shared. */
constexpr std::size_t FrameOverheadBytes = 6;

/** Motivation: Fixes the header byte count written before the payload (magic, source id, two length bytes). */
constexpr std::size_t FrameHeaderBytes = 4;

/** Motivation: Locates the source node id byte immediately after the magic byte. */
constexpr std::size_t FrameSourceNodeIdByteIndex = 1;

/** Motivation: Fixes the prefix byte count the running CRC covers (source id plus both length bytes). */
constexpr std::size_t FrameCrcCoveredPrefixBytes = 3;

/** Motivation: Holds the CRC-16/CCITT-FALSE accumulator value before the first covered byte is folded in. */
constexpr std::uint16_t Crc16InitValue = 0xFFFFu;

/** Motivation: Selects the CRC-16 top bit tested once per bit advance. */
constexpr std::uint16_t Crc16TopBitMask = 0x8000u;

/** Motivation: Holds the CRC-16/CCITT generator polynomial applied when the top bit is set. */
constexpr std::uint16_t Crc16CcittPolynomial = 0x1021u;

static_assert(FrameOverheadBytes == FrameHeaderBytes + 2, "Frame overhead is the header plus the two trailing CRC bytes.");

/**
 * Motivation: Advances a CRC-16/CCITT-FALSE accumulator by one byte so a caller can fold a stream incrementally.
 * Responsibilities: Update InOutCrc in place over eight bit advances; the caller initializes it to 0xFFFF before the first byte.
 */
inline void UpdateCrc16Byte(std::uint16_t& InOutCrc, const std::uint8_t InByte) noexcept
{
	InOutCrc = static_cast<std::uint16_t>(InOutCrc ^ static_cast<std::uint16_t>(static_cast<std::uint16_t>(InByte) << Core::HighByteShift));
	for (int Bit = 0; Bit < Core::BitsPerByte; ++Bit)
	{
		if ((InOutCrc & Crc16TopBitMask) != 0u)
		{
			InOutCrc = static_cast<std::uint16_t>(static_cast<std::uint16_t>(InOutCrc << 1) ^ Crc16CcittPolynomial);
		}
		else
		{
			InOutCrc = static_cast<std::uint16_t>(InOutCrc << 1);
		}
	}
}

/**
 * Motivation: Computes a CRC-16/CCITT-FALSE checksum over a byte span so a frame can be integrity-checked without allocating.
 * Responsibilities: Apply polynomial 0x1021, init 0xFFFF, no input or output reflection, and xorout 0x0000 (the canonical
 *   check value of ASCII "123456789" is 0x29B1); return 0xFFFF for a valid empty span and avoid dereferencing a null
 *   nonzero-count view.
 */
inline std::uint16_t ComputeCrc16Ccitt(const Core::TSpan<const std::uint8_t> InBytes) noexcept
{
	std::uint16_t Crc = Crc16InitValue;
	const std::uint8_t* const ChecksumBytes = InBytes.Data();
	const std::size_t ByteCount = InBytes.Size();
	// A null pointer with a nonzero count is an invalid view; do not dereference it.
	if (ChecksumBytes == nullptr)
	{
		return Crc;
	}
	for (std::size_t Index = 0; Index < ByteCount; ++Index)
	{
		UpdateCrc16Byte(Crc, ChecksumBytes[Index]);
	}
	return Crc;
}

/**
 * Motivation: Rejects every invalid encode input before the destination is touched so an encode rejection is transactional.
 * Responsibilities: Return Invalid for a null-with-length payload or destination, or a payload over Uint16Max that can never
 *   fit the 16-bit length field; return Full when the destination is too small; otherwise Success.
 */
inline Core::ETransportResult ValidateEncodeInputs(const Core::TSpan<const std::uint8_t> InPayload, const Core::TSpan<std::uint8_t> InFrame) noexcept
{
	const std::size_t PayloadSize = InPayload.Size();
	if (PayloadSize != 0 && InPayload.Data() == nullptr)
	{
		return Core::ETransportResult::Invalid;
	}
	if (InFrame.Size() != 0 && InFrame.Data() == nullptr)
	{
		return Core::ETransportResult::Invalid;
	}
	if (PayloadSize > Core::Uint16Max)
	{
		// Oversize input can never fit the 16-bit length field, so it can never succeed on retry (D7).
		return Core::ETransportResult::Invalid;
	}
	if (PayloadSize + FrameOverheadBytes > InFrame.Size())
	{
		return Core::ETransportResult::Full;
	}
	return Core::ETransportResult::Success;
}

/**
 * Motivation: Writes the fixed frame header so a decoder can locate the source id and declared length in known positions.
 * Responsibilities: Stamp the magic byte, the source node id, and the payload length as two big-endian bytes.
 */
inline void WriteFrameHeader(const std::uint8_t InSourceNodeId, const std::size_t InPayloadSize, const Core::TSpan<std::uint8_t> OutFrame) noexcept
{
	OutFrame[0] = FrameMagicByte;
	OutFrame[1] = InSourceNodeId;
	// The frame length is big-endian (high byte first) so a LoRa packet sniffer shows it in on-air reading order (D6).
	WriteUint16BigEndian(static_cast<std::uint16_t>(InPayloadSize), &OutFrame[2]);
}

/**
 * Motivation: Completes a frame body and its integrity check so the trailing CRC sits at a fixed offset.
 * Responsibilities: Copy the payload after the header, then append the CRC-16 computed over the source id, both length bytes,
 *   and the payload (magic and CRC bytes excluded).
 */
inline void AppendPayloadAndChecksum(const Core::TSpan<const std::uint8_t> InPayload, const Core::TSpan<std::uint8_t> OutFrame) noexcept
{
	const std::size_t PayloadSize = InPayload.Size();
	if (PayloadSize != 0)
	{
		std::memcpy(&OutFrame[FrameHeaderBytes], InPayload.Data(), PayloadSize);
	}
	// CRC covers the source node id, both length bytes, and the payload; magic and CRC are excluded.
	const std::uint16_t Crc =
		ComputeCrc16Ccitt(Core::TSpan<const std::uint8_t>(&OutFrame[FrameSourceNodeIdByteIndex], FrameCrcCoveredPrefixBytes + PayloadSize));
	OutFrame[FrameHeaderBytes + PayloadSize] = static_cast<std::uint8_t>(Crc >> Core::HighByteShift);
	OutFrame[FrameHeaderBytes + PayloadSize + 1] = static_cast<std::uint8_t>(Crc & Core::LowByteMask);
}

/**
 * Motivation: Encodes one complete framed message transactionally so a failed encode leaves the destination and count intact.
 * Responsibilities: Validate before any write (Invalid for a null-with-length payload or destination, an oversize payload, or a
 *   too-small destination as Full), then on Success write the full frame and set OutWritten to payload plus overhead; on any
 *   non-success leave the destination and OutWritten unchanged.
 */
inline Core::ETransportResult EncodeFrame(
	const std::uint8_t InSourceNodeId,
	const Core::TSpan<const std::uint8_t> InPayload,
	const Core::TSpan<std::uint8_t> OutFrame,
	std::size_t& OutWritten) noexcept
{
	const Core::ETransportResult ValidationResult = ValidateEncodeInputs(InPayload, OutFrame);
	if (ValidationResult != Core::ETransportResult::Success)
	{
		return ValidationResult;
	}
	const std::size_t PayloadSize = InPayload.Size();
	WriteFrameHeader(InSourceNodeId, PayloadSize, OutFrame);
	AppendPayloadAndChecksum(InPayload, OutFrame);
	OutWritten = PayloadSize + FrameOverheadBytes;
	return Core::ETransportResult::Success;
}

} // namespace MicroWorld::Transport::FrameCodec
