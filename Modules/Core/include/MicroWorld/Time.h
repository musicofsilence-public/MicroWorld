#pragma once

#include <MicroWorld/RuntimeResult.h>

#include <cstdint>

namespace MicroWorld
{

/** Uses a wide monotonic domain so long-running consumers do not need wrap policy. */
using TimePointMilliseconds = std::uint64_t;

/** Bounds per-tick deltas while keeping the hot context compact on MCUs. */
using DurationMilliseconds = std::uint32_t;

/** Carries canonical dispatcher time for one executed tick. */
struct FTickContext
{
	/** Preserves the caller's canonical time so hooks never sample another clock. */
	TimePointMilliseconds NowMilliseconds{0};

	/** Reports elapsed time for this schedule, independent of other tickables. */
	DurationMilliseconds DeltaMilliseconds{0};
};

/** Combines tick eligibility, timing, and any dispatcher error. */
struct FTickDecision
{
	/** Keeps scheduling errors separate from consumer tick behavior. */
	ERuntimeResult Result{ERuntimeResult::Success};

	/** Avoids invoking a hook when lifecycle, enablement, or cadence says not due. */
	bool bShouldTick{false};

	/** Carries time only for an accepted execution decision. */
	FTickContext Context{};

	/** Builds a decision that reports a scheduling error without a tick. */
	static FTickDecision Rejected(ERuntimeResult Result) noexcept
	{
		FTickDecision Decision;
		Decision.Result = Result;
		return Decision;
	}

	/** Builds an accepted decision whose cadence says no tick is due. */
	static FTickDecision NotDue() noexcept { return FTickDecision{}; }

	/** Builds an accepted due-tick decision carrying its canonical time and delta. */
	static FTickDecision Ticked(TimePointMilliseconds NowMilliseconds, DurationMilliseconds DeltaMilliseconds) noexcept
	{
		FTickDecision Decision;
		Decision.bShouldTick = true;
		Decision.Context.NowMilliseconds = NowMilliseconds;
		Decision.Context.DeltaMilliseconds = DeltaMilliseconds;
		return Decision;
	}
};

} // namespace MicroWorld
