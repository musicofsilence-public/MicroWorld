#pragma once

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Core
{

/**
 * Motivation: Gives every byte-level codec one shared place for the numeric values that would
 *   otherwise appear as bare literals in shifts, masks, and bounds checks across Transport
 *   framing and Messaging encode/decode, both of which depend on Core.
 * Responsibilities: Name only the shared octet-level values; leave module-specific sentinels
 *   such as CRC polynomials or sequence-number half spaces local to the module that owns them.
 */
/** Motivation: Names the octet width used by bit-by-bit CRC advance loops and bit-pack shifts. */
inline constexpr int BitsPerByte = 8;

/** Motivation: Selects the low eight bits of a wider value as the single-byte fragment written or read. */
inline constexpr std::uint8_t LowByteMask = 0xFFu;

/** Motivation: Places one byte into the high octet of a 16-bit value. */
inline constexpr std::uint16_t HighByteShift = 8u;

/** Motivation: Bounds the framed payload length with the largest unsigned 16-bit value. */
inline constexpr std::uint16_t Uint16Max = 0xFFFFu;

} // namespace MicroWorld::Core
