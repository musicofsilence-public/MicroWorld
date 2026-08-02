#pragma once

#include <cstdint>

namespace MicroWorld::Engine
{

/**
 * Motivation: Limits one incremental call by semantic operations rather than hidden time, so collection progress stays
 *   predictable and caller-driven.
 * Responsibilities: Bound root entries scanned, reachable-object visitor executions, and object slots inspected;
 *   reference enqueue and deduplication stay inside the bounded class visitor.
 * Example:
 *   FGarbageCollectionBudget Budget{8, 8, 16};
 *   Collector.Advance(Budget);
 */
struct FGarbageCollectionBudget
{
	/** Motivation: Limits root-table entries inspected while seeding reachability. */
	std::uint32_t MaxRootOperations{0};

	/** Motivation: Limits complete reachable-object visitor executions. */
	std::uint32_t MaxMarkOperations{0};

	/** Motivation: Limits object slots inspected for reclamation. */
	std::uint32_t MaxSweepOperations{0};
};

} // namespace MicroWorld::Engine
