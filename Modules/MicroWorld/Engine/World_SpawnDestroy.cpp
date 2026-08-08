#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/World.h>

#include <MicroWorld/Engine/ClassRegistryRegistrationView.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/ObjectStoreDispatchGuard.h>
#include <MicroWorld/Engine/ObjectPtr.h>

namespace MicroWorld::Engine
{

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

FActorSpawnStatus UWorld::GetSpawnStatus(const FActorSpawnHandle InHandle) const noexcept
{
	return DeferredSpawns.GetStatus(InHandle);
}

EActorSpawnRequestResult UWorld::CheckDeferredSpawnRequest() const noexcept
{
	const Core::ELifecycleState LifecycleState = Lifecycle.GetState();
	if (LifecycleState != Core::ELifecycleState::Constructed && LifecycleState != Core::ELifecycleState::Playing)
	{
		return EActorSpawnRequestResult::LifecycleLocked;
	}
	if (!DeferredSpawns.IsValid() || !Classes.IsValid())
	{
		return EActorSpawnRequestResult::Unconfigured;
	}
	FObjectStore* const ObjectStore = GetObjectStore();
	if (ObjectStore == nullptr || ObjectStore->IsCollectionActive())
	{
		return EActorSpawnRequestResult::LifecycleLocked;
	}
	if (!Actors.IsValid())
	{
		return EActorSpawnRequestResult::CapacityExceeded;
	}
	if (!CanAcceptMoreActors())
	{
		return EActorSpawnRequestResult::CapacityExceeded;
	}
	return EActorSpawnRequestResult::Queued;
}

std::size_t UWorld::DeferredActorSpawnInlineBytes() const noexcept
{
	return DeferredSpawns.InlineBytes();
}

EEngineResult UWorld::CheckSpawnable(const TObjectPtr<AActor> InActor) const noexcept
{
	// Deferred spawn is a play-time structural request; it only queues here and
	// the actual registration and begin happen at the next ApplyPending barrier.
	if (Lifecycle.GetState() != Core::ELifecycleState::Playing)
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
	// Capacity counts every actor that can enter the live registry at the next
	// barrier so manual and typed requests cannot overfill the fixed world limit.
	if (!CanAcceptMoreActors())
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
	if (Lifecycle.GetState() != Core::ELifecycleState::Playing)
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

Core::ERuntimeResult UWorld::ApplyPending(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	const Core::ERuntimeResult PlayingResult = Lifecycle.RequirePlaying();
	if (PlayingResult != Core::ERuntimeResult::Success)
	{
		return PlayingResult;
	}
	FObjectStore* const ObjectStore = GetObjectStore();
	if (ObjectStore == nullptr)
	{
		return Core::ERuntimeResult::InvalidLifecycle;
	}
	// Freeze typed work before destroy or BeginPlay callbacks can queue new requests.
	if (DeferredSpawns.IsValid())
	{
		DeferredSpawns.SealBarrier();
	}
	if (!HasPendingBarrierWork())
	{
		return Core::ERuntimeResult::Success;
	}

	Core::ERuntimeResult FirstError = Core::ERuntimeResult::Success;

	// Destroys apply before spawns so ending actors free capacity that the same
	// barrier can immediately reuse for pending spawns. Each cascade phase aborts
	// the whole barrier if its dispatch guard cannot be acquired.
	const Core::ERuntimeResult EndGuardResult = EndDoomedActorsUnderGuard(*ObjectStore, FirstError);
	if (EndGuardResult != Core::ERuntimeResult::Success)
	{
		DeferredSpawns.RestoreUnconstructedFrom(0);
		DeferredSpawns.RestoreUnpublishedFrom(0);
		return EndGuardResult;
	}

	MarkAndUnregisterDoomedActors(*ObjectStore);

	const Core::ERuntimeResult BeginGuardResult = BeginPendingSpawnsUnderGuard(*ObjectStore, InNowMilliseconds, FirstError);
	if (BeginGuardResult != Core::ERuntimeResult::Success)
	{
		DeferredSpawns.RestoreUnconstructedFrom(0);
		DeferredSpawns.RestoreUnpublishedFrom(0);
		return BeginGuardResult;
	}

	if (DeferredSpawns.IsValid())
	{
		if (ObjectStore->IsCollectionActive())
		{
			DeferredSpawns.RestoreUnconstructedFrom(0);
			DeferredSpawns.RestoreUnpublishedFrom(0);
			return Core::ERuntimeResult::LifecycleLocked;
		}
		ConstructDeferredSpawns(*ObjectStore);
		const Core::ERuntimeResult DeferredBeginResult = BeginDeferredSpawnsUnderGuard(*ObjectStore, InNowMilliseconds, FirstError);
		if (DeferredBeginResult != Core::ERuntimeResult::Success)
		{
			return DeferredBeginResult;
		}
	}

	return FirstError;
}

Core::ERuntimeResult UWorld::EndDoomedActorsUnderGuard(FObjectStore& InObjectStore, Core::ERuntimeResult& InOutFirstError) noexcept
{
	// The end cascade holds the dispatch guard; store destruction marking waits
	// until the guard releases because MarkPendingDestroy is rejected while
	// dispatch is locked.
	FObjectStoreDispatchGuard DispatchGuard(InObjectStore);
	if (!DispatchGuard.IsAcquired())
	{
		return Core::ERuntimeResult::LifecycleLocked;
	}
	for (std::size_t Index = 0; Index < Actors.GetPendingDestroyCount(); ++Index)
	{
		if (AActor* const Actor = Actors.PendingDestroyAt(Index).Get())
		{
			const Core::ERuntimeResult EndResult = DispatchActorEnd(*Actor);
			if (InOutFirstError == Core::ERuntimeResult::Success && EndResult != Core::ERuntimeResult::Success)
			{
				InOutFirstError = EndResult;
			}
		}
	}
	return Core::ERuntimeResult::Success;
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
				DeferredSpawns.ReleaseActor(DoomedActor.Handle());
				break;
			}
		}
		(void)InObjectStore.MarkPendingDestroy(DoomedActor.Handle());
	}
	Actors.ClearPendingDestroy();
}

void UWorld::ConstructDeferredSpawns(FObjectStore& InObjectStore) noexcept
{
	for (std::size_t Index = 0; Index < DeferredSpawns.SealedFactoryCount(); ++Index)
	{
		DeferredSpawns.Construct(DeferredSpawns.SealedFactoryAt(Index), InObjectStore, Classes);
	}
}

void UWorld::AbortPrePlayConstruction(FObjectStore& InObjectStore) noexcept
{
	for (std::size_t Index = 0; Index < DeferredSpawns.SealedPublishCount(); ++Index)
	{
		const TObjectPtr<AActor> ActorReference = DeferredSpawns.GetConstructedActor(DeferredSpawns.SealedPublishAt(Index));
		AActor* const Actor = ActorReference.Get();
		if (Actor == nullptr)
		{
			continue;
		}
		Actor->MarkRegisteredComponentsPendingDestroy();
		(void)InObjectStore.MarkPendingDestroy(ActorReference.Handle());
	}
	DeferredSpawns.AbortSealedBatch();
	(void)InObjectStore.ApplyPendingDestroy(InObjectStore.Stats().SlotCapacity);
}

Core::ERuntimeResult UWorld::BeginDeferredSpawnsUnderGuard(
	FObjectStore& InObjectStore, const Core::TimePointMilliseconds InNowMilliseconds, Core::ERuntimeResult& InOutFirstError) noexcept
{
	if (DeferredSpawns.SealedPublishCount() == 0)
	{
		return Core::ERuntimeResult::Success;
	}

	FObjectStoreDispatchGuard DispatchGuard(InObjectStore);
	if (!DispatchGuard.IsAcquired())
	{
		DeferredSpawns.RestoreUnpublishedFrom(0);
		return Core::ERuntimeResult::LifecycleLocked;
	}
	return BeginDeferredSpawnsWithGuardHeld(InObjectStore, InNowMilliseconds, InOutFirstError);
}

Core::ERuntimeResult UWorld::BeginDeferredSpawnsWithGuardHeld(
	FObjectStore&, const Core::TimePointMilliseconds InNowMilliseconds, Core::ERuntimeResult& InOutFirstError) noexcept
{
	for (std::size_t Index = 0; Index < DeferredSpawns.SealedPublishCount(); ++Index)
	{
		const FActorSpawnHandle SpawnHandle = DeferredSpawns.SealedPublishAt(Index);
		const TObjectPtr<AActor> SpawnedActor = DeferredSpawns.GetConstructedActor(SpawnHandle);
		AActor* const Actor = SpawnedActor.Get();
		if (Actor == nullptr)
		{
			continue;
		}
		Actor->AssignWorld(GetObjectHandle());
		Actors.Add(SpawnedActor);
		const Core::ERuntimeResult BeginResult = DispatchActorBegin(*Actor, InNowMilliseconds);
		if (InOutFirstError == Core::ERuntimeResult::Success && BeginResult != Core::ERuntimeResult::Success)
		{
			InOutFirstError = BeginResult;
		}
		DeferredSpawns.CompletePublish(SpawnHandle);
	}
	return Core::ERuntimeResult::Success;
}

Core::ERuntimeResult UWorld::BeginPendingSpawnsUnderGuard(
	FObjectStore& InObjectStore, const Core::TimePointMilliseconds InNowMilliseconds, Core::ERuntimeResult& InOutFirstError) noexcept
{
	// Spawns register into the freed live capacity and begin under a fresh guard.
	// The guard scope closes before ClearPendingSpawn so the pending list is only
	// cleared once every queued spawn has begun.
	{
		FObjectStoreDispatchGuard DispatchGuard(InObjectStore);
		if (!DispatchGuard.IsAcquired())
		{
			return Core::ERuntimeResult::LifecycleLocked;
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
			const Core::ERuntimeResult BeginResult = DispatchActorBegin(*Actor, InNowMilliseconds);
			if (InOutFirstError == Core::ERuntimeResult::Success && BeginResult != Core::ERuntimeResult::Success)
			{
				InOutFirstError = BeginResult;
			}
		}
	}
	Actors.ClearPendingSpawn();
	return Core::ERuntimeResult::Success;
}

} // namespace MicroWorld::Engine
