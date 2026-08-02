#pragma once

#include <MicroWorld/Engine/ObjectResult.h>

#include <cstdint>

namespace MicroWorld::Engine
{

/**
 * Motivation: Reports bounded pending-destruction work performed at one mutation barrier so a caller can drive
 *   reclamation across multiple frames.
 * Responsibilities: Summarize the result, slots visited, objects destroyed, and pending objects remaining.
 * Example:
 *   FObjectMutationResult R = Store.ApplyPendingDestroy(Budget);
 *   if (R.PendingObjectsRemaining == 0) { Done(); }
 */
struct FObjectMutationResult
{
	/** Motivation: Reports invalid store configuration or successful bounded traversal. */
	EObjectResult Result{EObjectResult::Success};

	/** Motivation: Counts slots inspected so every call's work remains observable and bounded. */
	std::uint32_t SlotsVisited{0};

	/** Motivation: Counts objects whose BeginDestroy and exact destructor ran in this call. */
	std::uint32_t ObjectsDestroyed{0};

	/** Motivation: Reports pending objects left for a later explicit mutation barrier. */
	std::uint32_t PendingObjectsRemaining{0};
};

} // namespace MicroWorld::Engine
