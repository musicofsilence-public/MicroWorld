#pragma once

#include <MicroWorld/Core/ByteCodecConstants.h>
#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Transport/ByteReader.h>
#include <MicroWorld/Transport/ByteWriter.h>
#include <MicroWorld/Transport/TransportResult.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace MicroWorld::Transport::FrameCodec
{

/** Single-byte sentinel that marks the start of every framed message on the wire. */
constexpr std::uint8_t FrameMagicByte = 0xA5;

/** Fixed framing cost in bytes: one magic, one source node id, two length bytes, and two CRC bytes. */
constexpr std::size_t FrameOverheadBytes = 6;

/** Header bytes written before the payload: one magic, one source node id, and two length bytes. */
constexpr std::size_t FrameHeaderBytes = 4;

/** Frame offset of the source node id byte (immediately after the magic byte). */
constexpr std::size_t FrameSourceNodeIdByteIndex = 1;

/** Byte count covered by the running CRC: source id, both length bytes, and the payload. */
constexpr std::size_t FrameCrcCoveredPrefixBytes = 3;

/** CRC-16/CCITT-FALSE initializer; the accumulator value before the first covered byte. */
constexpr std::uint16_t Crc16InitValue = 0xFFFFu;

/** Mask selecting the CRC-16 top bit, tested once per bit advance. */
constexpr std::uint16_t Crc16TopBitMask = 0x8000u;

/** CRC-16/CCITT generator polynomial applied when the top bit is set. */
constexpr std::uint16_t Crc16CcittPolynomial = 0x1021u;

static_assert(FrameOverheadBytes == FrameHeaderBytes + 2, "Frame overhead is the header plus the two trailing CRC bytes.");

/**
 * Advances a CRC-16/CCITT-FALSE accumulator by one byte.
 *
 * @param InOutCrc Accumulator updated in place; initialize to 0xFFFF before the first byte.
 * @param InByte Next byte covered by the checksum.
 */
inline void UpdateCrc16Byte(std::uint16_t& InOutCrc, const std::uint8_t InByte) noexcept
{
	InOutCrc = static_cast<std::uint16_t>(InOutCrc ^ static_cast<std::uint16_t>(static_cast<std::uint16_t>(InByte) << HighByteShift));
	for (int Bit = 0; Bit < BitsPerByte; ++Bit)
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
 * Computes a CRC-16/CCITT-FALSE checksum over a byte span without allocating or throwing.
 *
 * Parameters are polynomial 0x1021, init 0xFFFF, no input or output reflection, and xorout
 * 0x0000; the canonical check value of ASCII "123456789" is 0x29B1.
 *
 * @param InBytes Caller-owned span covered by the checksum; a valid empty span returns 0xFFFF.
 * @return The computed checksum.
 */
inline std::uint16_t ComputeCrc16Ccitt(const TSpan<const std::uint8_t> InBytes) noexcept
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
 * Rejects every invalid encode input before the destination is touched, so a rejection is transactional.
 *
 * @param InPayload Caller-owned payload bytes to frame.
 * @param InFrame Caller-owned destination whose capacity is checked before any write.
 * @return Invalid for a null-with-length span or an oversize payload, Full for a destination too small, else Success.
 */
inline ETransportResult ValidateEncodeInputs(const TSpan<const std::uint8_t> InPayload, const TSpan<std::uint8_t> InFrame) noexcept
{
	const std::size_t PayloadSize = InPayload.Size();
	if (PayloadSize != 0 && InPayload.Data() == nullptr)
	{
		return ETransportResult::Invalid;
	}
	if (InFrame.Size() != 0 && InFrame.Data() == nullptr)
	{
		return ETransportResult::Invalid;
	}
	if (PayloadSize > Uint16Max)
	{
		// Oversize input can never fit the 16-bit length field, so it can never succeed on retry (D7).
		return ETransportResult::Invalid;
	}
	if (PayloadSize + FrameOverheadBytes > InFrame.Size())
	{
		return ETransportResult::Full;
	}
	return ETransportResult::Success;
}

/** Writes the fixed frame header: magic byte, source node id, then the payload length as two big-endian bytes. */
inline void WriteFrameHeader(const std::uint8_t InSourceNodeId, const std::size_t InPayloadSize, const TSpan<std::uint8_t> OutFrame) noexcept
{
	OutFrame[0] = FrameMagicByte;
	OutFrame[1] = InSourceNodeId;
	// The frame length is big-endian (high byte first) so a LoRa packet sniffer shows it in on-air reading order (D6).
	WriteUint16BigEndian(static_cast<std::uint16_t>(InPayloadSize), &OutFrame[2]);
}

/** Copies the payload after the header, then appends the CRC-16 over the source id, length, and payload. */
inline void AppendPayloadAndChecksum(const TSpan<const std::uint8_t> InPayload, const TSpan<std::uint8_t> OutFrame) noexcept
{
	const std::size_t PayloadSize = InPayload.Size();
	if (PayloadSize != 0)
	{
		std::memcpy(&OutFrame[FrameHeaderBytes], InPayload.Data(), PayloadSize);
	}
	// CRC covers the source node id, both length bytes, and the payload; magic and CRC are excluded.
	const std::uint16_t Crc =
		ComputeCrc16Ccitt(TSpan<const std::uint8_t>(&OutFrame[FrameSourceNodeIdByteIndex], FrameCrcCoveredPrefixBytes + PayloadSize));
	OutFrame[FrameHeaderBytes + PayloadSize] = static_cast<std::uint8_t>(Crc >> HighByteShift);
	OutFrame[FrameHeaderBytes + PayloadSize + 1] = static_cast<std::uint8_t>(Crc & LowByteMask);
}

/**
 * Encodes one complete framed message transactionally.
 *
 * Validates before writing: a null payload with nonzero length or a payload too large for the
 * 16-bit length field returns Invalid, a payload that cannot fit the destination returns Full,
 * and a null destination with nonzero length returns Invalid. On Success the full frame is
 * written and OutWritten is set to InPayload.Size()+FrameOverheadBytes; on any non-Success the
 * destination and OutWritten are unchanged.
 *
 * @param InSourceNodeId Sender node id stamped into byte 1 of the frame.
 * @param InPayload Caller-owned bytes framed as the message body.
 * @param OutFrame Caller-owned destination for the complete frame.
 * @param OutWritten Filled with the byte count written only on Success.
 * @return Outcome of the single encode attempt.
 */
inline ETransportResult EncodeFrame(
	const std::uint8_t InSourceNodeId,
	const TSpan<const std::uint8_t> InPayload,
	const TSpan<std::uint8_t> OutFrame,
	std::size_t& OutWritten) noexcept
{
	const ETransportResult ValidationResult = ValidateEncodeInputs(InPayload, OutFrame);
	if (ValidationResult != ETransportResult::Success)
	{
		return ValidationResult;
	}
	const std::size_t PayloadSize = InPayload.Size();
	WriteFrameHeader(InSourceNodeId, PayloadSize, OutFrame);
	AppendPayloadAndChecksum(InPayload, OutFrame);
	OutWritten = PayloadSize + FrameOverheadBytes;
	return ETransportResult::Success;
}

/** Classifies the result of feeding one byte to a frame decoder. */
enum class EFrameEvent : std::uint8_t
{
	/** The byte was accepted but no frame completed or was discarded. */
	None,
	/** A CRC-valid frame completed and is held until ClearFrame or the next consuming PushByte. */
	FrameReady,
	/** A candidate frame was dropped for a bad length or CRC mismatch; the decoder resynced. */
	Discarded,
};

/**
 * Feeds a bounded byte stream and holds the most recent CRC-valid frame in fixed storage without allocating or throwing.
 *
 * After corruption the decoder resyncs on the next well-formed frame, but a length-prefixed framer cannot
 * rewind, so the frame right after a truncated one may be consumed as its payload and lost; the caller must
 * deliver or clear a held frame before pushing more bytes.
 *
 * @tparam MaxPayloadBytes Largest payload byte count the decoder accepts and holds.
 */
template<std::size_t MaxPayloadBytes>
class TFrameDecoder final
{
	static_assert(MaxPayloadBytes > 0, "MaxPayloadBytes must be nonzero so the decoder can hold at least an empty frame.");

public:
	/** Creates a decoder with no held frame, waiting for the next magic byte. */
	TFrameDecoder() noexcept = default;

	/** A bounded value type with fixed storage; destruction releases nothing. */
	~TFrameDecoder() noexcept = default;

	/** Prevents copying so one decoder owns its fixed assembly and held-frame storage. */
	TFrameDecoder(const TFrameDecoder&) = delete;

	/** Prevents copying so one decoder owns its fixed assembly and held-frame storage. */
	TFrameDecoder& operator=(const TFrameDecoder&) = delete;

	/** Prevents moving so the held-frame storage address stays stable for caller-held spans. */
	TFrameDecoder(TFrameDecoder&&) = delete;

	/** Prevents moving so the held-frame storage address stays stable for caller-held spans. */
	TFrameDecoder& operator=(TFrameDecoder&&) = delete;

	/**
	 * Feeds one byte and advances the assembly state machine.
	 *
	 * Returns FrameReady when the byte completes a CRC-valid frame, Discarded when a candidate is
	 * rejected for a bad length or CRC, and None otherwise.
	 *
	 * @param InByte Next byte from the transport stream.
	 * @return Classification of the assembly step.
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

	/** Reports whether a completed frame is currently held and readable. */
	bool HasFrame() const noexcept { return bHasFrame; }

	/** Returns the held frame's source node id; valid only when HasFrame is true. */
	std::uint8_t FrameNodeId() const noexcept { return HeldSourceNodeId; }

	/** Returns a view of the held frame's payload bytes; valid only when HasFrame is true. */
	TSpan<const std::uint8_t> FramePayload() const noexcept { return TSpan<const std::uint8_t>(PayloadStorage, HeldLength); }

	/** Releases the held frame so assembly of the next frame may overwrite its storage. */
	void ClearFrame() noexcept { bHasFrame = false; }

private:
	/** Assembly phase the decoder is currently advancing through. */
	enum class EState : std::uint8_t
	{
		/** Dropping bytes until the next magic byte arrives. */
		WaitingForMagic,
		/** Holding the magic byte and waiting for the source node id. */
		ReadingSourceNodeId,
		/** Waiting for the high byte of the declared payload length. */
		ReadingLengthHighByte,
		/** Waiting for the low byte of the declared payload length. */
		ReadingLengthLowByte,
		/** Accumulating payload bytes until the declared length is reached. */
		ReadingPayload,
		/** Waiting for the high byte of the declared CRC. */
		ReadingCrcHighByte,
		/** Waiting for the low byte of the declared CRC. */
		ReadingCrcLowByte,
	};

	/** WaitingForMagic: arm the running CRC and advance when the magic byte arrives; otherwise drop the byte. */
	EFrameEvent BeginFrameOnMagic(const std::uint8_t InByte) noexcept
	{
		if (InByte == FrameMagicByte)
		{
			RunningCrc = Crc16InitValue;
			State = EState::ReadingSourceNodeId;
		}
		return EFrameEvent::None;
	}

	/** ReadingSourceNodeId: capture the sender id, fold it into the CRC, and wait for the length high byte. */
	EFrameEvent CaptureSourceNodeId(const std::uint8_t InByte) noexcept
	{
		PendingSourceNodeId = InByte;
		UpdateCrc16Byte(RunningCrc, InByte);
		State = EState::ReadingLengthHighByte;
		return EFrameEvent::None;
	}

	/** ReadingLengthHighByte: capture the high length byte and fold it into the CRC. */
	EFrameEvent CaptureLengthHighByte(const std::uint8_t InByte) noexcept
	{
		PendingLengthHighByte = InByte;
		UpdateCrc16Byte(RunningCrc, InByte);
		State = EState::ReadingLengthLowByte;
		return EFrameEvent::None;
	}

	/** ReadingLengthLowByte: complete the declared length, reject an oversize frame, and route to payload
	 * assembly or straight to the CRC when the length is zero. */
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

	/** ReadingPayload: store one payload byte, fold it into the CRC, and advance to the CRC once the payload is full. */
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

	/** ReadingCrcHighByte: capture the high CRC byte (itself excluded from the running CRC) and wait for the low byte. */
	EFrameEvent CaptureChecksumHighByte(const std::uint8_t InByte) noexcept
	{
		PendingCrcHighByte = InByte;
		State = EState::ReadingCrcLowByte;
		return EFrameEvent::None;
	}

	/** ReadingCrcLowByte: compare the received CRC; hold the completed frame on a match, else discard and resync. */
	EFrameEvent CompleteFrameIfChecksumMatches(const std::uint8_t InByte) noexcept
	{
		const std::uint16_t ReceivedCrc =
			static_cast<std::uint16_t>((static_cast<std::uint16_t>(PendingCrcHighByte) << HighByteShift) | static_cast<std::uint16_t>(InByte));
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

	/** Current assembly phase. */
	EState State{EState::WaitingForMagic};

	/** Source node id captured before the length and payload arrive. */
	std::uint8_t PendingSourceNodeId{0};

	/** High byte of the declared length, retained until the low byte arrives. */
	std::uint8_t PendingLengthHighByte{0};

	/** Declared payload byte count captured once both length bytes have arrived. */
	std::uint16_t PendingLength{0};

	/** High byte of the declared CRC, retained until the low byte arrives. */
	std::uint8_t PendingCrcHighByte{0};

	/** Running CRC-16/CCITT-FALSE over the source node id, length, and payload bytes. */
	std::uint16_t RunningCrc{0xFFFFu};

	/** Count of payload bytes accumulated so far in the current candidate frame. */
	std::size_t PayloadIndex{0};

	/** Source node id of the held frame, valid while HasFrame is true. */
	std::uint8_t HeldSourceNodeId{0};

	/** Payload byte count of the held frame, valid while HasFrame is true. */
	std::size_t HeldLength{0};

	/** True once a CRC-valid frame has completed and before ClearFrame is called. */
	bool bHasFrame{false};

	/** Fixed storage for the payload of the frame currently being assembled or held. */
	std::uint8_t PayloadStorage[MaxPayloadBytes]{};
};

} // namespace MicroWorld::Transport::FrameCodec
