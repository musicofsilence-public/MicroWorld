#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineClassIds.h>

#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/Object.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Core/TickConfiguration.h>
#include <MicroWorld/Core/TickDecision.h>

namespace MicroWorld::Engine
{

UActorComponent::UActorComponent(Core::FTickConfiguration InTickConfiguration) noexcept : Core::FTickable(InTickConfiguration) {}

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

Core::ERuntimeResult UActorComponent::DispatchBeginPlay(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	const Core::ERuntimeResult BeginResult = Lifecycle.Begin();
	if (BeginResult != Core::ERuntimeResult::Success)
	{
		return BeginResult;
	}

	// The primary tick lifecycle starts before the consumer hook so a Tick in
	// the same dispatcher step observes a baseline time.
	BeginPrimaryTickLifecycle(InNowMilliseconds);
	BeginPlay();
	return Core::ERuntimeResult::Success;
}

Core::ERuntimeResult UActorComponent::DispatchAdvance(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	const Core::ERuntimeResult PlayingResult = Lifecycle.RequirePlaying();
	if (PlayingResult != Core::ERuntimeResult::Success)
	{
		return PlayingResult;
	}

	const Core::FTickDecision Decision = AdvancePrimaryTick(InNowMilliseconds);
	if (Decision.Result != Core::ERuntimeResult::Success)
	{
		return Decision.Result;
	}
	if (Decision.bShouldTick)
	{
		TickComponent(Decision.Context);
	}
	return Core::ERuntimeResult::Success;
}

Core::ERuntimeResult UActorComponent::DispatchEndPlay() noexcept
{
	// EndPlay is idempotent after a successful end so repeated shutdown paths
	// never re-enter the consumer hook.
	if (Lifecycle.GetState() == Core::ELifecycleState::Ended)
	{
		return Core::ERuntimeResult::Success;
	}
	const Core::ERuntimeResult EndResult = Lifecycle.End();
	if (EndResult != Core::ERuntimeResult::Success)
	{
		return EndResult;
	}

	EndPlay();
	EndPrimaryTickLifecycle();
	return Core::ERuntimeResult::Success;
}

void UActorComponent::AssignOwner(const FObjectHandle InOwner) noexcept
{
	OwnerObjectHandle = InOwner;
}

} // namespace MicroWorld::Engine
