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

/** Motivation: Fixes the active byte count of a 6-byte UDP address (four IPv4 octets plus two port bytes). */
inline constexpr std::uint8_t UdpAddressByteCount = 6;

/** Motivation: Locates the first IPv4 octet stored in a UDP address. */
inline constexpr std::uint8_t UdpAddressOctetAIndex = 0;

/** Motivation: Locates the second IPv4 octet stored in a UDP address. */
inline constexpr std::uint8_t UdpAddressOctetBIndex = 1;

/** Motivation: Locates the third IPv4 octet stored in a UDP address. */
inline constexpr std::uint8_t UdpAddressOctetCIndex = 2;

/** Motivation: Locates the fourth IPv4 octet stored in a UDP address. */
inline constexpr std::uint8_t UdpAddressOctetDIndex = 3;

/** Motivation: Locates the high (most-significant) byte of the network-order port in a UDP address. */
inline constexpr std::uint8_t UdpAddressPortHighByteIndex = 4;

/** Motivation: Locates the low (least-significant) byte of the network-order port in a UDP address. */
inline constexpr std::uint8_t UdpAddressPortLowByteIndex = 5;

/** Motivation: Shifts the high IPv4 octet to the top of a host-order 32-bit address. */
inline constexpr std::uint32_t Ipv4OctetAShift = 24u;

/** Motivation: Shifts the second IPv4 octet within a host-order 32-bit address. */
inline constexpr std::uint32_t Ipv4OctetBShift = 16u;

/** Motivation: Shifts the third IPv4 octet within a host-order 32-bit address. */
inline constexpr std::uint32_t Ipv4OctetCShift = 8u;

/**
 * Motivation: Encodes an IPv4 UDP endpoint into the opaque address so platform adapters and UDP devices share one encoding.
 * Responsibilities: Store bytes 0-3 as the four IPv4 octets in dotted order and bytes 4-5 as the port in network byte order
 *   (high byte first), matching the encoding this package's device reads and writes.
 */
constexpr ::MicroWorld::Transport::Address::FDeviceAddress MakeUdpAddress(
	const std::uint8_t InA, const std::uint8_t InB, const std::uint8_t InC, const std::uint8_t InD, const std::uint16_t InPort) noexcept
{
	::MicroWorld::Transport::Address::FDeviceAddress Address{};
	Address.Bytes[UdpAddressOctetAIndex] = InA;
	Address.Bytes[UdpAddressOctetBIndex] = InB;
	Address.Bytes[UdpAddressOctetCIndex] = InC;
	Address.Bytes[UdpAddressOctetDIndex] = InD;
	Address.Bytes[UdpAddressPortHighByteIndex] = static_cast<std::uint8_t>(InPort >> Core::HighByteShift);
	Address.Bytes[UdpAddressPortLowByteIndex] = static_cast<std::uint8_t>(InPort & Core::LowByteMask);
	Address.Size = UdpAddressByteCount;
	return Address;
}

/**
 * Motivation: Guards UDP code against an address whose encoding it cannot interpret.
 * Responsibilities: Inspect the active length only and report whether it is exactly six bytes; byte contents are validated when
 *   a device routes the address, so a loopback or LoRa address is never mistaken for a UDP one.
 */
constexpr bool IsUdpAddress(const ::MicroWorld::Transport::Address::FDeviceAddress& InAddress) noexcept
{
	return InAddress.Size == UdpAddressByteCount;
}

/**
 * Motivation: Recomposes the UDP port from an address's big-endian bytes so callers avoid hand-copying the decode.
 * Responsibilities: Shift the high byte up before OR-ing the low byte, mirroring MakeUdpAddress's write order for an exact round
 *   trip; callers must first confirm IsUdpAddress to avoid reading unrelated bytes.
 */
constexpr std::uint16_t UdpAddressPort(const ::MicroWorld::Transport::Address::FDeviceAddress& InAddress) noexcept
{
	return static_cast<std::uint16_t>(
		(static_cast<std::uint16_t>(InAddress.Bytes[UdpAddressPortHighByteIndex]) << Core::HighByteShift)
		| static_cast<std::uint16_t>(InAddress.Bytes[UdpAddressPortLowByteIndex]));
}

/**
 * Motivation: Packs a host-order IPv4 address and port into the 6-byte UDP address so the octet split lives in one place.
 * Responsibilities: Take the four octets most-significant first (matching MakeUdpAddress's dotted order) after a device decodes a
 *   received datagram's sender with ntohl/ntohs.
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
