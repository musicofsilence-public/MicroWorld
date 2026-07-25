#pragma once

// =============================================================================
// src/Esp32SocketPlatformImplementation.h is the SOLE translation unit that pulls lwIP socket
// headers. It is included only by Esp32UdpDriver.cpp; a public header must
// never reach it. Every lwIP/POSIX divergence is hidden behind the helpers
// below so Esp32UdpDriver.cpp reads one platform-free receive/send path that
// mirrors the host driver. This platform implementation is COMPILE-VERIFIED on ESP32-S3 (Phase 5.2)
// but the exact oversize-datagram receive behavior is UNVERIFIED at runtime:
// when lwIP exposes MSG_TRUNC the sizing peek returns the true datagram length,
// otherwise it returns the delivered length and an oversize datagram is sized
// only up to the 1200-byte scratch.
// =============================================================================

#include <MicroWorld/Time.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <lwip/sockets.h>

namespace MicroWorld::Detail
{

/** lwIP socket descriptor width; a negative value is its sentinel. */
using FSocketHandle = int;

/** Address-length type expected by the lwIP `sockaddr` accessors. */
using FSockLen = socklen_t;

/**
 * Stamps the open/closed state of one socket handle.
 *
 * lwIP uses a negative `int` for its invalid descriptor, so callers must not
 * test the raw value against a Windows-style sentinel.
 *
 * @param InSocket Handle whose validity is in question.
 * @return True when the handle names an open socket.
 */
inline bool IsValidHandle(const FSocketHandle InSocket) noexcept
{
	return InSocket >= 0;
}

/**
 * Converts an opaque stored handle back to its lwIP socket type.
 *
 * `std::uintptr_t` is at least as wide as `int`, so the round trip is lossless
 * and keeps `std::uintptr_t` out of the public header.
 *
 * @param InStored Opaque handle value saved by the driver.
 * @return lwIP socket handle.
 */
inline FSocketHandle AsSocketHandle(const std::uintptr_t InStored) noexcept
{
	return static_cast<FSocketHandle>(InStored);
}

/**
 * Converts an lwIP socket handle to the driver's opaque stored form.
 *
 * @param InSocket lwIP socket handle.
 * @return Opaque handle value the driver stores.
 */
inline std::uintptr_t AsOpaqueHandle(const FSocketHandle InSocket) noexcept
{
	return static_cast<std::uintptr_t>(InSocket);
}

/**
 * Releases one lwIP socket descriptor.
 *
 * A no-op on an invalid handle so the driver's destructor does not need its own
 * validity branch.
 *
 * @param InSocket Handle to release.
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
 * Switches one socket to non-blocking mode.
 *
 * Sets `O_NONBLOCK` via `fcntl`; returns false on any syscall failure so the
 * constructor can roll back cleanly.
 *
 * @param InSocket Handle whose mode to change.
 * @return True when the socket is now non-blocking.
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
 * Builds an IPv4 `sockaddr_in` from dotted octets and a host-order port.
 *
 * The octets are packed and converted with `htonl`; the port with `htons`, so the
 * returned address is ready for `bind` or `sendto` without further byte swapping.
 *
 * @param InA First IPv4 octet.
 * @param InB Second IPv4 octet.
 * @param InC Third IPv4 octet.
 * @param InD Fourth IPv4 octet.
 * @param InPort Host-order UDP port.
 * @return Network-ready IPv4 socket address.
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

/** Normalized result of one non-blocking send attempt. */
enum class ESendOutcome : std::uint8_t
{
	/** The whole datagram was accepted. */
	Success,
	/** The send would block because the socket buffer is full. */
	WouldBlock,
	/** Any other socket error. */
	Error,
};

/**
 * Sends one complete datagram to a network-ready IPv4 address.
 *
 * The whole span is handed to one `sendto`; the outcome classifies only whether
 * it was fully accepted, would block, or failed, so the driver can map it to the
 * shared `ENetResult` without inspecting lwIP error codes.
 *
 * @param InSocket Open non-blocking socket.
 * @param InDatagramBytes First byte of the datagram to send.
 * @param InLength Number of bytes to send.
 * @param InTo Network-ready destination address.
 * @return Normalized outcome of the single send attempt.
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

/** Normalized result of a non-consuming peek at the head datagram. */
enum class EPeekStatus : std::uint8_t
{
	/** A datagram is queued; `BytesReady` carries its observed length. */
	Ready,
	/** No datagram is ready right now. */
	WouldBlock,
	/** A socket error occurred. */
	Error,
};

/**
 * Carries one peek result plus, on `Ready`, the head datagram's observed length.
 */
struct FPeekProbe
{
	/** Classifies what the non-consuming peek observed. */
	EPeekStatus Status;

	/** Valid only when `Status == Ready`; the observed byte count of the queued datagram. */
	std::size_t BytesReady;
};

/**
 * Largest datagram the sizing peek can observe without an overflow error.
 *
 * Mirrors `FEsp32UdpDriver::UdpMaxPacketBytes` (kept in sync by a `static_assert`
 * in `Esp32UdpDriver.cpp`); the sizing peek never reads more than this, so it is
 * also the largest payload one send accepts.
 */
constexpr std::size_t PeekScratchBytes = 1200;

/**
 * Maps a peek-time socket error code to a peek-probe outcome.
 *
 * A would-block error is the common "nothing queued yet" case; every other code
 * is a hard error. lwIP has no oversize-datagram error, so this mirrors the host
 * POSIX branch without a size sentinel.
 *
 * @param InErrorCode Platform last-error captured right after a failed peek.
 * @return Peek probe the driver acts on without inspecting platform codes.
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
 * Peeks the head datagram into an internal scratch buffer, never the caller's.
 *
 * When lwIP defines `MSG_TRUNC` the peek returns the true datagram length in
 * `BytesReady`; otherwise it returns the delivered length, so a datagram larger
 * than the scratch is under-reported (capped at `PeekScratchBytes`). For every
 * in-contract capacity (at most the scratch size) this still drives `Full`
 * correctly; the exact oversize-datagram behavior is UNVERIFIED at runtime
 * (compile-only phase). The peek never touches the caller-owned destination,
 * keeping `Full` transactional.
 *
 * @param InSocket Open non-blocking socket.
 * @return Peek classification with the observed datagram length when `Ready`.
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
 * Carries one consuming receive result plus, on success, the received byte count.
 */
struct FConsumeResult
{
	/** True when a datagram was consumed into the destination. */
	bool bSuccess;

	/** Valid only when `bSuccess`; bytes written to the destination. */
	std::size_t BytesReceived;
};

/**
 * Consumes the previously-probed head datagram into the destination.
 *
 * Uses a plain `recvfrom` (flags zero) so the datagram leaves the socket queue.
 * The caller's `OutSender` is filled only on success; the driver decodes its
 * IPv4 fields with `ntohl`/`ntohs`.
 *
 * @param InSocket Open non-blocking socket.
 * @param OutDestination Caller-owned buffer for the received bytes.
 * @param InCapacity Byte capacity of `OutDestination`.
 * @param OutSender Filled with the sender's IPv4 socket address on success.
 * @return Consume result with byte count on success.
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
 * Result of opening and binding one non-blocking UDP socket.
 */
struct FOpenedSocket
{
	/** lwIP socket handle; invalid when `bOpen` is false. */
	FSocketHandle Handle;

	/** True when the socket was created, set non-blocking, and bound. */
	bool bOpen;

	/** Host-order port the OS actually bound; valid only when `bOpen`. */
	std::uint16_t BoundPort;
};

/**
 * Closes a partially opened socket and reports the open as failed.
 *
 * Folds the close-then-report rollback shared by every post-open syscall failure
 * so each failure site reads as one line; the returned descriptor carries the
 * now-closed handle with `bOpen` false.
 *
 * @param InSocket Partially opened handle to release.
 * @return Failed opened-socket descriptor.
 */
inline FOpenedSocket CloseAndReportFailure(const FSocketHandle InSocket) noexcept
{
	CloseSocket(InSocket);
	return FOpenedSocket{InSocket, false, 0};
}

/**
 * Opens, binds, and sizes one non-blocking UDP socket on all IPv4 interfaces.
 *
 * An `InBindPort` of zero requests an ephemeral port; the actual port is read back
 * via `getsockname`. On any syscall failure the partially opened socket is closed
 * and `bOpen` is false, so the constructor can leave the driver inert without
 * throwing. Binding to `INADDR_ANY` keeps the adapter free of netif assumptions;
 * a real deployment brings up netif/WiFi before any traffic can flow.
 *
 * @param InBindPort Host-order UDP port to bind, or zero for an ephemeral port.
 * @return Opened-socket descriptor with the actual bound port.
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
 * Waits up to `InTimeoutMilliseconds` for the socket to become readable.
 *
 * Uses `select()` with a bounded timeout so ESP32 demos wait for readiness
 * deterministically without a sleep-poll loop. A true return means a datagram
 * is ready to consume; a false return means the timeout elapsed.
 *
 * @param InSocket Open non-blocking socket.
 * @param InTimeoutMilliseconds Upper bound on the readiness wait.
 * @return True when the socket is readable within the timeout.
 */
inline bool WaitForReadable(const FSocketHandle InSocket, const DurationMilliseconds InTimeoutMilliseconds) noexcept
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

} // namespace MicroWorld::Detail
