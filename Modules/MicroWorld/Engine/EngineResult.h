#pragma once

#include <cstdint>

namespace MicroWorld
{

/**
 * Reports registration outcomes for the managed engine.
 *
 * Engine lifecycle entry points reuse Core's ERuntimeResult so that BeginPlay,
 * Advance, and EndPlay failures stay comparable across the released Core
 * contract and the managed engine. Registration entry points face two failure
 * modes that ERuntimeResult cannot honestly express: a managed reference that
 * belongs to a different FObjectStore, and an empty, stale, or non-resolvable
 * managed reference. EEngineResult adds CrossStore and InvalidReference so
 * registration never collapses distinct failures into an ambiguous result.
 */
enum class EEngineResult : std::uint8_t
{
	Success,		  ///< Lets registration use one explicit success channel.
	Duplicate,		  ///< Rejects a managed object already registered with this owner.
	CapacityExceeded, ///< Keeps fixed-capacity registration failure observable, including zero capacity.
	LifecycleLocked,  ///< Prevents registration after BeginPlay can begin dispatch.
	AlreadyOwned,	  ///< Prevents one managed object from entering two owner relationships.
	CrossStore,		  ///< Rejects a managed reference that belongs to a different FObjectStore.
	InvalidReference, ///< Rejects an empty, stale, or non-resolvable managed reference.
};

} // namespace MicroWorld
