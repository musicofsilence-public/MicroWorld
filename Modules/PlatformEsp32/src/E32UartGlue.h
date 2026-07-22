#pragma once

// =============================================================================
// src/E32UartGlue.h is the SOLE translation unit that pulls ESP-IDF UART headers.
// It is included only by Esp32E32LoraDriver.cpp; a public header must never reach
// it. Every ESP-IDF UART divergence is hidden behind the helpers below so
// Esp32E32LoraDriver.cpp reads one platform-free send/receive path that mirrors
// the UDP driver. The glue is COMPILE-VERIFIED on ESP32-S3 (Phase 5.3) but the
// exact would-block/partial-write behavior of uart_write_bytes and the exact
// drain behavior of uart_read_bytes are UNVERIFIED at runtime: the mapping below
// treats a short write as a would-block candidate and any negative result as an
// error, and a receive that drains with no byte is Unavailable; no radio traffic
// is exercised in the compile-only phase.
// =============================================================================

#include <driver/uart.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Detail
{

/** UART port number type matching the ESP-IDF enum so call sites need no implicit conversion. */
using FUartPort = uart_port_t;

/**
 * Reinterprets the opaque stored port number as its ESP-IDF UART port type.
 *
 * The public header stores the port as a plain `std::int32_t` so it stays free of the ESP-IDF enum; this
 * helper restores the type only where the UART syscalls expect it.
 *
 * @param Stored Opaque port number saved by the driver.
 * @return ESP-IDF UART port number.
 */
inline FUartPort AsUartPort(const std::int32_t Stored) noexcept
{
	return static_cast<FUartPort>(Stored);
}

/** Normalized result of one non-blocking UART write attempt. */
enum class EUartWriteOutcome : std::uint8_t
{
	/** The whole frame was accepted by the UART driver. */
	Sent,
	/** The write accepted fewer than the requested bytes; treat as a transient full condition. */
	WouldBlock,
	/** Any other UART error. */
	Error,
};

/**
 * Writes one complete framed message to the UART.
 *
 * Hands the whole span to one `uart_write_bytes` call; the outcome classifies only whether it was fully
 * accepted, partially accepted, or failed, so the driver can map it to the shared `ENetResult`. The exact
 * would-block and partial-write behavior is UNVERIFIED at runtime (compile-only phase); a short write is
 * mapped to `WouldBlock` so the caller can treat the UART as transiently full.
 *
 * @param Port Open UART port number.
 * @param Data First byte of the framed message to send.
 * @param Length Number of bytes to send.
 * @return Normalized outcome of the single write attempt.
 */
inline EUartWriteOutcome WriteUart(const FUartPort Port, const std::uint8_t* const Data, const std::size_t Length) noexcept
{
	const int Written = uart_write_bytes(Port, reinterpret_cast<const char*>(Data), Length);
	if (Written < 0)
	{
		return EUartWriteOutcome::Error;
	}
	if (static_cast<std::size_t>(Written) != Length)
	{
		return EUartWriteOutcome::WouldBlock;
	}
	return EUartWriteOutcome::Sent;
}

/** Normalized result of one non-blocking single-byte UART read. */
enum class EUartReadStatus : std::uint8_t
{
	/** One byte was read and is available in the out parameter. */
	GotByte,
	/** No byte is ready right now. */
	WouldBlock,
	/** A UART error occurred. */
	Error,
};

/**
 * Reads at most one byte from the UART without blocking.
 *
 * Uses `uart_read_bytes` with a zero timeout so the receive pump polls one byte at a time; a return of zero
 * means the UART is empty and the pump should drain, while a negative return is an error. The exact drain
 * behavior is UNVERIFIED at runtime (compile-only phase).
 *
 * @param Port Open UART port number.
 * @param OutByte Filled with the received byte only when the status is GotByte.
 * @return Normalized status of the single-byte read.
 */
inline EUartReadStatus ReadUartByte(const FUartPort Port, std::uint8_t& OutByte) noexcept
{
	const int Read = uart_read_bytes(Port, &OutByte, 1, 0);
	if (Read < 0)
	{
		return EUartReadStatus::Error;
	}
	if (Read == 0)
	{
		return EUartReadStatus::WouldBlock;
	}
	return EUartReadStatus::GotByte;
}

/** Result of opening and configuring one UART for E32 LoRa traffic. */
struct FOpenedUart
{
	/** True when the UART was parameterized, pinned, and installed; false when construction rolled back. */
	bool bOpen;
};

/**
 * Configures and installs one UART for 8N1 E32 LoRa traffic.
 *
 * Sets the UART to 8N1 at the given baud rate, routes it to the given TX/RX GPIOs with no hardware flow
 * control, and installs the ESP-IDF driver with RX and TX ring buffers of two hardware FIFOs so the install
 * clears the ESP-IDF minimum (both must exceed `UART_HW_FIFO_LEN`). On any configuration failure the partially
 * installed driver is uninstalled and `bOpen` is false, so the constructor can leave the driver inert without
 * throwing. The ring-buffer headroom suits LoRa baud between receive pumps; airtime-tuned sizing is deferred
 * to measured bring-up.
 *
 * @param Port UART port number to open.
 * @param TxGpio TX GPIO number wired to the E32 module's RX pin.
 * @param RxGpio RX GPIO number wired to the E32 module's TX pin.
 * @param BaudRate Baud rate shared with the E32 module's UART configuration.
 * @return Opened-UART descriptor reporting whether installation succeeded.
 */
inline FOpenedUart OpenConfiguredUartPort(
	const FUartPort Port, const std::int32_t TxGpio, const std::int32_t RxGpio, const std::uint32_t BaudRate) noexcept
{
	uart_config_t Config{};
	Config.baud_rate = static_cast<uint32_t>(BaudRate);
	Config.data_bits = UART_DATA_8_BITS;
	Config.parity = UART_PARITY_DISABLE;
	Config.stop_bits = UART_STOP_BITS_1;
	Config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
	Config.source_clk = UART_SCLK_DEFAULT;
	if (uart_param_config(Port, &Config) != ESP_OK)
	{
		return FOpenedUart{false};
	}
	if (uart_set_pin(Port, static_cast<int>(TxGpio), static_cast<int>(RxGpio), UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
	{
		return FOpenedUart{false};
	}
	// ESP-IDF requires the RX ring buffer to exceed the hardware FIFO and the TX ring buffer to be zero or
	// exceed it (esp_driver_uart/src/uart.c); a nonzero TX buffer also keeps uart_write_bytes non-blocking.
	// Two hardware FIFOs clears that floor with headroom for one E32 frame at LoRa baud between pumps.
	const int RingBufferBytes = 2 * UART_HW_FIFO_LEN(Port);
	if (uart_driver_install(Port, RingBufferBytes, RingBufferBytes, 0, nullptr, 0) != ESP_OK)
	{
		return FOpenedUart{false};
	}
	return FOpenedUart{true};
}

/**
 * Uninstalls the UART driver opened by `OpenConfiguredUartPort`.
 *
 * A safe no-op when the UART was never installed; the return value is ignored because the driver is already
 * inert and there is no recovery action at this layer.
 *
 * @param Port UART port number to release.
 */
inline void CloseUart(const FUartPort Port) noexcept
{
	(void)uart_driver_delete(Port);
}

} // namespace MicroWorld::Detail
