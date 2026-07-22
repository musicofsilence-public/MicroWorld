#pragma once

// =============================================================================
// src/UdpSocketGlue.h is the SOLE translation unit that pulls OS socket headers.
// It is included only by HostUdpDriver.cpp; a public header must never reach it.
// Every platform divergence (handle width, close, non-blocking mode, last-error
// classification, the MSG_PEEK-vs-MSG_TRUNC size probe) is hidden behind the
// helpers below so HostUdpDriver.cpp reads one platform-free receive/send path.
// The POSIX branch is compiled but NOT verified on this Windows-only host; it
// exists so Phase 5.2 (ESP32 UDP) can reuse the same seams under a POSIX build.
// =============================================================================

#include <MicroWorld/Time.h>

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

namespace MicroWorld::Detail
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
 * @param Socket Handle whose validity is in question.
 * @return True when the handle names an open socket.
 */
inline bool IsValidHandle(const FSocketHandle Socket) noexcept
{
#ifdef _WIN32
	return Socket != INVALID_SOCKET;
#else
	return Socket >= 0;
#endif
}

/**
 * Converts an opaque stored handle back to its OS socket type.
 *
 * `std::uintptr_t` is the same width as `SOCKET`/`int` on every supported host,
 * so the round trip is lossless and keeps `std::uintptr_t` out of the public header.
 *
 * @param Stored Opaque handle value saved by the driver.
 * @return OS socket handle.
 */
inline FSocketHandle AsSocketHandle(const std::uintptr_t Stored) noexcept
{
	return static_cast<FSocketHandle>(Stored);
}

/**
 * Converts an OS socket handle to the driver's opaque stored form.
 *
 * @param Socket OS socket handle.
 * @return Opaque handle value the driver stores.
 */
inline std::uintptr_t AsOpaqueHandle(const FSocketHandle Socket) noexcept
{
	return static_cast<std::uintptr_t>(Socket);
}

/**
 * Releases one OS socket descriptor.
 *
 * Windows closes via `closesocket`; POSIX via `close`. A no-op on an invalid
 * handle so the driver's destructor does not need its own validity branch.
 *
 * @param Socket Handle to release.
 */
inline void CloseSocket(const FSocketHandle Socket) noexcept
{
	if (!IsValidHandle(Socket))
	{
		return;
	}
#ifdef _WIN32
	(void)closesocket(Socket);
#else
	(void)close(Socket);
#endif
}

/**
 * Switches one socket to non-blocking mode.
 *
 * Windows sets `FIONBIO` via `ioctlsocket`; POSIX sets `O_NONBLOCK` via `fcntl`.
 * Returns false on any syscall failure so the constructor can roll back cleanly.
 *
 * @param Socket Handle whose mode to change.
 * @return True when the socket is now non-blocking.
 */
inline bool SetNonBlocking(const FSocketHandle Socket) noexcept
{
#ifdef _WIN32
	u_long Mode = 1;
	return ioctlsocket(Socket, FIONBIO, &Mode) == 0;
#else
	int Flags = fcntl(Socket, F_GETFL, 0);
	if (Flags < 0)
	{
		return false;
	}
	return fcntl(Socket, F_SETFL, Flags | O_NONBLOCK) == 0;
#endif
}

/**
 * Builds an IPv4 `sockaddr_in` from dotted octets and a host-order port.
 *
 * The octets are packed and converted with `htonl`; the port with `htons`, so the
 * returned address is ready for `bind` or `sendto` without further byte swapping.
 *
 * @param A First IPv4 octet.
 * @param B Second IPv4 octet.
 * @param C Third IPv4 octet.
 * @param D Fourth IPv4 octet.
 * @param Port Host-order UDP port.
 * @return Network-ready IPv4 socket address.
 */
inline sockaddr_in MakeSockAddrIn(
	const std::uint8_t A, const std::uint8_t B, const std::uint8_t C, const std::uint8_t D, const std::uint16_t Port) noexcept
{
	sockaddr_in Address{};
	Address.sin_family = AF_INET;
	const std::uint32_t Packed = (static_cast<std::uint32_t>(A) << 24) | (static_cast<std::uint32_t>(B) << 16) | (static_cast<std::uint32_t>(C) << 8)
		| static_cast<std::uint32_t>(D);
	Address.sin_addr.s_addr = htonl(Packed);
	Address.sin_port = htons(Port);
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
 * shared `ENetResult` without inspecting platform error codes.
 *
 * @param Socket Open non-blocking socket.
 * @param Data First byte of the datagram to send.
 * @param Length Number of bytes to send.
 * @param To Network-ready destination address.
 * @return Normalized outcome of the single send attempt.
 */
inline ESendOutcome SendDatagram(const FSocketHandle Socket, const std::uint8_t* const Data, const std::size_t Length, const sockaddr_in& To) noexcept
{
	const int Sent =
#ifdef _WIN32
		sendto(Socket, reinterpret_cast<const char*>(Data), static_cast<int>(Length), 0, reinterpret_cast<const sockaddr*>(&To), sizeof(To));
#else
		sendto(Socket, reinterpret_cast<const char*>(Data), Length, 0, reinterpret_cast<const sockaddr*>(&To), sizeof(To));
#endif
	if (Sent >= 0 && static_cast<std::size_t>(Sent) == Length)
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
 * Mirrors `FHostUdpDriver::UdpMaxPacketBytes` (kept in sync by a `static_assert`
 * in `HostUdpDriver.cpp`); the sizing peek never reads more than this, so it is
 * also the largest payload one send accepts.
 */
constexpr std::size_t PeekScratchBytes = 1200;

/**
 * Peeks the head datagram into an internal scratch buffer, never the caller's.
 *
 * POSIX `MSG_PEEK|MSG_TRUNC` returns the true datagram length in `BytesReady`.
 * Windows `MSG_PEEK` returns the delivered length, or `WSAEMSGSIZE` when the
 * datagram exceeds the scratch; that case is reported `Ready` with a sentinel
 * `BytesReady = PeekScratchBytes + 1` so the single fits-vs-`Full` decision in
 * the driver sees one uniform "does not fit" signal. The peek never touches the
 * caller-owned destination, keeping `Full` transactional on both platforms.
 *
 * @param Socket Open non-blocking socket.
 * @return Peek classification with the true datagram length when `Ready`.
 */
inline FPeekProbe ProbeReadableDatagram(const FSocketHandle Socket) noexcept
{
	std::uint8_t Scratch[PeekScratchBytes];
	sockaddr_storage Sender{};
	FSockLen SenderLen = sizeof(Sender);
#ifdef _WIN32
	const int Peeked = recvfrom(
		Socket, reinterpret_cast<char*>(Scratch), static_cast<int>(PeekScratchBytes), MSG_PEEK, reinterpret_cast<sockaddr*>(&Sender), &SenderLen);
	if (Peeked == SOCKET_ERROR)
	{
		const int Error = WSAGetLastError();
		if (Error == WSAEWOULDBLOCK)
		{
			return FPeekProbe{EPeekStatus::WouldBlock, 0};
		}
		if (Error == WSAEMSGSIZE)
		{
			// Datagram exceeds even the scratch; signal "does not fit" for the driver's single decision site.
			return FPeekProbe{EPeekStatus::Ready, PeekScratchBytes + 1};
		}
		return FPeekProbe{EPeekStatus::Error, 0};
	}
	return FPeekProbe{EPeekStatus::Ready, static_cast<std::size_t>(Peeked)};
#else
	const ssize_t Peeked =
		recvfrom(Socket, reinterpret_cast<char*>(Scratch), PeekScratchBytes, MSG_PEEK | MSG_TRUNC, reinterpret_cast<sockaddr*>(&Sender), &SenderLen);
	if (Peeked < 0)
	{
		const int Error = errno;
		if (Error == EWOULDBLOCK || Error == EAGAIN)
		{
			return FPeekProbe{EPeekStatus::WouldBlock, 0};
		}
		return FPeekProbe{EPeekStatus::Error, 0};
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
 * The caller's `OutSender` is filled only on success; the driver decodes its
 * IPv4 fields with `ntohl`/`ntohs`.
 *
 * @param Socket Open non-blocking socket.
 * @param Destination Caller-owned buffer for the received bytes.
 * @param Capacity Byte capacity of `Destination`.
 * @param OutSender Filled with the sender's IPv4 socket address on success.
 * @return Consume result with byte count on success.
 */
inline FConsumeResult ConsumeDatagram(
	const FSocketHandle Socket, std::uint8_t* const Destination, const std::size_t Capacity, sockaddr_in& OutSender) noexcept
{
	FSockLen SenderLen = sizeof(OutSender);
	const int Received =
#ifdef _WIN32
		recvfrom(Socket, reinterpret_cast<char*>(Destination), static_cast<int>(Capacity), 0, reinterpret_cast<sockaddr*>(&OutSender), &SenderLen);
#else
		recvfrom(Socket, reinterpret_cast<char*>(Destination), Capacity, 0, reinterpret_cast<sockaddr*>(&OutSender), &SenderLen);
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
 * Opens, binds, and sizes one non-blocking UDP socket on the IPv4 loopback.
 *
 * A `BindPort` of zero requests an ephemeral port; the actual port is read back
 * via `getsockname`. On any syscall failure the partially opened socket is closed
 * and `bOpen` is false, so the constructor can leave the driver inert without
 * throwing. Binding to loopback keeps Windows firewall prompts out of the tests.
 *
 * @param BindPort Host-order UDP port to bind, or zero for an ephemeral port.
 * @return Opened-socket descriptor with the actual bound port.
 */
inline FOpenedSocket OpenBoundLoopbackUdpSocket(const std::uint16_t BindPort) noexcept
{
	const FSocketHandle Socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (!IsValidHandle(Socket))
	{
		return FOpenedSocket{Socket, false, 0};
	}
	if (!SetNonBlocking(Socket))
	{
		CloseSocket(Socket);
		return FOpenedSocket{Socket, false, 0};
	}
	sockaddr_in Local = MakeSockAddrIn(127, 0, 0, 1, BindPort);
	if (bind(Socket, reinterpret_cast<const sockaddr*>(&Local), sizeof(Local)) != 0)
	{
		CloseSocket(Socket);
		return FOpenedSocket{Socket, false, 0};
	}
	sockaddr_in Bound{};
	FSockLen BoundLen = sizeof(Bound);
	if (getsockname(Socket, reinterpret_cast<sockaddr*>(&Bound), &BoundLen) != 0)
	{
		CloseSocket(Socket);
		return FOpenedSocket{Socket, false, 0};
	}
	return FOpenedSocket{Socket, true, ntohs(Bound.sin_port)};
}

/**
 * Waits up to `TimeoutMilliseconds` for the socket to become readable.
 *
 * Uses `select()` with a bounded timeout so host tests and demos wait for
 * readiness deterministically without a sleep-poll loop. A true return means a
 * datagram is ready to consume; a false return means the timeout elapsed.
 *
 * @param Socket Open non-blocking socket.
 * @param TimeoutMilliseconds Upper bound on the readiness wait.
 * @return True when the socket is readable within the timeout.
 */
inline bool WaitForReadable(const FSocketHandle Socket, const DurationMilliseconds TimeoutMilliseconds) noexcept
{
	fd_set ReadSet;
	FD_ZERO(&ReadSet);
	FD_SET(Socket, &ReadSet);
	timeval Timeout{};
	Timeout.tv_sec = static_cast<long>(TimeoutMilliseconds / 1000u);
	Timeout.tv_usec = static_cast<long>((TimeoutMilliseconds % 1000u) * 1000u);
	const int Ready = select(static_cast<int>(Socket + 1), &ReadSet, nullptr, nullptr, &Timeout);
	return Ready > 0;
}

} // namespace MicroWorld::Detail
