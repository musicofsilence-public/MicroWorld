#pragma once

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Engine
{

struct FObjectSlotMetadata;
struct FObjectRootEntry;

/**
 * Motivation: Describes all non-owning fixed storage required by one object store so the store itself stays allocation-free.
 * Responsibilities: Bundle slot payload bytes, slot metadata, slot sizing, root entries, and root capacity into one value
 *   the application owns for the store's lifetime.
 * Example:
 *   FObjectStoreStorage Storage{Bytes, N, Meta, N, Size, Align, Roots, R};
 */
struct FObjectStoreStorage
{
	/** Motivation: Provides the first byte of equal-size, non-moving object slots. */
	std::byte* SlotPayloadBytes{nullptr};

	/** Motivation: Provides enough bytes for SlotCount consecutive SlotSizeBytes ranges. */
	std::size_t TotalSlotStorageBytes{0};

	/** Motivation: Provides one lifecycle record for every equal-size object slot. */
	FObjectSlotMetadata* SlotMetadata{nullptr};

	/** Motivation: Fixes the number of object lifetimes that may be active concurrently. */
	std::uint32_t SlotCount{0};

	/** Motivation: Fixes the maximum object extent and internal-fragmentation unit. */
	std::size_t SlotSizeBytes{0};

	/** Motivation: Fixes the maximum supported object alignment for every slot. */
	std::size_t SlotAlignmentBytes{0};

	/** Motivation: Provides independently reusable entries for explicit strong-root tokens. */
	FObjectRootEntry* Roots{nullptr};

	/** Motivation: Fixes the number of simultaneous independently owned root tokens. */
	std::uint32_t RootCapacity{0};
};

} // namespace MicroWorld::Engine
