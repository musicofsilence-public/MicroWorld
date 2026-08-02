#pragma once

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

} // namespace MicroWorld::Platform::Host
