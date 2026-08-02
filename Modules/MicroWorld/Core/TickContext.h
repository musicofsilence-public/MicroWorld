#pragma once

#include <MicroWorld/Core/Time.h>

namespace MicroWorld::Core
{

/**
 * Motivation: Carries the canonical dispatcher time for one executed tick.
 * Responsibilities: Hold the caller's canonical time and the elapsed delta for this schedule.
 * Example:
 *   FTickContext Context;
 *   Context.NowMilliseconds = Now;
 */
struct FTickContext
{
	/** Motivation: Preserves the caller's canonical time so hooks never sample another clock. */
	TimePointMilliseconds NowMilliseconds{0};

	/** Motivation: Reports elapsed time for this schedule, independent of other tickables. */
	DurationMilliseconds DeltaMilliseconds{0};
};

} // namespace MicroWorld::Core
