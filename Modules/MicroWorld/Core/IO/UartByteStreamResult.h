#pragma once

#include <cstdint>

namespace MicroWorld::Core
{

/**
 * Motivation: Gives one non-blocking UART byte operation an outcome that separates normal
 *   backpressure from a real platform failure.
 * Responsibilities: Distinguish success from temporary unavailability and from an error that
 *   leaves the transfer incomplete.
 * Example:
 *   if (Stream.TryReadByte(Byte) == EUartByteStreamResult::Unavailable) { Retry(); }
 */
enum class EUartByteStreamResult : std::uint8_t
{
	Success,	 ///< Motivation: Confirms that exactly one byte moved.
	Unavailable, ///< Motivation: Reports that no byte moved and callers may retry the operation later.
	Error,		 ///< Motivation: Makes a platform UART failure observable to the caller.
};

} // namespace MicroWorld::Core
