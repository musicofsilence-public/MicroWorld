#pragma once

#include "PeekStatus.h"
#include "SocketHandle.h"

#include <cstddef>
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

/**
 * Motivation: Carries one peek result plus the head datagram's length so the device sizes a receive in one probe.
 * Responsibilities: Hold the status and, on Ready, the true byte count of the queued datagram.
 * Example:
 *   const FPeekProbe Probe = ProbeReadableDatagram(Socket);
 *   if (Probe.BytesReady > Capacity) { return Full; }
 */
struct FPeekProbe
{
	/** Motivation: Classifies what the non-consuming peek observed. */
	EPeekStatus Status;

	/** Motivation: Valid only when Status == Ready; the true byte count of the queued datagram. */
	std::size_t BytesReady;
};

/**
 * Motivation: Sizes the peek scratch buffer so the sizing probe never overflows and never reads more than a send accepts.
 * Responsibilities: Mirror FHostWifiDevice::UdpMaxPacketBytes exactly (kept in sync by a static_assert in
 *   HostWifiDevice.cpp); the peek reads at most this many bytes, so it also bounds the largest accepted payload.
 */
constexpr std::size_t PeekScratchBytes = 1200;

/** Motivation: One past the peek scratch, so a datagram at least this large reports Full without being consumed. */
constexpr std::size_t OversizeDatagramSentinelBytes = PeekScratchBytes + 1;

/**
 * Motivation: Lets the device act on a peek error through one platform-free probe instead of raw error codes.
 * Responsibilities: Map a would-block error to the common "nothing queued yet" probe, and on Windows map WSAEMSGSIZE
 *   to a Ready probe carrying the oversize sentinel so the device's single fits-vs-Full decision sees one uniform
 *   "does not fit" signal; every other code is a hard error.
 */
inline FPeekProbe ClassifyPeekError(const int InErrorCode) noexcept
{
#ifdef _WIN32
	if (InErrorCode == WSAEWOULDBLOCK)
	{
		return FPeekProbe{EPeekStatus::WouldBlock, 0};
	}
	if (InErrorCode == WSAEMSGSIZE)
	{
		// Datagram exceeds even the scratch; signal "does not fit" for the device's single decision site.
		return FPeekProbe{EPeekStatus::Ready, OversizeDatagramSentinelBytes};
	}
	return FPeekProbe{EPeekStatus::Error, 0};
#else
	if (InErrorCode == EWOULDBLOCK || InErrorCode == EAGAIN)
	{
		return FPeekProbe{EPeekStatus::WouldBlock, 0};
	}
	return FPeekProbe{EPeekStatus::Error, 0};
#endif
}

/**
 * Motivation: Lets the device size the head datagram before committing a consuming read, without touching caller state.
 * Responsibilities: Peek the head datagram into an internal scratch buffer (never the caller's), return its true
 *   length via POSIX MSG_PEEK|MSG_TRUNC, and on Windows surface an oversize datagram as Ready carrying the oversize
 *   sentinel so the device's single fits-vs-Full decision sees one uniform "does not fit" signal; keep Full
 *   transactional on both platforms.
 */
inline FPeekProbe ProbeReadableDatagram(const FSocketHandle InSocket) noexcept
{
	std::uint8_t Scratch[PeekScratchBytes];
	sockaddr_storage Sender{};
	FSockLen SenderLen = sizeof(Sender);
#ifdef _WIN32
	const int Peeked = recvfrom(
		InSocket, reinterpret_cast<char*>(Scratch), static_cast<int>(PeekScratchBytes), MSG_PEEK, reinterpret_cast<sockaddr*>(&Sender), &SenderLen);
	if (Peeked == SOCKET_ERROR)
	{
		return ClassifyPeekError(WSAGetLastError());
	}
	return FPeekProbe{EPeekStatus::Ready, static_cast<std::size_t>(Peeked)};
#else
	const ssize_t Peeked = recvfrom(
		InSocket, reinterpret_cast<char*>(Scratch), PeekScratchBytes, MSG_PEEK | MSG_TRUNC, reinterpret_cast<sockaddr*>(&Sender), &SenderLen);
	if (Peeked < 0)
	{
		return ClassifyPeekError(errno);
	}
	return FPeekProbe{EPeekStatus::Ready, static_cast<std::size_t>(Peeked)};
#endif
}

} // namespace MicroWorld::Platform::Host
