#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineClassIds.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/World.h>

#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/ClassRegistryRegistrationView.h>
#include <MicroWorld/Engine/ReferenceCollector.h>
#include <MicroWorld/Engine/Object.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/ObjectStoreDispatchGuard.h>
#include <MicroWorld/Engine/ObjectPtr.h>

#include <utility>

namespace MicroWorld::Engine
{

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

bool UWorld::CanAcceptMoreActors() const noexcept
{
	const std::size_t LiveAndPendingActorCount = Actors.GetCount() + Actors.GetPendingSpawnCount() + DeferredSpawns.PendingCount();
	return LiveAndPendingActorCount < Actors.GetCapacity();
}

bool UWorld::HasPendingBarrierWork() const noexcept
{
	const bool bHasPendingDestroys = Actors.GetPendingDestroyCount() != 0;
	const bool bHasPendingSpawns = Actors.GetPendingSpawnCount() != 0;
	const bool bHasDeferredSpawns = DeferredSpawns.PendingCount() != 0;
	return bHasPendingDestroys || bHasPendingSpawns || bHasDeferredSpawns;
}

EEngineResult UWorld::CheckActorRegistrable(const TObjectPtr<AActor> InActor) const noexcept
{
	// Registration is only permitted before BeginPlay can begin dispatch.
	if (Lifecycle.GetState() != Core::ELifecycleState::Constructed)
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

} // namespace MicroWorld::Engine
