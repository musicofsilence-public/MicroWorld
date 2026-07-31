#pragma once

// =============================================================================
// src/UartPlatformImplementation.h is the SOLE header that pulls ESP-IDF UART
// headers. It hides ESP-IDF UART divergence behind shared open/read/write/close
// helpers so platform adapters keep public headers free of vendor types. Example
// 18's two-board ping-pong runtime-verifies the wired-UART full-write and
// one-byte-drain paths on ESP32-S3 (2026-07-23). The short-write would-block
// mapping remains unexercised because that checkpoint never saturates the TX
// FIFO. See ../AGENTS.md for the rule this comment satisfies.
// =============================================================================

#include <driver/uart.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld
{

/** UART port number type matching the ESP-IDF enum so call sites need no implicit conversion. */
using FUartPort = uart_port_t;

/**
 * Reinterprets the opaque stored port number as its ESP-IDF UART port type.
 *
 * The public header stores the port as a plain `std::int32_t` so it stays free of the ESP-IDF enum; this
 * helper restores the type only where the UART syscalls expect it.
 *
 * @param InStored Opaque port number saved by the device.
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
 * accepted, partially accepted, or failed, so the device can map it to the shared `ETransportResult`. The
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

/**
 * Attempts one UART byte write without waiting for TX ring-buffer capacity.
 *
 * `uart_write_bytes` may wait for buffered TX space, so this first confirms one free byte through ESP-IDF. Exclusive
 * UART ownership means no competing writer can consume that confirmed capacity before the one-byte WriteUart call;
 * hardware transmission can only free more capacity. The existing whole-frame WriteUart behavior remains unchanged.
 *
 * @param InPort Open UART port number.
 * @param InByte One byte to write after confirming ring-buffer capacity.
 * @return Sent after acceptance, WouldBlock when no byte fits now, or Error after an ESP-IDF failure.
 */
inline EUartWriteOutcome TryWriteUartByte(const FUartPort InPort, const std::uint8_t InByte) noexcept
{
	std::size_t FreeBytes = 0;
	if (uart_get_tx_buffer_free_size(InPort, &FreeBytes) != ESP_OK)
	{
		return EUartWriteOutcome::Error;
	}
	if (FreeBytes == 0)
	{
		return EUartWriteOutcome::WouldBlock;
	}

	return WriteUart(InPort, &InByte, 1);
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

/** Result of opening and configuring one UART for a framed transport. */
struct FOpenedUart
{
	/** True when the UART was parameterized, pinned, and installed; false when construction rolled back. */
	bool bOpen;
};

/**
 * Configures and installs one UART for 8N1 framed transport traffic.
 *
 * Sets the UART to 8N1 at the given baud rate, routes it to the given TX/RX GPIOs with no hardware flow
 * control, and installs the ESP-IDF driver with RX and TX ring buffers of two hardware FIFOs so the install
 * clears the ESP-IDF minimum (both must exceed `UART_HW_FIFO_LEN`). On any configuration failure the partially
 * installed driver is uninstalled and `bOpen` is false, so the constructor can leave the device inert without
 * throwing. The ring-buffer headroom suits bounded framing work between receive pumps; airtime-tuned sizing is
 * deferred to measured bring-up.
 *
 * @param InPort UART port number to open.
 * @param InTxGpio TX GPIO number wired to the attached device RX pin.
 * @param InRxGpio RX GPIO number wired from the attached device TX pin.
 * @param InBaudRate Baud rate shared with the attached device UART configuration.
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
	// Two hardware FIFOs clear that floor with headroom for one framed transport message between pumps.
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
 * A safe no-op when the UART was never installed; the return value is ignored because the device is already
 * inert and there is no recovery action at this layer.
 *
 * @param InPort UART port number to release.
 */
inline void CloseUart(const FUartPort InPort) noexcept
{
	(void)uart_driver_delete(InPort);
}

} // namespace MicroWorld
