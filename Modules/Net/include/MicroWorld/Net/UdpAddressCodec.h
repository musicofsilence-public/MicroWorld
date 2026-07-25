#pragma once

// The 6-byte UDP address encoding lives here, in Net, so both platform adapters
// (PlatformHost, PlatformEsp32) and both UDP drivers share ONE definition rather
// than hand-copying it per package. It is pure arithmetic over FNetAddress with
// no OS includes, so the Core <- Memory <- Net dependency direction still holds.

#include <MicroWorld/Net/NetAddress.h>

#include <cstdint>

namespace MicroWorld
{

/**
 * Encodes an IPv4 UDP endpoint into an opaque 6-byte `FNetAddress`.
 *
 * Bytes 0-3 hold the four IPv4 octets in dotted order and bytes 4-5 hold the
 * port in network byte order (high byte first), so one address spans the UDP
 * encoding this package's driver reads and writes. The encoding is owned here
 * because `FNetAddress` ascribes no meaning to its bytes.
 *
 * @param InA First IPv4 octet.
 * @param InB Second IPv4 octet.
 * @param InC Third IPv4 octet.
 * @param InD Fourth IPv4 octet.
 * @param InPort UDP port in host byte order.
 * @return Six-byte address carrying the octets and the big-endian port.
 */
constexpr FNetAddress MakeUdpAddress(
	const std::uint8_t InA, const std::uint8_t InB, const std::uint8_t InC, const std::uint8_t InD, const std::uint16_t InPort) noexcept
{
	FNetAddress Address{};
	Address.Bytes[0] = InA;
	Address.Bytes[1] = InB;
	Address.Bytes[2] = InC;
	Address.Bytes[3] = InD;
	Address.Bytes[4] = static_cast<std::uint8_t>(InPort >> 8);
	Address.Bytes[5] = static_cast<std::uint8_t>(InPort & 0xFF);
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
 * @param InAddress Address whose encoding to test.
 * @return True when the active length is exactly six bytes.
 */
constexpr bool IsUdpAddress(const FNetAddress& InAddress) noexcept
{
	return InAddress.Size == 6;
}

/**
 * Recomposes the UDP port from an address's big-endian bytes 4-5.
 *
 * The high byte is shifted up before the low byte is OR-ed in, mirroring the
 * `MakeUdpAddress` write order so the round trip is exact. Callers must first
 * confirm `IsUdpAddress` to avoid reading unrelated bytes.
 *
 * @param InAddress Address whose bytes 4-5 hold a network-order port.
 * @return Port in host byte order.
 */
constexpr std::uint16_t UdpAddressPort(const FNetAddress& InAddress) noexcept
{
	return static_cast<std::uint16_t>((static_cast<std::uint16_t>(InAddress.Bytes[4]) << 8) | static_cast<std::uint16_t>(InAddress.Bytes[5]));
}

/**
 * Packs a host-order IPv4 address and port into the 6-byte UDP `FNetAddress`.
 *
 * A driver decodes a received datagram's sender with `ntohl`/`ntohs`, then hands
 * the host-order values here so the octet split lives in one place instead of
 * being hand-copied per platform. The four octets are taken most-significant
 * first to match `MakeUdpAddress`'s dotted order.
 *
 * @param InPackedIpv4Address IPv4 address in host byte order (octet A in the high byte).
 * @param InPort UDP port in host byte order.
 * @return Six-byte address carrying the octets and the big-endian port.
 */
constexpr FNetAddress MakeUdpAddressFromPackedHostOrder(const std::uint32_t InPackedIpv4Address, const std::uint16_t InPort) noexcept
{
	return MakeUdpAddress(
		static_cast<std::uint8_t>(InPackedIpv4Address >> 24),
		static_cast<std::uint8_t>(InPackedIpv4Address >> 16),
		static_cast<std::uint8_t>(InPackedIpv4Address >> 8),
		static_cast<std::uint8_t>(InPackedIpv4Address),
		InPort);
}

} // namespace MicroWorld
