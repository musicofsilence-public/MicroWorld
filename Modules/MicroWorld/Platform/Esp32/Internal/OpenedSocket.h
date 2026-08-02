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

} // namespace MicroWorld::Platform::Esp32
