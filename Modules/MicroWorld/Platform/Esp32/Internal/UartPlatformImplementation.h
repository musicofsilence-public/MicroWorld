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

namespace MicroWorld::Platform::Esp32
{

/** Motivation: Names the ESP-IDF UART port number type so call sites need no implicit conversion. */
using FUartPort = uart_port_t;

/**
 * Motivation: Restores the ESP-IDF UART port type from the opaque stored port number so the public header never
 *   carries the ESP-IDF enum.
 * Responsibilities: Reinterpret one opaque port number to its ESP-IDF UART port type where the syscalls expect it.
 */
inline FUartPort AsUartPort(const std::int32_t InStored) noexcept
{
	return static_cast<FUartPort>(InStored);
}

/**
 * Motivation: Gives the device one vocabulary for a UART write attempt that is free of ESP-IDF error codes.
 * Responsibilities: Distinguish a fully accepted write, a short write (treat as transiently full), and a hard error.
 * Example:
 *   if (WriteUart(Port, Frame, Len) == EUartWriteOutcome::Sent) { Done(); }
 */
enum class EUartWriteOutcome : std::uint8_t
{
	Sent,		///< Motivation: The whole frame was accepted by the UART driver.
	WouldBlock, ///< Motivation: The write accepted fewer than the requested bytes; treat as a transient full condition.
	Error,		///< Motivation: Any other UART error.
};

/**
 * Motivation: Writes one complete framed message to the UART behind a normalized outcome so the device never inspects
 *   platform codes.
 * Responsibilities: Hand the whole span to one uart_write_bytes call and classify whether it was fully accepted,
 *   partially accepted, or failed; a short write maps to WouldBlock to treat the UART as transiently full.
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
 * Motivation: Attempts one UART byte write without waiting for TX ring-buffer capacity so a byte-stream poller never blocks.
 * Responsibilities: Confirm one free byte through ESP-IDF before the one-byte WriteUart call; exclusive UART ownership
 *   means no competing writer can consume that confirmed capacity before the write.
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

/**
 * Motivation: Gives the device one vocabulary for a non-blocking single-byte UART read that is free of ESP-IDF
 *   error codes.
 * Responsibilities: Distinguish a read byte, a would-block, and a hard error.
 * Example:
 *   if (ReadUartByte(Port, Byte) == EUartReadStatus::GotByte) { Pump(Byte); }
 */
enum class EUartReadStatus : std::uint8_t
{
	GotByte,	///< Motivation: One byte was read and is available in the out parameter.
	WouldBlock, ///< Motivation: No byte is ready right now.
	Error,		///< Motivation: A UART error occurred.
};

/**
 * Motivation: Reads at most one byte from the UART without blocking so the receive pump polls one byte at a time.
 * Responsibilities: Use uart_read_bytes with a zero timeout; a return of zero means the UART is empty and the pump
 *   should drain, while a negative return is an error.
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
