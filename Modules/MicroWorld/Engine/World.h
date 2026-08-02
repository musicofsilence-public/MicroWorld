#pragma once

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/DeferredActorSpawn.h>
#include <MicroWorld/Engine/EngineRegistryView.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Core/LifecycleGuard.h>
#include <MicroWorld/Engine/Object.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Core/Time.h>

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace MicroWorld::Engine
{

class AActor;
struct FClassDescriptor;
class FObjectStore;
class FReferenceCollector;

/**
 * Motivation: Provides the smallest managed world anchored on UObject, giving an application one owner for the actors
 *   and deferred spawns in a scene.
 * Responsibilities: Hold the caller-owned actor registry and optional typed spawn storage, trace its actors for
 *   collection without ticking on its own, and drive a forward-only BeginPlay/Advance/EndPlay lifecycle with a single
 *   per-frame structural-change barrier.
 * Example:
 *   UWorld& World = *Store.NewObject<UWorld>(RegistryRef).Object.Get();
 *   (void)World.RegisterActor(Actor);
 *   (void)World.BeginPlay(Now); World.Advance(Now + 16);
 */
class UWorld : public UObject
{
public:
	/**
	 * Motivation: Prevents copying or moving from duplicating a managed object's slot identity.
	 * Responsibilities: Reject copy construction so each world lives and dies in one store slot.
	 */
	UWorld(const UWorld&) = delete;

	/**
	 * Motivation: Prevents copy assignment from duplicating a managed object's slot identity.
	 * Responsibilities: Reject copy assignment so each world keeps one slot identity.
	 */
	UWorld& operator=(const UWorld&) = delete;

	/**
	 * Motivation: Prevents moving a managed object away from its stable slot.
	 * Responsibilities: Reject move construction so each world keeps one slot identity.
	 */
	UWorld(UWorld&&) = delete;

	/**
	 * Motivation: Prevents moving another identity into this managed object's stable slot.
	 * Responsibilities: Reject move assignment so each world keeps one slot identity.
	 */
	UWorld& operator=(UWorld&&) = delete;

	/**
	 * Motivation: Returns the stable descriptor that lets the store construct and trace this type.
	 * Responsibilities: Return the canonical UWorld class descriptor registered into the registry.
	 */
	static const FClassDescriptor& StaticClassDescriptor() noexcept;

	/**
	 * Motivation: Binds this world to the unique caller-owned actor registry reference that will hold its registered
	 *   actors.
	 * Responsibilities: Take the move-only registry reference; the store assigns canonical ownership after publish, so
	 *   callers cannot supply a second store identity.
	 */
	explicit UWorld(FWorldActorRegistryReference InActorStorage) noexcept;

	/**
	 * Motivation: Binds optional caller-owned typed spawn storage and a narrow canonical descriptor capability.
	 * Responsibilities: Take the registry reference, deferred spawn storage, and class registration view together.
	 */
	UWorld(
		FWorldActorRegistryReference InActorStorage,
		FDeferredActorSpawnStorageReference InSpawnStorage,
		FClassRegistryRegistrationView InClasses) noexcept;

	/**
	 * Motivation: Keeps exact derived destruction behind the descriptor/store boundary.
	 * Responsibilities: Override the destructor so the registered exact destructor runs derived teardown.
	 */
	~UWorld() noexcept override;

	/**
	 * Motivation: Lets a caller register one actor before BeginPlay.
	 * Responsibilities: Reject duplicates, exhausted or zero capacity, a lifecycle-locked world, an actor already owned
	 *   by another world, a cross-store actor, and an empty, stale, or non-resolvable reference atomically, leaving the
	 *   world and actor unchanged on rejection.
	 */
	EEngineResult RegisterActor(TObjectPtr<AActor> InActor) noexcept;

	/**
	 * Motivation: Starts registered actors, then pre-play queued actors, from one canonical time.
	 * Responsibilities: Move the world lifecycle forward and begin every registered and queued actor in order.
	 */
	Core::ERuntimeResult BeginPlay(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/**
	 * Motivation: Advances every registered actor once after validating monotonic world time.
	 * Responsibilities: Reject a rolled-back clock, then advance every registered actor for the given time.
	 */
	Core::ERuntimeResult Advance(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/**
	 * Motivation: Ends every registered actor in reverse registration order; idempotent after success.
	 * Responsibilities: End actors in reverse and stay idempotent after a successful first call.
	 */
	Core::ERuntimeResult EndPlay() noexcept;

	/**
	 * Motivation: Lets a caller queue one constructed, same-store, unowned actor to begin at the next barrier while the
	 *   world is playing.
	 * Responsibilities: Reject a non-playing world, empty, stale, or cross-store references, an actor already registered
	 *   or already pending-spawn, an actor owned by another world, and exhausted live-plus-pending capacity,
	 *   transactionally.
	 */
	EEngineResult SpawnActor(TObjectPtr<AActor> InActor) noexcept;

	/**
	 * Motivation: Lets a caller capture a typed actor factory for safe construction at the next world barrier without
	 *   moving caller arguments until preflight succeeds.
	 * Responsibilities: Reject lifecycle, collection, capacity, and inline-layout failures before capturing, so a
	 *   request accepted before BeginPlay is constructed and begun by BeginPlay and one accepted during play waits for
	 *   the next frame barrier.
	 */
	template<typename TActor, typename... TArguments>
	[[nodiscard]] FActorSpawnRequest SpawnActor(TArguments&&... InArguments) noexcept
	{
		static_assert(std::is_base_of<AActor, TActor>::value, "Deferred SpawnActor requires an AActor-derived type.");
		using TFactory = TActorFactory<TActor, std::decay_t<TArguments>...>;
		static_assert(std::is_nothrow_constructible<TActor, std::decay_t<TArguments>...>::value, "Deferred actor construction must be noexcept.");
		static_assert(std::is_nothrow_constructible<TFactory, TArguments...>::value, "Deferred actor factory capture must be noexcept.");

		const EActorSpawnRequestResult PreflightResult = CheckDeferredSpawnRequest();
		if (PreflightResult != EActorSpawnRequestResult::Queued)
		{
			return FActorSpawnRequest{PreflightResult, {}};
		}
		if (sizeof(TFactory) > DeferredActorSpawnInlineBytes())
		{
			return FActorSpawnRequest{EActorSpawnRequestResult::FactoryTooLarge, {}};
		}
		// The else branch is load-bearing: an over-aligned factory takes the return above, which
		// leaves everything after it unreachable for that instantiation. Discarding the branch
		// instead of instantiating dead statements is what keeps the warning from firing.
		if constexpr (alignof(TFactory) > alignof(std::max_align_t))
		{
			return FActorSpawnRequest{EActorSpawnRequestResult::FactoryAlignmentUnsupported, {}};
		}
		else
		{
			const FActorSpawnHandle SpawnHandle = DeferredSpawns.Reserve();
			void* const FactoryStorage = DeferredSpawns.GetFactoryStorage(SpawnHandle);
			if (FactoryStorage == nullptr)
			{
				return FActorSpawnRequest{EActorSpawnRequestResult::CapacityExceeded, {}};
			}
			::new (FactoryStorage) TFactory(std::forward<TArguments>(InArguments)...);
			DeferredSpawns.Activate(
				SpawnHandle,
				FFactoryOperations{
					&TFactory::Invoke,
					&TFactory::Destroy,
					&TFactory::VisitReferences,
					&TFactory::ResolveDescriptor,
				});
			return FActorSpawnRequest{EActorSpawnRequestResult::Queued, SpawnHandle};
		}
	}

	/**
	 * Motivation: Lets a caller read deferred typed-spawn completion state without exposing storage internals.
	 * Responsibilities: Return the public status for the generation-checked handle.
	 */
	[[nodiscard]] FActorSpawnStatus GetSpawnStatus(FActorSpawnHandle InHandle) const noexcept;

	/**
	 * Motivation: Lets a caller queue one actor registered with this world to end and release at the next barrier while
	 *   the world is playing.
	 * Responsibilities: Reject a non-playing world, empty, stale, or cross-store references, an actor not registered
	 *   with this world, and an actor already pending-destroy, transactionally.
	 */
	EEngineResult DestroyActor(TObjectPtr<AActor> InActor) noexcept;

	/**
	 * Motivation: Applies pending destroys first, then pending spawns, so structural change happens only at this barrier
	 *   once per frame after Advance.
	 * Responsibilities: Apply every queued change and return the first end or begin failure while still applying the rest.
	 */
	Core::ERuntimeResult ApplyPending(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/**
	 * Motivation: Lets a caller report how many actors are queued to begin at the next barrier.
	 * Responsibilities: Return the pending-spawn count.
	 */
	std::size_t PendingSpawnCount() const noexcept;

	/**
	 * Motivation: Lets a caller report how many actors are queued to end and release at the next barrier.
	 * Responsibilities: Return the pending-destroy count.
	 */
	std::size_t PendingDestroyCount() const noexcept;

private:
	/**
	 * Motivation: Reports whether the live registry still has room for one more actor at the next barrier.
	 * Responsibilities: Return true when registered plus pending-spawn count is below capacity.
	 */
	bool CanAcceptMoreActors() const noexcept;

	/**
	 * Motivation: Reports whether spawn or destroy work remains queued for the next barrier.
	 * Responsibilities: Return true when either pending list is non-empty.
	 */
	bool HasPendingBarrierWork() const noexcept;

	/**
	 * Motivation: Reports the first reason an actor cannot register before any world or actor mutation.
	 * Responsibilities: Return Success or the first rejection reason for the candidate actor.
	 */
	EEngineResult CheckActorRegistrable(TObjectPtr<AActor> InActor) const noexcept;

	/**
	 * Motivation: Reports the first reason a deferred spawn cannot be queued.
	 * Responsibilities: Return Success or the first rejection reason for the candidate spawn.
	 */
	EEngineResult CheckSpawnable(TObjectPtr<AActor> InActor) const noexcept;

	/**
	 * Motivation: Reports the first reason a registered actor cannot be queued for destroy.
	 * Responsibilities: Return Success or the first rejection reason for the candidate destroy.
	 */
	EEngineResult CheckDestroyable(TObjectPtr<AActor> InActor) const noexcept;

	/**
	 * Motivation: Links an actor to this world and adds it to the registry after all checks pass.
	 * Responsibilities: Assign world ownership and append the actor to the registry.
	 */
	void PublishActor(TObjectPtr<AActor> InActor) noexcept;

	/**
	 * Motivation: Begins one actor's lifecycle while letting the world roll back on failure.
	 * Responsibilities: Begin the actor and propagate its result for rollback decisions.
	 */
	Core::ERuntimeResult DispatchActorBegin(AActor& InActor, Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/**
	 * Motivation: Advances one actor for one dispatcher step.
	 * Responsibilities: Forward the actor's advance and return its result.
	 */
	Core::ERuntimeResult DispatchActorAdvance(AActor& InActor, Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/**
	 * Motivation: Ends one actor while the world retains the first error and still ends every actor.
	 * Responsibilities: Forward the actor's end and return its result.
	 */
	Core::ERuntimeResult DispatchActorEnd(AActor& InActor) noexcept;

	/**
	 * Motivation: Begins every registered actor in order and, on the first failure, ends the already-begun actors in
	 *   reverse so the world lifecycle fails atomically.
	 * Responsibilities: Begin actors forward and roll back on the first failure.
	 */
	Core::ERuntimeResult BeginRegisteredActorsWithRollback(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/**
	 * Motivation: Ends every registered actor in reverse order, retaining the first error while still ending every actor.
	 * Responsibilities: End actors in reverse and fold the first error out.
	 */
	Core::ERuntimeResult EndRegisteredActorsReverse() noexcept;

	/**
	 * Motivation: Ends every doomed actor under the dispatch guard and folds the first end failure into FirstError.
	 * Responsibilities: Acquire the guard, end doomed actors in reverse, and return LifecycleLocked only when the guard
	 *   cannot be acquired.
	 */
	Core::ERuntimeResult EndDoomedActorsUnderGuard(FObjectStore& InObjectStore, Core::ERuntimeResult& InOutFirstError) noexcept;

	/**
	 * Motivation: Marks each doomed actor's components and itself for the destruction barrier and removes it from the
	 *   live set after the dispatch guard has released.
	 * Responsibilities: Mark doomed actors and their components pending destroy and unregister them.
	 */
	void MarkAndUnregisterDoomedActors(FObjectStore& InObjectStore) noexcept;

	/**
	 * Motivation: Begins every pending-spawn actor under a fresh dispatch guard and folds the first begin failure into
	 *   FirstError.
	 * Responsibilities: Acquire the guard, begin pending spawns in FIFO order, and return LifecycleLocked only when the
	 *   guard cannot be acquired.
	 */
	Core::ERuntimeResult BeginPendingSpawnsUnderGuard(
		FObjectStore& InObjectStore, Core::TimePointMilliseconds InNowMilliseconds, Core::ERuntimeResult& InOutFirstError) noexcept;

	/**
	 * Motivation: Reports typed factory admission failure without moving caller constructor arguments.
	 * Responsibilities: Return the first preflight reason or Queued.
	 */
	EActorSpawnRequestResult CheckDeferredSpawnRequest() const noexcept;

	/**
	 * Motivation: Returns this World's caller-selected inline factory extent for template layout preflight.
	 * Responsibilities: Return the configured inline factory bytes or zero when unconfigured.
	 */
	std::size_t DeferredActorSpawnInlineBytes() const noexcept;

	/**
	 * Motivation: Constructs the immutable factory snapshot only while no collection owns store traversal.
	 * Responsibilities: Seal the barrier and construct each queued factory at the safe point.
	 */
	void ConstructDeferredSpawns(FObjectStore& InObjectStore) noexcept;

	/**
	 * Motivation: Publishes retained deferred actors under one fresh dispatch guard in FIFO order.
	 * Responsibilities: Acquire the guard, begin and publish each constructed deferred actor, and fold the first failure.
	 */
	Core::ERuntimeResult BeginDeferredSpawnsUnderGuard(
		FObjectStore& InObjectStore, Core::TimePointMilliseconds InNowMilliseconds, Core::ERuntimeResult& InOutFirstError) noexcept;

	/**
	 * Motivation: Traces queued captures and temporarily unpublished constructed actors before world registry edges.
	 * Responsibilities: Forward deferred-spawn reference tracing to the collector.
	 */
	void VisitDeferredSpawnReferences(FReferenceCollector& InCollector) noexcept;

	/**
	 * Motivation: Presents every registered actor to the active iterative collector.
	 * Responsibilities: Add each registered actor reference to the collector.
	 */
	void VisitReferences(FReferenceCollector& InCollector) noexcept override;

	/** Motivation: Holds the unique caller-owned actor registry reference for this world's lifetime. */
	FWorldActorRegistryReference Actors;

	/** Motivation: Holds optional caller-owned factory storage; an empty capability preserves direct World contracts. */
	FDeferredActorSpawnStorageReference DeferredSpawns;

	/** Motivation: Supplies canonical actor descriptors without exposing the application registry to World. */
	FClassRegistryRegistrationView Classes;

	/** Motivation: Guards the forward-only world lifecycle without scattering boolean flags. */
	Core::FLifecycleGuard Lifecycle;

	/** Motivation: Caches the last observed dispatcher time so rollback stays observable. */
	Core::TimePointMilliseconds LastUpdateMilliseconds{0};
};

} // namespace MicroWorld::Engine
