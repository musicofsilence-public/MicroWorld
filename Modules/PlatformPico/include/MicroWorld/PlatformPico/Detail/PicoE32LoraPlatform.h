#pragma once

#include <cstdint>

namespace MicroWorld::Detail
{

/**
 * Narrow Pico UART binding used to test E32 driver policy without Pico SDK headers.
 *
 * The driver borrows this interface for its lifetime. It is a PlatformPico implementation detail rather than a
 * reusable hardware abstraction.
 */
class IPicoE32LoraPlatform
{
public:
	/** Releases an E32 UART opened by this platform binding. */
	virtual ~IPicoE32LoraPlatform() noexcept = default;

	/**
	 * Opens and configures one Pico UART for the requested E32 transport settings.
	 *
	 * @return The baud rate accepted by the UART, or zero when the UART index is unavailable.
	 */
	virtual std::uint32_t OpenUart(std::uint8_t InUartIndex, unsigned int InTxGpio, unsigned int InRxGpio, std::uint32_t InBaudRate) noexcept = 0;

	/** Releases the UART identified by the requested Pico UART index. */
	virtual void CloseUart(std::uint8_t InUartIndex) noexcept = 0;

	/** Reports whether one raw UART byte can be accepted without blocking. */
	virtual bool IsUartWritable(std::uint8_t InUartIndex) noexcept = 0;

	/** Writes one raw UART byte after IsUartWritable reported capacity. */
	virtual void WriteUartByte(std::uint8_t InUartIndex, std::uint8_t InByte) noexcept = 0;

	/** Reads one raw UART byte when available and leaves OutByte untouched otherwise. */
	virtual bool TryReadUartByte(std::uint8_t InUartIndex, std::uint8_t& OutByte) noexcept = 0;

protected:
	/** Restricts construction to concrete bindings because callers only depend on the stable interface. */
	IPicoE32LoraPlatform() noexcept = default;
};

/** Returns the process-lifetime Pico SDK binding used by the production E32 driver constructor. */
IPicoE32LoraPlatform& GetPicoE32LoraPlatform() noexcept;

} // namespace MicroWorld::Detail
