#pragma once

#include <MicroWorld/Transport/DeviceAddress.h>
#include <MicroWorld/Transport/Device.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Platform/Host/UdpAddress.h>
#include <MicroWorld/Platform/Host/WinSockScope.h>
#include <MicroWorld/Core/Time.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld
{

using namespace ::MicroWorld::Transport;
using namespace ::MicroWorld::Transport::Address;
using namespace ::MicroWorld::Transport::Device;

/**
 * Non-blocking UDP `IDevice` that carries traffic over one real host socket.
 *
 * Owns a single `SOCK_DGRAM` socket bound to an IPv4 loopback port and maps each
 * BSD/WinSock outcome to the shared `ETransportResult` so callers poll without blocking.
 * It validates every argument before any syscall and leaves caller-owned outputs
 * unchanged on any non-`Success` result, and is the host template that the ESP32
 * UDP and E32 LoRa adapters in Phase 5 will mirror.
 */
class FHostWifiDevice final : public IDevice
{
public:
	/** Largest UDP payload one send accepts and one receive destination may exceed. */
	static constexpr std::size_t UdpMaxPacketBytes = 1200;

	/**
	 * Opens one non-blocking UDP socket bound to `127.0.0.1:InBindPort`.
	 *
	 * An `InBindPort` of zero asks the host for an ephemeral port, readable through
	 * `BoundPort()`. On any syscall failure the constructor closes what it opened
	 * and leaves the device with `IsOpen() == false`; it never throws.
	 *
	 * @param InBindPort Host-order UDP port to bind, or zero for an ephemeral port.
	 */
	explicit FHostWifiDevice(std::uint16_t InBindPort) noexcept;

	/** Closes the owned socket and releases the shared socket-stack reference. */
	~FHostWifiDevice() noexcept override;

	/** Prevents copying so one device value owns exactly one socket identity. */
	FHostWifiDevice(const FHostWifiDevice&) = delete;

	/** Prevents copying so one device value owns exactly one socket identity. */
	FHostWifiDevice& operator=(const FHostWifiDevice&) = delete;

	/** Prevents moving so the owned socket handle and interface identity stay fixed. */
	FHostWifiDevice(FHostWifiDevice&&) = delete;

	/** Prevents moving so the owned socket handle and interface identity stay fixed. */
	FHostWifiDevice& operator=(FHostWifiDevice&&) = delete;

	/**
	 * Sends one complete datagram to a UDP-encoded `InTo` address, transactionally.
	 *
	 * Returns `Invalid` for an address that is not a UDP encoding, an oversize
	 * packet, or a null span with nonzero length; `Full` when the send would
	 * block; and `Success` only when the whole datagram was accepted. A
	 * non-success result leaves the socket state unchanged.
	 *
	 * @param InTo Destination whose bytes encode IPv4 octets and a port.
	 * @param InPacket Caller-owned bytes to deliver as one complete datagram.
	 * @return Normalized outcome of the single send attempt.
	 */
	ETransportResult TrySend(const FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept override;

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
	 * @param InDestination Caller-owned buffer for the received bytes.
	 * @param OutResult Filled with the received byte count only on `Success`.
	 * @return Normalized outcome of the single receive attempt.
	 */
	ETransportResult TryReceive(FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept override;

	/** Reports the largest datagram, in bytes, one send accepts. */
	std::size_t MaxPacketBytes() const noexcept override;

	/** Reports whether the constructor opened a usable socket. */
	bool IsOpen() const noexcept;

	/** Reports the actual host-order port the socket bound, post-construction. */
	std::uint16_t BoundPort() const noexcept;

	/**
	 * Waits up to `InTimeoutMilliseconds` for a datagram to be readable on the socket.
	 *
	 * Uses `select()` with a bounded timeout so host tests and demos can wait for
	 * readiness deterministically without sleeping in a poll loop. A true return
	 * means a subsequent `TryReceive` has data to consume.
	 *
	 * @param InTimeoutMilliseconds Upper bound on the readiness wait.
	 * @return True when the socket is readable within the timeout.
	 */
	bool PollReadable(Core::DurationMilliseconds InTimeoutMilliseconds) const noexcept;

private:
	/** Holds one reference to the shared socket-stack lifetime for the owned socket. */
	FWinSockScope WinSock{};

	/** Opaque OS socket handle reinterpreted to `SOCKET` or `int` only in the source file. */
	std::uintptr_t SocketHandle{0};

	/** Host-order port captured from `getsockname` so callers can address this socket. */
	std::uint16_t BoundPortValue{0};

	/** Remains false when construction failed, so every op short-circuits safely. */
	bool bOpen{false};
};

} // namespace MicroWorld
