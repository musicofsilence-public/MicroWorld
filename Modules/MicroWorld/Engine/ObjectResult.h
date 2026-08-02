#pragma once

#include <cstdint>

namespace MicroWorld::Engine
{

/**
 * Motivation: Lets every bounded managed-object operation report its outcome without exceptions, independent of the
 *   cross-layer lifecycle and tick vocabulary that ERuntimeResult carries.
 * Responsibilities: Distinguish success from capacity, layout, class, root-table, stale handle, lifecycle, descriptor,
 *   duplicate, and generation-exhaustion failures so distinct conditions never collapse into one result.
 * Example:
 *   if (Store.Spawn(Object) == EObjectResult::CapacityExceeded) { Stop(); }
 */
enum class EObjectResult : std::uint8_t
{
	/** Motivation: Confirms that the requested managed-object operation completed. */
	Success,

	/** Motivation: Reports that no reusable object slot remains in caller-owned storage. */
	CapacityExceeded,

	/** Motivation: Rejects an object whose size or alignment cannot fit the configured slots. */
	UnsupportedObjectLayout,

	/** Motivation: Rejects an unregistered type identifier or descriptor relationship. */
	UnknownClass,

	/** Motivation: Reports that the fixed caller-owned root table cannot accept another root. */
	RootCapacityExceeded,

	/** Motivation: Rejects a handle whose slot is unused, retired, or has another generation. */
	StaleHandle,

	/** Motivation: Makes repeated destruction requests observable and idempotent. */
	AlreadyPendingDestroy,

	/** Motivation: Rejects mutation while the owning runtime has locked the relevant barrier. */
	LifecycleLocked,

	/** Motivation: Rejects a malformed class descriptor before registry state changes. */
	InvalidClassDescriptor,

	/** Motivation: Rejects a type identifier already owned by the class registry. */
	DuplicateClass,

	/** Motivation: Reports permanent slot retirement before its generation could wrap. */
	GenerationExhausted,
};

} // namespace MicroWorld::Engine
