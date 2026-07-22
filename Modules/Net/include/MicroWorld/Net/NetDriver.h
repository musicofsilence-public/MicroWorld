#pragma once

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetResult.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld
{

/**
 * Carries the byte count produced by one non-blocking receive.
 *
 * A receive is transactional: on `Full`, `Invalid`, or `Unavailable` the driver
 * leaves the caller-owned destination and `BytesReceived` unchanged, so a caller
 * never confuses a failed receive with a short successful read. Only a `Success`
 * result writes a received byte count and (possibly zero) destination bytes.
 */
struct FNetReceiveResult
{
	/**
	 * Bytes written to the caller-owned destination.
	 * Unchanged on any non-success result; set only on `Success` (zero for a zero-length packet).
	 */
	std::size_t BytesReceived{0};
};

/**
 * Bounds one non-blocking addressed byte transport behind a single reference-held interface.
 *
 * Each call performs at most one transport operation and returns an explicit
 * `ENetResult` so a caller can poll without blocking and distinguish transient
 * unavailability from permanent rejection. The interface owns no scheduler,
 * clock, thread, retry policy, peer identity, session, or protocol behavior;
 * those concerns belong to the caller or to a higher layer that this package
 * intentionally does not provide.
 */
class INetDriver
{
public:
	/**
	 * Sends one complete packet to `To` or rejects it transactionally.
	 * Returns `Success` only when the whole span was accepted, `Full` when the
	 * transport lacks capacity for the packet, `Invalid` for a null span with
	 * nonzero length, a packet larger than the transport's maximum, or a
	 * destination address the driver cannot route. A non-success result leaves
	 * the transport state unchanged.
	 */
	virtual ENetResult TrySend(const FNetAddress& To, TSpan<const std::uint8_t> Packet) noexcept = 0;

	/**
	 * Receives at most one packet into the caller-owned destination.
	 * The operation is transactional: on `Full`, `Invalid`, or `Unavailable` the
	 * destination, `OutResult.BytesReceived`, and `OutFrom` are unchanged.
	 * Returns `Success` only when a packet was delivered (writes the head bytes,
	 * the byte count, and the sender address into `OutFrom`), `Unavailable` when
	 * no packet is ready, `Full` when the destination is too small for the queued
	 * head packet, and `Invalid` for a null destination with nonzero length.
	 */
	virtual ENetResult TryReceive(FNetAddress& OutFrom, TSpan<std::uint8_t> Destination, FNetReceiveResult& OutResult) noexcept = 0;

	/** Reports the largest packet, in bytes, the transport accepts on a single send. */
	virtual std::size_t MaxPacketBytes() const noexcept = 0;

	/** Gives every concrete driver one stable virtual destructor out of line. */
	virtual ~INetDriver() noexcept;

	/** Prevents slicing through the interface; drivers are held by reference. */
	INetDriver(const INetDriver&) = delete;

	/** Prevents slicing through the interface; drivers are held by reference. */
	INetDriver& operator=(const INetDriver&) = delete;

protected:
	/** Lets concrete drivers construct without exposing the interface as instantiable. */
	INetDriver() noexcept = default;
};

} // namespace MicroWorld
