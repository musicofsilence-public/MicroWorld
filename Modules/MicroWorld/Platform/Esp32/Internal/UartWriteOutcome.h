#pragma once

#include <MicroWorld/Platform/Esp32/Internal/UartPort.h>

#include <driver/uart.h>

#include <cstddef>

namespace MicroWorld::Platform::Esp32
{

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

} // namespace MicroWorld::Platform::Esp32
