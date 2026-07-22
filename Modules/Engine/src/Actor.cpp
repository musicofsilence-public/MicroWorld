#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineClassIds.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/World.h>

#include <MicroWorld/Object/ClassDescriptor.h>
#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/Object.h>
#include <MicroWorld/Object/ObjectStore.h>
#include <MicroWorld/Object/ObjectPtr.h>
#include <MicroWorld/TickFunction.h>

#include <utility>

namespace MicroWorld
{

AActor::AActor(FActorComponentRegistryBase ComponentStorage, const FTickConfiguration TickConfiguration) noexcept
	: UObject(), FTickable(TickConfiguration), Components(std::move(ComponentStorage))
{
}

AActor::~AActor() noexcept = default;

const FClassDescriptor& AActor::StaticClassDescriptor() noexcept
{
	static const FClassDescriptor Descriptor = MakeClassDescriptor<AActor>(AActorClassId, "AActor", nullptr, &TraceManagedObjectReferences);
	return Descriptor;
}

EEngineResult AActor::RegisterComponent(const TObjectPtr<UActorComponent> Component) noexcept
{
	// Registration is only permitted before BeginPlay can begin dispatch.
	if (!IsRegistrationOpen())
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
	UActorComponent* const Resolved = Component.Get();
	if (Resolved == nullptr)
	{
		return EEngineResult::InvalidReference;
	}
	// The component must belong to the same canonical store as this actor so a
	// foreign handle can never be traced through this owner.
	if (!Component.BelongsTo(*ObjectStore))
	{
		return EEngineResult::CrossStore;
	}
	if (!Components.IsValid())
	{
		return EEngineResult::CapacityExceeded;
	}
	// A duplicate of a component already registered with this actor is reported
	// before the cross-owner check so a repeated registration stays honest.
	for (std::size_t Index = 0; Index < Components.GetCount(); ++Index)
	{
		if (Components.At(Index).Handle() == Component.Handle())
		{
			return EEngineResult::Duplicate;
		}
	}
	// Capacity (including zero capacity) is a structural property of this actor,
	// so it is reported before the candidate's existing ownership is inspected.
	if (Components.GetCount() >= Components.GetCapacity())
	{
		return EEngineResult::CapacityExceeded;
	}
	if (Resolved->HasAssignedActor())
	{
		return EEngineResult::AlreadyOwned;
	}

	// Atomic publish: every fallible check precedes the parent link and registry update.
	Resolved->AssignOwner(GetObjectHandle());
	Components.Add(Component);
	return EEngineResult::Success;
}

UWorld* AActor::GetOwnerWorld() const noexcept
{
	// Same-store registration lets the child's canonical store validate the
	// weak parent generation without retaining a duplicate store pointer.
	FObjectStore* const ObjectStore = GetObjectStore();
	if (ObjectStore == nullptr || !WorldObjectHandle.IsValid())
	{
		return nullptr;
	}
	return static_cast<UWorld*>(ResolveObjectHandle(*ObjectStore, WorldObjectHandle));
}

ERuntimeResult AActor::DispatchBeginPlay(const TimePointMilliseconds NowMilliseconds) noexcept
{
	if (!Components.IsValid())
	{
		return ERuntimeResult::CapacityExceeded;
	}
	const ERuntimeResult BeginResult = Lifecycle.Begin();
	if (BeginResult != ERuntimeResult::Success)
	{
		return BeginResult;
	}
	BeginPrimaryTickLifecycle(NowMilliseconds);

	// Components begin in registration order; on first failure the previously
	// begun components are ended in reverse so the actor never observes a
	// partially begun set.
	std::size_t BegunComponentCount = 0;
	for (std::size_t Index = 0; Index < Components.GetCount(); ++Index)
	{
		UActorComponent* const Component = Components.At(Index).Get();
		const ERuntimeResult ComponentResult =
			Component != nullptr ? Component->DispatchBeginPlay(NowMilliseconds) : ERuntimeResult::InvalidLifecycle;
		if (ComponentResult != ERuntimeResult::Success)
		{
			for (std::size_t RollbackIndex = BegunComponentCount; RollbackIndex > 0; --RollbackIndex)
			{
				if (UActorComponent* const Begun = Components.At(RollbackIndex - 1).Get())
				{
					(void)Begun->DispatchEndPlay();
				}
			}
			EndPrimaryTickLifecycle();
			Lifecycle.Fail();
			return ComponentResult;
		}
		++BegunComponentCount;
	}

	BeginPlay();
	return ERuntimeResult::Success;
}

ERuntimeResult AActor::DispatchAdvance(const TimePointMilliseconds NowMilliseconds) noexcept
{
	const ERuntimeResult PlayingResult = Lifecycle.RequirePlaying();
	if (PlayingResult != ERuntimeResult::Success)
	{
		return PlayingResult;
	}

	// Components tick before their actor so a Tick hook can observe component
	// state already advanced for this dispatcher step.
	for (std::size_t Index = 0; Index < Components.GetCount(); ++Index)
	{
		UActorComponent* const Component = Components.At(Index).Get();
		if (Component == nullptr)
		{
			return ERuntimeResult::InvalidLifecycle;
		}
		const ERuntimeResult ComponentResult = Component->DispatchAdvance(NowMilliseconds);
		if (ComponentResult != ERuntimeResult::Success)
		{
			return ComponentResult;
		}
	}

	const FTickDecision Decision = AdvancePrimaryTick(NowMilliseconds);
	if (Decision.Result != ERuntimeResult::Success)
	{
		return Decision.Result;
	}
	if (Decision.bShouldTick)
	{
		Tick(Decision.Context);
	}
	return ERuntimeResult::Success;
}

ERuntimeResult AActor::DispatchEndPlay() noexcept
{
	// EndPlay is idempotent after a successful end so repeated shutdown paths
	// never re-enter the consumer hook or component end cascade.
	if (Lifecycle.State() == ELifecycleState::Ended)
	{
		return ERuntimeResult::Success;
	}
	const ERuntimeResult EndResult = Lifecycle.End();
	if (EndResult != ERuntimeResult::Success)
	{
		return EndResult;
	}

	// The actor's own hook runs before its components end, matching Core.
	EndPlay();
	ERuntimeResult FirstError = ERuntimeResult::Success;
	for (std::size_t Index = Components.GetCount(); Index > 0; --Index)
	{
		if (UActorComponent* const Component = Components.At(Index - 1).Get())
		{
			const ERuntimeResult ComponentResult = Component->DispatchEndPlay();
			if (FirstError == ERuntimeResult::Success && ComponentResult != ERuntimeResult::Success)
			{
				FirstError = ComponentResult;
			}
		}
	}
	EndPrimaryTickLifecycle();
	return FirstError;
}

void AActor::AssignWorld(const FObjectHandle World) noexcept
{
	WorldObjectHandle = World;
}

void AActor::MarkRegisteredComponentsPendingDestroy() noexcept
{
	FObjectStore* const ObjectStore = GetObjectStore();
	if (ObjectStore == nullptr)
	{
		return;
	}
	// Components have already ended (reverse order in DispatchEndPlay); marking
	// each one queues it for the same destruction barrier as its owning actor.
	// The caller invokes this outside any dispatch guard so the store accepts it.
	for (std::size_t Index = 0; Index < Components.GetCount(); ++Index)
	{
		(void)ObjectStore->MarkPendingDestroy(Components.At(Index).Handle());
	}
}

void AActor::VisitReferences(FReferenceCollector& Collector) noexcept
{
	// Every registered component is a traced downward edge; the weak world link
	// is deliberately not traced so the parent-child graph stays acyclic.
	for (std::size_t Index = 0; Index < Components.GetCount(); ++Index)
	{
		Collector.AddReferencedObject(Components.At(Index));
	}
}

} // namespace MicroWorld
