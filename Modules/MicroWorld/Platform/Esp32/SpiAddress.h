#pragma once

#include <MicroWorld/Transport/DeviceAddress.h>

#include <cstdint>

namespace MicroWorld
{

/**
 * Encodes an SPI node id into an opaque one-byte `FDeviceAddress`.
 *
 * The byte carries a frame node id: the local id stamped on an outgoing frame, or the SENDER's id read
 * from a received frame. It is NOT a bus address — SPI selects the device with the CS pin, so the link is
 * point-to-point and this address only stamps and reports identity, exactly as the UART, I2C, and LoRa
 * encodings do. The encoding is owned here because `FDeviceAddress` ascribes no meaning to its bytes and is
 * shared with the UDP, loopback, LoRa, UART, and I2C encodings.
 *
 * @param InNodeId Node id of the sender or recipient this address names.
 * @return One-byte address carrying the node id.
 */
constexpr FDeviceAddress MakeSpiAddress(const std::uint8_t InNodeId) noexcept
{
	FDeviceAddress Address{};
	Address.Bytes[0] = InNodeId;
	Address.Size = 1;
	return Address;
}

/**
 * Reports whether an address carries this package's one-byte SPI encoding.
 *
 * Only the active length is inspected, so a six-byte UDP address is never mistaken for an SPI one; the byte
 * value is validated when a device actually routes the address. The one-byte UART, I2C, LoRa, and loopback
 * encodings share this shape by design — a single device instance only ever handles addresses meant for its
 * own transport.
 *
 * @param InAddress Address whose encoding to test.
 * @return True when the active length is exactly one byte.
 */
constexpr bool IsSpiAddress(const FDeviceAddress& InAddress) noexcept
{
	return InAddress.Size == 1;
}

/**
 * Recomposes the SPI node id from an address's first byte.
 *
 * The wire is point-to-point, so this id is meaningful as the sender on a received frame or as the local
 * stamp on an outgoing frame; callers must first confirm `IsSpiAddress` to avoid reading unrelated bytes.
 *
 * @param InAddress Address whose first byte holds the node id.
 * @return Node id carried by the address.
 */
constexpr std::uint8_t SpiAddressNodeId(const FDeviceAddress& InAddress) noexcept
{
	return InAddress.Bytes[0];
}

} // namespace MicroWorld
