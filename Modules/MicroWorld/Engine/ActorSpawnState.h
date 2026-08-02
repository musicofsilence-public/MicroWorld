#pragma once

#include <cstdint>

namespace MicroWorld::Engine
{

/**
 * Motivation: Gives one issued deferred-spawn request a single observable lifetime vocabulary so a caller can branch on
 *   its progress without inspecting private slot state.
 * Responsibilities: Distinguish queued, spawned, failed, released, and stale states.
 * Example:
 *   if (Storage.GetStatus(Handle).State == EActorSpawnState::Spawned) { UseActor(); }
 */
enum class EActorSpawnState : std::uint8_t
{
	/** Motivation: Marks a request accepted but not yet a world-owned live actor. */
	Queued,
	/** Motivation: Marks a request whose actor now lives in the world registry. */
	Spawned,
	/** Motivation: Marks a request that failed construction and freed its capture. */
	Failed,
	/** Motivation: Marks a spawned request whose actor has since left the world. */
	Released,
	/** Motivation: Marks a handle that names no live or in-flight request. */
	Stale,
};

} // namespace MicroWorld::Engine
