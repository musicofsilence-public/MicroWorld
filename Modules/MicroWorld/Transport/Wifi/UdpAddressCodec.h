#pragma once

// The 6-byte UDP address encoding lives here, in Transport, so both platform adapters
// (PlatformHost, PlatformEsp32) and both UDP devices share ONE definition rather
// than hand-copying it per package. It is pure arithmetic over ::MicroWorld::Transport::Address::FDeviceAddress with
// no OS includes, so the Core <- Transport dependency direction still holds.

#include <MicroWorld/Core/ByteCodecConstants.h>
#include <MicroWorld/Transport/DeviceAddress.h>

#include <cstdint>

namespace MicroWorld::Transport
{

/** Active byte count of a 6-byte UDP `::MicroWorld::Transport::Address::FDeviceAddress`: four IPv4 octets plus two port bytes. */
inline constexpr std::uint8_t UdpAddressByteCount = 6;

/** Index of the first IPv4 octet stored in a UDP `::MicroWorld::Transport::Address::FDeviceAddress`. */
inline constexpr std::uint8_t UdpAddressOctetAIndex = 0;

/** Index of the second IPv4 octet stored in a UDP `::MicroWorld::Transport::Address::FDeviceAddress`. */
inline constexpr std::uint8_t UdpAddressOctetBIndex = 1;

/** Index of the third IPv4 octet stored in a UDP `::MicroWorld::Transport::Address::FDeviceAddress`. */
inline constexpr std::uint8_t UdpAddressOctetCIndex = 2;

/** Index of the fourth IPv4 octet stored in a UDP `::MicroWorld::Transport::Address::FDeviceAddress`. */
inline constexpr std::uint8_t UdpAddressOctetDIndex = 3;

/** Index of the high (most-significant) byte of the network-order port in a UDP `::MicroWorld::Transport::Address::FDeviceAddress`. */
inline constexpr std::uint8_t UdpAddressPortHighByteIndex = 4;

/** Index of the low (least-significant) byte of the network-order port in a UDP `::MicroWorld::Transport::Address::FDeviceAddress`. */
inline constexpr std::uint8_t UdpAddressPortLowByteIndex = 5;

/** Shift placing the high IPv4 octet at the top of a host-order 32-bit address. */
inline constexpr std::uint32_t Ipv4OctetAShift = 24u;

/** Shift placing the second IPv4 octet within a host-order 32-bit address. */
inline constexpr std::uint32_t Ipv4OctetBShift = 16u;

/** Shift placing the third IPv4 octet within a host-order 32-bit address. */
inline constexpr std::uint32_t Ipv4OctetCShift = 8u;

/**
 * Encodes an IPv4 UDP endpoint into an opaque 6-byte `::MicroWorld::Transport::Address::FDeviceAddress`.
 *
 * Bytes 0-3 hold the four IPv4 octets in dotted order and bytes 4-5 hold the
 * port in network byte order (high byte first), so one address spans the UDP
 * encoding this package's device reads and writes. The encoding is owned here
 * because `::MicroWorld::Transport::Address::FDeviceAddress` ascribes no meaning to its bytes.
 *
 * @param InA First IPv4 octet.
 * @param InB Second IPv4 octet.
 * @param InC Third IPv4 octet.
 * @param InD Fourth IPv4 octet.
 * @param InPort UDP port in host byte order.
 * @return Six-byte address carrying the octets and the big-endian port.
 */
constexpr ::MicroWorld::Transport::Address::FDeviceAddress MakeUdpAddress(
	const std::uint8_t InA, const std::uint8_t InB, const std::uint8_t InC, const std::uint8_t InD, const std::uint16_t InPort) noexcept
{
	::MicroWorld::Transport::Address::FDeviceAddress Address{};
	Address.Bytes[UdpAddressOctetAIndex] = InA;
	Address.Bytes[UdpAddressOctetBIndex] = InB;
	Address.Bytes[UdpAddressOctetCIndex] = InC;
	Address.Bytes[UdpAddressOctetDIndex] = InD;
	Address.Bytes[UdpAddressPortHighByteIndex] = static_cast<std::uint8_t>(InPort >> HighByteShift);
	Address.Bytes[UdpAddressPortLowByteIndex] = static_cast<std::uint8_t>(InPort & LowByteMask);
	Address.Size = UdpAddressByteCount;
	return Address;
}

/**
 * Reports whether an address carries this package's 6-byte UDP encoding.
 *
 * Only the active length is inspected; the byte contents are validated when a
 * device actually routes the address, so a loopback or LoRa address is never
 * mistaken for a UDP one.
 *
 * @param InAddress Address whose encoding to test.
 * @return True when the active length is exactly six bytes.
 */
constexpr bool IsUdpAddress(const ::MicroWorld::Transport::Address::FDeviceAddress& InAddress) noexcept
{
	return InAddress.Size == UdpAddressByteCount;
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
constexpr std::uint16_t UdpAddressPort(const ::MicroWorld::Transport::Address::FDeviceAddress& InAddress) noexcept
{
	return static_cast<std::uint16_t>(
		(static_cast<std::uint16_t>(InAddress.Bytes[UdpAddressPortHighByteIndex]) << HighByteShift)
		| static_cast<std::uint16_t>(InAddress.Bytes[UdpAddressPortLowByteIndex]));
}

/**
 * Packs a host-order IPv4 address and port into the 6-byte UDP `::MicroWorld::Transport::Address::FDeviceAddress`.
 *
 * A device decodes a received datagram's sender with `ntohl`/`ntohs`, then hands
 * the host-order values here so the octet split lives in one place instead of
 * being hand-copied per platform. The four octets are taken most-significant
 * first to match `MakeUdpAddress`'s dotted order.
 *
 * @param InPackedIpv4Address IPv4 address in host byte order (octet A in the high byte).
 * @param InPort UDP port in host byte order.
 * @return Six-byte address carrying the octets and the big-endian port.
 */
constexpr ::MicroWorld::Transport::Address::FDeviceAddress MakeUdpAddressFromPackedHostOrder(
	const std::uint32_t InPackedIpv4Address, const std::uint16_t InPort) noexcept
{
	return MakeUdpAddress(
		static_cast<std::uint8_t>(InPackedIpv4Address >> Ipv4OctetAShift),
		static_cast<std::uint8_t>(InPackedIpv4Address >> Ipv4OctetBShift),
		static_cast<std::uint8_t>(InPackedIpv4Address >> Ipv4OctetCShift),
		static_cast<std::uint8_t>(InPackedIpv4Address),
		InPort);
}

} // namespace MicroWorld::Transport
