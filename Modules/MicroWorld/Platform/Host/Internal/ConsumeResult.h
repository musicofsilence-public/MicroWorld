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

} // namespace MicroWorld::Platform::Host
