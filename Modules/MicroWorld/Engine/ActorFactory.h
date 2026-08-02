#pragma once

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/ClassRegistryRegistrationView.h>
#include <MicroWorld/Engine/DeferredActorSpawnCaptureTraits.h>
#include <MicroWorld/Engine/EngineClassIds.h>
#include <MicroWorld/Engine/Object.h>
#include <MicroWorld/Engine/ObjectCreationResult.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Engine/ObjectResult.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace MicroWorld::Engine
{

/**
 * Motivation: Owns decayed constructor values until a safe world barrier performs managed construction, so a caller can
 *   defer an actor spawn without holding its arguments on the call stack.
 * Responsibilities: Capture arguments once after preflight, construct exactly once at the barrier through the registered
 *   descriptor, trace direct managed captures for collection, and resolve or register the actor's descriptor.
 * Example:
 *   using F = TActorFactory<AMyActor, int>;
 *   F Factory{42};
 *   auto Result = F::Invoke(&Factory, Store, Descriptor);
 */
template<typename TActor, typename... TArguments>
class TActorFactory final
{
public:
	/**
	 * Motivation: Captures decayed constructor values only after every non-mutating queue preflight has passed.
	 * Responsibilities: Move each argument into the retained tuple.
	 */
	explicit TActorFactory(TArguments... InArguments) noexcept : Arguments(std::move(InArguments)...) {}

	/**
	 * Motivation: Uses the canonical registered descriptor to construct the actor in the world's store.
	 * Responsibilities: Forward to InvokeWithArguments with the retained tuple expanded.
	 */
	static TObjectCreationResult<AActor> Invoke(void* const InFactory, FObjectStore& InStore, const FClassDescriptor& InDescriptor) noexcept
	{
		TActorFactory& Factory = *static_cast<TActorFactory*>(InFactory);
		return InvokeWithArguments(Factory, InStore, InDescriptor, std::index_sequence_for<TArguments...>{});
	}

	/**
	 * Motivation: Destroys moved constructor values exactly once after construction or terminal failure.
	 * Responsibilities: Invoke the destructor on the placement-constructed factory.
	 */
	static void Destroy(void* const InFactory) noexcept { static_cast<TActorFactory*>(InFactory)->~TActorFactory(); }

	/**
	 * Motivation: Traces direct managed pointer captures while the queued factory owns them.
	 * Responsibilities: Visit each captured argument through its capture traits.
	 */
	static void VisitReferences(const void* const InFactory, FReferenceCollector& InCollector) noexcept
	{
		const TActorFactory& Factory = *static_cast<const TActorFactory*>(InFactory);
		std::apply(
			[&InCollector](const TArguments&... InCapturedArguments) noexcept
			{ (TDeferredActorSpawnCaptureTraits<TArguments>::Visit(InCapturedArguments, InCollector), ...); },
			Factory.Arguments);
	}

	/**
	 * Motivation: Reuses a manually registered descriptor or registers a direct AActor child with a local automatic ID.
	 * Responsibilities: Find an existing descriptor by type token, else register a direct child of AActor and return the
	 *   stable owned address.
	 */
	static EObjectResult ResolveDescriptor(const FClassRegistryRegistrationView InClasses, const FClassDescriptor*& OutDescriptor) noexcept
	{
		OutDescriptor = InClasses.FindByTypeToken(ManagedObjectTypeToken<TActor>());
		if (OutDescriptor != nullptr)
		{
			return EObjectResult::Success;
		}
		const FClassDescriptor* const ActorDescriptor = InClasses.Find(AActorClassId);
		if (ActorDescriptor == nullptr)
		{
			return EObjectResult::UnknownClass;
		}
		const FClassDescriptor Candidate = MakeClassDescriptor<TActor>(0, "DeferredActor", ActorDescriptor, &TraceManagedObjectReferences);
		return InClasses.RegisterAutomatic(Candidate, OutDescriptor);
	}

private:
	/**
	 * Motivation: Expands the retained tuple at the only safe managed-construction point.
	 * Responsibilities: Move each argument into the store's NewObject and return the creation result as an AActor.
	 */
	template<std::size_t... Indices>
	static TObjectCreationResult<AActor> InvokeWithArguments(
		TActorFactory& InFactory, FObjectStore& InStore, const FClassDescriptor& InDescriptor, std::index_sequence<Indices...>) noexcept
	{
		const TObjectCreationResult<TActor> Creation = InStore.NewObject<TActor>(InDescriptor, std::move(std::get<Indices>(InFactory.Arguments))...);
		return TObjectCreationResult<AActor>{Creation.Result, TObjectPtr<AActor>{Creation.Object}};
	}

	/** Motivation: Holds decayed constructor values across the caller callback and barrier boundary. */
	std::tuple<TArguments...> Arguments;
};

} // namespace MicroWorld::Engine
