#pragma once

#include <MicroWorld/Transport/DeviceAddress.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Transport
{

/**
 * Motivation: Fixes the largest payload an E32 adapter accepts so every platform shares one capacity bound and a frame one
 *   accepts another decodes.
 * Responsibilities: Leave 58 payload bytes inside the E32's 64-byte transparent frame after MicroWorld's six-byte frame overhead.
 */
constexpr std::size_t E32MaxPayloadBytes = 58;

/**
 * Motivation: Encodes an E32 node id into an opaque one-byte address so a LoRa frame can carry its sender without on-air routing.
 * Responsibilities: Stamp the node id into the first byte and set the active length to one; transparent-mode E32 transmission
 *   is broadcast, so the destination address is device-relative metadata rather than an on-air command.
 */
constexpr ::MicroWorld::Transport::Address::FDeviceAddress MakeLoraAddress(const std::uint8_t InNodeId) noexcept
{
	::MicroWorld::Transport::Address::FDeviceAddress Address{};
	Address.Bytes[0] = InNodeId;
	Address.Size = 1;
	return Address;
}

/**
 * Motivation: Guards E32 code against an address whose shape it cannot interpret.
 * Responsibilities: Check shape only and report whether the active length is exactly one byte; another device may assign a
 *   different meaning to a one-byte address, so callers interpret a positive result within the active device's contract.
 */
constexpr bool IsLoraAddress(const ::MicroWorld::Transport::Address::FDeviceAddress& InAddress) noexcept
{
	return InAddress.Size == 1;
}

/**
 * Motivation: Reads the E32 node id from a one-byte address after shape validation.
 * Responsibilities: Return the first byte of a previously validated address; callers must confirm IsLoraAddress first, since
 *   reading another address shape would interpret unrelated storage as a node id.
 */
constexpr std::uint8_t LoraAddressNodeId(const ::MicroWorld::Transport::Address::FDeviceAddress& InAddress) noexcept
{
	return InAddress.Bytes[0];
}

} // namespace MicroWorld::Transport
