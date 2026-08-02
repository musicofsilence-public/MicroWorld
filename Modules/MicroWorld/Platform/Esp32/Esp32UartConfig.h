#pragma once

#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Carries the plain-integer UART parameters one wired device needs at construction so the public header
 *   stays free of the ESP-IDF uart_port_t/gpio_num_t enum types.
 * Responsibilities: Hold UART port, TX/RX GPIO, baud rate, and local node id as plain integers.
 * Example:
 *   FEsp32UartConfig Config;
 *   Config.BaudRate = 115200;
 */
struct FEsp32UartConfig
{
	/** Motivation: UART port number (ESP-IDF uart_port_t, e.g. UART_NUM_1) passed as a plain integer. */
	std::int32_t UartPort{0};

	/** Motivation: TX GPIO number wired to the peer board's RX pin, passed as a plain integer. */
	std::int32_t TxGpio{0};

	/** Motivation: RX GPIO number wired to the peer board's TX pin, passed as a plain integer. */
	std::int32_t RxGpio{0};

	/** Motivation: Baud rate shared with the peer board's UART configuration (a wire is fast, so 115200 by default). */
	std::uint32_t BaudRate{115200};

	/** Motivation: Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeId{0};
};

} // namespace MicroWorld::Platform::Esp32
