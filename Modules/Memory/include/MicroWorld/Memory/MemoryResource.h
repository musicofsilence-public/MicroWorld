#pragma once

#include <cstddef>
#include <cstdint>

namespace MicroWorld
{

/** Reports the complete portable outcome of one memory-resource operation. */
enum class EMemoryResult : std::uint8_t
{
	/** Confirms that the requested resource state transition completed. */
	Success,

	/** Makes bounded-capacity exhaustion observable without a heap fallback. */
	OutOfMemory,

	/** Rejects an alignment the selected resource cannot guarantee. */
	UnsupportedAlignment,

	/** Rejects a block that is not the resource's exact active allocation. */
	InvalidBlock,
};

/** Preserves the exact allocation identity that must return to its resource. */
struct FMemoryBlock
{
	/** Identifies the first caller-owned byte without implying object lifetime. */
	void* Address{nullptr};

	/** Retains the allocation extent needed for exact deallocation validation. */
	std::size_t SizeBytes{0};
};

/** Defines explicit allocation over caller-selected storage with no fallback. */
class IMemoryResource
{
public:
	/** Supports destruction through the resource boundary without owning storage policy. */
	virtual ~IMemoryResource() noexcept;

	/**
	 * Attempts one aligned allocation and clears OutBlock whenever allocation fails.
	 *
	 * The returned block must later be passed unchanged to Deallocate on this
	 * same resource.
	 */
	virtual EMemoryResult TryAllocate(std::size_t SizeBytes, std::size_t AlignmentBytes, FMemoryBlock& OutBlock) noexcept = 0;

	/** Releases one exact active block originally returned by this resource. */
	virtual EMemoryResult Deallocate(FMemoryBlock Block) noexcept = 0;

	/** Reports the resource's caller-usable byte capacity without metadata. */
	virtual std::size_t CapacityBytes() const noexcept = 0;

	/** Reports bytes held by active blocks so exhaustion remains observable. */
	virtual std::size_t UsedBytes() const noexcept = 0;
};

} // namespace MicroWorld
