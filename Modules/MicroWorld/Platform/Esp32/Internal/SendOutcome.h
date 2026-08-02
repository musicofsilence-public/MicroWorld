#pragma once

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

} // namespace MicroWorld::Platform::Esp32
