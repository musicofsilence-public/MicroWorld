#pragma once

#include <MicroWorld/Transport/DeviceAddress.h>
#include <MicroWorld/Transport/Device.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Platform/Esp32/UdpAddress.h>
#include <MicroWorld/Core/Time.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Gives one composition root a non-blocking UDP IDevice that carries traffic over one real lwIP socket,
 *   the ESP32 sibling of the host adapter.
 * Responsibilities: Own one SOCK_DGRAM socket bound to an IPv4 port, map each lwIP outcome to the shared
 *   ETransportResult so callers poll without blocking, validate every argument before any syscall, and leave
 *   caller-owned outputs unchanged on any non-Success result.
 * Example:
 *   FEsp32WifiDevice Wifi(8888);
 *   if (Wifi.IsOpen()) { Wifi.TrySend(To, Packet); }
 */
class FEsp32WifiDevice final : public Transport::Device::IDevice
{
public:
	/** Motivation: Largest UDP payload one send accepts and one receive destination may exceed. */
	static constexpr std::size_t UdpMaxPacketBytes = 1200;

	/**
	 * Motivation: Opens one non-blocking UDP socket bound to INADDR_ANY:InBindPort before any traffic flows.
	 * Responsibilities: On any syscall failure close what was opened and leave IsOpen false; never throw and never
	 *   initialize netif or WiFi (a real deployment brings those up first).
	 */
	explicit FEsp32WifiDevice(std::uint16_t InBindPort) noexcept;

	/**
	 * Motivation: Closes the owned socket so construction-allocated lwIP resources never leak.
	 * Responsibilities: Close the socket opened by construction.
	 */
	~FEsp32WifiDevice() noexcept override;

	/**
	 * Motivation: Keeps one device value owning exactly one socket identity so the handle never aliases.
	 * Responsibilities: Reject copy construction so the device stays the single owner of its socket.
	 */
	FEsp32WifiDevice(const FEsp32WifiDevice&) = delete;

	/**
	 * Motivation: Keeps one device value owning exactly one socket identity so the handle never aliases.
	 * Responsibilities: Reject copy assignment so the device stays the single owner of its socket.
	 */
	FEsp32WifiDevice& operator=(const FEsp32WifiDevice&) = delete;

	/**
	 * Motivation: Keeps the owned socket handle and interface identity fixed at one address for the link's lifetime.
	 * Responsibilities: Reject move construction so the opaque handle never relocates.
	 */
	FEsp32WifiDevice(FEsp32WifiDevice&&) = delete;

	/**
	 * Motivation: Keeps the owned socket handle and interface identity fixed at one address for the link's lifetime.
	 * Responsibilities: Reject move assignment so the opaque handle never relocates.
	 */
	FEsp32WifiDevice& operator=(FEsp32WifiDevice&&) = delete;

	/**
	 * Motivation: Sends one complete datagram to a UDP-encoded address, transactionally.
	 * Responsibilities: Return Invalid for a non-UDP address, oversize packet, or null span with nonzero length,
	 *   Full when the send would block, and Success only after the whole datagram is accepted; leave socket state
	 *   unchanged on any non-success result.
	 */
	Transport::ETransportResult TrySend(const Transport::Address::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept override;

	/**
	 * Motivation: Receives at most one datagram into the caller-owned destination, transactionally.
	 * Responsibilities: Peek the head datagram to size it without consuming; report Unavailable when no datagram is
	 *   ready, Full when the destination is too small (the datagram stays queued), Invalid for a null destination
	 *   with nonzero length, or Success after a consuming read writes bytes, count, and sender address into OutFrom.
	 */
	Transport::ETransportResult TryReceive(
		Transport::Address::FDeviceAddress& OutFrom,
		Core::TSpan<std::uint8_t> InDestination,
		Transport::Device::FReceiveResult& OutResult) noexcept override;

	/**
	 * Motivation: Lets a caller size a datagram against the transport's capacity without a magic number.
	 * Responsibilities: Report the largest datagram, in bytes, one send accepts.
	 */
	std::size_t MaxPacketBytes() const noexcept override;

	/**
	 * Motivation: Lets a caller gate every op on whether construction opened a usable socket.
	 * Responsibilities: Report the open flag set at construction and never mutated afterward except by destruction.
	 */
	bool IsOpen() const noexcept;

	/**
	 * Motivation: Lets a caller read the actual port the OS bound (including an ephemeral zero bind).
	 * Responsibilities: Report the host-order port captured from getsockname at construction.
	 */
	std::uint16_t BoundPort() const noexcept;

	/**
	 * Motivation: Lets an ESP32 demo wait for inbound readiness deterministically without a sleep-poll loop.
	 * Responsibilities: Use lwIP select() with a bounded timeout and return true only when the socket is readable.
	 */
	bool PollReadable(Core::DurationMilliseconds InTimeoutMilliseconds) const noexcept;

private:
	/** Motivation: Opaque OS socket handle reinterpreted to its lwIP type only in the source file. */
	std::uintptr_t SocketHandle{0};

	/** Motivation: Host-order port captured from getsockname so callers can address this socket. */
	std::uint16_t BoundPortValue{0};

	/** Motivation: Remains false when construction failed, so every op short-circuits safely. */
	bool bOpen{false};
};

} // namespace MicroWorld::Platform::Esp32
