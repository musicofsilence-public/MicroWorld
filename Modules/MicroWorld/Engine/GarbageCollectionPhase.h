#pragma once

#include <cstdint>

namespace MicroWorld::Engine
{

/**
 * Motivation: Names the current bounded stage of one explicit mark/sweep cycle so the collector and its callers branch
 *   on progress without re-deriving it from internal state.
 * Responsibilities: Distinguish idle, root-seeding, mark, and sweep phases.
 * Example:
 *   if (Collector.Phase() == EGarbageCollectionPhase::Idle) { Collector.RequestCollection(); }
 */
enum class EGarbageCollectionPhase : std::uint8_t
{
	/** Motivation: Reports that no collection is requested or in progress. */
	Idle,

	/** Motivation: Scans fixed root-table entries before graph traversal begins. */
	SeedRoots,

	/** Motivation: Iteratively traces reachable objects through caller-owned worklist storage. */
	Mark,

	/** Motivation: Inspects fixed object slots and reclaims each unreachable live object. */
	Sweep,
};

} // namespace MicroWorld::Engine
