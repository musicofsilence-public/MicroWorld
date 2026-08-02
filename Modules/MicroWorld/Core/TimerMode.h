#pragma once

#include <cstdint>

namespace MicroWorld::Core
{

/**
 * Motivation: Selects one timer schedule shape independently of its bound callback.
 * Responsibilities: Distinguish the unscheduled default from one-shot and looping schedules.
 * Example:
 *   ETimerMode Mode = ETimerMode::Looping;
 */
enum class ETimerMode : std::uint8_t
{
	/** Motivation: Rejects scheduling so an uninitialized mode never silently becomes OneShot or Looping. */
	None,

	/** Motivation: Fires once and removes the timer so its handle becomes stale. */
	OneShot,

	/** Motivation: Reschedules from the accepted NowMilliseconds after each fire and stays in insertion order. */
	Looping,
};

} // namespace MicroWorld::Core
