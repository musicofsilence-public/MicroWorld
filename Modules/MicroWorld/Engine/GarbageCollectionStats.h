#pragma once

#include <cstdint>

namespace MicroWorld::Engine
{

/**
 * Motivation: Exposes cumulative collector outcomes without logging or hidden clocks so a caller can observe collector
 *   health over many cycles.
 * Responsibilities: Count completed cycles, reclaimed objects, rejected requests, and worklist overflows.
 * Example:
 *   FGarbageCollectionStats S = Collector.Stats();
 *   if (S.WorklistOverflows > 0) { GrowWorklist(); }
 */
struct FGarbageCollectionStats
{
	/** Motivation: Counts complete explicit collection cycles. */
	std::uint32_t CompletedCycles{0};

	/** Motivation: Counts objects reclaimed across complete and incremental calls. */
	std::uint32_t ReclaimedObjects{0};

	/** Motivation: Counts requests rejected because a cycle was active or storage was invalid. */
	std::uint32_t RejectedRequests{0};

	/** Motivation: Counts traces that could not enqueue a reachable object in caller storage. */
	std::uint32_t WorklistOverflows{0};
};

} // namespace MicroWorld::Engine
