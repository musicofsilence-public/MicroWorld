#pragma once

#include <MicroWorld/Core/RuntimeResult.h>

#include <cstdint>
#include <limits>

namespace MicroWorld::Engine
{

/** Motivation: Selects one caller-owned object-store slot without exposing its address. */
using ObjectIndex = std::uint32_t;

/** Motivation: Distinguishes every reusable lifetime published from one object-store slot. */
using ObjectGeneration = std::uint32_t;

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

/**
 * Motivation: Lets a caller carry one local managed-object lifetime as a stable slot-plus-generation pair without
 *   exposing a raw object address.
 * Responsibilities: Hold index and generation and never outlive or be transported across the store that issued them;
 *   a handle is local diagnostic identity, never a serialized or transport identity.
 * Example:
 *   FObjectHandle Handle;
 *   if (Handle.IsValid()) { Store.Destroy(Handle); }
 */
struct FObjectHandle
{
	/** Motivation: Reserves the maximum index as the only invalid slot representation. */
	static constexpr ObjectIndex InvalidIndex = std::numeric_limits<ObjectIndex>::max();

	/** Motivation: Selects a caller-owned store slot or InvalidIndex when no object is referenced. */
	ObjectIndex Index{InvalidIndex};

	/** Motivation: Distinguishes reused slot lifetimes; zero is never published for a live object. */
	ObjectGeneration Generation{0};

	/**
	 * Motivation: Lets a caller reject a default or stale value before consulting its owning store.
	 * Responsibilities: Report true only when the index and generation together look like a live published object.
	 */
	constexpr bool IsValid() const noexcept { return Index != InvalidIndex && Generation != 0; }
};

/**
 * Motivation: Lets containers compare two handles by complete local lifetime identity rather than a potentially reused slot.
 * Responsibilities: Return true only when both index and generation match.
 */
constexpr bool operator==(const FObjectHandle InLeft, const FObjectHandle InRight) noexcept
{
	return InLeft.Index == InRight.Index && InLeft.Generation == InRight.Generation;
}

/**
 * Motivation: Lets a caller tell two handles apart by complete local lifetime identity.
 * Responsibilities: Return true whenever the slot or generation identity differs.
 */
constexpr bool operator!=(const FObjectHandle InLeft, const FObjectHandle InRight) noexcept
{
	return !(InLeft == InRight);
}

/**
 * Motivation: Gives one local managed object a type-safe diagnostic identifier that never masquerades as a transport
 *   or serialized identity.
 * Responsibilities: Carry an application-defined diagnostic value only, with no wire semantics.
 * Example:
 *   FObjectId Id{0x1234u};
 */
struct FObjectId
{
	/** Motivation: Carries an application-defined diagnostic value without wire semantics. */
	std::uint32_t Value{0};
};

/**
 * Motivation: Confirms that one more live generation can be published without wrapping.
 * Responsibilities: Report whether the generation is below its maximum, so a store can retire a slot before wrapping
 *   would make an old handle valid again.
 */
constexpr bool CanAdvanceObjectGeneration(const ObjectGeneration InCurrentGeneration) noexcept
{
	return InCurrentGeneration < std::numeric_limits<ObjectGeneration>::max();
}

} // namespace MicroWorld::Engine
