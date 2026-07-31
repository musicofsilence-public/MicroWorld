#pragma once

#include <cstdint>

namespace MicroWorld::Core
{

/**
 * Reports the immediate outcome of one non-blocking UART byte operation.
 *
 * `Unavailable` is normal backpressure; `Error` reports that the underlying
 * platform operation failed and callers must handle the incomplete transfer.
 */
enum class EUartByteStreamResult : std::uint8_t
{
	Success,	 ///< Confirms that exactly one byte moved.
	Unavailable, ///< Reports that no byte moved and callers may retry the operation later.
	Error,		 ///< Makes a platform UART failure observable to the caller.
};

/**
 * Provides the smallest portable non-blocking UART transfer interface.
 *
 * Platform adapters own UART configuration and lifetime; this interface only
 * moves individual bytes without allocating, flushing, or exposing buffering.
 */
class IUartByteStream
{
public:
	/** Lets a platform adapter be destroyed through the portable byte-stream contract. */
	virtual ~IUartByteStream() noexcept;

	/** Attempts to write one byte; `Unavailable` means no byte moved and the caller retains `InByte` for retry. */
	virtual EUartByteStreamResult TryWriteByte(std::uint8_t InByte) noexcept = 0;

	/** Attempts to read one byte and changes `OutByte` only when the result is `Success`. */
	virtual EUartByteStreamResult TryReadByte(std::uint8_t& OutByte) noexcept = 0;

protected:
	/** Keeps the interface non-instantiable while allowing platform adapters to construct it. */
	IUartByteStream() noexcept = default;
};

} // namespace MicroWorld::Core
