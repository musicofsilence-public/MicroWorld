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
 * Fixed-capacity backing storage for one `TTransportManager` outbound FIFO.
 *
 * The manager never owns packet storage: the caller constructs one
 * `TTransportPacketStorage` with the exact packet count and per-packet byte capacity
 * the application needs, then lends it to the manager by reference, and only
 * the matching `TTransportManager` specialization observes the private packet arrays.
 * Both template capacities must be nonzero; a zero-packet or zero-byte FIFO has
 * no useful contract and is rejected at compile time.
 */
template<std::size_t MaxPackets, std::size_t MaxPacketBytes>
class TTransportPacketStorage final
{
	static_assert(MaxPackets > 0, "TTransportPacketStorage requires at least one packet slot.");
	static_assert(MaxPacketBytes > 0, "TTransportPacketStorage requires a nonzero per-packet byte capacity.");

public:
	/** Defaulted so the storage can live in automatic or static storage without side effects. */
	TTransportPacketStorage() noexcept = default;

	/** Prevents copying so one storage instance backs exactly one manager FIFO. */
	TTransportPacketStorage(const TTransportPacketStorage&) = delete;

	/** Prevents copying so one storage instance backs exactly one manager FIFO. */
	TTransportPacketStorage& operator=(const TTransportPacketStorage&) = delete;

	/** Defaulted so storage with automatic storage destructs without side effects. */
	~TTransportPacketStorage() noexcept = default;

	/** Reports the fixed packet-slot capacity of this storage. */
	static constexpr std::size_t Capacity() noexcept { return MaxPackets; }

	/** Reports the maximum byte length accepted per packet. */
	static constexpr std::size_t MaximumPacketBytes() noexcept { return MaxPacketBytes; }

private:
	// Storage and manager are deliberately separate types: caller-owned storage is the
	// repo-wide pattern, and keeping the FIFO mechanics in TTransportManager lets one manager
	// implementation be reused over any caller-provided backing (D8).
	/** Grants the manager holding the same template parameters exclusive access to its FIFO slots. */
	friend class TTransportManager<MaxPackets, MaxPacketBytes>;

	/** Fixed per-packet byte storage; only the leading `PacketLengths[i]` bytes are valid. */
	std::array<std::array<std::uint8_t, MaxPacketBytes>, MaxPackets> PacketBytes{};

	/** Records the valid byte length of each queued packet so sends and receives stay exact. */
	std::array<std::size_t, MaxPackets> PacketLengths{};

	/** Records the destination address queued with each packet so AdvanceSend routes it correctly. */
	std::array<::MicroWorld::Transport::Address::FDeviceAddress, MaxPackets> Destinations{};
};

} // namespace MicroWorld::Transport
