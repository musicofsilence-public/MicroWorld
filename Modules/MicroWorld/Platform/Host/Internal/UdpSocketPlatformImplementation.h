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

namespace MicroWorld
{

#ifdef _WIN32
/** Windows socket descriptor width; `INVALID_SOCKET` is its sentinel. */
using FSocketHandle = SOCKET;
#else
/** POSIX socket descriptor width; a negative value is its sentinel. */
using FSocketHandle = int;
#endif

#ifdef _WIN32
/** Address-length type expected by the Windows `sockaddr` accessors. */
using FSockLen = int;
#else
/** Address-length type expected by the POSIX `sockaddr` accessors. */
using FSockLen = socklen_t;
#endif

/**
 * Stamps the open/closed state of one socket handle.
 *
 * Windows uses `INVALID_SOCKET` and POSIX uses a negative `int`, so callers must
 * not test the raw value across platforms.
 *
 * @param InSocket Handle whose validity is in question.
 * @return True when the handle names an open socket.
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
 * Converts an opaque stored handle back to its OS socket type.
 *
 * `std::uintptr_t` is the same width as `SOCKET`/`int` on every supported host,
 * so the round trip is lossless and keeps `std::uintptr_t` out of the public header.
 *
 * @param InStored Opaque handle value saved by the device.
 * @return OS socket handle.
 */
inline FSocketHandle AsSocketHandle(const std::uintptr_t InStored) noexcept
{
	return static_cast<FSocketHandle>(InStored);
}

/**
 * Converts an OS socket handle to the device's opaque stored form.
 *
 * @param InSocket OS socket handle.
 * @return Opaque handle value the device stores.
 */
inline std::uintptr_t AsOpaqueHandle(const FSocketHandle InSocket) noexcept
{
	return static_cast<std::uintptr_t>(InSocket);
}

/**
 * Releases one OS socket descriptor.
 *
 * Windows closes via `closesocket`; POSIX via `close`. A no-op on an invalid
 * handle so the device's destructor does not need its own validity branch.
 *
 * @param InSocket Handle to release.
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
 * Switches one socket to non-blocking mode.
 *
 * Windows sets `FIONBIO` via `ioctlsocket`; POSIX sets `O_NONBLOCK` via `fcntl`.
 * Returns false on any syscall failure so the constructor can roll back cleanly.
 *
 * @param InSocket Handle whose mode to change.
 * @return True when the socket is now non-blocking.
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
 * it was fully accepted, would block, or failed, so the device can map it to the
 * shared `ETransportResult` without inspecting platform error codes.
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

/** Normalized result of a non-consuming peek at the head datagram. */
enum class EPeekStatus : std::uint8_t
{
	/** A datagram is queued; `BytesReady` carries its true length. */
	Ready,
	/** No datagram is ready right now. */
	WouldBlock,
	/** A socket error occurred. */
	Error,
};

/**
 * Carries one peek result plus, on `Ready`, the head datagram's length.
 */
struct FPeekProbe
{
	/** Classifies what the non-consuming peek observed. */
	EPeekStatus Status;

	/** Valid only when `Status == Ready`; the true byte count of the queued datagram. */
	std::size_t BytesReady;
};

/**
 * Largest datagram the sizing peek can observe without an overflow error.
 *
 * Mirrors `FHostWifiDevice::UdpMaxPacketBytes` (kept in sync by a `static_assert`
 * in `HostWifiDevice.cpp`); the sizing peek never reads more than this, so it is
 * also the largest payload one send accepts.
 */
constexpr std::size_t PeekScratchBytes = 1200;

/** One past the peek scratch: a datagram at least this large cannot fit, so the device's fits-vs-Full check reports Full without consuming it. */
constexpr std::size_t OversizeDatagramSentinelBytes = PeekScratchBytes + 1;

/**
 * Maps a peek-time socket error code to a peek-probe outcome.
 *
 * A would-block error is the common "nothing queued yet" case. On Windows an
 * oversize datagram surfaces as `WSAEMSGSIZE`, reported `Ready` with the oversize
 * sentinel so the device's single fits-vs-`Full` decision sees one uniform "does
 * not fit" signal; every other code is a hard error.
 *
 * @param InErrorCode Platform last-error captured right after a failed peek.
 * @return Peek probe the device acts on without inspecting platform codes.
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
 * Peeks the head datagram into an internal scratch buffer, never the caller's.
 *
 * POSIX `MSG_PEEK|MSG_TRUNC` returns the true datagram length in `BytesReady`.
 * Windows `MSG_PEEK` returns the delivered length, or `WSAEMSGSIZE` when the
 * datagram exceeds the scratch; that case is reported `Ready` with a sentinel
 * `BytesReady = OversizeDatagramSentinelBytes` so the single fits-vs-`Full` decision in
 * the device sees one uniform "does not fit" signal. The peek never touches the
 * caller-owned destination, keeping `Full` transactional on both platforms.
 *
 * @param InSocket Open non-blocking socket.
 * @return Peek classification with the true datagram length when `Ready`.
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
 * The caller's `OutSender` is filled only on success; the device decodes its
 * IPv4 fields with `ntohl`/`ntohs`.
 *
 * @param InSocket Open non-blocking socket.
 * @param OutDestination Caller-owned buffer for the received bytes.
 * @param InCapacity Byte capacity of the destination buffer.
 * @param OutSender Filled with the sender's IPv4 socket address on success.
 * @return Consume result with byte count on success.
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
 * Result of opening and binding one non-blocking loopback UDP socket.
 */
struct FOpenedSocket
{
	/** OS socket handle; invalid when `bOpen` is false. */
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
 * Opens, binds, and sizes one non-blocking UDP socket on the IPv4 loopback.
 *
 * An `InBindPort` of zero requests an ephemeral port; the actual port is read back
 * via `getsockname`. On any syscall failure the partially opened socket is closed
 * and `bOpen` is false, so the constructor can leave the device inert without
 * throwing. Binding to loopback keeps Windows firewall prompts out of the tests.
 *
 * @param InBindPort Host-order UDP port to bind, or zero for an ephemeral port.
 * @return Opened-socket descriptor with the actual bound port.
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
 * Waits up to `InTimeoutMilliseconds` for the socket to become readable.
 *
 * Uses `select()` with a bounded timeout so host tests and demos wait for
 * readiness deterministically without a sleep-poll loop. A true return means a
 * datagram is ready to consume; a false return means the timeout elapsed.
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
	const int Ready = select(static_cast<int>(InSocket + 1), &ReadSet, nullptr, nullptr, &Timeout);
	return Ready > 0;
}

} // namespace MicroWorld
