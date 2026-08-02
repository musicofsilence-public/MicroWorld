#pragma once

#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Carries the plain-integer UART parameters one ESP-IDF byte stream needs at open time so this Detail
 *   type stays free of ESP-IDF enum types and RadioE32 depends only on Core's IUartByteStream interface.
 * Responsibilities: Hold UART port, TX/RX GPIO, and baud rate as plain integers.
 * Example:
 *   FEsp32UartByteStreamConfig Config;
 *   Config.BaudRate = 9600;
 */
struct FEsp32UartByteStreamConfig
{
	/** Motivation: ESP-IDF UART port number stored as a plain integer outside private implementation code. */
	std::int32_t UartPort{0};

	/** Motivation: GPIO wired from the UART TX signal to the attached device RX pin. */
	std::int32_t TxGpio{0};

	/** Motivation: GPIO wired from the attached device TX pin to the UART RX signal. */
	std::int32_t RxGpio{0};

	/** Motivation: Baud rate shared with the attached device; zero lets the platform open attempt reject configuration. */
	std::uint32_t BaudRate{0};
};

} // namespace MicroWorld::Platform::Esp32
