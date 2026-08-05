#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/World.h>

#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/ObjectStoreDispatchGuard.h>
#include <MicroWorld/Engine/ObjectPtr.h>

namespace MicroWorld::Engine
{

Core::ERuntimeResult UWorld::BeginPlay(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	if (Lifecycle.GetState() != Core::ELifecycleState::Constructed)
	{
		return Core::ERuntimeResult::InvalidLifecycle;
	}
	if (!Actors.IsValid())
	{
		return Core::ERuntimeResult::CapacityExceeded;
	}
	FObjectStore* const ObjectStore = GetObjectStore();
	if (ObjectStore == nullptr)
	{
		return Core::ERuntimeResult::InvalidLifecycle;
	}
	if (DeferredSpawns.IsValid() && ObjectStore->IsCollectionActive())
	{
		return Core::ERuntimeResult::LifecycleLocked;
	}
	{
		FObjectStoreDispatchGuard DispatchGuard(*ObjectStore);
		if (!DispatchGuard.IsAcquired())
		{
			return Core::ERuntimeResult::LifecycleLocked;
		}

		const Core::ERuntimeResult BeginResult = Lifecycle.Begin();
		if (BeginResult != Core::ERuntimeResult::Success)
		{
			return BeginResult;
		}
	}
	LastUpdateMilliseconds = InNowMilliseconds;

	if (DeferredSpawns.IsValid())
	{
		// Freeze composition-time requests before actor callbacks can append
		// play-time work, then use the normal barrier construction path.
		DeferredSpawns.SealBarrier();
		ConstructDeferredSpawns(*ObjectStore);
	}

	FObjectStoreDispatchGuard StartupDispatchGuard(*ObjectStore);
	if (!StartupDispatchGuard.IsAcquired())
	{
		Lifecycle.Fail();
		return Core::ERuntimeResult::LifecycleLocked;
	}

	const Core::ERuntimeResult SubsystemResult = InitializeSubsystemsWithRollback();
	if (SubsystemResult != Core::ERuntimeResult::Success)
	{
		Lifecycle.Fail();
		return SubsystemResult;
	}

	const Core::ERuntimeResult ActorResult = BeginRegisteredActorsWithRollback(InNowMilliseconds);
	if (ActorResult != Core::ERuntimeResult::Success)
	{
		(void)DeinitializeSubsystemsReverse();
		return ActorResult;
	}

	Core::ERuntimeResult DeferredFirstError = Core::ERuntimeResult::Success;
	const Core::ERuntimeResult DeferredDispatchResult = BeginDeferredSpawnsWithGuardHeld(*ObjectStore, InNowMilliseconds, DeferredFirstError);
	if (DeferredDispatchResult != Core::ERuntimeResult::Success || DeferredFirstError != Core::ERuntimeResult::Success)
	{
		(void)EndRegisteredActorsReverse();
		(void)DeinitializeSubsystemsReverse();
		Lifecycle.Fail();
		return DeferredDispatchResult != Core::ERuntimeResult::Success ? DeferredDispatchResult : DeferredFirstError;
	}

	return Core::ERuntimeResult::Success;
}

Core::ERuntimeResult UWorld::Advance(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	const Core::ERuntimeResult PlayingResult = Lifecycle.RequirePlaying();
	if (PlayingResult != Core::ERuntimeResult::Success)
	{
		return PlayingResult;
	}
	// The world rejects rollback before any actor observes the timestamp so a
	// non-monotonic dispatcher cannot corrupt per-actor scheduling baselines.
	if (InNowMilliseconds < LastUpdateMilliseconds)
	{
		return Core::ERuntimeResult::NonMonotonicTime;
	}
	FObjectStore* const ObjectStore = GetObjectStore();
	if (ObjectStore == nullptr)
	{
		return Core::ERuntimeResult::InvalidLifecycle;
	}
	FObjectStoreDispatchGuard DispatchGuard(*ObjectStore);
	if (!DispatchGuard.IsAcquired())
	{
		return Core::ERuntimeResult::LifecycleLocked;
	}
	LastUpdateMilliseconds = InNowMilliseconds;

	for (std::size_t Index = 0; Index < Actors.GetCount(); ++Index)
	{
		AActor* const Actor = Actors.At(Index).Get();
		if (Actor == nullptr)
		{
			return Core::ERuntimeResult::InvalidLifecycle;
		}
		const Core::ERuntimeResult ActorResult = DispatchActorAdvance(*Actor, InNowMilliseconds);
		if (ActorResult != Core::ERuntimeResult::Success)
		{
			return ActorResult;
		}
	}
	return Core::ERuntimeResult::Success;
}

Core::ERuntimeResult UWorld::EndPlay() noexcept
{
	// EndPlay is idempotent after a successful end so repeated shutdown paths
	// never re-enter the actor end cascade.
	if (Lifecycle.GetState() == Core::ELifecycleState::Ended)
	{
		return Core::ERuntimeResult::Success;
	}
	if (Lifecycle.GetState() != Core::ELifecycleState::Playing)
	{
		return Core::ERuntimeResult::InvalidLifecycle;
	}
	FObjectStore* const ObjectStore = GetObjectStore();
	if (ObjectStore == nullptr)
	{
		return Core::ERuntimeResult::InvalidLifecycle;
	}
	FObjectStoreDispatchGuard DispatchGuard(*ObjectStore);
	if (!DispatchGuard.IsAcquired())
	{
		return Core::ERuntimeResult::LifecycleLocked;
	}
	const Core::ERuntimeResult EndResult = Lifecycle.End();
	if (EndResult != Core::ERuntimeResult::Success)
	{
		return EndResult;
	}

	const Core::ERuntimeResult ActorEndResult = EndRegisteredActorsReverse();
	const Core::ERuntimeResult SubsystemEndResult = DeinitializeSubsystemsReverse();
	return ActorEndResult != Core::ERuntimeResult::Success ? ActorEndResult : SubsystemEndResult;
}

Core::ERuntimeResult UWorld::BeginRegisteredActorsWithRollback(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	// Actors begin in registration order; on first failure the previously begun
	// actors are ended in reverse so the world never observes a partially begun
	// set and its own lifecycle becomes terminal.
	std::size_t BegunActorCount = 0;
	for (std::size_t Index = 0; Index < Actors.GetCount(); ++Index)
	{
		AActor* const Actor = Actors.At(Index).Get();
		const Core::ERuntimeResult ActorResult =
			Actor != nullptr ? DispatchActorBegin(*Actor, InNowMilliseconds) : Core::ERuntimeResult::InvalidLifecycle;
		if (ActorResult != Core::ERuntimeResult::Success)
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
	return Core::ERuntimeResult::Success;
}

Core::ERuntimeResult UWorld::EndRegisteredActorsReverse() noexcept
{
	// Actors end in reverse registration order; the first error is retained but
	// every actor still receives its EndPlay so shutdown stays symmetric.
	Core::ERuntimeResult FirstError = Core::ERuntimeResult::Success;
	for (std::size_t Index = Actors.GetCount(); Index > 0; --Index)
	{
		if (AActor* const Actor = Actors.At(Index - 1).Get())
		{
			const Core::ERuntimeResult ActorResult = DispatchActorEnd(*Actor);
			if (FirstError == Core::ERuntimeResult::Success && ActorResult != Core::ERuntimeResult::Success)
			{
				FirstError = ActorResult;
			}
		}
	}
	return FirstError;
}

Core::ERuntimeResult UWorld::DispatchActorBegin(AActor& InActor, const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	return InActor.DispatchBeginPlay(InNowMilliseconds);
}

Core::ERuntimeResult UWorld::DispatchActorAdvance(AActor& InActor, const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	return InActor.DispatchAdvance(InNowMilliseconds);
}

Core::ERuntimeResult UWorld::DispatchActorEnd(AActor& InActor) noexcept
{
	return InActor.DispatchEndPlay();
}

} // namespace MicroWorld::Engine
