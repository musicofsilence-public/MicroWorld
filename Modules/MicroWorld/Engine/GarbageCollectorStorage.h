#pragma once

#include <MicroWorld/Engine/ObjectHandle.h>

#include <cstdint>

namespace MicroWorld::Engine
{

/**
 * Motivation: Supplies caller-owned iterative bookkeeping with no collector heap fallback, so collection stays
 *   allocation-free and bounded.
 * Responsibilities: Carry the worklist buffer and its capacity, which must cover the configured object-slot count.
 * Example:
 *   FGarbageCollectorStorage Storage{Worklist.data(), N};
 */
struct FGarbageCollectorStorage
{
	/** Motivation: Holds generation-checked reachable identities awaiting one finite visitor run. */
	FObjectHandle* Worklist{nullptr};

	/** Motivation: Bounds worklist occupancy and must cover the configured object-slot count. */
	std::uint32_t WorklistCapacity{0};
};

} // namespace MicroWorld::Engine
