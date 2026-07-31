#include <MicroWorld/Platform/Pico/PicoLoraDevice.h>

#include <MicroWorld/Platform/Pico/Internal/PicoUartByteStream.h>
#include <MicroWorld/Platform/Pico/Internal/PicoUartPlatform.h>

#include <hardware/gpio.h>
#include <hardware/uart.h>

namespace
{

/**
 * Motivation: Maps a portable UART index to the concrete RP2040 SDK handle at the platform edge.
 * Responsibilities: Return uart0 for index 0, uart1 for index 1, or nullptr for any other index.
 */
uart_inst_t* ResolveUart(const std::uint8_t InUartIndex) noexcept
{
	if (InUartIndex == 0u)
	{
		return uart0;
	}

	if (InUartIndex == 1u)
	{
		return uart1;
	}

	return nullptr;
}

/**
 * Motivation: Backs the borrowed byte-stream interface with the RP2040 SDK from one place so byte-stream callers stay
 *   SDK-free.
 * Responsibilities: Live for the whole process so the default byte-stream binding stays valid; translate each byte-stream
 *   operation into the matching uart/gpio SDK call.
 * Example:
 *   IPicoUartPlatform& Platform = GetPicoUartPlatform();
 *   Platform.OpenUart(0, 0, 1, 115200);
 */
class FPicoUartPlatform final : public MicroWorld::Platform::Pico::IPicoUartPlatform
{
public:
	/**
	 * Motivation: Lets a byte stream bring one RP2040 UART online behind a single SDK call.
	 * Responsibilities: Resolve the UART, accept only the exact requested baud, and configure TX/RX pins and format.
	 */
	std::uint32_t OpenUart(
		const std::uint8_t InUartIndex, const unsigned int InTxGpio, const unsigned int InRxGpio, const std::uint32_t InBaudRate) noexcept override
	{
		uart_inst_t* const Uart = ResolveUart(InUartIndex);
		if (Uart == nullptr)
		{
			return 0u;
		}

		const std::uint32_t ActualBaudRate = uart_init(Uart, InBaudRate);
		if (ActualBaudRate != InBaudRate)
		{
			return ActualBaudRate;
		}

		gpio_set_function(InTxGpio, GPIO_FUNC_UART);
		gpio_set_function(InRxGpio, GPIO_FUNC_UART);
		uart_set_format(Uart, 8u, 1u, UART_PARITY_NONE);
		uart_set_hw_flow(Uart, false, false);
		uart_set_fifo_enabled(Uart, true);
		return ActualBaudRate;
	}

	/**
	 * Motivation: Lets a byte stream return its UART to the SDK on close or after a failed configuration.
	 * Responsibilities: Resolve and deinitialize the UART identified by its index, ignoring an unknown index.
	 */
	void CloseUart(const std::uint8_t InUartIndex) noexcept override
	{
		if (uart_inst_t* const Uart = ResolveUart(InUartIndex); Uart != nullptr)
		{
			uart_deinit(Uart);
		}
	}

	/**
	 * Motivation: Lets a byte stream ask for non-blocking transmit capacity before queueing a byte.
	 * Responsibilities: Report the resolved UART's writability, or false for an unknown index.
	 */
	bool IsUartWritable(const std::uint8_t InUartIndex) noexcept override
	{
		if (uart_inst_t* const Uart = ResolveUart(InUartIndex); Uart != nullptr)
		{
			return uart_is_writable(Uart);
		}

		return false;
	}

	/**
	 * Motivation: Lets a byte stream push one byte after it observed writable capacity.
	 * Responsibilities: Write the byte to the resolved UART, ignoring an unknown index.
	 */
	void WriteUartByte(const std::uint8_t InUartIndex, const std::uint8_t InByte) noexcept override
	{
		if (uart_inst_t* const Uart = ResolveUart(InUartIndex); Uart != nullptr)
		{
			uart_putc_raw(Uart, static_cast<char>(InByte));
		}
	}

	/**
	 * Motivation: Lets a byte stream poll one byte without blocking on an empty UART.
	 * Responsibilities: Read a byte only when the resolved UART reports data, leaving OutByte untouched otherwise.
	 */
	bool TryReadUartByte(const std::uint8_t InUartIndex, std::uint8_t& OutByte) noexcept override
	{
		if (uart_inst_t* const Uart = ResolveUart(InUartIndex); Uart != nullptr && uart_is_readable(Uart))
		{
			OutByte = static_cast<std::uint8_t>(uart_getc(Uart));
			return true;
		}

		return false;
	}
};

} // namespace

namespace MicroWorld::Platform::Pico
{

IPicoUartPlatform& GetPicoUartPlatform() noexcept
{
	static FPicoUartPlatform Platform;
	return Platform;
}

FPicoUartByteStream::FPicoUartByteStream() noexcept : FPicoUartByteStream(GetPicoUartPlatform()) {}

} // namespace MicroWorld::Platform::Pico

namespace MicroWorld::Platform::Pico
{

FPicoLoraDevice::FPicoLoraDevice() noexcept : ByteStream(), RadioDevice(ByteStream) {}

} // namespace MicroWorld::Platform::Pico
