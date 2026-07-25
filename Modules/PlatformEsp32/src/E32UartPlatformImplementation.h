#pragma once

// =============================================================================
// src/E32UartPlatformImplementation.h is the SOLE header that pulls ESP-IDF UART headers.
// It is included by two driver translation units — Esp32E32LoraDriver.cpp (the E32
// LoRa radio link) and Esp32UartDriver.cpp (the wired point-to-point UART link) —
// and a public header must never reach it. Every ESP-IDF UART divergence is hidden
// behind the helpers below so both drivers read one platform-free send/receive path
// that mirrors the UDP driver. Example 18's two-board ping-pong runtime-verifies the
// wired-UART path on ESP32-S3 (2026-07-23): uart_write_bytes fully accepts each frame
// and the one-byte uart_read_bytes drain reassembles it, so the full-accept and
// empty-drain outcomes below are proven on real hardware. Two branches stay
// unexercised: the short-write would-block mapping (the ping-pong never saturates the
// TX FIFO) and E32 radio frame traffic (Phase 6.2 measured only no-traffic pump
// overhead). See docs/WIRED_TRANSPORTS_ROADMAP.md §1.2.
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
 * @param InStored Opaque port number saved by the driver.
 * @return ESP-IDF UART port number.
 */
inline FUartPort AsUartPort(const std::int32_t InStored) noexcept
{
	return static_cast<FUartPort>(InStored);
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
 * accepted, partially accepted, or failed, so the driver can map it to the shared `ENetResult`. The
 * full-accept path is runtime-verified (example 18, 2026-07-23); the short-write would-block branch stays
 * unexercised, so a short write is still mapped to `WouldBlock` to treat the UART as transiently full.
 *
 * @param InPort Open UART port number.
 * @param InFrameBytes First byte of the framed message to send.
 * @param InLength Number of bytes to send.
 * @return Normalized outcome of the single write attempt.
 */
inline EUartWriteOutcome WriteUart(const FUartPort InPort, const std::uint8_t* const InFrameBytes, const std::size_t InLength) noexcept
{
	const int Written = uart_write_bytes(InPort, reinterpret_cast<const char*>(InFrameBytes), InLength);
	if (Written < 0)
	{
		return EUartWriteOutcome::Error;
	}
	if (static_cast<std::size_t>(Written) != InLength)
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
 * means the UART is empty and the pump should drain, while a negative return is an error. The one-byte drain
 * is runtime-verified by example 18's ping-pong (2026-07-23).
 *
 * @param InPort Open UART port number.
 * @param OutByte Filled with the received byte only when the status is GotByte.
 * @return Normalized status of the single-byte read.
 */
inline EUartReadStatus ReadUartByte(const FUartPort InPort, std::uint8_t& OutByte) noexcept
{
	const int Read = uart_read_bytes(InPort, &OutByte, 1, 0);
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
 * @param InPort UART port number to open.
 * @param InTxGpio TX GPIO number wired to the E32 module's RX pin.
 * @param InRxGpio RX GPIO number wired to the E32 module's TX pin.
 * @param InBaudRate Baud rate shared with the E32 module's UART configuration.
 * @return Opened-UART descriptor reporting whether installation succeeded.
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
	// Two hardware FIFOs clears that floor with headroom for one E32 frame at LoRa baud between pumps.
	const int RingBufferBytes = 2 * UART_HW_FIFO_LEN(InPort);
	if (uart_driver_install(InPort, RingBufferBytes, RingBufferBytes, 0, nullptr, 0) != ESP_OK)
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
 * @param InPort UART port number to release.
 */
inline void CloseUart(const FUartPort InPort) noexcept
{
	(void)uart_driver_delete(InPort);
}

} // namespace MicroWorld::Detail
