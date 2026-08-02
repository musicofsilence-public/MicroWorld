#pragma once

#include <MicroWorld/Core/IO/DeviceAddress.h>

#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Encodes one I2C node id into an opaque one-byte FDeviceAddress so callers route a point-to-point
 *   link by identity without leaking the 7-bit bus address the hardware selects with; the encoding lives here
 *   because FDeviceAddress ascribes no meaning to its bytes and is shared across every transport.
 * Responsibilities: Stamp exactly one node id into the first byte and set the active length to one.
 */
constexpr Core::FDeviceAddress MakeI2cAddress(const std::uint8_t InNodeId) noexcept
{
	Core::FDeviceAddress Address{};
	Address.Bytes[0] = InNodeId;
	Address.Size = 1;
	return Address;
}

/**
 * Motivation: Lets a device reject a non-I2C encoding before routing it, so a multi-transport caller cannot
 *   hand a UDP or wrong-length address to a point-to-point I2C device.
 * Responsibilities: Inspect only the active length and return true when it is exactly one byte.
 */
constexpr bool IsI2cAddress(const Core::FDeviceAddress& InAddress) noexcept
{
	return InAddress.Size == 1;
}

/**
 * Motivation: Recovers the I2C node id a one-byte address carries so a received frame can report its sender
 *   identity back to the caller.
 * Responsibilities: Return the first byte; callers must confirm IsI2cAddress first to avoid reading unrelated bytes.
 */
constexpr std::uint8_t I2cAddressNodeId(const Core::FDeviceAddress& InAddress) noexcept
{
	return InAddress.Bytes[0];
}

} // namespace MicroWorld::Platform::Esp32
