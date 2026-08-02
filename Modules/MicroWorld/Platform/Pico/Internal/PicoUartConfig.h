#pragma once

#include <cstdint>

namespace MicroWorld::Platform::Pico
{

/**
 * Motivation: Carries one RP2040 UART identity to the platform entry point without dragging Pico SDK types into
 *   the public header.
 * Responsibilities: Hold UART index, TX/RX GPIO routing, and an exact baud rate for the byte stream to validate.
 * Example:
 *   FPicoUartConfig Config{0, 0, 1, 115200};
 *   Stream.Open(Config);
 */
struct FPicoUartConfig
{
	/** Motivation: Selects the RP2040 UART hardware block: 0 for UART0 or 1 for UART1. */
	std::uint8_t UartIndex{0};

	/** Motivation: Names the RP2040 GPIO routed from UART TX to the attached device RX pin. */
	unsigned int TxGpio{0};

	/** Motivation: Names the RP2040 GPIO routed from the attached device TX pin to UART RX. */
	unsigned int RxGpio{1};

	/** Motivation: Pins the exact baud rate shared with the attached device; zero is treated as invalid. */
	std::uint32_t BaudRate{0};
};

} // namespace MicroWorld::Platform::Pico
