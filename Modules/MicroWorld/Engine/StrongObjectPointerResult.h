#pragma once

#include <MicroWorld/Engine/ObjectResult.h>
#include <MicroWorld/Engine/StrongObjectPtr.h>

namespace MicroWorld::Engine
{

/**
 * Motivation: Couples explicit root-registration failure with optional strong ownership in one return value.
 * Responsibilities: Report the registration outcome and own a strong pointer only when Result is Success.
 * Example:
 *   auto Outcome = Store.AcquireRoot(Handle);
 *   if (Outcome.Result == EObjectResult::Success) { Keep(Outcome.Pointer); }
 */
template<typename T>
struct TStrongObjectPointerResult
{
	/** Motivation: Reports root capacity, stale identity, pending destruction, or success. */
	EObjectResult Result{EObjectResult::RootCapacityExceeded};

	/** Motivation: Owns one root token only when Result is Success. */
	TStrongObjectPtr<T> Pointer{};
};

} // namespace MicroWorld::Engine
