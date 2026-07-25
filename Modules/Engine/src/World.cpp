#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineClassIds.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/World.h>

#include <MicroWorld/Object/ClassDescriptor.h>
#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/Object.h>
#include <MicroWorld/Object/ObjectStore.h>
#include <MicroWorld/Object/ObjectPtr.h>

#include <utility>

namespace MicroWorld
{

UWorld::UWorld(FWorldActorRegistryReference InActorStorage) noexcept : UObject(), Actors(std::move(InActorStorage)) {}

UWorld::~UWorld() noexcept = default;

const FClassDescriptor& UWorld::StaticClassDescriptor() noexcept
{
	static const FClassDescriptor Descriptor = MakeClassDescriptor<UWorld>(UWorldClassId, "UWorld", nullptr, &TraceManagedObjectReferences);
	return Descriptor;
}

EEngineResult UWorld::RegisterActor(const TObjectPtr<AActor> InActor) noexcept
{
	const EEngineResult Verdict = CheckActorRegistrable(InActor);
	if (Verdict != EEngineResult::Success)
	{
		return Verdict;
	}
	PublishActor(InActor);
	return EEngineResult::Success;
}

EEngineResult UWorld::CheckActorRegistrable(const TObjectPtr<AActor> InActor) const noexcept
{
	// Registration is only permitted before BeginPlay can begin dispatch.
	if (Lifecycle.GetState() != ELifecycleState::Constructed)
	{
		return EEngineResult::LifecycleLocked;
	}
	FObjectStore* const ObjectStore = GetObjectStore();
	if (ObjectStore == nullptr)
	{
		return EEngineResult::InvalidReference;
	}
	if (ObjectStore->IsMutationLocked())
	{
		return EEngineResult::LifecycleLocked;
	}
	AActor* const Resolved = InActor.Get();
	if (Resolved == nullptr)
	{
		return EEngineResult::InvalidReference;
	}
	// The actor must belong to the same canonical store as this world so a
	// foreign handle can never be traced through this owner.
	if (!InActor.BelongsTo(*ObjectStore))
	{
		return EEngineResult::CrossStore;
	}
	if (!Actors.IsValid())
	{
		return EEngineResult::CapacityExceeded;
	}
	// A duplicate of an actor already registered with this world is reported
	// before the cross-owner check so a repeated registration stays honest.
	for (std::size_t Index = 0; Index < Actors.GetCount(); ++Index)
	{
		if (Actors.At(Index).Handle() == InActor.Handle())
		{
			return EEngineResult::Duplicate;
		}
	}
	// Capacity (including zero capacity) is a structural property of this world,
	// so it is reported before the candidate's existing ownership is inspected.
	if (Actors.GetCount() >= Actors.GetCapacity())
	{
		return EEngineResult::CapacityExceeded;
	}
	if (Resolved->HasAssignedWorld())
	{
		return EEngineResult::AlreadyOwned;
	}
	return EEngineResult::Success;
}

void UWorld::PublishActor(const TObjectPtr<AActor> InActor) noexcept
{
	// Atomic publish: every fallible check precedes the parent link and registry update.
	AActor* const Resolved = InActor.Get();
	Resolved->AssignWorld(GetObjectHandle());
	Actors.Add(InActor);
}

ERuntimeResult UWorld::BeginPlay(const TimePointMilliseconds InNowMilliseconds) noexcept
{
	if (Lifecycle.GetState() != ELifecycleState::Constructed)
	{
		return ERuntimeResult::InvalidLifecycle;
	}
	if (!Actors.IsValid())
	{
		return ERuntimeResult::CapacityExceeded;
	}
	FObjectStore* const ObjectStore = GetObjectStore();
	if (ObjectStore == nullptr)
	{
		return ERuntimeResult::InvalidLifecycle;
	}
	FObjectStoreDispatchGuard DispatchGuard(*ObjectStore);
	if (!DispatchGuard.IsAcquired())
	{
		return ERuntimeResult::LifecycleLocked;
	}

	const ERuntimeResult BeginResult = Lifecycle.Begin();
	if (BeginResult != ERuntimeResult::Success)
	{
		return BeginResult;
	}
	LastUpdateMilliseconds = InNowMilliseconds;

	return BeginRegisteredActorsWithRollback(InNowMilliseconds);
}

ERuntimeResult UWorld::Advance(const TimePointMilliseconds InNowMilliseconds) noexcept
{
	const ERuntimeResult PlayingResult = Lifecycle.RequirePlaying();
	if (PlayingResult != ERuntimeResult::Success)
	{
		return PlayingResult;
	}
	// The world rejects rollback before any actor observes the timestamp so a
	// non-monotonic dispatcher cannot corrupt per-actor scheduling baselines.
	if (InNowMilliseconds < LastUpdateMilliseconds)
	{
		return ERuntimeResult::NonMonotonicTime;
	}
	FObjectStore* const ObjectStore = GetObjectStore();
	if (ObjectStore == nullptr)
	{
		return ERuntimeResult::InvalidLifecycle;
	}
	FObjectStoreDispatchGuard DispatchGuard(*ObjectStore);
	if (!DispatchGuard.IsAcquired())
	{
		return ERuntimeResult::LifecycleLocked;
	}
	LastUpdateMilliseconds = InNowMilliseconds;

	for (std::size_t Index = 0; Index < Actors.GetCount(); ++Index)
	{
		AActor* const Actor = Actors.At(Index).Get();
		if (Actor == nullptr)
		{
			return ERuntimeResult::InvalidLifecycle;
		}
		const ERuntimeResult ActorResult = DispatchActorAdvance(*Actor, InNowMilliseconds);
		if (ActorResult != ERuntimeResult::Success)
		{
			return ActorResult;
		}
	}
	return ERuntimeResult::Success;
}

ERuntimeResult UWorld::EndPlay() noexcept
{
	// EndPlay is idempotent after a successful end so repeated shutdown paths
	// never re-enter the actor end cascade.
	if (Lifecycle.GetState() == ELifecycleState::Ended)
	{
		return ERuntimeResult::Success;
	}
	if (Lifecycle.GetState() != ELifecycleState::Playing)
	{
		return ERuntimeResult::InvalidLifecycle;
	}
	FObjectStore* const ObjectStore = GetObjectStore();
	if (ObjectStore == nullptr)
	{
		return ERuntimeResult::InvalidLifecycle;
	}
	FObjectStoreDispatchGuard DispatchGuard(*ObjectStore);
	if (!DispatchGuard.IsAcquired())
	{
		return ERuntimeResult::LifecycleLocked;
	}
	const ERuntimeResult EndResult = Lifecycle.End();
	if (EndResult != ERuntimeResult::Success)
	{
		return EndResult;
	}

	return EndRegisteredActorsReverse();
}

ERuntimeResult UWorld::BeginRegisteredActorsWithRollback(const TimePointMilliseconds InNowMilliseconds) noexcept
{
	// Actors begin in registration order; on first failure the previously begun
	// actors are ended in reverse so the world never observes a partially begun
	// set and its own lifecycle becomes terminal.
	std::size_t BegunActorCount = 0;
	for (std::size_t Index = 0; Index < Actors.GetCount(); ++Index)
	{
		AActor* const Actor = Actors.At(Index).Get();
		const ERuntimeResult ActorResult = Actor != nullptr ? DispatchActorBegin(*Actor, InNowMilliseconds) : ERuntimeResult::InvalidLifecycle;
		if (ActorResult != ERuntimeResult::Success)
		{
			for (std::size_t RollbackIndex = BegunActorCount; RollbackIndex > 0; --RollbackIndex)
			{
				if (AActor* const Begun = Actors.At(RollbackIndex - 1).Get())
				{
					(void)Begun->DispatchEndPlay();
				}
			}
			Lifecycle.Fail();
			return ActorResult;
		}
		++BegunActorCount;
	}
	return ERuntimeResult::Success;
}

ERuntimeResult UWorld::EndRegisteredActorsReverse() noexcept
{
	// Actors end in reverse registration order; the first error is retained but
	// every actor still receives its EndPlay so shutdown stays symmetric.
	ERuntimeResult FirstError = ERuntimeResult::Success;
	for (std::size_t Index = Actors.GetCount(); Index > 0; --Index)
	{
		if (AActor* const Actor = Actors.At(Index - 1).Get())
		{
			const ERuntimeResult ActorResult = DispatchActorEnd(*Actor);
			if (FirstError == ERuntimeResult::Success && ActorResult != ERuntimeResult::Success)
			{
				FirstError = ActorResult;
			}
		}
	}
	return FirstError;
}

ERuntimeResult UWorld::DispatchActorBegin(AActor& InActor, const TimePointMilliseconds InNowMilliseconds) noexcept
{
	return InActor.DispatchBeginPlay(InNowMilliseconds);
}

ERuntimeResult UWorld::DispatchActorAdvance(AActor& InActor, const TimePointMilliseconds InNowMilliseconds) noexcept
{
	return InActor.DispatchAdvance(InNowMilliseconds);
}

ERuntimeResult UWorld::DispatchActorEnd(AActor& InActor) noexcept
{
	return InActor.DispatchEndPlay();
}

EEngineResult UWorld::SpawnActor(const TObjectPtr<AActor> InActor) noexcept
{
	const EEngineResult Verdict = CheckSpawnable(InActor);
	if (Verdict != EEngineResult::Success)
	{
		return Verdict;
	}
	// World identity is bound at the barrier, not here, so a repeated request is
	// caught as a pending-spawn duplicate rather than as an already-owned actor.
	Actors.AddPendingSpawn(InActor);
	return EEngineResult::Success;
}

EEngineResult UWorld::CheckSpawnable(const TObjectPtr<AActor> InActor) const noexcept
{
	// Deferred spawn is a play-time structural request; it only queues here and
	// the actual registration and begin happen at the next ApplyPending barrier.
	if (Lifecycle.GetState() != ELifecycleState::Playing)
	{
		return EEngineResult::LifecycleLocked;
	}
	FObjectStore* const ObjectStore = GetObjectStore();
	if (ObjectStore == nullptr)
	{
		return EEngineResult::InvalidReference;
	}
	AActor* const Resolved = InActor.Get();
	if (Resolved == nullptr)
	{
		return EEngineResult::InvalidReference;
	}
	if (!InActor.BelongsTo(*ObjectStore))
	{
		return EEngineResult::CrossStore;
	}
	if (!Actors.IsValid())
	{
		return EEngineResult::CapacityExceeded;
	}
	// A duplicate is any actor already live or already queued to spawn.
	for (std::size_t Index = 0; Index < Actors.GetCount(); ++Index)
	{
		if (Actors.At(Index).Handle() == InActor.Handle())
		{
			return EEngineResult::Duplicate;
		}
	}
	for (std::size_t Index = 0; Index < Actors.GetPendingSpawnCount(); ++Index)
	{
		if (Actors.PendingSpawnAt(Index).Handle() == InActor.Handle())
		{
			return EEngineResult::Duplicate;
		}
	}
	// Capacity counts live and pending-spawn actors together so a queued spawn can
	// never exceed the world's fixed registry once the barrier applies it.
	if (Actors.GetCount() + Actors.GetPendingSpawnCount() >= Actors.GetCapacity())
	{
		return EEngineResult::CapacityExceeded;
	}
	if (Resolved->HasAssignedWorld())
	{
		return EEngineResult::AlreadyOwned;
	}
	return EEngineResult::Success;
}

EEngineResult UWorld::DestroyActor(const TObjectPtr<AActor> InActor) noexcept
{
	const EEngineResult Verdict = CheckDestroyable(InActor);
	if (Verdict != EEngineResult::Success)
	{
		return Verdict;
	}
	Actors.AddPendingDestroy(InActor);
	return EEngineResult::Success;
}

EEngineResult UWorld::CheckDestroyable(const TObjectPtr<AActor> InActor) const noexcept
{
	if (Lifecycle.GetState() != ELifecycleState::Playing)
	{
		return EEngineResult::LifecycleLocked;
	}
	FObjectStore* const ObjectStore = GetObjectStore();
	if (ObjectStore == nullptr)
	{
		return EEngineResult::InvalidReference;
	}
	if (InActor.Get() == nullptr)
	{
		return EEngineResult::InvalidReference;
	}
	if (!InActor.BelongsTo(*ObjectStore))
	{
		return EEngineResult::CrossStore;
	}
	// Only an actor currently registered with this world can be destroyed; a
	// pending-spawn actor is not yet registered and is rejected as invalid.
	bool bRegistered = false;
	for (std::size_t Index = 0; Index < Actors.GetCount(); ++Index)
	{
		if (Actors.At(Index).Handle() == InActor.Handle())
		{
			bRegistered = true;
			break;
		}
	}
	if (!bRegistered)
	{
		return EEngineResult::InvalidReference;
	}
	// A repeated destroy of the same actor before the barrier applies is a duplicate.
	for (std::size_t Index = 0; Index < Actors.GetPendingDestroyCount(); ++Index)
	{
		if (Actors.PendingDestroyAt(Index).Handle() == InActor.Handle())
		{
			return EEngineResult::Duplicate;
		}
	}
	return EEngineResult::Success;
}

ERuntimeResult UWorld::ApplyPending(const TimePointMilliseconds InNowMilliseconds) noexcept
{
	const ERuntimeResult PlayingResult = Lifecycle.RequirePlaying();
	if (PlayingResult != ERuntimeResult::Success)
	{
		return PlayingResult;
	}
	FObjectStore* const ObjectStore = GetObjectStore();
	if (ObjectStore == nullptr)
	{
		return ERuntimeResult::InvalidLifecycle;
	}
	if (Actors.GetPendingDestroyCount() == 0 && Actors.GetPendingSpawnCount() == 0)
	{
		return ERuntimeResult::Success;
	}

	ERuntimeResult FirstError = ERuntimeResult::Success;

	// Destroys apply before spawns so ending actors free capacity that the same
	// barrier can immediately reuse for pending spawns. Each cascade phase aborts
	// the whole barrier if its dispatch guard cannot be acquired.
	const ERuntimeResult EndGuardResult = EndDoomedActorsUnderGuard(*ObjectStore, FirstError);
	if (EndGuardResult != ERuntimeResult::Success)
	{
		return EndGuardResult;
	}

	MarkAndUnregisterDoomedActors(*ObjectStore);

	const ERuntimeResult BeginGuardResult = BeginPendingSpawnsUnderGuard(*ObjectStore, InNowMilliseconds, FirstError);
	if (BeginGuardResult != ERuntimeResult::Success)
	{
		return BeginGuardResult;
	}

	return FirstError;
}

ERuntimeResult UWorld::EndDoomedActorsUnderGuard(FObjectStore& InObjectStore, ERuntimeResult& InOutFirstError) noexcept
{
	// The end cascade holds the dispatch guard; store destruction marking waits
	// until the guard releases because MarkPendingDestroy is rejected while
	// dispatch is locked.
	FObjectStoreDispatchGuard DispatchGuard(InObjectStore);
	if (!DispatchGuard.IsAcquired())
	{
		return ERuntimeResult::LifecycleLocked;
	}
	for (std::size_t Index = 0; Index < Actors.GetPendingDestroyCount(); ++Index)
	{
		if (AActor* const Actor = Actors.PendingDestroyAt(Index).Get())
		{
			const ERuntimeResult EndResult = DispatchActorEnd(*Actor);
			if (InOutFirstError == ERuntimeResult::Success && EndResult != ERuntimeResult::Success)
			{
				InOutFirstError = EndResult;
			}
		}
	}
	return ERuntimeResult::Success;
}

void UWorld::MarkAndUnregisterDoomedActors(FObjectStore& InObjectStore) noexcept
{
	// With no dispatch guard held, mark each doomed actor's components and then the
	// actor itself for the destruction barrier, and unregister it from the live set
	// while preserving the order of the survivors.
	for (std::size_t Index = 0; Index < Actors.GetPendingDestroyCount(); ++Index)
	{
		const TObjectPtr<AActor> DoomedActor = Actors.PendingDestroyAt(Index);
		AActor* const Actor = DoomedActor.Get();
		if (Actor == nullptr)
		{
			continue;
		}
		Actor->MarkRegisteredComponentsPendingDestroy();
		for (std::size_t LiveIndex = 0; LiveIndex < Actors.GetCount(); ++LiveIndex)
		{
			if (Actors.At(LiveIndex).Handle() == DoomedActor.Handle())
			{
				Actors.RemoveAt(LiveIndex);
				break;
			}
		}
		(void)InObjectStore.MarkPendingDestroy(DoomedActor.Handle());
	}
	Actors.ClearPendingDestroy();
}

ERuntimeResult UWorld::BeginPendingSpawnsUnderGuard(
	FObjectStore& InObjectStore, const TimePointMilliseconds InNowMilliseconds, ERuntimeResult& InOutFirstError) noexcept
{
	// Spawns register into the freed live capacity and begin under a fresh guard.
	// The guard scope closes before ClearPendingSpawn so the pending list is only
	// cleared once every queued spawn has begun.
	{
		FObjectStoreDispatchGuard DispatchGuard(InObjectStore);
		if (!DispatchGuard.IsAcquired())
		{
			return ERuntimeResult::LifecycleLocked;
		}
		for (std::size_t Index = 0; Index < Actors.GetPendingSpawnCount(); ++Index)
		{
			const TObjectPtr<AActor> SpawnedActor = Actors.PendingSpawnAt(Index);
			AActor* const Actor = SpawnedActor.Get();
			if (Actor == nullptr)
			{
				continue;
			}
			Actor->AssignWorld(GetObjectHandle());
			Actors.Add(SpawnedActor);
			const ERuntimeResult BeginResult = DispatchActorBegin(*Actor, InNowMilliseconds);
			if (InOutFirstError == ERuntimeResult::Success && BeginResult != ERuntimeResult::Success)
			{
				InOutFirstError = BeginResult;
			}
		}
	}
	Actors.ClearPendingSpawn();
	return ERuntimeResult::Success;
}

std::size_t UWorld::PendingSpawnCount() const noexcept
{
	return Actors.GetPendingSpawnCount();
}

std::size_t UWorld::PendingDestroyCount() const noexcept
{
	return Actors.GetPendingDestroyCount();
}

void UWorld::VisitReferences(FReferenceCollector& InCollector) noexcept
{
	// Every registered actor is a traced downward edge. Pending-spawn actors are
	// also reachable so they survive collection until the barrier begins them;
	// pending-destroy actors are still in the live set until the barrier removes
	// them, so they need no separate edge here.
	for (std::size_t Index = 0; Index < Actors.GetCount(); ++Index)
	{
		InCollector.AddReferencedObject(Actors.At(Index));
	}
	for (std::size_t Index = 0; Index < Actors.GetPendingSpawnCount(); ++Index)
	{
		InCollector.AddReferencedObject(Actors.PendingSpawnAt(Index));
	}
}

} // namespace MicroWorld
