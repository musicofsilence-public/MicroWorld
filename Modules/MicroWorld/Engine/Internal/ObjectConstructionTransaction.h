#pragma once

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ClassRegistryRegistrationView.h>
#include <MicroWorld/Engine/ObjectCreationResult.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Engine/ObjectResult.h>
#include <MicroWorld/Engine/ObjectStore.h>

#include <array>
#include <cstddef>

namespace MicroWorld::Engine
{

template<typename TActor, typename... TArguments>
class TActorFactory;

class FObjectInitializer;

class AActor;
class UActorComponent;

/**
 * Motivation: Keeps initializer-created actor graphs invisible until every required object exists and attaches safely.
 * Responsibilities: Reserve hidden slots, retain the first error, publish components before their actor, and restore every reservation if
 * construction cannot complete.
 *
 * Example: TActorFactory<FActor>::Invoke(Factory, Store, Classes, Descriptor);
 */
class FObjectConstructionTransaction final
{
public:
	/**
	 * Motivation: Guarantees that an incomplete graph never becomes externally resolvable.
	 * Responsibilities: Roll back uncommitted construction and restore the previous lock.
	 */
	~FObjectConstructionTransaction() noexcept;
	FObjectConstructionTransaction(const FObjectConstructionTransaction&) = delete;
	FObjectConstructionTransaction& operator=(const FObjectConstructionTransaction&) = delete;

private:
	template<typename TActor, typename... TArguments>
	friend class TActorFactory;
	friend class FObjectInitializer;

	/**
	 * Motivation: Acquires the private construction capability when the store is otherwise structurally idle.
	 * Responsibilities: Hold the store locked until commit or rollback for the owning actor factory.
	 */
	FObjectConstructionTransaction(FObjectStore& InStore, FClassRegistryRegistrationView InClasses) noexcept;

	/**
	 * Motivation: Constructs the provisional actor that owns this transaction's default components.
	 * Responsibilities: Attach and publish only after all constructor requests succeed.
	 */
	template<typename TActor, typename... TArguments>
	TObjectCreationResult<TActor> ConstructActor(const FClassDescriptor& InDescriptor, TArguments&&... Arguments) noexcept;
	/**
	 * Motivation: Lets an initializer-aware actor add one component while public store mutation remains locked.
	 * Responsibilities: Reserve, construct, and retain the component until the actor commits.
	 */
	template<typename TComponent, typename... TArguments>
	TObjectPtr<TComponent> CreateDefaultSubobject(TArguments&&... Arguments) noexcept;
	/**
	 * Motivation: Exposes the first sticky construction result to the owning factory.
	 * Responsibilities: Return Success only while the transaction has no error.
	 */
	EObjectResult Result() const noexcept { return FirstError; }

private:
	/**
	 * Motivation: Retains one hidden slot and its exact descriptor until publication or rollback.
	 * Responsibilities: Keep only private construction facts.
	 * Example: FReservedObject Reserved{};
	 */
	struct FReservedObject final
	{
		/** Motivation: Identifies the reserved slot or the invalid sentinel before reservation. */
		ObjectIndex SlotIndex{FObjectHandle::InvalidIndex};
		/** Motivation: Preserves the provisional identity used for parent ownership before public publication. */
		FObjectHandle Handle{};
		/** Motivation: Preserves exact validation and destruction behavior for rollback. */
		const FClassDescriptor* Descriptor{nullptr};
		/** Motivation: Points at the placement-constructed object while the slot remains hidden. */
		UObject* Object{nullptr};
	};

	FReservedObject Reserve(const FClassDescriptor& InDescriptor) noexcept;
	void Publish(FReservedObject& InReserved) noexcept;
	void Rollback() noexcept;
	void RecordError(EObjectResult InError) noexcept;

	/** Motivation: Owns the fixed object storage and publication authority for this one transaction. */
	FObjectStore& Store;
	/** Motivation: Resolves shared class descriptors without exposing registry mutation to the actor constructor. */
	FClassRegistryRegistrationView Classes;
	/** Motivation: Retains the one provisional actor so it is destroyed before all default components on rollback. */
	FReservedObject Actor{};
	/** Motivation: Bounds default components to the actor's existing fixed component capacity. */
	std::array<FReservedObject, AActor::MaxComponentsPerActor> Components{};
	/** Motivation: Counts leading constructed default components. */
	std::size_t ComponentCount{0};
	/** Motivation: Retains the first construction error so later requests cannot mutate the graph. */
	EObjectResult FirstError{EObjectResult::Success};
	/** Motivation: Distinguishes successful complete publication from required rollback. */
	bool bCommitted{false};
	/** Motivation: Restores a surrounding store mutation scope if construction was nested legally. */
	bool bPreviousMutationLock{false};
};

} // namespace MicroWorld::Engine
