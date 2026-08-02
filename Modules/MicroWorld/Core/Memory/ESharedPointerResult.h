#pragma once

#include <MicroWorld/Core/Memory/MemoryResult.h>

#include <cstdint>

namespace MicroWorld::Core
{

/**
 * Motivation: Gives every fallible shared or weak ownership operation one result vocabulary without exceptions.
 * Responsibilities: Distinguish acquisition success from capacity, alignment, expiry, overflow, and resource failure.
 * Example:
 *   if (Result == ESharedPointerResult::Expired) { Recover(); }
 */
enum class ESharedPointerResult : std::uint8_t
{
	/** Motivation: Confirms that the requested owner or observer was acquired. */
	Success,

	/** Motivation: Reports that the selected resource could not hold the combined allocation. */
	OutOfMemory,

	/** Motivation: Reports that the selected resource cannot satisfy the combined alignment. */
	UnsupportedAlignment,

	/** Motivation: Rejects acquisition after the observed value's last strong owner released it. */
	Expired,

	/** Motivation: Rejects an increment that would make a reference counter wrap. */
	ReferenceCountOverflow,

	/** Motivation: Preserves an unexpected resource failure without pretending it was exhaustion. */
	ResourceFailure,
};

/**
 * Motivation: Lets the shared-pointer internals translate a memory-resource failure into their own result domain.
 * Responsibilities: Map each EMemoryResult to the matching ESharedPointerResult, defaulting unexpected results to ResourceFailure.
 */
inline ESharedPointerResult ToSharedPointerResult(const EMemoryResult InResult) noexcept
{
	switch (InResult)
	{
		case EMemoryResult::Success:
			return ESharedPointerResult::Success;
		case EMemoryResult::OutOfMemory:
			return ESharedPointerResult::OutOfMemory;
		case EMemoryResult::UnsupportedAlignment:
			return ESharedPointerResult::UnsupportedAlignment;
		case EMemoryResult::InvalidBlock:
		default:
			return ESharedPointerResult::ResourceFailure;
	}
}

} // namespace MicroWorld::Core
