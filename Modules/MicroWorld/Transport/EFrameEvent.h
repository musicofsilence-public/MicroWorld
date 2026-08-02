#pragma once

#include <cstdint>

namespace MicroWorld::Transport::FrameCodec
{

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

} // namespace MicroWorld::Transport::FrameCodec
