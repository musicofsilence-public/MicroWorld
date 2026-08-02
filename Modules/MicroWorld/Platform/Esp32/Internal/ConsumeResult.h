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

} // namespace MicroWorld::Platform::Esp32
