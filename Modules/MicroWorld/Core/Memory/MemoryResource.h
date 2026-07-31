#pragma once

#include <cstddef>
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

/**
 * Motivation: Defines explicit allocation over caller-selected storage with no heap fallback.
 * Responsibilities: Allocate and release exact active blocks, and report capacity and usage, without
 *   compaction or fallback to another resource.
 * Example:
 *   TFixedArena<128, 8> Arena;
 *   IMemoryResource& Resource = Arena;
 */
class IMemoryResource
{
public:
	/**
	 * Motivation: Lets a concrete resource be destroyed through the portable boundary.
	 * Responsibilities: Release resource identity without owning a particular storage policy.
	 */
	virtual ~IMemoryResource() noexcept;

	/**
	 * Motivation: Lets a caller reserve one aligned range from the resource.
	 * Responsibilities: Attempt one aligned allocation, clear OutBlock on any failure, and require the returned block
	 *   be passed unchanged to Deallocate on this same resource.
	 */
	virtual EMemoryResult TryAllocate(std::size_t InSizeBytes, std::size_t InAlignmentBytes, FMemoryBlock& OutBlock) noexcept = 0;

	/**
	 * Motivation: Lets a caller return one exact active block to its resource.
	 * Responsibilities: Release one exact active block originally returned by this resource, rejecting anything else.
	 */
	virtual EMemoryResult Deallocate(FMemoryBlock InBlock) noexcept = 0;

	/**
	 * Motivation: Lets a caller test a request against the resource's limit without magic numbers.
	 * Responsibilities: Report the resource's caller-usable byte capacity without metadata.
	 */
	virtual std::size_t CapacityBytes() const noexcept = 0;

	/**
	 * Motivation: Keeps exhaustion observable so a caller can react before allocation fails.
	 * Responsibilities: Report bytes held by active blocks.
	 */
	virtual std::size_t UsedBytes() const noexcept = 0;
};

} // namespace MicroWorld::Core
