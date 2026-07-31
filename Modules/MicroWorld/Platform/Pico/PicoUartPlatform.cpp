#include <MicroWorld/Platform/Pico/PicoLoraDevice.h>

#include <MicroWorld/Platform/Pico/Internal/PicoUartByteStream.h>
#include <MicroWorld/Platform/Pico/Internal/PicoUartPlatform.h>

#include <hardware/gpio.h>
#include <hardware/uart.h>

namespace
{

/** Resolves the two UART identities supported by the RP2040 Pico SDK. */
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

/** Pico SDK implementation whose process-lifetime storage keeps the default byte-stream binding valid. */
class FPicoUartPlatform final : public MicroWorld::Platform::Pico::IPicoUartPlatform
{
public:
	/** Opens the requested UART and configures its pins after the requested baud rate was accepted. */
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

	/** Releases an initialized UART when its byte stream ends or configuration failed after initialization. */
	void CloseUart(const std::uint8_t InUartIndex) noexcept override
	{
		if (uart_inst_t* const Uart = ResolveUart(InUartIndex); Uart != nullptr)
		{
			uart_deinit(Uart);
		}
	}

	/** Reports the SDK UART's non-blocking transmit capacity. */
	bool IsUartWritable(const std::uint8_t InUartIndex) noexcept override
	{
		if (uart_inst_t* const Uart = ResolveUart(InUartIndex); Uart != nullptr)
		{
			return uart_is_writable(Uart);
		}

		return false;
	}

	/** Writes one byte only after the byte stream observed writable capacity. */
	void WriteUartByte(const std::uint8_t InUartIndex, const std::uint8_t InByte) noexcept override
	{
		if (uart_inst_t* const Uart = ResolveUart(InUartIndex); Uart != nullptr)
		{
			uart_putc_raw(Uart, static_cast<char>(InByte));
		}
	}

	/** Reads one byte only when the SDK UART reports data, preserving bounded byte-stream polling. */
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
