#pragma once

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Engine
{

/**
 * Motivation: Exposes fixed capacity, occupancy, roots, retirement, and fragmentation as one snapshot so a caller can
 *   observe store health without inspecting private state.
 * Responsibilities: Count slot capacity, occupancy, pending-destroy and retired slots, root capacity and active roots,
 *   and payload and fragmentation bytes.
 * Example:
 *   FObjectStoreStats S = Store.Stats();
 *   if (S.OccupiedSlots == S.SlotCapacity) { Full(); }
 */
struct FObjectStoreStats
{
	/** Motivation: Reports the caller-selected number of equal-size object slots. */
	std::uint32_t SlotCapacity{0};

	/** Motivation: Counts constructed live and pending objects that still occupy slots. */
	std::uint32_t OccupiedSlots{0};

	/** Motivation: Counts occupied objects hidden until the destruction barrier reclaims them. */
	std::uint32_t PendingDestroySlots{0};

	/** Motivation: Counts slots permanently unavailable because their generation was exhausted. */
	std::uint32_t RetiredSlots{0};

	/** Motivation: Reports the caller-selected independent explicit-root capacity. */
	std::uint32_t RootCapacity{0};

	/** Motivation: Counts independently owned root tokens, including duplicate handles. */
	std::uint32_t ActiveRoots{0};

	/** Motivation: Reports the fixed byte extent reserved for every object slot. */
	std::size_t SlotSizeBytes{0};

	/** Motivation: Sums exact descriptor extents for all occupied objects. */
	std::size_t ObjectPayloadBytes{0};

	/** Motivation: Reports equal-slot bytes unavailable to object payloads while occupied. */
	std::size_t InternalFragmentationBytes{0};
};

} // namespace MicroWorld::Engine
