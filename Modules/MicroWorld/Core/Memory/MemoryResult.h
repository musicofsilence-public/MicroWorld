#pragma once

#include <cstdint>

namespace MicroWorld::Core
{

/**
 * Motivation: Gives every memory-resource operation one portable outcome that needs no exceptions.
 * Responsibilities: Distinguish success from capacity exhaustion, unsupported alignment, and an invalid block.
 * Example:
 *   if (Resource.TryAllocate(16, 4, Block) == EMemoryResult::OutOfMemory) { Recover(); }
 */
enum class EMemoryResult : std::uint8_t
{
	/** Motivation: Confirms that the requested resource state transition completed. */
	Success,

	/** Motivation: Makes bounded-capacity exhaustion observable without a heap fallback. */
	OutOfMemory,

	/** Motivation: Rejects an alignment the selected resource cannot guarantee. */
	UnsupportedAlignment,

	/** Motivation: Rejects a block that is not the resource's exact active allocation. */
	InvalidBlock,
};

} // namespace MicroWorld::Core
