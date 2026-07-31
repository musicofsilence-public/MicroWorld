#pragma once

// =============================================================================
// src/UdpSocketPlatformImplementation.h is the SOLE translation unit that pulls OS socket headers.
// It is included only by HostWifiDevice.cpp; a public header must never reach it.
// Every platform divergence (handle width, close, non-blocking mode, last-error
// classification, the MSG_PEEK-vs-MSG_TRUNC size probe) is hidden behind the
// helpers below so HostWifiDevice.cpp reads one platform-free receive/send path.
// The POSIX branch is compiled but NOT verified on this Windows-only host; it
// exists so Phase 5.2 (ESP32 UDP) can reuse the same interfaces under a POSIX build.
// =============================================================================

#include <MicroWorld/Core/Time.h>

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
 * Motivation: Gives SendDatagram one platform-free vocabulary for a non-blocking send so the device never inspects
 *   raw socket error codes.
 * Responsibilities: Distinguish a fully accepted datagram from a would-block buffer state and any other error.
 * Example:
 *   if (SendDatagram(Socket, Bytes, Len, To) == ESendOutcome::WouldBlock) { RetryLater(); }
 */
enum class ESendOutcome : std::uint8_t
{
	Success,	///< Motivation: The whole datagram was accepted.
	WouldBlock, ///< Motivation: The send would block because the socket buffer is full.
	Error,		///< Motivation: Any other socket error.
};

/**
 * Motivation: Lets the device send one datagram through a single call that hides the platform send API.
 * Responsibilities: Hand the whole span to one sendto and classify the outcome as fully accepted, would block, or
 *   failed, so the device can map it to the shared ETransportResult without inspecting platform error codes.
 */
inline ESendOutcome SendDatagram(
	const FSocketHandle InSocket, const std::uint8_t* const InDatagramBytes, const std::size_t InLength, const sockaddr_in& InTo) noexcept
{
	const int Sent =
#ifdef _WIN32
		sendto(
			InSocket,
			reinterpret_cast<const char*>(InDatagramBytes),
			static_cast<int>(InLength),
			0,
			reinterpret_cast<const sockaddr*>(&InTo),
			sizeof(InTo));
#else
		sendto(InSocket, reinterpret_cast<const char*>(InDatagramBytes), InLength, 0, reinterpret_cast<const sockaddr*>(&InTo), sizeof(InTo));
#endif
	if (Sent >= 0 && static_cast<std::size_t>(Sent) == InLength)
	{
		return ESendOutcome::Success;
	}
#ifdef _WIN32
	const int Error = WSAGetLastError();
	if (Error == WSAEWOULDBLOCK)
	{
		return ESendOutcome::WouldBlock;
	}
#else
	const int Error = errno;
	if (Error == EWOULDBLOCK || Error == EAGAIN)
	{
		return ESendOutcome::WouldBlock;
	}
#endif
	return ESendOutcome::Error;
}

/**
 * Motivation: Gives the sizing peek one platform-free vocabulary for what it observed at the head of the queue.
 * Responsibilities: Distinguish a queued datagram from an empty queue and a socket error.
 * Example:
 *   if (ProbeReadableDatagram(Socket).Status == EPeekStatus::Ready) { Consume(); }
 */
enum class EPeekStatus : std::uint8_t
{
	Ready,		///< Motivation: A datagram is queued; BytesReady carries its true length.
	WouldBlock, ///< Motivation: No datagram is ready right now.
	Error,		///< Motivation: A socket error occurred.
};

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

/**
 * Motivation: Carries one consuming receive result plus the byte count so the device reports both in one value.
 * Responsibilities: Hold the success flag and, on success, the bytes written to the destination.
 * Example:
 *   const FConsumeResult Result = ConsumeDatagram(Socket, Dest, Cap, Sender);
 *   if (Result.bSuccess) { Use(Result.BytesReceived); }
 */
struct FConsumeResult
{
	/** Motivation: True when a datagram was consumed into the destination. */
	bool bSuccess;

	/** Motivation: Valid only when bSuccess; bytes written to the destination. */
	std::size_t BytesReceived;
};

/**
 * Motivation: Lets the device take in exactly the datagram the peek already sized and approved.
 * Responsibilities: Consume the previously-probed head datagram with a plain recvfrom (flags zero) so it leaves the
 *   queue, fill OutSender only on success, and report failure without writing the destination.
 */
inline FConsumeResult ConsumeDatagram(
	const FSocketHandle InSocket, std::uint8_t* const OutDestination, const std::size_t InCapacity, sockaddr_in& OutSender) noexcept
{
	FSockLen SenderLen = sizeof(OutSender);
	const int Received =
#ifdef _WIN32
		recvfrom(
			InSocket, reinterpret_cast<char*>(OutDestination), static_cast<int>(InCapacity), 0, reinterpret_cast<sockaddr*>(&OutSender), &SenderLen);
#else
		recvfrom(InSocket, reinterpret_cast<char*>(OutDestination), InCapacity, 0, reinterpret_cast<sockaddr*>(&OutSender), &SenderLen);
#endif
	if (Received < 0)
	{
		return FConsumeResult{false, 0};
	}
	return FConsumeResult{true, static_cast<std::size_t>(Received)};
}

/**
 * Motivation: Carries the outcome of opening and binding one non-blocking loopback socket so the constructor branches on one value.
 * Responsibilities: Hold the socket handle, the open flag, and the host-order port the OS actually bound.
 * Example:
 *   const FOpenedSocket Opened = OpenBoundLoopbackUdpSocket(BindPort);
 *   if (!Opened.bOpen) { return; }
 */
struct FOpenedSocket
{
	/** Motivation: OS socket handle; invalid when bOpen is false. */
	FSocketHandle Handle;

	/** Motivation: True when the socket was created, set non-blocking, and bound. */
	bool bOpen;

	/** Motivation: Host-order port the OS actually bound; valid only when bOpen. */
	std::uint16_t BoundPort;
};

/**
 * Motivation: Folds the close-then-report rollback shared by every post-open syscall failure into one helper.
 * Responsibilities: Close the partially opened socket and return a descriptor that carries the now-closed handle with
 *   bOpen false.
 */
inline FOpenedSocket CloseAndReportFailure(const FSocketHandle InSocket) noexcept
{
	CloseSocket(InSocket);
	return FOpenedSocket{InSocket, false, 0};
}

/**
 * Motivation: Lets the constructor open a working socket from one bind port argument.
 * Responsibilities: Open, set non-blocking, and bind one UDP socket on the IPv4 loopback, read back the actual port
 *   via getsockname (an InBindPort of zero requests an ephemeral port), and on any syscall failure close the
 *   partially opened socket and return bOpen false so the device is left inert without throwing; binding to loopback
 *   keeps Windows firewall prompts out of the tests.
 */
inline FOpenedSocket OpenBoundLoopbackUdpSocket(const std::uint16_t InBindPort) noexcept
{
	const FSocketHandle Socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (!IsValidHandle(Socket))
	{
		return FOpenedSocket{Socket, false, 0};
	}
	if (!SetNonBlocking(Socket))
	{
		return CloseAndReportFailure(Socket);
	}
	sockaddr_in Local = MakeSockAddrIn(127, 0, 0, 1, InBindPort);
	if (bind(Socket, reinterpret_cast<const sockaddr*>(&Local), sizeof(Local)) != 0)
	{
		return CloseAndReportFailure(Socket);
	}
	sockaddr_in Bound{};
	FSockLen BoundLen = sizeof(Bound);
	if (getsockname(Socket, reinterpret_cast<sockaddr*>(&Bound), &BoundLen) != 0)
	{
		return CloseAndReportFailure(Socket);
	}
	return FOpenedSocket{Socket, true, ntohs(Bound.sin_port)};
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
