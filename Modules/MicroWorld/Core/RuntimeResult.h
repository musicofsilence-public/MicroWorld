#pragma once

#include <cstdint>

namespace MicroWorld
{

/**
 * Reports framework outcomes without exceptions or platform logging.
 *
 * Core owns this shared vocabulary, but several values are raised only by higher
 * layers: Object and Engine registration produce `Duplicate`, `AlreadyOwned`,
 * and the lifecycle-locked results. It lives in its own header so a low-level
 * type such as `TStaticVector` can report outcomes without depending on the tick
 * and time types in `Time.h`.
 *
 * Cross-layer lifecycle and tick outcomes speak `ERuntimeResult`; the object
 * store and its handles speak `EObjectResult` (`ObjectHandle.h`). The two overlap
 * by design and are deliberately not merged.
 */
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

} // namespace MicroWorld
