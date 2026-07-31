#pragma once

#include <MicroWorld/Transport/DeviceAddress.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Transport
{

/**
 * Leaves 58 payload bytes inside the E32's 64-byte transparent frame after MicroWorld's six-byte frame overhead.
 *
 * This bound is shared by every E32 adapter so a frame accepted by one platform can be decoded by another without
 * platform-specific capacity negotiation.
 */
constexpr std::size_t E32MaxPayloadBytes = 58;

/**
 * Encodes an E32 node id into an opaque one-byte `::MicroWorld::Transport::Address::FDeviceAddress`.
 *
 * The byte names the sender carried by a received MicroWorld frame. Transparent-mode E32 transmission is broadcast,
 * so a destination address is device-relative metadata rather than an on-air routing command.
 *
 * @param InNodeId Node id this address names.
 * @return One-byte address carrying the node id.
 */
constexpr ::MicroWorld::Transport::Address::FDeviceAddress MakeLoraAddress(const std::uint8_t InNodeId) noexcept
{
	::MicroWorld::Transport::Address::FDeviceAddress Address{};
	Address.Bytes[0] = InNodeId;
	Address.Size = 1;
	return Address;
}

/**
 * Reports whether an address has the one-byte shape used by E32 devices.
 *
 * This checks shape only. Another device may assign different meaning to a one-byte address, so callers interpret a
 * positive result within the active device's contract.
 *
 * @param InAddress Address whose active length to test.
 * @return True when the active length is exactly one byte.
 */
constexpr bool IsLoraAddress(const ::MicroWorld::Transport::Address::FDeviceAddress& InAddress) noexcept
{
	return InAddress.Size == 1;
}

/**
 * Reads the E32 node id from a previously validated one-byte address.
 *
 * Callers must first confirm `IsLoraAddress(InAddress)`; reading another address shape would interpret unrelated
 * storage as a node id.
 *
 * @param InAddress Validated one-byte E32 address.
 * @return Node id carried by the address.
 */
constexpr std::uint8_t LoraAddressNodeId(const ::MicroWorld::Transport::Address::FDeviceAddress& InAddress) noexcept
{
	return InAddress.Bytes[0];
}

} // namespace MicroWorld::Transport
