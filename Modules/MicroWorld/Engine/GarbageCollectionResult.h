#pragma once

#include <MicroWorld/Core/RuntimeResult.h>
#include <MicroWorld/Engine/GarbageCollectionPhase.h>

#include <cstdint>

namespace MicroWorld::Engine
{

/**
 * Motivation: Reports exact work and reclamation performed by one collector call so a caller can observe incremental
 *   progress without logging or hidden clocks.
 * Responsibilities: Carry the result, current phase, per-phase and total operation counts, reclaimed count, and
 *   cycle-complete signal for the call.
 * Example:
 *   FGarbageCollectionResult R = Collector.Advance(Budget);
 *   if (R.bCycleComplete) { Done(); }
 */
struct FGarbageCollectionResult
{
	/** Motivation: Reports invalid lifecycle or caller-storage capacity without throwing. */
	Core::ERuntimeResult Result{Core::ERuntimeResult::Success};

	/** Motivation: Exposes the phase waiting for the next caller-provided budget. */
	EGarbageCollectionPhase Phase{EGarbageCollectionPhase::Idle};

	/** Motivation: Reports the sum of root, mark, and sweep operations performed this call. */
	std::uint32_t OperationsPerformed{0};

	/** Motivation: Reports root-table entries inspected during this call. */
	std::uint32_t RootOperations{0};

	/** Motivation: Reports reachable objects whose finite visitor completed during this call. */
	std::uint32_t MarkOperations{0};

	/** Motivation: Reports object slots inspected during this call. */
	std::uint32_t SweepOperations{0};

	/** Motivation: Reports objects reclaimed during this call rather than over the whole cycle. */
	std::uint32_t ObjectsReclaimed{0};

	/** Motivation: Signals the exact call that returned the collector to Idle. */
	bool bCycleComplete{false};
};

} // namespace MicroWorld::Engine
