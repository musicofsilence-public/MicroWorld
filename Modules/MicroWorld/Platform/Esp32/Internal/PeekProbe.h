#pragma once

#include <MicroWorld/Platform/Esp32/Internal/PeekStatus.h>
#include <MicroWorld/Platform/Esp32/Internal/SocketHandle.h>

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

} // namespace MicroWorld::Platform::Esp32
