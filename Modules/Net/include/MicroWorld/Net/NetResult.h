#pragma once

#include <cstdint>

namespace MicroWorld
{

/**
 * Reports the complete portable outcome of one Net byte, queue, packet, or driver operation.
 *
 * Byte I/O, the manager, and every driver share one result enum with one normalized
 * meaning per value so a caller never confuses a transient "try again later" with a
 * permanent rejection or vice versa.
 */
enum class ENetResult : std::uint8_t
{
	/**
	 * The complete operation succeeded.
	 * Partial writes, partial reads, partial sends, and partial receives never report Success.
	 */
	Success,

	/**
	 * A valid operation lacks destination, queue, or transport capacity.
	 * The request itself is well-formed; only the bounded destination or FIFO is full right now.
	 */
	Full,

	/**
	 * Rejects an invalid span/configuration, an oversized packet, or a truncated byte-reader request.
	 * The request can never succeed as stated; retrying it unchanged cannot help.
	 */
	Invalid,

	/**
	 * A valid non-blocking driver or manager operation has no work or cannot make progress now.
	 * The transport is empty, the FIFO is empty, or a peer is not ready; a later poll may succeed.
	 */
	Unavailable,
};

} // namespace MicroWorld
