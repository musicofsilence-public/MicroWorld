#pragma once

#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Engine/ReferenceCollector.h>

namespace MicroWorld::Engine
{

/**
 * Motivation: Lets custom deferred factory capture wrappers retain managed references during collection without tying
 *   the storage to each capture type.
 * Responsibilities: Visit a capture's references for the active collector; the default owns none.
 * Example:
 *   TDeferredActorSpawnCaptureTraits<int>::Visit(Value, Collector);
 */
template<typename T>
struct TDeferredActorSpawnCaptureTraits
{
	/**
	 * Motivation: Confirms default captures do not carry traced managed references.
	 * Responsibilities: Do nothing for a capture that owns no managed references.
	 */
	static void Visit(const T&, FReferenceCollector&) noexcept {}
};

/**
 * Motivation: Retains a directly captured managed pointer until its queued factory executes or fails.
 * Responsibilities: Present the captured reference to the active collector so it stays reachable while queued.
 * Example:
 *   TDeferredActorSpawnCaptureTraits<TObjectPtr<UActorComponent>>::Visit(Comp, Collector);
 */
template<typename T>
struct TDeferredActorSpawnCaptureTraits<TObjectPtr<T>>
{
	/**
	 * Motivation: Presents the direct captured reference to the active collector.
	 * Responsibilities: Add the captured reference to the collector.
	 */
	static void Visit(const TObjectPtr<T>& InReference, FReferenceCollector& InCollector) noexcept { InCollector.AddReferencedObject(InReference); }
};

} // namespace MicroWorld::Engine
