#pragma once

#include "SocketHandle.h"

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
 * Motivation: Carries the outcome of opening and binding one non-blocking loopback socket so the constructor branches on one value.
 * Responsibilities: Hold the socket handle, the open flag, and the host-order port the OS actually bound.
 * Example:
 *   const FOpenedSocket Opened = OpenBoundLoopbackUdpSocket(BindPort);
 *   if (!Opened.bOpen) { return; }
 */
struct FOpenedSocket
{
	/** Motivation: OS socket handle; invalid when bOpen is false. */
	FSocketHandle Handle;

	/** Motivation: True when the socket was created, set non-blocking, and bound. */
	bool bOpen;

	/** Motivation: Host-order port the OS actually bound; valid only when bOpen. */
	std::uint16_t BoundPort;
};

/**
 * Motivation: Folds the close-then-report rollback shared by every post-open syscall failure into one helper.
 * Responsibilities: Close the partially opened socket and return a descriptor that carries the now-closed handle with
 *   bOpen false.
 */
inline FOpenedSocket CloseAndReportFailure(const FSocketHandle InSocket) noexcept
{
	CloseSocket(InSocket);
	return FOpenedSocket{InSocket, false, 0};
}

/**
 * Motivation: Lets the constructor open a working socket from one bind port argument.
 * Responsibilities: Open, set non-blocking, and bind one UDP socket on the IPv4 loopback, read back the actual port
 *   via getsockname (an InBindPort of zero requests an ephemeral port), and on any syscall failure close the
 *   partially opened socket and return bOpen false so the device is left inert without throwing; binding to loopback
 *   keeps Windows firewall prompts out of the tests.
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

} // namespace MicroWorld::Platform::Host
