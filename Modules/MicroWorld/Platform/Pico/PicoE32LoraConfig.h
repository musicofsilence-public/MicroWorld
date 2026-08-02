#pragma once

#include <cstdint>

namespace MicroWorld::Platform::Pico
{

/**
 * Motivation: Describes one RP2040 UART connection to an E32 module without leaking Pico SDK types into device callers.
 * Responsibilities: Hold UART index, TX/RX GPIO routing, baud rate, and local node id for the application entry point to pass
 *   into Initialize; the device takes the UART exclusively and never shares it.
 * Example:
 *   FPicoE32LoraConfig Config{0, 0, 1, 115200, 0x42};
 *   Device.Initialize(Config);
 */
struct FPicoE32LoraConfig
{
	/** Motivation: Selects the RP2040 UART hardware block: 0 for UART0 or 1 for UART1. */
	std::uint8_t UartIndex{0};

	/** Motivation: Names the RP2040 GPIO routed from UART TX to the E32 RXD pin. */
	unsigned int TxGpio{0};

	/** Motivation: Names the RP2040 GPIO routed from the E32 TXD pin to UART RX. */
	unsigned int RxGpio{1};

	/** Motivation: Pins the exact baud rate configured on the E32; zero is treated as invalid. */
	std::uint32_t BaudRate{0};

	/** Motivation: Carries the source node id stamped into every outgoing MicroWorld frame. */
	std::uint8_t LocalNodeId{0};
};

} // namespace MicroWorld::Platform::Pico
