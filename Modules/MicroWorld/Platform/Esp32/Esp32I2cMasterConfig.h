#pragma once

#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Carries the plain-integer bus parameters one wired I2C master needs at construction so the public
 *   header stays free of ESP-IDF enum types.
 * Responsibilities: Hold port, SDA/SCL GPIO, SCL speed, peer slave address, and local node id as plain integers.
 * Example:
 *   FEsp32I2cMasterConfig Config;
 *   Config.SlaveAddress = 0x28;
 */
struct FEsp32I2cMasterConfig
{
	/** Motivation: I2C port number (ESP-IDF i2c_port_num_t, e.g. I2C_NUM_0) passed as a plain integer. */
	std::int32_t I2cPort{0};

	/** Motivation: SDA GPIO number shared with the slave's SDA pin, passed as a plain integer. */
	std::int32_t SdaGpio{0};

	/** Motivation: SCL GPIO number shared with the slave's SCL pin, passed as a plain integer. */
	std::int32_t SclGpio{0};

	/** Motivation: SCL clock frequency in hertz (100 kHz standard mode is reliable over short jumper wires). */
	std::uint32_t SclSpeedHz{100000};

	/** Motivation: 7-bit bus address of the peer slave this master addresses. */
	std::uint8_t SlaveAddress{0x28};

	/** Motivation: Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeId{0};
};

} // namespace MicroWorld::Platform::Esp32
