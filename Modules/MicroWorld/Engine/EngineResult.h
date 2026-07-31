#pragma once

#include <MicroWorld/Core/RuntimeResult.h>

#include <cstdint>

namespace MicroWorld::Engine
{

/**
 * Motivation: Lets registration entry points report failures that Core's ERuntimeResult cannot honestly express, so a
 *   cross-store reference and an empty, stale, or non-resolvable managed reference never collapse into an ambiguous result.
 * Responsibilities: Distinguish success, duplicate, capacity, lifecycle lock, double ownership, cross-store, and bad
 *   reference outcomes while staying comparable to ERuntimeResult across BeginPlay, Advance, and EndPlay.
 * Example:
 *   if (Engine.Register(Object) == EEngineResult::CrossStore) { RejectObject(); }
 */
enum class EEngineResult : std::uint8_t
{
	Success,		  ///< Motivation: Lets registration use one explicit success channel.
	Duplicate,		  ///< Motivation: Rejects a managed object already registered with this owner.
	CapacityExceeded, ///< Motivation: Keeps fixed-capacity registration failure observable, including zero capacity.
	LifecycleLocked,  ///< Motivation: Prevents registration after BeginPlay can begin dispatch.
	AlreadyOwned,	  ///< Motivation: Prevents one managed object from entering two owner relationships.
	CrossStore,		  ///< Motivation: Rejects a managed reference that belongs to a different FObjectStore.
	InvalidReference, ///< Motivation: Rejects an empty, stale, or non-resolvable managed reference.
};

} // namespace MicroWorld::Engine
