#pragma once

#include <MicroWorld/Transport/DeviceAddress.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Platform/Host/UdpAddress.h>
#include <MicroWorld/Platform/Host/WinSockScope.h>
#include <MicroWorld/Core/Time.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Platform::Host
{

/**
 * Motivation: Gives the engine a real host socket through Core::ITransportDevice so a host build can exercise the
 *   networking path without bespoke hardware.
 * Responsibilities: Own one non-blocking SOCK_DGRAM socket bound to an IPv4 loopback port, validate every
 *   argument before any syscall, map each BSD/WinSock outcome to the shared ETransportResult, and leave
 *   caller-owned outputs unchanged on any non-Success result.
 * Example:
 *   FHostWifiDevice Device(0);
 *   if (Device.IsOpen()) { Device.TrySend(Peer, Packet); }
 */
class FHostWifiDevice final : public Core::ITransportDevice
{
public:
	/** Motivation: Largest UDP payload one send accepts and one receive destination may exceed. */
	static constexpr std::size_t UdpMaxPacketBytes = 1200;

	/**
	 * Motivation: Lets a composition root open a working socket from one construction argument.
	 * Responsibilities: Open one non-blocking UDP socket bound to 127.0.0.1 on InBindPort (zero asks for an
	 *   ephemeral port, readable through BoundPort), and on any syscall failure close what was opened so the
	 *   device is left inert without throwing.
	 */
	explicit FHostWifiDevice(std::uint16_t InBindPort) noexcept;

	/**
	 * Motivation: Lets an owning system release the device's single socket and socket-stack contribution.
	 * Responsibilities: Close the owned socket and release the shared socket-stack reference, never throwing.
	 */
	~FHostWifiDevice() noexcept override;

	/**
	 * Motivation: Prevents copying so one device value owns exactly one socket identity.
	 * Responsibilities: Reject copy construction so a duplicate device never claims the same socket.
	 */
	FHostWifiDevice(const FHostWifiDevice&) = delete;

	/**
	 * Motivation: Prevents copying so one device value owns exactly one socket identity.
	 * Responsibilities: Reject copy assignment so a duplicate device never claims the same socket.
	 */
	FHostWifiDevice& operator=(const FHostWifiDevice&) = delete;

	/**
	 * Motivation: Prevents moving so the owned socket handle and interface identity stay fixed.
	 * Responsibilities: Reject move construction so the socket handle never relocates to another value.
	 */
	FHostWifiDevice(FHostWifiDevice&&) = delete;

	/**
	 * Motivation: Prevents moving so the owned socket handle and interface identity stay fixed.
	 * Responsibilities: Reject move assignment so the socket handle never relocates to another value.
	 */
	FHostWifiDevice& operator=(FHostWifiDevice&&) = delete;

	/**
	 * Motivation: Lets the device deliver one complete datagram per call without blocking the caller.
	 * Responsibilities: Reject a non-UDP address, an oversize packet, or a null span with nonzero length as
	 *   Invalid, report Full when the send would block, and return Success only when the whole datagram was
	 *   accepted; a non-success result leaves the socket state unchanged.
	 */
	Transport::ETransportResult TrySend(const Transport::Address::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept override;

	/**
	 * Motivation: Lets the device take in at most one datagram per call without blocking or overwriting caller state on failure.
	 * Responsibilities: Peek the head datagram to size it without consuming, return Unavailable when nothing is queued,
	 *   Full when the destination is too small (datagram left queued), Invalid for a null destination with nonzero length,
	 *   and Success only after a consuming read writes the bytes, count, and sender address into OutFrom.
	 */
	Transport::ETransportResult TryReceive(
		Transport::Address::FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, Core::FReceiveResult& OutResult) noexcept override;

	/**
	 * Motivation: Lets a caller size a send destination against the device's accepted maximum.
	 * Responsibilities: Report the largest datagram, in bytes, one send accepts.
	 */
	std::size_t MaxPacketBytes() const noexcept override;

	/**
	 * Motivation: Records that synchronous UDP sends leave no deferred transport work for this turn.
	 * Responsibilities: Do no work because TrySend submits each datagram directly to the socket.
	 */
	void PreAdvance(Core::TimePointMilliseconds) noexcept override {}

	/**
	 * Motivation: Lets a caller gate every operation on a usable socket after construction.
	 * Responsibilities: Report whether the constructor opened a usable socket.
	 */
	bool IsOpen() const noexcept;

	/**
	 * Motivation: Lets a caller discover the actual port a zero-bind landed on, so peers can address this socket.
	 * Responsibilities: Report the actual host-order port the socket bound, post-construction.
	 */
	std::uint16_t BoundPort() const noexcept;

	/**
	 * Motivation: Lets a host test or demo wait for inbound data deterministically without a sleep-poll loop.
	 * Responsibilities: Use select() with a bounded timeout and return true only when a subsequent TryReceive has data
	 *   to consume.
	 */
	bool PollReadable(Core::DurationMilliseconds InTimeoutMilliseconds) const noexcept;

private:
	/** Motivation: Holds one reference to the shared socket-stack lifetime for the owned socket. */
	FWinSockScope WinSock{};

	/** Motivation: Opaque OS socket handle reinterpreted to SOCKET or int only in the source file. */
	std::uintptr_t SocketHandle{0};

	/** Motivation: Host-order port captured from getsockname so callers can address this socket. */
	std::uint16_t BoundPortValue{0};

	/** Motivation: Remains false when construction failed, so every op short-circuits safely. */
	bool bOpen{false};
};

} // namespace MicroWorld::Platform::Host
