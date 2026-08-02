#pragma once

#include <cstdint>

namespace MicroWorld::Core
{

/**
 * Motivation: Gives every bounded timer operation one result vocabulary that does not borrow unrelated
 *   lifecycle errors.
 * Responsibilities: Distinguish success from capacity, callback, handle, mode, dispatch, and time-rollback failures.
 * Example:
 *   if (Manager.Cancel(Handle) == ETimerResult::StaleHandle) { IgnoreStale(); }
 */
enum class ETimerResult : std::uint8_t
{
	/** Motivation: Confirms that the requested timer operation completed. */
	Success,

	/** Motivation: Reports that no reusable timer slot remains, including zero capacity and retired generations. */
	CapacityExceeded,

	/** Motivation: Rejects an unbound delegate before any slot is consumed or callback ownership moves. */
	InvalidCallback,

	/** Motivation: Rejects a default, sentinel, or out-of-range handle before consulting slot state. */
	InvalidHandle,

	/** Motivation: Rejects a handle whose slot is free, retired, removed, expired, or holds another generation. */
	StaleHandle,

	/** Motivation: Rejects a timer mode that is neither OneShot nor Looping. */
	InvalidMode,

	/** Motivation: Prevents Schedule, Cancel, and nested Advance from mutating an active dispatch. */
	DispatchLocked,

	/** Motivation: Prevents unsigned time arithmetic from accepting a rolled-back caller clock. */
	NonMonotonicTime,
};

} // namespace MicroWorld::Core
