#pragma once

// =============================================================================
// src/Esp32SocketPlatformImplementation.h is the SOLE translation unit that pulls lwIP socket
// headers. It is included only by Esp32WifiDevice.cpp; a public header must
// never reach it. Every lwIP/POSIX divergence is hidden behind the helpers
// below so Esp32WifiDevice.cpp reads one platform-free receive/send path that
// mirrors the host device. This platform implementation is COMPILE-VERIFIED on ESP32-S3 (Phase 5.2)
// but the exact oversize-datagram receive behavior is UNVERIFIED at runtime:
// when lwIP exposes MSG_TRUNC the sizing peek returns the true datagram length,
// otherwise it returns the delivered length and an oversize datagram is sized
// only up to the 1200-byte scratch.
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
 *   no byte-swap boilerplate.
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
 * Motivation: Gives the device one vocabulary for a send attempt that is free of lwIP error codes.
 * Responsibilities: Distinguish accepted, would-block, and failed send outcomes.
 * Example:
 *   if (SendDatagram(Sock, Bytes, Len, To) == ESendOutcome::WouldBlock) { Retry(); }
 */
enum class ESendOutcome : std::uint8_t
{
	Success,	///< Motivation: The whole datagram was accepted.
	WouldBlock, ///< Motivation: The send would block because the socket buffer is full.
	Error,		///< Motivation: Any other socket error.
};

/**
 * Motivation: Sends one complete datagram through one lwIP sendto so the device maps the result without inspecting
 *   platform error codes.
 * Responsibilities: Hand the whole span to one sendto and classify whether it was fully accepted, would block, or failed.
 */
inline ESendOutcome SendDatagram(
	const FSocketHandle InSocket, const std::uint8_t* const InDatagramBytes, const std::size_t InLength, const sockaddr_in& InTo) noexcept
{
	const ssize_t Sent = sendto(InSocket, InDatagramBytes, InLength, 0, reinterpret_cast<const sockaddr*>(&InTo), sizeof(InTo));
	if (Sent >= 0 && static_cast<std::size_t>(Sent) == InLength)
	{
		return ESendOutcome::Success;
	}
	const int Error = errno;
	if (Error == EWOULDBLOCK || Error == EAGAIN)
	{
		return ESendOutcome::WouldBlock;
	}
	return ESendOutcome::Error;
}

/**
 * Motivation: Gives the device one vocabulary for a non-consuming peek that is free of lwIP error codes.
 * Responsibilities: Distinguish a queued datagram, a would-block, and a socket error.
 * Example:
 *   EPeekStatus S = ProbeReadableDatagram(Sock).Status;
 */
enum class EPeekStatus : std::uint8_t
{
	Ready,		///< Motivation: A datagram is queued; BytesReady carries its observed length.
	WouldBlock, ///< Motivation: No datagram is ready right now.
	Error,		///< Motivation: A socket error occurred.
};

/**
 * Motivation: Carries one peek result and, on Ready, the observed head datagram length so the device sizes a
 *   consuming read without inspecting platform codes.
 * Responsibilities: Hold the status and the observed byte count.
 * Example:
 *   FPeekProbe Probe = ProbeReadableDatagram(Sock);
 */
struct FPeekProbe
{
	/** Motivation: Classifies what the non-consuming peek observed. */
	EPeekStatus Status;

	/** Motivation: Valid only when Status == Ready; the observed byte count of the queued datagram. */
	std::size_t BytesReady;
};

/** Motivation: Bounds the sizing peek so it never overflows its scratch, mirroring FEsp32WifiDevice::UdpMaxPacketBytes (kept in sync by a
 * static_assert). */
constexpr std::size_t PeekScratchBytes = 1200;

/**
 * Motivation: Maps a peek-time socket error to a peek-probe outcome so the device never inspects platform codes.
 * Responsibilities: Treat a would-block error as the common nothing-queued case and every other code as a hard error.
 */
inline FPeekProbe ClassifyPeekError(const int InErrorCode) noexcept
{
	if (InErrorCode == EWOULDBLOCK || InErrorCode == EAGAIN)
	{
		return FPeekProbe{EPeekStatus::WouldBlock, 0};
	}
	return FPeekProbe{EPeekStatus::Error, 0};
}

/**
 * Motivation: Sizes the head datagram without consuming it, so a Full receive leaves the datagram queued and
 *   transactional.
 * Responsibilities: Peek into an internal scratch buffer (never the caller's destination); report the observed
 *   length via MSG_TRUNC when available, otherwise the delivered length capped at PeekScratchBytes.
 */
inline FPeekProbe ProbeReadableDatagram(const FSocketHandle InSocket) noexcept
{
	std::uint8_t Scratch[PeekScratchBytes];
	sockaddr_storage Sender{};
	FSockLen SenderLen = sizeof(Sender);
#ifdef MSG_TRUNC
	const ssize_t Peeked = recvfrom(InSocket, Scratch, PeekScratchBytes, MSG_PEEK | MSG_TRUNC, reinterpret_cast<sockaddr*>(&Sender), &SenderLen);
#else
	const ssize_t Peeked = recvfrom(InSocket, Scratch, PeekScratchBytes, MSG_PEEK, reinterpret_cast<sockaddr*>(&Sender), &SenderLen);
#endif
	if (Peeked < 0)
	{
		return ClassifyPeekError(errno);
	}
	return FPeekProbe{EPeekStatus::Ready, static_cast<std::size_t>(Peeked)};
}

/**
 * Motivation: Carries one consuming receive result and, on success, the received byte count so the device reports
 *   the count without inspecting platform codes.
 * Responsibilities: Hold the success flag and the byte count.
 * Example:
 *   FConsumeResult R = ConsumeDatagram(Sock, Dest, Cap, Sender);
 */
struct FConsumeResult
{
	/** Motivation: True when a datagram was consumed into the destination. */
	bool bSuccess;

	/** Motivation: Valid only when bSuccess; bytes written to the destination. */
	std::size_t BytesReceived;
};

/**
 * Motivation: Removes the previously-probed head datagram from the queue and copies it into the caller's buffer.
 * Responsibilities: Use a plain recvfrom so the datagram leaves the queue, fill OutSender only on success, and leave
 *   the destination untouched on failure.
 */
inline FConsumeResult ConsumeDatagram(
	const FSocketHandle InSocket, std::uint8_t* const OutDestination, const std::size_t InCapacity, sockaddr_in& OutSender) noexcept
{
	FSockLen SenderLen = sizeof(OutSender);
	const ssize_t Received = recvfrom(InSocket, OutDestination, InCapacity, 0, reinterpret_cast<sockaddr*>(&OutSender), &SenderLen);
	if (Received < 0)
	{
		return FConsumeResult{false, 0};
	}
	return FConsumeResult{true, static_cast<std::size_t>(Received)};
}

/**
 * Motivation: Reports whether opening and binding one non-blocking UDP socket succeeded, plus the actual bound port.
 * Responsibilities: Carry the handle, the open flag, and the host-order bound port.
 * Example:
 *   FOpenedSocket Opened = OpenBoundUdpSocket(Port);
 */
struct FOpenedSocket
{
	/** Motivation: lwIP socket handle; invalid when bOpen is false. */
	FSocketHandle Handle;

	/** Motivation: True when the socket was created, set non-blocking, and bound. */
	bool bOpen;

	/** Motivation: Host-order port the OS actually bound; valid only when bOpen. */
	std::uint16_t BoundPort;
};

/**
 * Motivation: Folds the close-then-report rollback shared by every post-open syscall failure so each failure site
 *   reads as one line.
 * Responsibilities: Close the partially opened socket and return a failed opened-socket descriptor.
 */
inline FOpenedSocket CloseAndReportFailure(const FSocketHandle InSocket) noexcept
{
	CloseSocket(InSocket);
	return FOpenedSocket{InSocket, false, 0};
}

/**
 * Motivation: Opens, binds, and sizes one non-blocking UDP socket on all IPv4 interfaces so the device can be
 *   constructed without throwing.
 * Responsibilities: On any syscall failure close the partially opened socket and return bOpen false; bind to
 *   INADDR_ANY so the adapter stays free of netif assumptions and read the actual port back via getsockname.
 */
inline FOpenedSocket OpenBoundUdpSocket(const std::uint16_t InBindPort) noexcept
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
	sockaddr_in Local{};
	Local.sin_family = AF_INET;
	Local.sin_addr.s_addr = htonl(INADDR_ANY);
	Local.sin_port = htons(InBindPort);
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
