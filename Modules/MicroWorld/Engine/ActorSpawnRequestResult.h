#pragma once

#include <cstdint>

namespace MicroWorld::Engine
{

/**
 * Motivation: Reports why a typed factory request was not accepted before it captured arguments, so admission failures
 *   stay distinguishable from later construction failures.
 * Responsibilities: Distinguish queued from capacity, lifecycle-lock, unconfigured, factory-too-large, and
 *   factory-alignment failures.
 * Example:
 *   if (Request.Result == EActorSpawnRequestResult::FactoryTooLarge) { ShrinkCapture(); }
 */
enum class EActorSpawnRequestResult : std::uint8_t
{
	/** Motivation: Confirms the request was admitted to the queue and a handle was issued. */
	Queued,
	/** Motivation: Rejects a request when no reusable request slot remains. */
	CapacityExceeded,
	/** Motivation: Rejects a request because the owning world's lifecycle forbids new spawns. */
	LifecycleLocked,
	/** Motivation: Rejects a request because typed spawning was never configured for this world. */
	Unconfigured,
	/** Motivation: Rejects a request whose factory capture exceeds the configured inline bytes. */
	FactoryTooLarge,
	/** Motivation: Rejects a request whose factory capture alignment the storage cannot satisfy. */
	FactoryAlignmentUnsupported,
};

} // namespace MicroWorld::Engine
