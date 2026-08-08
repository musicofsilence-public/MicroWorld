#pragma once

#include <cstdint>

namespace MicroWorld::Core
{

/**
 * Motivation: Lets the framework report outcomes without exceptions or platform logging.
 * Responsibilities: Hold one shared cross-layer result vocabulary, distinct from the object-store
 *   EObjectResult by design, kept in its own header so a low-level type can report outcomes without
 *   depending on the tick and time types in Time.h.
 * Example:
 *   ERuntimeResult Result = Vector.Add(Value);
 *   if (Result == ERuntimeResult::Success) { Continue(); }
 */
enum class ERuntimeResult : std::uint8_t
{
	Success,			  ///< Motivation: Lets callers use one explicit success/failure channel.
	Duplicate,			  ///< Motivation: Protects deterministic registration from repeated entries.
	CapacityExceeded,	  ///< Motivation: Keeps fixed storage failure observable instead of allocating.
	LifecycleLocked,	  ///< Motivation: Prevents structural mutation after dispatch can begin.
	InvalidLifecycle,	  ///< Motivation: Rejects hooks outside their forward-only lifecycle.
	CannotEverTick,		  ///< Motivation: Preserves construction-time capability as an invariant.
	NonMonotonicTime,	  ///< Motivation: Prevents unsigned time arithmetic from accepting rollback.
	AlreadyOwned,		  ///< Motivation: Prevents one object from entering two non-owning hierarchies.
	InitializationFailed, ///< Motivation: Reports an aborted required startup batch after pre-play construction cannot complete.
};

} // namespace MicroWorld::Core
