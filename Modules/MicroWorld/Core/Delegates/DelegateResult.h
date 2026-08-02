#pragma once

#include <cstdint>

namespace MicroWorld::Core
{

/**
 * Motivation: Gives every bounded delegate operation one result vocabulary that does not borrow
 *   unrelated lifecycle errors.
 * Responsibilities: Distinguish success from capacity, callable-fit, handle, and dispatch conflicts.
 * Example:
 *   EDelegateResult Result = Delegate.Execute();
 *   if (Result != EDelegateResult::Success) { Recover(); }
 */
enum class EDelegateResult : std::uint8_t
{
	/** Motivation: Confirms that the requested delegate operation completed. */
	Success,

	/** Motivation: Reports that no reusable multicast slot remains. */
	CapacityExceeded,

	/** Motivation: Rejects a callable whose object representation exceeds the declared inline capacity. */
	CallableTooLarge,

	/** Motivation: Rejects a callable whose alignment exceeds the delegate's inline storage guarantee. */
	CallableAlignmentUnsupported,

	/** Motivation: Rejects an unbound delegate or a structurally invalid handle. */
	InvalidHandle,

	/** Motivation: Rejects a handle whose binding was removed or whose slot generation has changed. */
	StaleHandle,

	/** Motivation: Prevents mutation or nested dispatch from changing an active broadcast iteration. */
	BroadcastLocked,
};

} // namespace MicroWorld::Core
