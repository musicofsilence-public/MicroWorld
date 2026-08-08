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

UWorld::UWorld(FWorldActorRegistryReference InActorStorage) noexcept : UObject(), Actors(std::move(InActorStorage)), Subsystems() {}

UWorld::UWorld(FWorldActorRegistryReference InActorStorage, FWorldSubsystemRegistryReference InSubsystemStorage) noexcept
	: UObject(), Actors(std::move(InActorStorage)), Subsystems(std::move(InSubsystemStorage))
{
}

UWorld::UWorld(
	FWorldActorRegistryReference InActorStorage,
	FDeferredActorSpawnStorageReference InSpawnStorage,
	const FClassRegistryRegistrationView InClasses) noexcept
	: UObject(), Actors(std::move(InActorStorage)), DeferredSpawns(std::move(InSpawnStorage)), Classes(InClasses)
{
}

UWorld::UWorld(
	FWorldActorRegistryReference InActorStorage,
	FDeferredActorSpawnStorageReference InSpawnStorage,
	FClassRegistryRegistrationView InClasses,
	FWorldSubsystemRegistryReference InSubsystemStorage,
	Networking::FNetworkSystem* const InNetwork) noexcept
	: UObject()
	, Actors(std::move(InActorStorage))
	, DeferredSpawns(std::move(InSpawnStorage))
	, Classes(InClasses)
	, Subsystems(std::move(InSubsystemStorage))
	, Network(InNetwork)
{
}

UWorld::~UWorld() noexcept = default;

const FClassDescriptor& UWorld::StaticClassDescriptor() noexcept
{
	static const FClassDescriptor Descriptor = MakeClassDescriptor<UWorld>(UWorldClassId, "UWorld", nullptr, &TraceManagedObjectReferences);
	return Descriptor;
}

bool UWorld::HasActiveNetworkPeer() const noexcept
{
	return Network != nullptr && Network->HasActivePeer();
}

} // namespace MicroWorld::Engine
