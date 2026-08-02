#pragma once

#include <MicroWorld/Engine/ActorSpawnHandle.h>
#include <MicroWorld/Engine/ActorSpawnRequestResult.h>

namespace MicroWorld::Engine
{

/**
 * Motivation: Couples immediate request admission with the handle available after successful queueing in one return
 *   value.
 * Responsibilities: Report the preflight result and carry a valid handle only when Result is Queued.
 * Example:
 *   FActorSpawnRequest R = World.DeferSpawnActor(...);
 *   if (R.Result == EActorSpawnRequestResult::Queued) { Keep(R.Handle); }
 */
struct FActorSpawnRequest final
{
	/** Motivation: Reports the preflight result without constructing an actor at the call site. */
	EActorSpawnRequestResult Result{EActorSpawnRequestResult::CapacityExceeded};

	/** Motivation: Identifies the request only when Result is Queued. */
	FActorSpawnHandle Handle{};
};

} // namespace MicroWorld::Engine
