#pragma once

#include <MicroWorld/Net/NetAddress.h>

#include <cstdint>

namespace MicroWorld
{

/**
 * Encodes a LoRa node id into an opaque one-byte `FNetAddress`.
 *
 * The byte carries the SENDER's node id as read from a received frame; LoRa is a broadcast
 * medium, so the address never selects a destination on the wire and per-destination routing
 * is a higher-layer concern. The encoding is owned here because `FNetAddress` ascribes no
 * meaning to its bytes and is shared with the UDP and loopback encodings.
 *
 * @param NodeId Node id of the sender or recipient this address names.
 * @return One-byte address carrying the node id.
 */
constexpr FNetAddress MakeLoraAddress(const std::uint8_t NodeId) noexcept
{
	FNetAddress Address{};
	Address.Bytes[0] = NodeId;
	Address.Size = 1;
	return Address;
}

/**
 * Reports whether an address carries this package's one-byte LoRa encoding.
 *
 * Only the active length is inspected, so a six-byte UDP or one-byte loopback address is never
 * mistaken for a LoRa one; the byte value is validated when a driver actually routes the address.
 *
 * @param Address Address whose encoding to test.
 * @return True when the active length is exactly one byte.
 */
constexpr bool IsLoraAddress(const FNetAddress& Address) noexcept
{
	return Address.Size == 1;
}

/**
 * Recomposes the LoRa node id from an address's first byte.
 *
 * The wire is broadcast, so this id is meaningful as the sender on a received frame or as the
 * local stamp on an outgoing frame; callers must first confirm `IsLoraAddress` to avoid reading
 * unrelated bytes.
 *
 * @param Address Address whose first byte holds the node id.
 * @return Node id carried by the address.
 */
constexpr std::uint8_t LoraAddressNodeId(const FNetAddress& Address) noexcept
{
	return Address.Bytes[0];
}

} // namespace MicroWorld
