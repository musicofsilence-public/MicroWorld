#include <MicroWorld/Engine/WorldSubsystem.h>

#include <MicroWorld/Engine/EngineClassIds.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/ReferenceCollector.h>
#include <MicroWorld/Engine/World.h>

namespace MicroWorld::Engine
{

UWorldSubsystem::~UWorldSubsystem() noexcept = default;

const FClassDescriptor& UWorldSubsystem::StaticClassDescriptor() noexcept
{
	static const FClassDescriptor Descriptor =
		MakeClassDescriptor<UWorldSubsystem>(UWorldSubsystemClassId, "UWorldSubsystem", nullptr, &TraceManagedObjectReferences);
	return Descriptor;
}

UWorld* UWorldSubsystem::GetWorld() const noexcept
{
	FObjectStore* ObjectStore = GetObjectStore();
	if (ObjectStore == nullptr || !WorldObjectHandle.IsValid())
	{
		return nullptr;
	}
	return static_cast<UWorld*>(ResolveObjectHandle(*ObjectStore, WorldObjectHandle));
}

Core::ERuntimeResult UWorldSubsystem::DispatchInitialize() noexcept
{
	const Core::ERuntimeResult Result = Lifecycle.Begin();
	if (Result != Core::ERuntimeResult::Success)
	{
		return Result;
	}
	Initialize();
	return Core::ERuntimeResult::Success;
}

Core::ERuntimeResult UWorldSubsystem::DispatchDeinitialize() noexcept
{
	if (Lifecycle.GetState() == Core::ELifecycleState::Ended)
	{
		return Core::ERuntimeResult::Success;
	}
	const Core::ERuntimeResult Result = Lifecycle.End();
	if (Result != Core::ERuntimeResult::Success)
	{
		return Result;
	}
	Deinitialize();
	return Core::ERuntimeResult::Success;
}

void UWorldSubsystem::AssignWorld(const FObjectHandle InWorld) noexcept
{
	WorldObjectHandle = InWorld;
}

} // namespace MicroWorld::Engine
