#pragma once

#include <MicroWorld/Net/NetAddress.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace MicroWorld
{

template<std::size_t MaxPackets, std::size_t MaxPacketBytes>
class FNetManager;

/**
 * Fixed-capacity backing storage for one `FNetManager` outbound FIFO.
 *
 * The manager never owns packet storage: the caller constructs one
 * `FNetPacketStorage` with the exact packet count and per-packet byte capacity
 * the application needs, then lends it to the manager by reference, and only
 * the matching `FNetManager` specialization observes the private packet arrays.
 * Both template capacities must be nonzero; a zero-packet or zero-byte FIFO has
 * no useful contract and is rejected at compile time.
 */
template<std::size_t MaxPackets, std::size_t MaxPacketBytes>
class FNetPacketStorage final
{
	static_assert(MaxPackets > 0, "FNetPacketStorage requires at least one packet slot.");
	static_assert(MaxPacketBytes > 0, "FNetPacketStorage requires a nonzero per-packet byte capacity.");

public:
	/** Defaulted so the storage can live in automatic or static storage without side effects. */
	FNetPacketStorage() noexcept = default;

	/** Prevents copying so one storage instance backs exactly one manager FIFO. */
	FNetPacketStorage(const FNetPacketStorage&) = delete;

	/** Prevents copying so one storage instance backs exactly one manager FIFO. */
	FNetPacketStorage& operator=(const FNetPacketStorage&) = delete;

	/** Defaulted so storage with automatic storage destructs without side effects. */
	~FNetPacketStorage() noexcept = default;

	/** Reports the fixed packet-slot capacity of this storage. */
	static constexpr std::size_t Capacity() noexcept { return MaxPackets; }

	/** Reports the maximum byte length accepted per packet. */
	static constexpr std::size_t MaximumPacketBytes() noexcept { return MaxPacketBytes; }

private:
	/** Grants the manager holding the same template parameters exclusive access to its FIFO slots. */
	friend class FNetManager<MaxPackets, MaxPacketBytes>;

	/** Fixed per-packet byte storage; only the leading `PacketLengths[i]` bytes are valid. */
	std::array<std::array<std::uint8_t, MaxPacketBytes>, MaxPackets> PacketBytes{};

	/** Records the valid byte length of each queued packet so sends and receives stay exact. */
	std::array<std::size_t, MaxPackets> PacketLengths{};

	/** Records the destination address queued with each packet so AdvanceSend routes it correctly. */
	std::array<FNetAddress, MaxPackets> Destinations{};
};

} // namespace MicroWorld
