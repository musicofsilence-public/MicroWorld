#include <MicroWorld/PlatformPico/Detail/PicoE32LoraPlatform.h>
#include <MicroWorld/PlatformPico/PicoE32LoraDriver.h>

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

/** Pico SDK implementation whose process-lifetime storage keeps the default driver binding valid. */
class FPicoE32LoraPlatform final : public MicroWorld::Detail::IPicoE32LoraPlatform
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

	/** Releases an initialized UART when its driver ends or configuration failed after initialization. */
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

	/** Writes one byte only after the driver observed writable capacity. */
	void WriteUartByte(const std::uint8_t InUartIndex, const std::uint8_t InByte) noexcept override
	{
		if (uart_inst_t* const Uart = ResolveUart(InUartIndex); Uart != nullptr)
		{
			uart_putc_raw(Uart, static_cast<char>(InByte));
		}
	}

	/** Reads one byte only when the SDK UART reports data, preserving bounded driver polling. */
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

namespace MicroWorld::Detail
{

IPicoE32LoraPlatform& GetPicoE32LoraPlatform() noexcept
{
	static FPicoE32LoraPlatform Platform;
	return Platform;
}

} // namespace MicroWorld::Detail

namespace MicroWorld
{

FPicoE32LoraDriver::FPicoE32LoraDriver() noexcept : FPicoE32LoraDriver(Detail::GetPicoE32LoraPlatform()) {}

} // namespace MicroWorld
