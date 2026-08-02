#pragma once

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorSpawnState.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Engine/ObjectResult.h>

namespace MicroWorld::Engine
{

/**
 * Motivation: Reports deferred construction completion and the world-owned actor while it remains live, without letting
 *   an unpublished actor escape.
 * Responsibilities: Map private slot state to a public state, carry the construction result only, and resolve the actor
 *   only after it becomes a world-owned live entry.
 * Example:
 *   FActorSpawnStatus S = Storage.GetStatus(Handle);
 *   if (S.State == EActorSpawnState::Spawned) { S.Actor.Get()->Tick(); }
 */
struct FActorSpawnStatus final
{
	/** Motivation: Maps private construction-pending state to Queued so no unpublished actor escapes. */
	EActorSpawnState State{EActorSpawnState::Stale};

	/** Motivation: Holds object construction outcome only; BeginPlay errors remain ApplyPending results. */
	EObjectResult CompletionResult{EObjectResult::StaleHandle};

	/** Motivation: Resolves only after the actor becomes a world-owned live entry. */
	TObjectPtr<AActor> Actor{};
};

} // namespace MicroWorld::Engine
