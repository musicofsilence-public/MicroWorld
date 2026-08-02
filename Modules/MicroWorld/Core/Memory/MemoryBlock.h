#pragma once

#include <cstddef>

namespace MicroWorld::Core
{

/**
 * Motivation: Preserves the exact allocation identity a caller must hand back to its resource.
 * Responsibilities: Carry the address and exact size of one active allocation without implying object lifetime.
 * Example:
 *   FMemoryBlock Block{};
 *   Resource.Deallocate(Block);
 */
struct FMemoryBlock
{
	/** Motivation: Identifies the first caller-owned byte without implying object lifetime. */
	void* Address{nullptr};

	/** Motivation: Retains the allocation extent needed for exact deallocation validation. */
	std::size_t SizeBytes{0};
};

/**
 * Motivation: Lets a caller round a byte size up so a value placed at the result begins on its own aligned boundary.
 * Responsibilities: Return the next multiple of InAlignmentBytes at least as large as InSizeBytes; InAlignmentBytes must be a power of two.
 */
constexpr std::size_t AlignSizeUp(const std::size_t InSizeBytes, const std::size_t InAlignmentBytes) noexcept
{
	return (InSizeBytes + InAlignmentBytes - 1U) & ~(InAlignmentBytes - 1U);
}

} // namespace MicroWorld::Core
