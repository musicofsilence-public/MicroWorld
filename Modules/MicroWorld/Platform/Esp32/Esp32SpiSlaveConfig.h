#pragma once

#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Carries the plain-integer bus parameters one wired SPI slave needs at construction so the public
 *   header stays free of the ESP-IDF SPI enum types; the slave is clocked by the master so it carries no frequency.
 * Responsibilities: Hold SPI host, MOSI/MISO/SCLK/CS GPIO, and local node id as plain integers.
 * Example:
 *   FEsp32SpiSlaveConfig Config;
 *   Config.SpiHost = 1;
 */
struct FEsp32SpiSlaveConfig
{
	/** Motivation: SPI host number (ESP-IDF spi_host_device_t, e.g. SPI2_HOST == 1) passed as a plain integer. */
	std::int32_t SpiHost{1};

	/** Motivation: MOSI GPIO number shared with the master's MOSI pin, passed as a plain integer. */
	std::int32_t MosiGpio{0};

	/** Motivation: MISO GPIO number shared with the master's MISO pin, passed as a plain integer. */
	std::int32_t MisoGpio{0};

	/** Motivation: SCLK GPIO number shared with the master's SCLK pin, passed as a plain integer. */
	std::int32_t SclkGpio{0};

	/** Motivation: CS GPIO number shared with the master's CS pin, passed as a plain integer. */
	std::int32_t CsGpio{0};

	/** Motivation: Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeId{0};
};

} // namespace MicroWorld::Platform::Esp32
