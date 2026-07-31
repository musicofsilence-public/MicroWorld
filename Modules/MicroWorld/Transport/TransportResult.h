#pragma once

#include <MicroWorld/Core/RuntimeResult.h>

#include <cstdint>

namespace MicroWorld::Transport
{

/**
 * Motivation: Gives every Transport byte, queue, packet, and device operation one portable outcome vocabulary so a caller
 *   never confuses a transient "try again later" with a permanent rejection.
 * Responsibilities: Distinguish complete success from missing capacity, malformed input, and transient unavailability with
 *   one normalized meaning per value shared by byte I/O, the manager, and every device.
 * Example:
 *   ETransportResult Result = Reader.ReadByte(Byte);
 *   if (Result == ETransportResult::Full) { Grow(); }
 */
enum class ETransportResult : std::uint8_t
{
	Success, ///< Motivation: Confirms the complete operation succeeded; partial ops never report it.

	Full, ///< Motivation: Reports a well-formed request lacks destination, queue, or transport capacity right now.

	Invalid, ///< Motivation: Rejects a malformed span, oversize packet, or truncated request that can never succeed.

	Unavailable, ///< Motivation: Reports a valid operation has no work or cannot progress now; a later poll may succeed.
};

} // namespace MicroWorld::Transport
