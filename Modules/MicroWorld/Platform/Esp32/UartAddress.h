#pragma once

#include <MicroWorld/Transport/DeviceAddress.h>

#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Encodes one UART node id into an opaque one-byte FDeviceAddress so callers route a point-to-point
 *   link by identity rather than a wire address (the UART never selects a destination on the wire); the encoding
 *   lives here because FDeviceAddress ascribes no meaning to its bytes and is shared across every transport.
 * Responsibilities: Stamp exactly one node id into the first byte and set the active length to one.
 */
constexpr Transport::Address::FDeviceAddress MakeUartAddress(const std::uint8_t InNodeId) noexcept
{
	Transport::Address::FDeviceAddress Address{};
	Address.Bytes[0] = InNodeId;
	Address.Size = 1;
	return Address;
}

/**
 * Motivation: Lets a device reject a non-UART encoding before routing it, so a multi-transport caller cannot
 *   hand a UDP or wrong-length address to a point-to-point UART device.
 * Responsibilities: Inspect only the active length and return true when it is exactly one byte.
 */
constexpr bool IsUartAddress(const Transport::Address::FDeviceAddress& InAddress) noexcept
{
	return InAddress.Size == 1;
}

/**
 * Motivation: Recovers the UART node id a one-byte address carries so a received frame can report its sender
 *   identity back to the caller.
 * Responsibilities: Return the first byte; callers must confirm IsUartAddress first to avoid reading unrelated bytes.
 */
constexpr std::uint8_t UartAddressNodeId(const Transport::Address::FDeviceAddress& InAddress) noexcept
{
	return InAddress.Bytes[0];
}

} // namespace MicroWorld::Platform::Esp32
