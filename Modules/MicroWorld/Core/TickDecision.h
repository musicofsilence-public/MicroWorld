#pragma once

#include <MicroWorld/Core/RuntimeResult.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Core/TickContext.h>

namespace MicroWorld::Core
{

/**
 * Motivation: Combines tick eligibility, timing, and any dispatcher error into one decision.
 * Responsibilities: Separate scheduling errors from consumer tick behavior and carry time only for an accepted tick.
 * Example:
 *   FTickDecision Decision = FTickDecision::Ticked(Now, 16);
 *   if (Decision.bShouldTick) { Tick(Decision.Context); }
 */
struct FTickDecision
{
	/** Motivation: Keeps scheduling errors separate from consumer tick behavior. */
	ERuntimeResult Result{ERuntimeResult::Success};

	/** Motivation: Avoids invoking a hook when lifecycle, enablement, or cadence says not due. */
	bool bShouldTick{false};

	/** Motivation: Carries time only for an accepted execution decision. */
	FTickContext Context{};

	/**
	 * Motivation: Builds a decision that reports a scheduling error without a tick.
	 * Responsibilities: Set the result and leave the tick suppressed.
	 */
	static FTickDecision Rejected(ERuntimeResult InResult) noexcept
	{
		FTickDecision Decision;
		Decision.Result = InResult;
		return Decision;
	}

	/**
	 * Motivation: Builds an accepted decision whose cadence says no tick is due.
	 * Responsibilities: Return a default decision with no tick and a success result.
	 */
	static FTickDecision NotDue() noexcept { return FTickDecision{}; }

	/**
	 * Motivation: Builds an accepted due-tick decision carrying its canonical time and delta.
	 * Responsibilities: Set the should-tick flag and populate the context with the supplied time and delta.
	 */
	static FTickDecision Ticked(TimePointMilliseconds InNowMilliseconds, DurationMilliseconds InDeltaMilliseconds) noexcept
	{
		FTickDecision Decision;
		Decision.bShouldTick = true;
		Decision.Context.NowMilliseconds = InNowMilliseconds;
		Decision.Context.DeltaMilliseconds = InDeltaMilliseconds;
		return Decision;
	}
};

} // namespace MicroWorld::Core
