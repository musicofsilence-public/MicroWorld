#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineClassIds.h>

#include <MicroWorld/Object/ClassDescriptor.h>
#include <MicroWorld/Object/Object.h>
#include <MicroWorld/Object/ObjectStore.h>
#include <MicroWorld/Object/ObjectPtr.h>
#include <MicroWorld/TickFunction.h>

namespace MicroWorld
{

UActorComponent::UActorComponent(FTickConfiguration TickConfiguration) noexcept : FTickable(TickConfiguration) {}

UActorComponent::~UActorComponent() noexcept = default;

const FClassDescriptor& UActorComponent::StaticClassDescriptor() noexcept
{
	static const FClassDescriptor Descriptor =
		MakeClassDescriptor<UActorComponent>(UActorComponentClassId, "UActorComponent", nullptr, &TraceManagedObjectReferences);
	return Descriptor;
}

AActor* UActorComponent::GetOwnerActor() const noexcept
{
	// Same-store registration lets the child's canonical store validate the
	// weak parent generation without retaining a duplicate store pointer.
	FObjectStore* const ObjectStore = GetObjectStore();
	if (ObjectStore == nullptr || !OwnerObjectHandle.IsValid())
	{
		return nullptr;
	}
	return static_cast<AActor*>(ResolveObjectHandle(*ObjectStore, OwnerObjectHandle));
}

ERuntimeResult UActorComponent::DispatchBeginPlay(const TimePointMilliseconds NowMilliseconds) noexcept
{
	const ERuntimeResult BeginResult = Lifecycle.Begin();
	if (BeginResult != ERuntimeResult::Success)
	{
		return BeginResult;
	}

	// The primary tick lifecycle starts before the consumer hook so a Tick in
	// the same dispatcher step observes a baseline time.
	BeginPrimaryTickLifecycle(NowMilliseconds);
	BeginPlay();
	return ERuntimeResult::Success;
}

ERuntimeResult UActorComponent::DispatchAdvance(const TimePointMilliseconds NowMilliseconds) noexcept
{
	const ERuntimeResult PlayingResult = Lifecycle.RequirePlaying();
	if (PlayingResult != ERuntimeResult::Success)
	{
		return PlayingResult;
	}

	const FTickDecision Decision = AdvancePrimaryTick(NowMilliseconds);
	if (Decision.Result != ERuntimeResult::Success)
	{
		return Decision.Result;
	}
	if (Decision.bShouldTick)
	{
		TickComponent(Decision.Context);
	}
	return ERuntimeResult::Success;
}

ERuntimeResult UActorComponent::DispatchEndPlay() noexcept
{
	// EndPlay is idempotent after a successful end so repeated shutdown paths
	// never re-enter the consumer hook.
	if (Lifecycle.State() == ELifecycleState::Ended)
	{
		return ERuntimeResult::Success;
	}
	const ERuntimeResult EndResult = Lifecycle.End();
	if (EndResult != ERuntimeResult::Success)
	{
		return EndResult;
	}

	EndPlay();
	EndPrimaryTickLifecycle();
	return ERuntimeResult::Success;
}

void UActorComponent::AssignOwner(const FObjectHandle Owner) noexcept
{
	OwnerObjectHandle = Owner;
}

} // namespace MicroWorld
