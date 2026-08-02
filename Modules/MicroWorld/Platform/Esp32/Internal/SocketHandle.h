#pragma once

// =============================================================================
// These Esp32-platform internal headers are the ONLY units that pull lwIP socket
// headers. A public header must never reach them.
// Every lwIP/POSIX divergence (handle width, close, non-blocking mode, last-error
// classification, the MSG_PEEK-vs-MSG_TRUNC size probe) is hidden behind the
// helpers below so Esp32WifiDevice.cpp reads one platform-free receive/send path
// that mirrors the host device. This platform implementation is COMPILE-VERIFIED
// on ESP32-S3 (Phase 5.2) but the exact oversize-datagram receive behavior is
// UNVERIFIED at runtime: when lwIP exposes MSG_TRUNC the sizing peek returns the
// true datagram length, otherwise it returns the delivered length and an oversize
// datagram is sized only up to the 1200-byte scratch.
// =============================================================================

#include <MicroWorld/Core/Time.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <lwip/sockets.h>

namespace MicroWorld::Platform::Esp32
{

/** Motivation: Names the lwIP socket descriptor width so call sites stay free of the platform int width, with a negative value as its sentinel. */
using FSocketHandle = int;

/** Motivation: Names the address-length type expected by the lwIP sockaddr accessors. */
using FSockLen = socklen_t;

/**
 * Motivation: Stamps the open/closed state of one socket handle behind one predicate so callers never test the raw
 *   int against a Windows-style sentinel.
 * Responsibilities: Return true only when the handle names an open socket.
 */
inline bool IsValidHandle(const FSocketHandle InSocket) noexcept
{
	return InSocket >= 0;
}

/**
 * Motivation: Restores the lwIP socket type from the device's opaque stored form so the public header never
 *   carries std::uintptr_t.
 * Responsibilities: Reinterpret one opaque handle back to its lwIP socket type losslessly.
 */
inline FSocketHandle AsSocketHandle(const std::uintptr_t InStored) noexcept
{
	return static_cast<FSocketHandle>(InStored);
}

/**
 * Motivation: Stores the lwIP socket handle in an opaque form so the public header never reaches the platform type.
 * Responsibilities: Reinterpret one lwIP socket handle to its opaque stored form losslessly.
 */
inline std::uintptr_t AsOpaqueHandle(const FSocketHandle InSocket) noexcept
{
	return static_cast<std::uintptr_t>(InSocket);
}

/**
 * Motivation: Releases one lwIP socket descriptor behind a safe helper so the device destructor needs no validity branch.
 * Responsibilities: Close the socket and no-op on an invalid handle.
 */
inline void CloseSocket(const FSocketHandle InSocket) noexcept
{
	if (!IsValidHandle(InSocket))
	{
		return;
	}
	(void)close(InSocket);
}

/**
 * Motivation: Switches one socket to non-blocking mode so the device polls rather than blocks.
 * Responsibilities: Set O_NONBLOCK via fcntl and return false on any syscall failure so the constructor can roll back.
 */
inline bool SetNonBlocking(const FSocketHandle InSocket) noexcept
{
	const int Flags = fcntl(InSocket, F_GETFL, 0);
	if (Flags < 0)
	{
		return false;
	}
	return fcntl(InSocket, F_SETFL, Flags | O_NONBLOCK) == 0;
}

/**
 * Motivation: Builds a network-ready IPv4 socket address from plain octets and a host-order port so call sites need
 *   no byte-swap code.
 * Responsibilities: Pack the octets with htonl and the port with htons so the returned address is ready for bind or sendto.
 */
inline sockaddr_in MakeSockAddrIn(
	const std::uint8_t InA, const std::uint8_t InB, const std::uint8_t InC, const std::uint8_t InD, const std::uint16_t InPort) noexcept
{
	sockaddr_in Address{};
	Address.sin_family = AF_INET;
	const std::uint32_t PackedIpv4Address = (static_cast<std::uint32_t>(InA) << 24) | (static_cast<std::uint32_t>(InB) << 16)
		| (static_cast<std::uint32_t>(InC) << 8) | static_cast<std::uint32_t>(InD);
	Address.sin_addr.s_addr = htonl(PackedIpv4Address);
	Address.sin_port = htons(InPort);
	return Address;
}

/**
 * Motivation: Lets an ESP32 demo wait for inbound readiness deterministically without a sleep-poll loop.
 * Responsibilities: Use select() with a bounded timeout and return true only when the socket is readable within it.
 */
inline bool WaitForReadable(const FSocketHandle InSocket, const Core::DurationMilliseconds InTimeoutMilliseconds) noexcept
{
	fd_set ReadSet;
	FD_ZERO(&ReadSet);
	FD_SET(InSocket, &ReadSet);
	timeval Timeout{};
	Timeout.tv_sec = static_cast<long>(InTimeoutMilliseconds / 1000u);
	Timeout.tv_usec = static_cast<long>((InTimeoutMilliseconds % 1000u) * 1000u);
	// POSIX select() nfds is the highest descriptor number plus one.
	const int Ready = select(InSocket + 1, &ReadSet, nullptr, nullptr, &Timeout);
	return Ready > 0;
}

} // namespace MicroWorld::Platform::Esp32
