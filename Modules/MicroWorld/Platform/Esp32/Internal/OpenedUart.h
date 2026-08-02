#pragma once

#include <MicroWorld/Platform/Esp32/Internal/UartPort.h>

#include <driver/uart.h>

#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Reports whether opening and configuring one UART for a framed transport succeeded.
 * Responsibilities: Carry the open flag.
 * Example:
 *   FOpenedUart Opened = OpenConfiguredUartPort(Port, Tx, Rx, Baud);
 */
struct FOpenedUart
{
	/** Motivation: True when the UART was parameterized, pinned, and installed; false when construction rolled back. */
	bool bOpen;
};

/**
 * Motivation: Configures and installs one UART for 8N1 framed transport behind one helper.
 * Responsibilities: Set 8N1 at the given baud, route to the given TX/RX GPIOs with no flow control, and install the
 *   driver with RX and TX ring buffers of two hardware FIFOs (both must exceed UART_HW_FIFO_LEN); on any failure
 *   uninstall the partially installed driver and return bOpen false so the constructor can leave the device inert
 *   without throwing.
 */
inline FOpenedUart OpenConfiguredUartPort(
	const FUartPort InPort, const std::int32_t InTxGpio, const std::int32_t InRxGpio, const std::uint32_t InBaudRate) noexcept
{
	uart_config_t Config{};
	Config.baud_rate = static_cast<uint32_t>(InBaudRate);
	Config.data_bits = UART_DATA_8_BITS;
	Config.parity = UART_PARITY_DISABLE;
	Config.stop_bits = UART_STOP_BITS_1;
	Config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
	Config.source_clk = UART_SCLK_DEFAULT;
	if (uart_param_config(InPort, &Config) != ESP_OK)
	{
		return FOpenedUart{false};
	}
	if (uart_set_pin(InPort, static_cast<int>(InTxGpio), static_cast<int>(InRxGpio), UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
	{
		return FOpenedUart{false};
	}
	// ESP-IDF requires the RX ring buffer to exceed the hardware FIFO and the TX ring buffer to be zero or
	// exceed it (esp_driver_uart/src/uart.c); a nonzero TX buffer also keeps uart_write_bytes non-blocking.
	// Two hardware FIFOs clear that floor with headroom for one framed transport message between pumps.
	const int RingBufferBytes = 2 * UART_HW_FIFO_LEN(InPort);
	if (uart_driver_install(InPort, RingBufferBytes, RingBufferBytes, 0, nullptr, 0) != ESP_OK)
	{
		return FOpenedUart{false};
	}
	return FOpenedUart{true};
}

/**
 * Motivation: Tears down the UART driver behind a safe helper so the device destructor needs no validity branch.
 * Responsibilities: Ignore the return value because the device is already inert and there is no recovery action at
 *   this layer.
 */
inline void CloseUart(const FUartPort InPort) noexcept
{
	(void)uart_driver_delete(InPort);
}

} // namespace MicroWorld::Platform::Esp32
