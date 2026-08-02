#pragma once

#include <MicroWorld/Core/ByteCodecConstants.h>
#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Transport/ByteReader.h>
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

/**
 * Motivation: Names the outcome of feeding one byte to a frame decoder so a caller can drive assembly and resync.
 * Responsibilities: Distinguish an accepted non-terminal byte, a completed CRC-valid frame, and a discarded candidate.
 * Example:
 *   if (Decoder.PushByte(Byte) == EFrameEvent::FrameReady) { Consume(Decoder.FramePayload()); }
 */
enum class EFrameEvent : std::uint8_t
{
	None,		///< Motivation: Reports that the byte was accepted but no frame completed or was discarded.
	FrameReady, ///< Motivation: Reports that a CRC-valid frame completed and is held until cleared.
	Discarded,	///< Motivation: Reports that a candidate was dropped for a bad length or CRC and the decoder resynced.
};

/**
 * Motivation: Feeds a bounded byte stream and holds the most recent CRC-valid frame in fixed storage so a transport decodes
 *   without allocating or throwing.
 * Responsibilities: Resync on the next well-formed frame after corruption, hold exactly one completed frame until delivered or
 *   cleared, and require that the caller deliver or clear a held frame before pushing more bytes (a length-prefixed framer
 *   cannot rewind, so the frame after a truncated one may be consumed as its payload and lost).
 * Example:
 *   TFrameDecoder<58> Decoder;
 *   if (Decoder.PushByte(Byte) == EFrameEvent::FrameReady) { Use(Decoder.FramePayload()); }
 */
template<std::size_t MaxPayloadBytes>
class TFrameDecoder final
{
	static_assert(MaxPayloadBytes > 0, "MaxPayloadBytes must be nonzero so the decoder can hold at least an empty frame.");

public:
	/**
	 * Motivation: Creates a decoder ready to receive its first frame.
	 * Responsibilities: Start with no held frame, waiting for the next magic byte.
	 */
	TFrameDecoder() noexcept = default;

	/**
	 * Motivation: Keeps a bounded value type with fixed storage side-effect free on destruction.
	 * Responsibilities: Release nothing, since the decoder owns only fixed value storage.
	 */
	~TFrameDecoder() noexcept = default;

	/**
	 * Motivation: Prevents copying so one decoder owns its fixed assembly and held-frame storage.
	 * Responsibilities: Reject copy construction so two decoders never alias one held-frame address.
	 */
	TFrameDecoder(const TFrameDecoder&) = delete;

	/**
	 * Motivation: Prevents copying so one decoder owns its fixed assembly and held-frame storage.
	 * Responsibilities: Reject copy assignment so two decoders never alias one held-frame address.
	 */
	TFrameDecoder& operator=(const TFrameDecoder&) = delete;

	/**
	 * Motivation: Prevents moving so the held-frame storage address stays stable for caller-held spans.
	 * Responsibilities: Reject move construction so a previously returned span never dangles.
	 */
	TFrameDecoder(TFrameDecoder&&) = delete;

	/**
	 * Motivation: Prevents moving so the held-frame storage address stays stable for caller-held spans.
	 * Responsibilities: Reject move assignment so a previously returned span never dangles.
	 */
	TFrameDecoder& operator=(TFrameDecoder&&) = delete;

	/**
	 * Motivation: Drives the assembly state machine one byte at a time over a transport stream.
	 * Responsibilities: Return FrameReady when the byte completes a CRC-valid frame, Discarded when a candidate is rejected for a
	 *   bad length or CRC, and None otherwise.
	 */
	EFrameEvent PushByte(const std::uint8_t InByte) noexcept
	{
		switch (State)
		{
			case EState::WaitingForMagic:
				return BeginFrameOnMagic(InByte);
			case EState::ReadingSourceNodeId:
				return CaptureSourceNodeId(InByte);
			case EState::ReadingLengthHighByte:
				return CaptureLengthHighByte(InByte);
			case EState::ReadingLengthLowByte:
				return CaptureLengthLowByte(InByte);
			case EState::ReadingPayload:
				return AccumulatePayloadByte(InByte);
			case EState::ReadingCrcHighByte:
				return CaptureChecksumHighByte(InByte);
			case EState::ReadingCrcLowByte:
				return CompleteFrameIfChecksumMatches(InByte);
			default:
				State = EState::WaitingForMagic;
				return EFrameEvent::None;
		}
	}

	/**
	 * Motivation: Lets a caller decide whether a completed frame is ready to read.
	 * Responsibilities: Report whether a completed frame is currently held.
	 */
	bool HasFrame() const noexcept { return bHasFrame; }

	/**
	 * Motivation: Lets a caller read the sender of a held frame without exposing assembly state.
	 * Responsibilities: Return the held frame's source node id, valid only when HasFrame is true.
	 */
	std::uint8_t FrameNodeId() const noexcept { return HeldSourceNodeId; }

	/**
	 * Motivation: Gives a caller a view of a held frame's payload to consume or copy.
	 * Responsibilities: Return a view over the held payload bytes, valid only when HasFrame is true.
	 */
	Core::TSpan<const std::uint8_t> FramePayload() const noexcept { return Core::TSpan<const std::uint8_t>(PayloadStorage, HeldLength); }

	/**
	 * Motivation: Lets a caller release a delivered frame so the next one can assemble.
	 * Responsibilities: Clear the held-frame flag without touching payload storage.
	 */
	void ClearFrame() noexcept { bHasFrame = false; }

private:
	/**
	 * Motivation: Names the assembly phase the decoder is currently advancing through.
	 * Responsibilities: Distinguish waiting for magic, the source id, the two length bytes, the payload, and the two CRC bytes.
	 * Example:
	 *   // Internal state machine; advanced by PushByte only.
	 */
	enum class EState : std::uint8_t
	{
		WaitingForMagic,	   ///< Motivation: Drops bytes until the next magic byte arrives.
		ReadingSourceNodeId,   ///< Motivation: Holds the magic byte and waits for the source node id.
		ReadingLengthHighByte, ///< Motivation: Waits for the high byte of the declared payload length.
		ReadingLengthLowByte,  ///< Motivation: Waits for the low byte of the declared payload length.
		ReadingPayload,		   ///< Motivation: Accumulates payload bytes until the declared length is reached.
		ReadingCrcHighByte,	   ///< Motivation: Waits for the high byte of the declared CRC.
		ReadingCrcLowByte,	   ///< Motivation: Waits for the low byte of the declared CRC.
	};

	/**
	 * Motivation: Implements the WaitingForMagic transition so the decoder arms the CRC only on a real frame start.
	 * Responsibilities: Arm the running CRC and advance to ReadingSourceNodeId when the magic byte arrives, otherwise drop the byte.
	 */
	EFrameEvent BeginFrameOnMagic(const std::uint8_t InByte) noexcept
	{
		if (InByte == FrameMagicByte)
		{
			RunningCrc = Crc16InitValue;
			State = EState::ReadingSourceNodeId;
		}
		return EFrameEvent::None;
	}

	/**
	 * Motivation: Implements the ReadingSourceNodeId transition so the sender id is captured for later delivery.
	 * Responsibilities: Capture the sender id, fold it into the CRC, and advance to ReadingLengthHighByte.
	 */
	EFrameEvent CaptureSourceNodeId(const std::uint8_t InByte) noexcept
	{
		PendingSourceNodeId = InByte;
		UpdateCrc16Byte(RunningCrc, InByte);
		State = EState::ReadingLengthHighByte;
		return EFrameEvent::None;
	}

	/**
	 * Motivation: Implements the ReadingLengthHighByte transition by retaining the high length byte.
	 * Responsibilities: Capture the high length byte, fold it into the CRC, and advance to ReadingLengthLowByte.
	 */
	EFrameEvent CaptureLengthHighByte(const std::uint8_t InByte) noexcept
	{
		PendingLengthHighByte = InByte;
		UpdateCrc16Byte(RunningCrc, InByte);
		State = EState::ReadingLengthLowByte;
		return EFrameEvent::None;
	}

	/**
	 * Motivation: Implements the ReadingLengthLowByte transition that finalizes the declared length and guards against overflow.
	 * Responsibilities: Fold the low byte into the CRC, reconstruct the big-endian declared length, reject an oversize frame by
	 *   resyncing, and route to payload assembly or straight to the CRC when the length is zero.
	 */
	EFrameEvent CaptureLengthLowByte(const std::uint8_t InByte) noexcept
	{
		UpdateCrc16Byte(RunningCrc, InByte);
		// The frame length is big-endian on the wire (high byte first) for LoRa sniffer readability (D6).
		const std::uint8_t DeclaredLengthBytes[2] = {PendingLengthHighByte, InByte};
		const std::uint16_t DeclaredLength = ReadUint16BigEndian(DeclaredLengthBytes);
		PendingLength = DeclaredLength;
		// A declared length above the capacity cannot be assembled; resync at the next magic.
		if (DeclaredLength > MaxPayloadBytes)
		{
			State = EState::WaitingForMagic;
			return EFrameEvent::Discarded;
		}
		PayloadIndex = 0;
		State = (DeclaredLength == 0u) ? EState::ReadingCrcHighByte : EState::ReadingPayload;
		return EFrameEvent::None;
	}

	/**
	 * Motivation: Implements the ReadingPayload transition that stores each payload byte and folds it into the CRC.
	 * Responsibilities: Store one payload byte, advance the index, fold the byte into the CRC, and move to ReadingCrcHighByte
	 *   once the declared length is reached.
	 */
	EFrameEvent AccumulatePayloadByte(const std::uint8_t InByte) noexcept
	{
		PayloadStorage[PayloadIndex] = InByte;
		++PayloadIndex;
		UpdateCrc16Byte(RunningCrc, InByte);
		if (PayloadIndex >= PendingLength)
		{
			State = EState::ReadingCrcHighByte;
		}
		return EFrameEvent::None;
	}

	/**
	 * Motivation: Implements the ReadingCrcHighByte transition by retaining the high CRC byte.
	 * Responsibilities: Capture the high CRC byte (itself excluded from the running CRC) and wait for the low byte.
	 */
	EFrameEvent CaptureChecksumHighByte(const std::uint8_t InByte) noexcept
	{
		PendingCrcHighByte = InByte;
		State = EState::ReadingCrcLowByte;
		return EFrameEvent::None;
	}

	/**
	 * Motivation: Implements the ReadingCrcLowByte transition that decides whether a candidate frame is kept or discarded.
	 * Responsibilities: Reconstruct the received CRC, compare it against the running CRC, hold the completed frame on a match,
	 *   and discard and resync on a mismatch.
	 */
	EFrameEvent CompleteFrameIfChecksumMatches(const std::uint8_t InByte) noexcept
	{
		const std::uint16_t ReceivedCrc =
			static_cast<std::uint16_t>((static_cast<std::uint16_t>(PendingCrcHighByte) << Core::HighByteShift) | static_cast<std::uint16_t>(InByte));
		State = EState::WaitingForMagic;
		// A CRC mismatch means the candidate was corrupted; resync at the next magic.
		if (ReceivedCrc != RunningCrc)
		{
			return EFrameEvent::Discarded;
		}
		HeldSourceNodeId = PendingSourceNodeId;
		HeldLength = PendingLength;
		bHasFrame = true;
		return EFrameEvent::FrameReady;
	}

	/** Motivation: Holds the current assembly phase advanced by PushByte. */
	EState State{EState::WaitingForMagic};

	/** Motivation: Retains the source node id captured before the length and payload arrive. */
	std::uint8_t PendingSourceNodeId{0};

	/** Motivation: Retains the high byte of the declared length until the low byte arrives. */
	std::uint8_t PendingLengthHighByte{0};

	/** Motivation: Holds the declared payload byte count once both length bytes have arrived. */
	std::uint16_t PendingLength{0};

	/** Motivation: Retains the high byte of the declared CRC until the low byte arrives. */
	std::uint8_t PendingCrcHighByte{0};

	/** Motivation: Holds the running CRC-16/CCITT-FALSE over the source node id, length, and payload bytes. */
	std::uint16_t RunningCrc{0xFFFFu};

	/** Motivation: Counts payload bytes accumulated so far in the current candidate frame. */
	std::size_t PayloadIndex{0};

	/** Motivation: Holds the source node id of the held frame, valid while HasFrame is true. */
	std::uint8_t HeldSourceNodeId{0};

	/** Motivation: Holds the payload byte count of the held frame, valid while HasFrame is true. */
	std::size_t HeldLength{0};

	/** Motivation: Flags a held CRC-valid frame before ClearFrame is called. */
	bool bHasFrame{false};

	/** Motivation: Provides fixed storage for the payload of the frame currently being assembled or held. */
	std::uint8_t PayloadStorage[MaxPayloadBytes]{};
};

} // namespace MicroWorld::Transport::FrameCodec
