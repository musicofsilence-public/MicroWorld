#pragma once

#include <MicroWorld/Net/NetAddress.h>

#include <cstdint>

namespace MicroWorld
{

/**
 * Encodes a UART node id into an opaque one-byte `FNetAddress`.
 *
 * The byte carries a node id: the local id stamped on an outgoing frame, or the SENDER's id read
 * from a received frame. The UART link is point-to-point, so the address never selects a
 * destination on the wire — it only stamps and reports identity, exactly as the LoRa encoding does.
 * The encoding is owned here because `FNetAddress` ascribes no meaning to its bytes and is shared
 * with the UDP, loopback, and LoRa encodings.
 *
 * @param NodeId Node id of the sender or recipient this address names.
 * @return One-byte address carrying the node id.
 */
constexpr FNetAddress MakeUartAddress(const std::uint8_t NodeId) noexcept
{
	FNetAddress Address{};
	Address.Bytes[0] = NodeId;
	Address.Size = 1;
	return Address;
}

/**
 * Reports whether an address carries this package's one-byte UART encoding.
 *
 * Only the active length is inspected, so a six-byte UDP address is never mistaken for a UART one;
 * the byte value is validated when a driver actually routes the address. The one-byte LoRa and
 * loopback encodings share this shape by design — a single driver instance only ever handles
 * addresses meant for its own transport.
 *
 * @param Address Address whose encoding to test.
 * @return True when the active length is exactly one byte.
 */
constexpr bool IsUartAddress(const FNetAddress& Address) noexcept
{
	return Address.Size == 1;
}

/**
 * Recomposes the UART node id from an address's first byte.
 *
 * The wire is point-to-point, so this id is meaningful as the sender on a received frame or as the
 * local stamp on an outgoing frame; callers must first confirm `IsUartAddress` to avoid reading
 * unrelated bytes.
 *
 * @param Address Address whose first byte holds the node id.
 * @return Node id carried by the address.
 */
constexpr std::uint8_t UartAddressNodeId(const FNetAddress& Address) noexcept
{
	return Address.Bytes[0];
}

} // namespace MicroWorld
