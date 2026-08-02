#pragma once

#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Carries the plain-integer bus parameters one wired I2C slave needs at construction so the public
 *   header stays free of ESP-IDF enum types.
 * Responsibilities: Hold port, SDA/SCL GPIO, this board's own 7-bit slave address, and local node id as plain integers.
 * Example:
 *   FEsp32I2cSlaveConfig Config;
 *   Config.SlaveAddress = 0x29;
 */
struct FEsp32I2cSlaveConfig
{
	/** Motivation: I2C port number (ESP-IDF i2c_port_num_t, e.g. I2C_NUM_0) passed as a plain integer. */
	std::int32_t I2cPort{0};

	/** Motivation: SDA GPIO number shared with the master's SDA pin, passed as a plain integer. */
	std::int32_t SdaGpio{0};

	/** Motivation: SCL GPIO number shared with the master's SCL pin, passed as a plain integer. */
	std::int32_t SclGpio{0};

	/** Motivation: This board's own 7-bit bus address, the address the master clocks. */
	std::uint8_t SlaveAddress{0x28};

	/** Motivation: Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeId{0};
};

} // namespace MicroWorld::Platform::Esp32
