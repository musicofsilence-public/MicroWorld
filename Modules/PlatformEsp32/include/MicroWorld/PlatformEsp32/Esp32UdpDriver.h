#pragma once

#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetDriver.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/PlatformEsp32/UdpAddress.h>
#include <MicroWorld/Time.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld
{

/**
 * Non-blocking UDP `INetDriver` that carries traffic over one real lwIP socket.
 *
 * Owns a single `SOCK_DGRAM` socket bound to an IPv4 port and maps each lwIP
 * outcome to the shared `ENetResult` so callers poll without blocking. It
 * validates every argument before any syscall and leaves caller-owned outputs
 * unchanged on any non-`Success` result, and is the ESP32 sibling of the host
 * adapter shipped in Phase 5.1.
 */
class FEsp32UdpDriver final : public INetDriver
{
public:
	/** Largest UDP payload one send accepts and one receive destination may exceed. */
	static constexpr std::size_t UdpMaxPacketBytes = 1200;

	/**
	 * Opens one non-blocking UDP socket bound to `INADDR_ANY:BindPort`.
	 *
	 * A `BindPort` of zero asks lwIP for an ephemeral port, readable through
	 * `BoundPort()`. On any syscall failure the constructor closes what it opened
	 * and leaves the driver with `IsOpen() == false`; it never throws. No netif
	 * or WiFi is initialized here; a real deployment brings those up first.
	 *
	 * @param BindPort Host-order UDP port to bind, or zero for an ephemeral port.
	 */
	explicit FEsp32UdpDriver(std::uint16_t BindPort) noexcept;

	/** Closes the owned socket. */
	~FEsp32UdpDriver() noexcept override;

	/** Prevents copying so one driver value owns exactly one socket identity. */
	FEsp32UdpDriver(const FEsp32UdpDriver&) = delete;

	/** Prevents copying so one driver value owns exactly one socket identity. */
	FEsp32UdpDriver& operator=(const FEsp32UdpDriver&) = delete;

	/** Prevents moving so the owned socket handle and interface identity stay fixed. */
	FEsp32UdpDriver(FEsp32UdpDriver&&) = delete;

	/** Prevents moving so the owned socket handle and interface identity stay fixed. */
	FEsp32UdpDriver& operator=(FEsp32UdpDriver&&) = delete;

	/**
	 * Sends one complete datagram to a UDP-encoded `To` address, transactionally.
	 *
	 * Returns `Invalid` for an address that is not a UDP encoding, an oversize
	 * packet, or a null span with nonzero length; `Full` when the send would
	 * block; and `Success` only when the whole datagram was accepted. A
	 * non-success result leaves the socket state unchanged.
	 *
	 * @param To Destination whose bytes encode IPv4 octets and a port.
	 * @param Packet Caller-owned bytes to deliver as one complete datagram.
	 * @return Normalized outcome of the single send attempt.
	 */
	ENetResult TrySend(const FNetAddress& To, TSpan<const std::uint8_t> Packet) noexcept override;

	/**
	 * Receives at most one datagram into the caller-owned destination, transactionally.
	 *
	 * Peeks the head datagram to size it without consuming: `Unavailable` when no
	 * datagram is ready, `Full` when the destination is too small (the datagram
	 * stays queued), `Invalid` for a null destination with nonzero length, and
	 * `Success` after a consuming read writes the bytes, the count, and the
	 * sender address into `OutFrom`.
	 *
	 * @param OutFrom Filled with the sender's UDP address only on `Success`.
	 * @param Destination Caller-owned buffer for the received bytes.
	 * @param OutResult Filled with the received byte count only on `Success`.
	 * @return Normalized outcome of the single receive attempt.
	 */
	ENetResult TryReceive(FNetAddress& OutFrom, TSpan<std::uint8_t> Destination, FNetReceiveResult& OutResult) noexcept override;

	/** Reports the largest datagram, in bytes, one send accepts. */
	std::size_t MaxPacketBytes() const noexcept override;

	/** Reports whether the constructor opened a usable socket. */
	bool IsOpen() const noexcept;

	/** Reports the actual host-order port the socket bound, post-construction. */
	std::uint16_t BoundPort() const noexcept;

	/**
	 * Waits up to `TimeoutMilliseconds` for a datagram to be readable on the socket.
	 *
	 * Uses lwIP `select()` with a bounded timeout so ESP32 demos can wait for
	 * readiness deterministically without sleeping in a poll loop. A true return
	 * means a subsequent `TryReceive` has data to consume.
	 *
	 * @param TimeoutMilliseconds Upper bound on the readiness wait.
	 * @return True when the socket is readable within the timeout.
	 */
	bool PollReadable(DurationMilliseconds TimeoutMilliseconds) const noexcept;

private:
	/** Opaque OS socket handle reinterpreted to its lwIP type only in the source file. */
	std::uintptr_t SocketHandle{0};

	/** Host-order port captured from `getsockname` so callers can address this socket. */
	std::uint16_t BoundPortValue{0};

	/** Remains false when construction failed, so every op short-circuits safely. */
	bool bOpen{false};
};

} // namespace MicroWorld
