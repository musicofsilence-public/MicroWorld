#pragma once

#include <MicroWorld/Core/Memory/MemoryBlock.h>
#include <MicroWorld/Core/Memory/MemoryResult.h>

#include <cstddef>

namespace MicroWorld::Core
{

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
