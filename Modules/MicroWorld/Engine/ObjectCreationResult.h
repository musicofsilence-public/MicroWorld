#pragma once

#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Engine/ObjectResult.h>

namespace MicroWorld::Engine
{

/**
 * Motivation: Reports placement construction success or one explicit bounded-store failure alongside the published
 *   object.
 * Responsibilities: Distinguish the outcome and resolve the object only when Result is Success.
 * Example:
 *   TObjectCreationResult<AActor> R = Store.NewObject<AActor>(Args);
 *   if (R.Result == EObjectResult::Success) { R.Object.Get()->Tick(); }
 */
template<typename T>
struct TObjectCreationResult
{
	/** Motivation: Distinguishes capacity, layout, class, generation, and successful outcomes. */
	EObjectResult Result{EObjectResult::CapacityExceeded};

	/** Motivation: Resolves the newly published object only when Result is Success. */
	TObjectPtr<T> Object{};
};

} // namespace MicroWorld::Engine
