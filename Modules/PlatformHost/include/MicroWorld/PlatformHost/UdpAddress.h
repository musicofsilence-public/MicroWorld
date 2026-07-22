#pragma once

#include <MicroWorld/Net/NetAddress.h>

#include <cstdint>

namespace MicroWorld
{

/**
 * Encodes an IPv4 loopback endpoint into an opaque 6-byte `FNetAddress`.
 *
 * Bytes 0-3 hold the four IPv4 octets in dotted order and bytes 4-5 hold the
 * port in network byte order (high byte first), so one address spans the UDP
 * encoding this package's driver reads and writes. The encoding is owned here
 * because `FNetAddress` ascribes no meaning to its bytes.
 *
 * @param A First IPv4 octet.
 * @param B Second IPv4 octet.
 * @param C Third IPv4 octet.
 * @param D Fourth IPv4 octet.
 * @param Port UDP port in host byte order.
 * @return Six-byte address carrying the octets and the big-endian port.
 */
constexpr FNetAddress MakeUdpAddress(
	const std::uint8_t A, const std::uint8_t B, const std::uint8_t C, const std::uint8_t D, const std::uint16_t Port) noexcept
{
	FNetAddress Address{};
	Address.Bytes[0] = A;
	Address.Bytes[1] = B;
	Address.Bytes[2] = C;
	Address.Bytes[3] = D;
	Address.Bytes[4] = static_cast<std::uint8_t>(Port >> 8);
	Address.Bytes[5] = static_cast<std::uint8_t>(Port & 0xFF);
	Address.Size = 6;
	return Address;
}

/**
 * Reports whether an address carries this package's 6-byte UDP encoding.
 *
 * Only the active length is inspected; the byte contents are validated when a
 * driver actually routes the address, so a loopback or LoRa address is never
 * mistaken for a UDP one.
 *
 * @param Address Address whose encoding to test.
 * @return True when the active length is exactly six bytes.
 */
constexpr bool IsUdpAddress(const FNetAddress& Address) noexcept
{
	return Address.Size == 6;
}

/**
 * Recomposes the UDP port from an address's big-endian bytes 4-5.
 *
 * The high byte is shifted up before the low byte is OR-ed in, mirroring the
 * `MakeUdpAddress` write order so the round trip is exact. Callers must first
 * confirm `IsUdpAddress` to avoid reading unrelated bytes.
 *
 * @param Address Address whose bytes 4-5 hold a network-order port.
 * @return Port in host byte order.
 */
constexpr std::uint16_t UdpAddressPort(const FNetAddress& Address) noexcept
{
	return static_cast<std::uint16_t>((static_cast<std::uint16_t>(Address.Bytes[4]) << 8) | static_cast<std::uint16_t>(Address.Bytes[5]));
}

} // namespace MicroWorld
