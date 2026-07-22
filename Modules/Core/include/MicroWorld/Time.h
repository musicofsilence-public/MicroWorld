#pragma once

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

/** Reports framework outcomes without exceptions or platform logging. */
enum class ERuntimeResult : std::uint8_t
{
	Success,		  ///< Lets callers use one explicit success/failure channel.
	Duplicate,		  ///< Protects deterministic registration from repeated entries.
	CapacityExceeded, ///< Keeps fixed storage failure observable instead of allocating.
	LifecycleLocked,  ///< Prevents structural mutation after dispatch can begin.
	InvalidLifecycle, ///< Rejects hooks outside their forward-only lifecycle.
	CannotEverTick,	  ///< Preserves construction-time capability as an invariant.
	NonMonotonicTime, ///< Prevents unsigned time arithmetic from accepting rollback.
	AlreadyOwned,	  ///< Prevents one object from entering two non-owning hierarchies.
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
};

} // namespace MicroWorld
