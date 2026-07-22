#pragma once

#include <cstdint>
#include <limits>

namespace MicroWorld
{

/** Selects one caller-owned object-store slot without exposing its address. */
using ObjectIndex = std::uint32_t;

/** Distinguishes every reusable lifetime published from one object-store slot. */
using ObjectGeneration = std::uint32_t;

/** Reports every bounded managed-object operation outcome without exceptions. */
enum class EObjectResult : std::uint8_t
{
	/** Confirms that the requested managed-object operation completed. */
	Success,

	/** Reports that no reusable object slot remains in caller-owned storage. */
	CapacityExceeded,

	/** Rejects an object whose size or alignment cannot fit the configured slots. */
	UnsupportedObjectLayout,

	/** Rejects an unregistered type identifier or descriptor relationship. */
	UnknownClass,

	/** Reports that the fixed caller-owned root table cannot accept another root. */
	RootCapacityExceeded,

	/** Rejects a handle whose slot is unused, retired, or has another generation. */
	StaleHandle,

	/** Makes repeated destruction requests observable and idempotent. */
	AlreadyPendingDestroy,

	/** Rejects mutation while the owning runtime has locked the relevant barrier. */
	LifecycleLocked,

	/** Rejects a malformed class descriptor before registry state changes. */
	InvalidClassDescriptor,

	/** Rejects a type identifier already owned by the class registry. */
	DuplicateClass,

	/** Reports permanent slot retirement before its generation could wrap. */
	GenerationExhausted,
};

/** Identifies one local managed-object lifetime by stable slot and generation. */
struct FObjectHandle
{
	/** Reserves the maximum index as the only invalid slot representation. */
	static constexpr ObjectIndex InvalidIndex = std::numeric_limits<ObjectIndex>::max();

	/** Selects a caller-owned store slot or InvalidIndex when no object is referenced. */
	ObjectIndex Index{InvalidIndex};

	/** Distinguishes reused slot lifetimes; zero is never published for a live object. */
	ObjectGeneration Generation{0};

	/** Confirms that both fields can represent a published local object lifetime. */
	constexpr bool IsValid() const noexcept { return Index != InvalidIndex && Generation != 0; }
};

/** Compares complete local lifetime identity rather than a potentially reused slot. */
constexpr bool operator==(const FObjectHandle Left, const FObjectHandle Right) noexcept
{
	return Left.Index == Right.Index && Left.Generation == Right.Generation;
}

/** Distinguishes any slot or generation mismatch. */
constexpr bool operator!=(const FObjectHandle Left, const FObjectHandle Right) noexcept
{
	return !(Left == Right);
}

/**
 * Provides a type-safe local diagnostic identifier.
 *
 * This value and FObjectHandle are process-local and must never become Net or
 * serialized identities.
 */
struct FObjectId
{
	/** Carries an application-defined diagnostic value without wire semantics. */
	std::uint32_t Value{0};
};

/**
 * Confirms that one more live generation can be published without wrapping.
 *
 * A store permanently retires the slot when this query is false; wrapping a
 * generation and making an old handle valid again is forbidden.
 */
constexpr bool CanAdvanceObjectGeneration(const ObjectGeneration CurrentGeneration) noexcept
{
	return CurrentGeneration < std::numeric_limits<ObjectGeneration>::max();
}

} // namespace MicroWorld
