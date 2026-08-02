#pragma once

#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Carries the plain-integer UART parameters one ESP32 E32 LoRa facade needs at construction so the
 *   released public config stays free of ESP-IDF enum types.
 * Responsibilities: Hold UART port, TX/RX GPIO, baud rate, and local node id as plain integers.
 * Example:
 *   FEsp32E32LoraConfig Config;
 *   Config.BaudRate = 9600;
 */
struct FEsp32E32LoraConfig
{
	/** Motivation: UART port number (ESP-IDF uart_port_t, e.g. UART_NUM_1) passed as a plain integer. */
	std::int32_t UartPort{0};

	/** Motivation: TX GPIO number wired to the E32 module's RX pin, passed as a plain integer. */
	std::int32_t TxGpio{0};

	/** Motivation: RX GPIO number wired to the E32 module's TX pin, passed as a plain integer. */
	std::int32_t RxGpio{0};

	/** Motivation: Baud rate shared with the E32 module's UART configuration (commonly 9600). */
	std::uint32_t BaudRate{9600};

	/** Motivation: Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeId{0};
};

} // namespace MicroWorld::Platform::Esp32
