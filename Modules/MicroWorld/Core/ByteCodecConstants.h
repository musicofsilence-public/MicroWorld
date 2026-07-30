#pragma once

#include <cstddef>
#include <cstdint>

namespace MicroWorld
{

/**
 * Numeric constants shared by every byte-level codec (Net framing and Messaging encode/decode).
 *
 * These name the values that would otherwise appear as bare literals in shifts, masks, and bounds
 * checks across Net and Messaging, both of which depend on Core. Module-specific sentinels such as
 * CRC polynomials or sequence-number half spaces stay local to the module that owns them.
 */

/** Number of bits per octet; used by bit-by-bit CRC advance loops and bit-pack shifts. */
inline constexpr int BitsPerByte = 8;

/** Mask selecting the low eight bits of a wider value; the single-byte fragment written or read. */
inline constexpr std::uint8_t LowByteMask = 0xFFu;

/** Shift placing a byte into the high octet of a 16-bit value. */
inline constexpr std::uint16_t HighByteShift = 8u;

/** Largest value representable in an unsigned 16-bit field; bounds the framed payload length. */
inline constexpr std::uint16_t Uint16Max = 0xFFFFu;

} // namespace MicroWorld
