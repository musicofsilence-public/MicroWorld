#pragma once

#include <MicroWorld/Core/IO/UartByteStreamResult.h>

#include <cstdint>

namespace MicroWorld::Core
{

/**
 * Motivation: Gives platform adapters the smallest portable contract for moving individual UART
 *   bytes without allocating, flushing, or exposing buffering.
 * Responsibilities: Move one byte at a time and leave UART configuration and lifetime to the adapter.
 * Example:
 *   class FDriver : public IUartByteStream { // adapter body
 *   };
 *   FDriver Stream;
 *   Stream.TryWriteByte(0x55);
 */
class IUartByteStream
{
public:
	/**
	 * Motivation: Lets a platform adapter be destroyed through the portable byte-stream contract.
	 * Responsibilities: Release adapter resources without touching UART configuration beyond this object.
	 */
	virtual ~IUartByteStream() noexcept;

	/**
	 * Motivation: Lets a caller push one byte without blocking the caller's thread.
	 * Responsibilities: Move exactly one byte or report Unavailable while the caller retains InByte for retry.
	 */
	virtual EUartByteStreamResult TryWriteByte(std::uint8_t InByte) noexcept = 0;

	/**
	 * Motivation: Lets a caller pull one byte without blocking the caller's thread.
	 * Responsibilities: Read one byte and change OutByte only when the result is Success.
	 */
	virtual EUartByteStreamResult TryReadByte(std::uint8_t& OutByte) noexcept = 0;

protected:
	/**
	 * Motivation: Keeps the interface non-instantiable while allowing platform adapters to construct it.
	 * Responsibilities: Provide a protected default construction so only derived adapters can create instances.
	 */
	IUartByteStream() noexcept = default;
};

} // namespace MicroWorld::Core
