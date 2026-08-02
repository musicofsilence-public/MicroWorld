#pragma once

// =============================================================================
// These Host-platform internal headers are the ONLY units that pull OS socket headers.
// A public header must never reach them.
// Every platform divergence (handle width, close, non-blocking mode, last-error
// classification, the MSG_PEEK-vs-MSG_TRUNC size probe) is hidden behind the
// helpers below so HostWifiDevice.cpp reads one platform-free receive/send path.
// The POSIX branch is compiled but NOT verified on this Windows-only host; it
// exists so Phase 5.2 (ESP32 UDP) can reuse the same interfaces under a POSIX build.
// =============================================================================

#include <MicroWorld/Core/Time.h>

#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
// ws2_32 is linked via CMake (target_link_libraries ... ws2_32); the MSVC-only
// `#pragma comment(lib, ...)` is omitted so -Werror=unknown-pragmas stays clean.
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace MicroWorld::Platform::Host
{

#ifdef _WIN32
/** Motivation: Windows socket descriptor width, whose sentinel is INVALID_SOCKET. */
using FSocketHandle = SOCKET;
#else
/** Motivation: POSIX socket descriptor width, whose sentinel is a negative value. */
using FSocketHandle = int;
#endif

#ifdef _WIN32
/** Motivation: Address-length type expected by the Windows sockaddr accessors. */
using FSockLen = int;
#else
/** Motivation: Address-length type expected by the POSIX sockaddr accessors. */
using FSockLen = socklen_t;
#endif

/**
 * Motivation: Lets every helper test socket validity without knowing each platform's sentinel.
 * Responsibilities: Report true when the handle names an open socket, treating INVALID_SOCKET on Windows and a
 *   negative int on POSIX as invalid so callers never test the raw value across platforms.
 */
inline bool IsValidHandle(const FSocketHandle InSocket) noexcept
{
#ifdef _WIN32
	return InSocket != INVALID_SOCKET;
#else
	return InSocket >= 0;
#endif
}

/**
 * Motivation: Lets the device erase its socket type behind std::uintptr_t in the public header.
 * Responsibilities: Convert the opaque stored handle back to its OS socket type losslessly, since std::uintptr_t
 *   is the same width as SOCKET or int on every supported host.
 */
inline FSocketHandle AsSocketHandle(const std::uintptr_t InStored) noexcept
{
	return static_cast<FSocketHandle>(InStored);
}

/**
 * Motivation: Lets the device store its socket handle without leaking an OS type into the public header.
 * Responsibilities: Convert the OS socket handle to the device's opaque stored form losslessly.
 */
inline std::uintptr_t AsOpaqueHandle(const FSocketHandle InSocket) noexcept
{
	return static_cast<std::uintptr_t>(InSocket);
}

/**
 * Motivation: Lets the device release a socket through one call regardless of platform close API.
 * Responsibilities: Close the socket via closesocket on Windows or close on POSIX, and do nothing on an invalid
 *   handle so the destructor needs no validity branch of its own.
 */
inline void CloseSocket(const FSocketHandle InSocket) noexcept
{
	if (!IsValidHandle(InSocket))
	{
		return;
	}
#ifdef _WIN32
	(void)closesocket(InSocket);
#else
	(void)close(InSocket);
#endif
}

/**
 * Motivation: Lets the constructor make its socket non-blocking through one platform-free call.
 * Responsibilities: Set FIONBIO via ioctlsocket on Windows or O_NONBLOCK via fcntl on POSIX, and return false on
 *   any syscall failure so the constructor can roll back cleanly.
 */
inline bool SetNonBlocking(const FSocketHandle InSocket) noexcept
{
#ifdef _WIN32
	u_long Mode = 1;
	return ioctlsocket(InSocket, FIONBIO, &Mode) == 0;
#else
	int Flags = fcntl(InSocket, F_GETFL, 0);
	if (Flags < 0)
	{
		return false;
	}
	return fcntl(InSocket, F_SETFL, Flags | O_NONBLOCK) == 0;
#endif
}

/**
 * Motivation: Lets the device build a bind- or sendto-ready IPv4 address from octets and a host-order port.
 * Responsibilities: Pack the four octets and convert them with htonl and the port with htons, returning a sockaddr_in
 *   that needs no further byte swapping.
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
 * Motivation: Lets a host test or demo wait for inbound data deterministically without a sleep-poll loop.
 * Responsibilities: Use select() with a bounded timeout and return true only when a datagram is ready to consume;
 *   return false when the timeout elapses.
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
	const int Ready = select(static_cast<int>(InSocket + 1), &ReadSet, nullptr, nullptr, &Timeout);
	return Ready > 0;
}

} // namespace MicroWorld::Platform::Host
