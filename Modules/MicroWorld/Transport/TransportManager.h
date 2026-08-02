#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Transport/TransportPacketStorage.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace MicroWorld::Transport
{

/**
 * Motivation: Provides one fixed-capacity outbound FIFO and direct receive over an externally referenced device so a host
 *   batches sends and drains receives without allocating.
 * Responsibilities: Hold one device and one TTransportPacketStorage by reference (both caller-owned), copy a complete accepted
 *   packet into the FIFO tail on QueueSend, attempt at most the head on AdvanceSend while retaining it on any device failure,
 *   and perform at most one direct device receive without building an inbound queue.
 * Example:
 *   TTransportPacketStorage<4, 64> Storage;
 *   TTransportManager<4, 64> Manager(Device, Storage);
 *   Manager.QueueSend(To, Packet);
 *   Manager.AdvanceSend();
 */
template<std::size_t MaxPackets, std::size_t MaxPacketBytes>
class TTransportManager final
{
	static_assert(MaxPackets > 0, "TTransportManager requires at least one packet slot.");
	static_assert(MaxPacketBytes > 0, "TTransportManager requires a nonzero per-packet byte capacity.");

public:
	/**
	 * Motivation: Binds the manager to the device and caller-owned storage it will drive.
	 * Responsibilities: Store references to the externally owned device and storage.
	 */
	TTransportManager(Core::ITransportDevice& InDevice, TTransportPacketStorage<MaxPackets, MaxPacketBytes>& InStorage) noexcept
		: Device(InDevice), Storage(InStorage)
	{
	}

	/**
	 * Motivation: Prevents copying so one manager value binds one device and one storage instance.
	 * Responsibilities: Reject copy construction so the device and storage references stay single-owner.
	 */
	TTransportManager(const TTransportManager&) = delete;

	/**
	 * Motivation: Prevents copying so one manager value binds one device and one storage instance.
	 * Responsibilities: Reject copy assignment so the device and storage references stay single-owner.
	 */
	TTransportManager& operator=(const TTransportManager&) = delete;

	/**
	 * Motivation: Keeps a manager with automatic storage side-effect free on destruction.
	 * Responsibilities: Default the destructor since the manager owns no resource.
	 */
	~TTransportManager() noexcept = default;

	/**
	 * Motivation: Queues one packet transactionally so a malformed or full request never disturbs FIFO contents or order.
	 * Responsibilities: Treat the address as opaque (the device validates it), return Invalid for a null packet with nonzero
	 *   length or a packet larger than MaxPacketBytes, return Full when the FIFO has no free slot, and otherwise copy the
	 *   packet into the tail.
	 */
	Core::ETransportResult QueueSend(const Core::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept
	{
		const std::size_t PacketSize = InPacket.Size();
		if (PacketSize == 0)
		{
			return EnqueuePacket(InTo, InPacket);
		}
		if (InPacket.Data() == nullptr)
		{
			return Core::ETransportResult::Invalid;
		}
		if (PacketSize > MaxPacketBytes)
		{
			// The packet can never fit a slot; the request is malformed.
			return Core::ETransportResult::Invalid;
		}
		return EnqueuePacket(InTo, InPacket);
	}

	/**
	 * Motivation: Drains the outbound FIFO one device send at a time so a transient device rejection never loses a packet.
	 * Responsibilities: Perform at most one device send, remove the head on Success, and retain the head and preserve order on
	 *   Full, Unavailable, or Invalid; return Unavailable when the FIFO is empty so "nothing to send" is distinguishable.
	 */
	Core::ETransportResult AdvanceSend() noexcept
	{
		if (QueuedPacketCount == 0)
		{
			return Core::ETransportResult::Unavailable;
		}
		const Core::TSpan<const std::uint8_t> HeadPacket(Storage.PacketBytes[HeadIndex].data(), Storage.PacketLengths[HeadIndex]);
		const Core::ETransportResult SendResult = Device.TrySend(Storage.Destinations[HeadIndex], HeadPacket);
		if (SendResult != Core::ETransportResult::Success)
		{
			// Retain the head and preserve order; the caller retries on the next advance.
			return SendResult;
		}
		Storage.PacketLengths[HeadIndex] = 0;
		HeadIndex = (HeadIndex + 1) % MaxPackets;
		--QueuedPacketCount;
		return Core::ETransportResult::Success;
	}

	/**
	 * Motivation: Performs one direct device receive so inbound packets never accumulate in the manager.
	 * Responsibilities: Delegate to the device transactionally, leaving the destination, OutResult.BytesReceived, and OutFrom
	 *   unchanged on Full, Invalid, or Unavailable.
	 */
	Core::ETransportResult Receive(Core::FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, Core::FReceiveResult& OutResult) noexcept
	{
		return Device.TryReceive(OutFrom, InDestination, OutResult);
	}

	/**
	 * Motivation: Lets a caller observe the fixed FIFO depth without magic numbers.
	 * Responsibilities: Report the fixed packet-slot capacity of this manager's outbound FIFO.
	 */
	static constexpr std::size_t QueueCapacity() noexcept { return MaxPackets; }

	/**
	 * Motivation: Lets a caller observe the per-packet byte ceiling without magic numbers.
	 * Responsibilities: Report the maximum byte length accepted per queued packet.
	 */
	static constexpr std::size_t MaximumPacketBytes() noexcept { return MaxPacketBytes; }

	/**
	 * Motivation: Lets a caller observe pending outbound work.
	 * Responsibilities: Report how many packets are currently queued for send.
	 */
	constexpr std::size_t QueuedCount() const noexcept { return QueuedPacketCount; }

	/**
	 * Motivation: Lets a caller skip AdvanceSend when there is nothing to drain.
	 * Responsibilities: Report whether the FIFO is empty.
	 */
	constexpr bool IsEmpty() const noexcept { return QueuedPacketCount == 0; }

	/**
	 * Motivation: Lets a caller observe backpressure before a QueueSend reports Full.
	 * Responsibilities: Report whether the FIFO is full.
	 */
	constexpr bool IsFull() const noexcept { return QueuedPacketCount >= MaxPackets; }

private:
	/**
	 * Motivation: Completes a validated queue by appending to the FIFO tail.
	 * Responsibilities: Return Full when no slot is free, otherwise store the packet and advance the tail.
	 */
	Core::ETransportResult EnqueuePacket(const Core::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept
	{
		if (QueuedPacketCount >= MaxPackets)
		{
			return Core::ETransportResult::Full;
		}
		StorePacketAt(TailIndex, InTo, InPacket, InPacket.Size());
		AdvanceTail();
		return Core::ETransportResult::Success;
	}

	/**
	 * Motivation: Persists one accepted packet into a named slot so send can read it back later.
	 * Responsibilities: Copy the bytes (if any), record the length, and store the destination address.
	 */
	void StorePacketAt(
		const std::size_t InIndex,
		const Core::FDeviceAddress& InTo,
		Core::TSpan<const std::uint8_t> InPacket,
		const std::size_t InPacketSize) noexcept
	{
		if (InPacketSize > 0)
		{
			std::memcpy(Storage.PacketBytes[InIndex].data(), InPacket.Data(), InPacketSize);
		}
		Storage.PacketLengths[InIndex] = InPacketSize;
		Storage.Destinations[InIndex] = InTo;
	}

	/**
	 * Motivation: Completes one enqueue by moving the tail and count forward.
	 * Responsibilities: Advance the tail index with wraparound and increment occupancy.
	 */
	void AdvanceTail() noexcept
	{
		TailIndex = (TailIndex + 1) % MaxPackets;
		++QueuedPacketCount;
	}

	/** Motivation: References the externally owned device whose lifetime the caller controls. */
	Core::ITransportDevice& Device;

	/** Motivation: References the caller-owned packet storage the manager reads and writes. */
	TTransportPacketStorage<MaxPackets, MaxPacketBytes>& Storage;

	/** Motivation: Indexes the next packet to send so the FIFO order is preserved. */
	std::size_t HeadIndex{0};

	/** Motivation: Indexes the next free slot so queues append without overwriting the head. */
	std::size_t TailIndex{0};

	/** Motivation: Tracks occupancy so full and empty states are observable without wrap arithmetic. */
	std::size_t QueuedPacketCount{0};
};

} // namespace MicroWorld::Transport
