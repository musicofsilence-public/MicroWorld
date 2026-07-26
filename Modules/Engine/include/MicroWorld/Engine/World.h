#pragma once

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/DeferredActorSpawn.h>
#include <MicroWorld/Engine/EngineRegistryView.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Lifecycle.h>
#include <MicroWorld/Object/Object.h>
#include <MicroWorld/Object/ObjectPtr.h>
#include <MicroWorld/Time.h>

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace MicroWorld
{

class AActor;
struct FClassDescriptor;
class FObjectStore;
class FReferenceCollector;

/**
 * The smallest managed world anchored on UObject.
 *
 * The application creates one UWorld (or a user-derived class) inside an
 * FObjectStore, registers or queues zero or more AActor instances before
 * BeginPlay, then roots the world with one TStrongObjectPtr<UWorld>.
 * UWorld
 * traces its actors; it does not tick on its own.
 */
class UWorld : public UObject
{
public:
	/** Copying or moving would duplicate a managed object's slot identity; each
	 * lives and dies in one object-store slot. */
	UWorld(const UWorld&) = delete;
	UWorld& operator=(const UWorld&) = delete;
	UWorld(UWorld&&) = delete;
	UWorld& operator=(UWorld&&) = delete;

	/** Returns the stable descriptor that lets the store construct and trace this type. */
	static const FClassDescriptor& StaticClassDescriptor() noexcept;

	/**
	 * Binds this world to the unique caller-owned actor registry reference that will
	 * hold its registered actors.
	 *
	 * The object store assigns canonical ownership only after construction
	 * publishes this UObject, so callers cannot supply a second store identity.
	 */
	explicit UWorld(FWorldActorRegistryReference InActorStorage) noexcept;

	/** Binds optional caller-owned typed spawn storage and a narrow canonical descriptor capability. */
	UWorld(
		FWorldActorRegistryReference InActorStorage,
		FDeferredActorSpawnStorageReference InSpawnStorage,
		FClassRegistryRegistrationView InClasses) noexcept;

	/** Keeps exact derived destruction behind the descriptor/store boundary. */
	~UWorld() noexcept override;

	/**
	 * Registers one actor before BeginPlay.
	 *
	 * Rejects duplicates, exhausted or zero capacity, lifecycle-locked worlds,
	 * actors already owned by another world, cross-store actors, and empty,
	 * stale, or non-resolvable references atomically: a rejected registration
	 * leaves the world and the actor unchanged.
	 */
	EEngineResult RegisterActor(TObjectPtr<AActor> InActor) noexcept;

	/** Starts registered actors, then pre-play queued actors, from one canonical time. */
	ERuntimeResult BeginPlay(TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Advances every registered actor once after validating monotonic world time. */
	ERuntimeResult Advance(TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Ends every registered actor in reverse registration order; idempotent after success. */
	ERuntimeResult EndPlay() noexcept;

	/**
	 * Queues one constructed, same-store, unowned actor to begin at the next
	 * barrier while the world is playing.
	 *
	 * Rejects a non-playing world, empty/stale/cross-store references, actors
	 * already registered or already pending-spawn, actors owned by another world,
	 * and exhausted live-plus-pending capacity, all transactionally.
	 */
	EEngineResult SpawnActor(TObjectPtr<AActor> InActor) noexcept;

	/**
	 * Captures a typed actor factory for safe construction at the next world barrier.
	 *
	 * A request accepted before BeginPlay is
	 * constructed and begun by BeginPlay;
	 * a request accepted during play waits for the next frame barrier.
	 *
	 * No actor or argument
	 * capture is created until lifecycle, collection, capacity,
	 * and inline-layout preflight succeeds, so calls from actor callbacks are safe.

	 */
	template<typename TActor, typename... TArguments>
	[[nodiscard]] FActorSpawnRequest SpawnActor(TArguments&&... InArguments) noexcept
	{
		static_assert(std::is_base_of<AActor, TActor>::value, "Deferred SpawnActor requires an AActor-derived type.");
		using TFactory = DeferredActorSpawnDetail::TActorFactory<TActor, std::decay_t<TArguments>...>;
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
		if constexpr (alignof(TFactory) > alignof(std::max_align_t))
		{
			return FActorSpawnRequest{EActorSpawnRequestResult::FactoryAlignmentUnsupported, {}};
		}

		const FActorSpawnHandle SpawnHandle = DeferredSpawns.Reserve();
		void* const FactoryStorage = DeferredSpawns.GetFactoryStorage(SpawnHandle);
		if (FactoryStorage == nullptr)
		{
			return FActorSpawnRequest{EActorSpawnRequestResult::CapacityExceeded, {}};
		}
		::new (FactoryStorage) TFactory(std::forward<TArguments>(InArguments)...);
		DeferredSpawns.Activate(
			SpawnHandle,
			DeferredActorSpawnDetail::FFactoryOperations{
				&TFactory::Invoke,
				&TFactory::Destroy,
				&TFactory::VisitReferences,
				&TFactory::ResolveDescriptor,
			});
		return FActorSpawnRequest{EActorSpawnRequestResult::Queued, SpawnHandle};
	}

	/** Returns deferred typed-spawn completion state without exposing storage internals. */
	[[nodiscard]] FActorSpawnStatus GetSpawnStatus(FActorSpawnHandle InHandle) const noexcept;

	/**
	 * Queues one actor registered with this world to end and release at the next
	 * barrier while the world is playing.
	 *
	 * Rejects a non-playing world, empty/stale/cross-store references, actors not
	 * registered with this world, and actors already pending-destroy, all
	 * transactionally.
	 */
	EEngineResult DestroyActor(TObjectPtr<AActor> InActor) noexcept;

	/**
	 * Applies pending destroys first, then pending spawns; call once per frame
	 * after Advance so structural change happens only at this barrier. Returns the
	 * first end or begin failure while still applying every queued change.
	 */
	ERuntimeResult ApplyPending(TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Reports how many actors are queued to begin at the next barrier. */
	std::size_t PendingSpawnCount() const noexcept;

	/** Reports how many actors are queued to end and release at the next barrier. */
	std::size_t PendingDestroyCount() const noexcept;

private:
	/** Reports the first reason an actor cannot register, or Success. */
	EEngineResult CheckActorRegistrable(TObjectPtr<AActor> InActor) const noexcept;

	/** Reports the first reason a deferred spawn cannot be queued, or Success. */
	EEngineResult CheckSpawnable(TObjectPtr<AActor> InActor) const noexcept;

	/** Reports the first reason a registered actor cannot be queued for destroy, or Success. */
	EEngineResult CheckDestroyable(TObjectPtr<AActor> InActor) const noexcept;

	/** Links an actor to this world and adds it to the registry after all checks pass. */
	void PublishActor(TObjectPtr<AActor> InActor) noexcept;

	/** Begins one actor's lifecycle while letting the world roll back on failure. */
	ERuntimeResult DispatchActorBegin(AActor& InActor, TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Advances one actor for one dispatcher step. */
	ERuntimeResult DispatchActorAdvance(AActor& InActor, TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Ends one actor while the world retains the first error and still ends every actor. */
	ERuntimeResult DispatchActorEnd(AActor& InActor) noexcept;

	/** Begins every registered actor in order and, on the first failure, ends the
	 * already-begun actors in reverse and fails the world lifecycle. */
	ERuntimeResult BeginRegisteredActorsWithRollback(TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Ends every registered actor in reverse order, retaining the first error while still ending every actor. */
	ERuntimeResult EndRegisteredActorsReverse() noexcept;

	/** Ends every doomed actor under the dispatch guard and folds the first end
	 * failure into FirstError; returns LifecycleLocked only when the guard cannot
	 * be acquired. */
	ERuntimeResult EndDoomedActorsUnderGuard(FObjectStore& InObjectStore, ERuntimeResult& InOutFirstError) noexcept;

	/** Marks each doomed actor's components and itself for the destruction barrier
	 * and removes it from the live set, run after the dispatch guard has released. */
	void MarkAndUnregisterDoomedActors(FObjectStore& InObjectStore) noexcept;

	/** Begins every pending-spawn actor under a fresh dispatch guard and folds the
	 * first begin failure into FirstError; returns LifecycleLocked only when the
	 * guard cannot be acquired. */
	ERuntimeResult BeginPendingSpawnsUnderGuard(
		FObjectStore& InObjectStore, TimePointMilliseconds InNowMilliseconds, ERuntimeResult& InOutFirstError) noexcept;

	/** Reports typed factory admission failure without moving caller constructor arguments. */
	EActorSpawnRequestResult CheckDeferredSpawnRequest() const noexcept;

	/** Returns this World's caller-selected inline factory extent for template layout preflight. */
	std::size_t DeferredActorSpawnInlineBytes() const noexcept;

	/** Constructs the immutable factory snapshot only while no collection owns store traversal. */
	void ConstructDeferredSpawns(FObjectStore& InObjectStore) noexcept;

	/** Publishes retained deferred actors under one fresh dispatch guard in FIFO order. */
	ERuntimeResult BeginDeferredSpawnsUnderGuard(
		FObjectStore& InObjectStore, TimePointMilliseconds InNowMilliseconds, ERuntimeResult& InOutFirstError) noexcept;

	/** Traces queued captures and temporarily unpublished constructed actors before world registry edges. */
	void VisitDeferredSpawnReferences(FReferenceCollector& InCollector) noexcept;

	/** Presents every registered actor to the active iterative collector. */
	void VisitReferences(FReferenceCollector& InCollector) noexcept override;

	/** Holds the unique caller-owned actor registry reference for this world's lifetime. */
	FWorldActorRegistryReference Actors;

	/** Holds optional caller-owned factory storage; an empty capability preserves direct World contracts. */
	FDeferredActorSpawnStorageReference DeferredSpawns;

	/** Supplies canonical actor descriptors without exposing the application registry to World. */
	FClassRegistryRegistrationView Classes;

	/** Guards the forward-only world lifecycle without scattering boolean flags. */
	FLifecycleGuard Lifecycle;

	/** Caches the last observed dispatcher time so rollback stays observable. */
	TimePointMilliseconds LastUpdateMilliseconds{0};
};

} // namespace MicroWorld
