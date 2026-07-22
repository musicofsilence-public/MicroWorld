#pragma once

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Net/NetResult.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace MicroWorld
{

/** Single-byte sentinel that marks the start of every framed message on the wire. */
constexpr std::uint8_t FrameMagicByte = 0xA5;

/** Fixed framing cost in bytes: one magic, one source node id, two length bytes, and two CRC bytes. */
constexpr std::size_t FrameOverheadBytes = 6;

namespace Detail
{

	/**
	 * Advances a CRC-16/CCITT-FALSE accumulator by one byte.
	 *
	 * @param Crc Accumulator updated in place; initialize to 0xFFFF before the first byte.
	 * @param Byte Next byte covered by the checksum.
	 */
	inline void UpdateCrc16Byte(std::uint16_t& Crc, const std::uint8_t Byte) noexcept
	{
		Crc = static_cast<std::uint16_t>(Crc ^ static_cast<std::uint16_t>(static_cast<std::uint16_t>(Byte) << 8));
		for (int Bit = 0; Bit < 8; ++Bit)
		{
			if ((Crc & 0x8000u) != 0u)
			{
				Crc = static_cast<std::uint16_t>(static_cast<std::uint16_t>(Crc << 1) ^ 0x1021u);
			}
			else
			{
				Crc = static_cast<std::uint16_t>(Crc << 1);
			}
		}
	}

} // namespace Detail

/**
 * Computes a CRC-16/CCITT-FALSE checksum over a byte span without allocating or throwing.
 *
 * Parameters are polynomial 0x1021, init 0xFFFF, no input or output reflection, and xorout
 * 0x0000; the canonical check value of ASCII "123456789" is 0x29B1.
 *
 * @param Bytes Caller-owned span covered by the checksum; a valid empty span returns 0xFFFF.
 * @return The computed checksum.
 */
inline std::uint16_t ComputeCrc16Ccitt(const TSpan<const std::uint8_t> Bytes) noexcept
{
	std::uint16_t Crc = 0xFFFFu;
	const std::uint8_t* const ChecksumBytes = Bytes.Data();
	const std::size_t ByteCount = Bytes.Size();
	// A null pointer with a nonzero count is an invalid view; do not dereference it.
	if (ChecksumBytes == nullptr)
	{
		return Crc;
	}
	for (std::size_t Index = 0; Index < ByteCount; ++Index)
	{
		Detail::UpdateCrc16Byte(Crc, ChecksumBytes[Index]);
	}
	return Crc;
}

/**
 * Encodes one complete framed message transactionally.
 *
 * Validates before writing: a null payload with nonzero length returns Invalid, an oversize
 * payload that cannot fit the length field or the destination returns Full, and a null
 * destination with nonzero length returns Invalid. On Success the full frame is written and
 * OutWritten is set to Payload.Size()+FrameOverheadBytes; on any non-Success the destination
 * and OutWritten are unchanged.
 *
 * @param SourceNodeId Sender node id stamped into byte 1 of the frame.
 * @param Payload Caller-owned bytes framed as the message body.
 * @param OutFrame Caller-owned destination for the complete frame.
 * @param OutWritten Filled with the byte count written only on Success.
 * @return Outcome of the single encode attempt.
 */
inline ENetResult EncodeFrame(
	const std::uint8_t SourceNodeId, const TSpan<const std::uint8_t> Payload, const TSpan<std::uint8_t> OutFrame, std::size_t& OutWritten) noexcept
{
	const std::size_t PayloadSize = Payload.Size();
	// Validate every input before touching the destination so a rejection is truly transactional.
	if (PayloadSize != 0 && Payload.Data() == nullptr)
	{
		return ENetResult::Invalid;
	}
	if (OutFrame.Size() != 0 && OutFrame.Data() == nullptr)
	{
		return ENetResult::Invalid;
	}
	if (PayloadSize > 0xFFFFu)
	{
		return ENetResult::Full;
	}
	const std::size_t RequiredFrameBytes = PayloadSize + FrameOverheadBytes;
	if (RequiredFrameBytes > OutFrame.Size())
	{
		return ENetResult::Full;
	}
	OutFrame[0] = FrameMagicByte;
	OutFrame[1] = SourceNodeId;
	OutFrame[2] = static_cast<std::uint8_t>(PayloadSize >> 8);
	OutFrame[3] = static_cast<std::uint8_t>(PayloadSize & 0xFFu);
	if (PayloadSize != 0)
	{
		std::memcpy(&OutFrame[4], Payload.Data(), PayloadSize);
	}
	// CRC covers the source node id, both length bytes, and the payload; magic and CRC are excluded.
	const std::uint16_t Crc = ComputeCrc16Ccitt(TSpan<const std::uint8_t>(&OutFrame[1], 3 + PayloadSize));
	OutFrame[4 + PayloadSize] = static_cast<std::uint8_t>(Crc >> 8);
	OutFrame[4 + PayloadSize + 1] = static_cast<std::uint8_t>(Crc & 0xFFu);
	OutWritten = RequiredFrameBytes;
	return ENetResult::Success;
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
	 * @param Byte Next byte from the transport stream.
	 * @return Classification of the assembly step.
	 */
	EFrameEvent PushByte(const std::uint8_t Byte) noexcept
	{
		switch (State)
		{
			case EState::WaitingForMagic:
				if (Byte == FrameMagicByte)
				{
					RunningCrc = 0xFFFFu;
					State = EState::ReadingSourceNodeId;
				}
				return EFrameEvent::None;
			case EState::ReadingSourceNodeId:
				PendingSourceNodeId = Byte;
				Detail::UpdateCrc16Byte(RunningCrc, Byte);
				State = EState::ReadingLengthHighByte;
				return EFrameEvent::None;
			case EState::ReadingLengthHighByte:
				PendingLengthHighByte = Byte;
				Detail::UpdateCrc16Byte(RunningCrc, Byte);
				State = EState::ReadingLengthLowByte;
				return EFrameEvent::None;
			case EState::ReadingLengthLowByte:
			{
				Detail::UpdateCrc16Byte(RunningCrc, Byte);
				const std::uint16_t DeclaredLength =
					static_cast<std::uint16_t>((static_cast<std::uint16_t>(PendingLengthHighByte) << 8) | static_cast<std::uint16_t>(Byte));
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
			case EState::ReadingPayload:
				PayloadStorage[PayloadIndex] = Byte;
				++PayloadIndex;
				Detail::UpdateCrc16Byte(RunningCrc, Byte);
				if (PayloadIndex >= PendingLength)
				{
					State = EState::ReadingCrcHighByte;
				}
				return EFrameEvent::None;
			case EState::ReadingCrcHighByte:
				PendingCrcHighByte = Byte;
				State = EState::ReadingCrcLowByte;
				return EFrameEvent::None;
			case EState::ReadingCrcLowByte:
			{
				const std::uint16_t ReceivedCrc =
					static_cast<std::uint16_t>((static_cast<std::uint16_t>(PendingCrcHighByte) << 8) | static_cast<std::uint16_t>(Byte));
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

} // namespace MicroWorld
