#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Transport/Device.h>
#include <MicroWorld/Transport/TransportPacketStorage.h>
#include <MicroWorld/Transport/TransportResult.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace MicroWorld
{

/**
 * Fixed-capacity outbound queue and direct receive over one externally referenced `IDevice`.
 *
 * The manager holds one driver and one `TTransportPacketStorage` by reference; the
 * caller owns both. `QueueSend` copies a complete accepted packet into the
 * caller-owned FIFO tail, one `AdvanceSend` attempts at most the head (retaining
 * it on any driver failure), and `Receive` performs at most one direct driver
 * receive without building an inbound queue.
 */
template<std::size_t MaxPackets, std::size_t MaxPacketBytes>
class TTransportManager final
{
	static_assert(MaxPackets > 0, "TTransportManager requires at least one packet slot.");
	static_assert(MaxPacketBytes > 0, "TTransportManager requires a nonzero per-packet byte capacity.");

public:
	/** Binds the manager to one externally referenced driver and caller-owned packet storage. */
	TTransportManager(IDevice& InDriver, TTransportPacketStorage<MaxPackets, MaxPacketBytes>& InStorage) noexcept
		: Driver(InDriver), Storage(InStorage)
	{
	}

	/** Prevents copying so one manager value binds one driver and one storage instance. */
	TTransportManager(const TTransportManager&) = delete;

	/** Prevents copying so one manager value binds one driver and one storage instance. */
	TTransportManager& operator=(const TTransportManager&) = delete;

	/** Defaulted so a manager with automatic storage destructs without side effects. */
	~TTransportManager() noexcept = default;

	/**
	 * Copies one complete packet with its destination address into the outbound FIFO tail.
	 * The address is opaque to the manager; the driver validates it. Returns `Invalid`
	 * for a null packet with nonzero length or a packet larger than `MaxPacketBytes`
	 * (it can never fit). Returns `Full` when the FIFO has no free slot. A non-success
	 * result leaves the FIFO contents and order unchanged.
	 */
	ETransportResult QueueSend(const FDeviceAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept
	{
		const std::size_t PacketSize = InPacket.Size();
		if (PacketSize == 0)
		{
			return EnqueuePacket(InTo, InPacket);
		}
		if (InPacket.Data() == nullptr)
		{
			return ETransportResult::Invalid;
		}
		if (PacketSize > MaxPacketBytes)
		{
			// The packet can never fit a slot; the request is malformed.
			return ETransportResult::Invalid;
		}
		return EnqueuePacket(InTo, InPacket);
	}

	/**
	 * Attempts to send the FIFO head through the driver to its stored destination.
	 * Performs at most one driver send. On `Success` the head packet is removed.
	 * On `Full`, `Unavailable`, or `Invalid` from the driver, the head packet is
	 * retained and FIFO order is preserved so the next advance can retry it.
	 * Returns `Unavailable` when the FIFO is empty, so a caller can distinguish
	 * "nothing to send" from a transient driver rejection.
	 */
	ETransportResult AdvanceSend() noexcept
	{
		if (QueuedPacketCount == 0)
		{
			return ETransportResult::Unavailable;
		}
		const TSpan<const std::uint8_t> HeadPacket(Storage.PacketBytes[HeadIndex].data(), Storage.PacketLengths[HeadIndex]);
		const ETransportResult SendResult = Driver.TrySend(Storage.Destinations[HeadIndex], HeadPacket);
		if (SendResult != ETransportResult::Success)
		{
			// Retain the head and preserve order; the caller retries on the next advance.
			return SendResult;
		}
		Storage.PacketLengths[HeadIndex] = 0;
		HeadIndex = (HeadIndex + 1) % MaxPackets;
		--QueuedPacketCount;
		return ETransportResult::Success;
	}

	/**
	 * Performs at most one direct driver receive into caller storage.
	 * The operation is transactional: on `Full`, `Invalid`, or `Unavailable` the
	 * destination, `OutResult.BytesReceived`, and `OutFrom` are unchanged. The
	 * manager never queues inbound packets.
	 */
	ETransportResult Receive(FDeviceAddress& OutFrom, TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept
	{
		return Driver.TryReceive(OutFrom, InDestination, OutResult);
	}

	/** Reports the fixed packet-slot capacity of this manager's outbound FIFO. */
	static constexpr std::size_t QueueCapacity() noexcept { return MaxPackets; }

	/** Reports the maximum byte length accepted per queued packet. */
	static constexpr std::size_t MaximumPacketBytes() noexcept { return MaxPacketBytes; }

	/** Reports how many packets are currently queued for send. */
	constexpr std::size_t QueuedCount() const noexcept { return QueuedPacketCount; }

	/** Distinguishes an empty FIFO so a caller can skip `AdvanceSend`. */
	constexpr bool IsEmpty() const noexcept { return QueuedPacketCount == 0; }

	/** Distinguishes a full FIFO so a caller can observe backpressure. */
	constexpr bool IsFull() const noexcept { return QueuedPacketCount >= MaxPackets; }

private:
	/** Copies one already-validated packet into the FIFO tail, or `Full` when no slot is free. */
	ETransportResult EnqueuePacket(const FDeviceAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept
	{
		if (QueuedPacketCount >= MaxPackets)
		{
			return ETransportResult::Full;
		}
		StorePacketAt(TailIndex, InTo, InPacket, InPacket.Size());
		AdvanceTail();
		return ETransportResult::Success;
	}

	/** Copies one accepted packet, its length, and its destination address into the slot at `InIndex`. */
	void StorePacketAt(
		const std::size_t InIndex, const FDeviceAddress& InTo, TSpan<const std::uint8_t> InPacket, const std::size_t InPacketSize) noexcept
	{
		if (InPacketSize > 0)
		{
			std::memcpy(Storage.PacketBytes[InIndex].data(), InPacket.Data(), InPacketSize);
		}
		Storage.PacketLengths[InIndex] = InPacketSize;
		Storage.Destinations[InIndex] = InTo;
	}

	/** Advances the tail and count after one accepted packet. */
	void AdvanceTail() noexcept
	{
		TailIndex = (TailIndex + 1) % MaxPackets;
		++QueuedPacketCount;
	}

	/** Holds the externally referenced driver; the caller owns its lifetime. */
	IDevice& Driver;

	/** Holds the externally referenced caller-owned packet storage. */
	TTransportPacketStorage<MaxPackets, MaxPacketBytes>& Storage;

	/** Indexes the next packet to send so the FIFO order is preserved. */
	std::size_t HeadIndex{0};

	/** Indexes the next free slot so queues append without overwriting the head. */
	std::size_t TailIndex{0};

	/** Tracks occupancy so full and empty states are observable without wrap arithmetic. */
	std::size_t QueuedPacketCount{0};
};

} // namespace MicroWorld
