#pragma once

#include <MicroWorld/Transport/DeviceAddress.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace MicroWorld::Transport
{

template<std::size_t MaxPackets, std::size_t MaxPacketBytes>
class TTransportManager;

/**
 * Motivation: Provides fixed-capacity backing storage for one TTransportManager outbound FIFO so the manager never owns packet
 *   bytes and storage stays caller-owned.
 * Responsibilities: Hold the exact packet count and per-packet byte capacity the application selects, expose its slots only to
 *   the matching TTransportManager specialization, and reject a zero-packet or zero-byte configuration at compile time.
 * Example:
 *   TTransportPacketStorage<4, 64> Storage;
 *   TTransportManager<4, 64> Manager(Device, Storage);
 */
template<std::size_t MaxPackets, std::size_t MaxPacketBytes>
class TTransportPacketStorage final
{
	static_assert(MaxPackets > 0, "TTransportPacketStorage requires at least one packet slot.");
	static_assert(MaxPacketBytes > 0, "TTransportPacketStorage requires a nonzero per-packet byte capacity.");

public:
	/**
	 * Motivation: Lets the storage live in automatic or static storage without side effects.
	 * Responsibilities: Default-construct fixed storage with all slots empty.
	 */
	TTransportPacketStorage() noexcept = default;

	/**
	 * Motivation: Prevents copying so one storage instance backs exactly one manager FIFO.
	 * Responsibilities: Reject copy construction so two managers never alias one FIFO's slots.
	 */
	TTransportPacketStorage(const TTransportPacketStorage&) = delete;

	/**
	 * Motivation: Prevents copying so one storage instance backs exactly one manager FIFO.
	 * Responsibilities: Reject copy assignment so two managers never alias one FIFO's slots.
	 */
	TTransportPacketStorage& operator=(const TTransportPacketStorage&) = delete;

	/**
	 * Motivation: Keeps storage with automatic storage side-effect free on destruction.
	 * Responsibilities: Default the destructor since the storage owns only fixed value bytes.
	 */
	~TTransportPacketStorage() noexcept = default;

	/**
	 * Motivation: Lets a caller observe the fixed slot count without magic numbers.
	 * Responsibilities: Report the fixed packet-slot capacity of this storage.
	 */
	static constexpr std::size_t Capacity() noexcept { return MaxPackets; }

	/**
	 * Motivation: Lets a caller observe the per-packet byte ceiling without magic numbers.
	 * Responsibilities: Report the maximum byte length accepted per packet.
	 */
	static constexpr std::size_t MaximumPacketBytes() noexcept { return MaxPacketBytes; }

private:
	// Storage and manager are deliberately separate types: caller-owned storage is the
	// repo-wide pattern, and keeping the FIFO mechanics in TTransportManager lets one manager
	// implementation be reused over any caller-provided backing (D8).
	/** Motivation: Grants the matching TTransportManager exclusive access to this storage's FIFO slots. */
	friend class TTransportManager<MaxPackets, MaxPacketBytes>;

	/** Motivation: Provides fixed per-packet byte storage where only the leading PacketLengths[i] bytes are valid. */
	std::array<std::array<std::uint8_t, MaxPacketBytes>, MaxPackets> PacketBytes{};

	/** Motivation: Records the valid byte length of each queued packet so sends and receives stay exact. */
	std::array<std::size_t, MaxPackets> PacketLengths{};

	/** Motivation: Records the destination address queued with each packet so AdvanceSend routes it correctly. */
	std::array<::MicroWorld::Transport::Address::FDeviceAddress, MaxPackets> Destinations{};
};

} // namespace MicroWorld::Transport
