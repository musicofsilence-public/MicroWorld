#pragma once

#include <MicroWorld/Core/ByteCodecConstants.h>
#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Transport/ByteReader.h>
#include <MicroWorld/Transport/EFrameEvent.h>
#include <MicroWorld/Transport/FrameCodec.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Transport::FrameCodec
{

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
