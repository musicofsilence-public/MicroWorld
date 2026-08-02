#pragma once

#include <cstddef>

namespace MicroWorld::Engine
{

/**
 * Motivation: Gives TEngine one starting set of compile-time capacities sized for an ESP32-S3, so a consumer whose needs
 *   match that baseline writes TEngine<> with no args.
 * Responsibilities: Hold the eight capacity members a project overrides in its own traits to grow or shrink the engine;
 *   these are a starting point, not a measurement.
 * Example:
 *   TEngine<FDefaultEngineTraits> Engine(Budget);
 */
struct FDefaultEngineTraits
{
	/** Motivation: Maximum registered class descriptors (engine bases plus user types). */
	static constexpr std::size_t MaxClasses = 8;

	/** Motivation: Maximum live managed objects across the world, actors, and components. */
	static constexpr std::size_t MaxObjects = 16;

	/** Motivation: Byte width of one equal-size, non-moving object slot. */
	static constexpr std::size_t SlotSizeBytes = 512;

	/** Motivation: Alignment every object slot preserves. */
	static constexpr std::size_t SlotAlign = 16;

	/** Motivation: Maximum independently reusable strong-root entries. */
	static constexpr std::size_t MaxRoots = 2;

	/** Motivation: Maximum actors the single world registers. */
	static constexpr std::size_t MaxActors = 4;

	/** Motivation: Maximum concurrent bounded timers. */
	static constexpr std::size_t MaxTimers = 8;

	/** Motivation: Inline bytes one timer callback's delegate storage may use. */
	static constexpr std::size_t InlineTimerCallbackBytes = 64;

	/** Motivation: Inline bytes one deferred actor factory may capture until the next world barrier. */
	static constexpr std::size_t InlineDeferredSpawnFactoryBytes = 64;
};

} // namespace MicroWorld::Engine
