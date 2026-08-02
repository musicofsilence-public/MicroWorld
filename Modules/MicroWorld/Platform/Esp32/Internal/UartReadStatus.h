#pragma once

#include <MicroWorld/Platform/Esp32/Internal/UartPort.h>

#include <driver/uart.h>

namespace MicroWorld::Platform::Esp32
{

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

} // namespace MicroWorld::Platform::Esp32
