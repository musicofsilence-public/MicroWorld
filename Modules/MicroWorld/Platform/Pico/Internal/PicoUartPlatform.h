#pragma once

#include <cstdint>

namespace MicroWorld::Platform::Pico
{

/**
 * Motivation: Keeps Pico SDK types out of the byte-stream policy interface so byte-stream logic can be tested without
 *   RP2040 headers.
 * Responsibilities: Offer one borrowed-lifetime UART binding; it is a PlatformPico implementation detail, not a reusable
 *   hardware abstraction or supported direct-composition API.
 * Example:
 *   IPicoUartPlatform& Platform = GetPicoUartPlatform();
 *   if (Platform.IsUartWritable(Index)) { Platform.WriteUartByte(Index, Byte); }
 */
class IPicoUartPlatform
{
public:
	/**
	 * Motivation: Ensures every UART a binding opened is released, including by deleting through the interface pointer.
	 * Responsibilities: Provide a virtual release hook that does nothing in the abstract base.
	 */
	virtual ~IPicoUartPlatform() noexcept = default;

	/**
	 * Motivation: Lets one byte stream claim and configure a Pico UART through a single SDK-free call.
	 * Responsibilities: Return the achieved baud rate when the UART accepts it exactly, or zero when the index is
	 *   unavailable, leaving pin and baud mismatch reporting to the caller.
	 */
	virtual std::uint32_t OpenUart(std::uint8_t InUartIndex, unsigned int InTxGpio, unsigned int InRxGpio, std::uint32_t InBaudRate) noexcept = 0;

	/**
	 * Motivation: Lets a byte stream release one UART by its identity on close or after a failed configuration.
	 * Responsibilities: Undo the matching OpenUart for the requested index.
	 */
	virtual void CloseUart(std::uint8_t InUartIndex) noexcept = 0;

	/**
	 * Motivation: Lets a byte stream ask for non-blocking transmit capacity before queuing one byte.
	 * Responsibilities: Report whether one raw byte can be written without blocking.
	 */
	virtual bool IsUartWritable(std::uint8_t InUartIndex) noexcept = 0;

	/**
	 * Motivation: Lets a byte stream push one byte after observing writable capacity.
	 * Responsibilities: Accept one raw byte only after IsUartWritable reported capacity.
	 */
	virtual void WriteUartByte(std::uint8_t InUartIndex, std::uint8_t InByte) noexcept = 0;

	/**
	 * Motivation: Lets a byte stream poll one byte without blocking when no data is available.
	 * Responsibilities: Read one raw byte when available and leave OutByte untouched otherwise.
	 */
	virtual bool TryReadUartByte(std::uint8_t InUartIndex, std::uint8_t& OutByte) noexcept = 0;

protected:
	/**
	 * Motivation: Restricts construction to concrete bindings so callers depend only on the stable interface.
	 * Responsibilities: Stay inert so subclasses own any SDK state.
	 */
	IPicoUartPlatform() noexcept = default;
};

/**
 * Motivation: Gives the default byte-stream constructor one shared process-lifetime Pico SDK binding.
 * Responsibilities: Return a single stable binding that outlives every borrowing stream.
 */
IPicoUartPlatform& GetPicoUartPlatform() noexcept;

} // namespace MicroWorld::Platform::Pico
