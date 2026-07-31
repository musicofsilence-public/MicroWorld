#pragma once

#include "ObjectConsumerProbe.h"

#include <MicroWorld/Core/Delegates/Delegate.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineClassIds.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Core/Timer.h>
#include <MicroWorld/Core/Version.h>

#include <cstddef>
#include <cstdint>
#include <utility>

static_assert(__cplusplus >= 201703L);

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#error "The MicroWorld Engine consumer must compile with exceptions disabled."
#endif

#if defined(__GXX_RTTI) || defined(_CPPRTTI)
#error "The MicroWorld Engine consumer must compile with RTTI disabled."
#endif

namespace MicroWorldConsumer
{

/** Stable process exit codes that identify the exact public-API probe failure. */
enum class EEngineConsumerExitCode : int
{
	Success = 0,
	ComponentBaseRegistrationFailed = 1,
	ActorBaseRegistrationFailed = 2,
	WorldBaseRegistrationFailed = 3,
	DerivedRegistrationFailed = 4,
	StoreConfigurationFailed = 5,
	ObjectCreationFailed = 6,
	ComponentRegistrationFailed = 7,
	ActorRegistrationFailed = 8,
	WorldRootFailed = 9,
	BeginPlayFailed = 10,
	AdvanceFailed = 11,
	EndPlayFailed = 12,
	RootedCollectionFailed = 13,
	UnrootedCollectionFailed = 14,
	TimerScheduleFailed = 15,
	TimerAdvanceFailed = 16,
	TimerDidNotFireOnce = 17,
	TimerFiredAfterCompletion = 18,
	TimerStaleCancelFailed = 19,
	ObjectProfileFailureOffset = 100,
};

/** A concrete component proving the engine component base is constructible. */
class FConsumerComponent final : public MicroWorld::Engine::UActorComponent
{
public:
	/** Keeps exact descriptor-driven destruction publicly instantiable. */
	~FConsumerComponent() noexcept override = default;
};

/** A concrete actor proving the engine actor base is constructible. */
class FConsumerActor final : public MicroWorld::Engine::AActor
{
public:
	/** Initializes the managed actor base, which owns its bounded component slots. */
	explicit FConsumerActor() noexcept : AActor() {}

	/** Keeps exact descriptor-driven destruction publicly instantiable. */
	~FConsumerActor() noexcept override = default;
};

/** Object-store and registry capacities the engine probe exercises. */
inline constexpr std::uint32_t EngineProbeSlotCount = 4;
inline constexpr std::uint32_t EngineProbeRootCapacity = 2;
inline constexpr std::size_t EngineProbeSlotSizeBytes = 256;
inline constexpr std::size_t EngineProbeSlotAlignmentBytes = 16;
inline constexpr std::size_t EngineProbeRegistryCapacity = 8;
inline constexpr std::size_t EngineProbeWorldActorCapacity = 1;
inline constexpr std::size_t EngineProbeTimerCapacity = 4;
inline constexpr std::size_t EngineProbeTimerInlineBytes = 32;

/** Type ids assigned to the probe's derived actor and component classes. */
inline constexpr MicroWorld::Engine::FTypeId ConsumerActorTypeId{0x00040001u};
inline constexpr MicroWorld::Engine::FTypeId ConsumerComponentTypeId{0x00040002u};

/** Number of managed objects the unrooted collection is expected to reclaim (world, actor, component). */
inline constexpr std::uint32_t EngineProbeExpectedReclaimedObjectCount = 3;

/** Timer probe inputs: a base clock reading and the one-shot fire delay. */
inline constexpr MicroWorld::Core::TimePointMilliseconds EngineProbeTimerInitialNow = 1000;
inline constexpr MicroWorld::Core::DurationMilliseconds EngineProbeTimerDuration = 100;

/** Expected timer fire count after exactly one one-shot schedule has elapsed. */
inline constexpr std::uint32_t EngineProbeExpectedTimerFireCount = 1;

} // namespace MicroWorldConsumer

/** Exercises representative Core+Object+Engine public APIs without platform I/O. */
inline int RunEngineConsumerProbe() noexcept
{
	using namespace MicroWorld::Core;
	using namespace MicroWorld::Engine;
	using MicroWorldConsumer::EEngineConsumerExitCode;
	using MicroWorldConsumer::FConsumerActor;
	using MicroWorldConsumer::FConsumerComponent;

	const int ObjectProfileResult = RunObjectConsumerProbe();
	if (ObjectProfileResult != 0)
	{
		return static_cast<int>(EEngineConsumerExitCode::ObjectProfileFailureOffset) + ObjectProfileResult;
	}

	TClassRegistry<MicroWorldConsumer::EngineProbeRegistryCapacity> Registry;
	if (Registry.Register(UActorComponent::StaticClassDescriptor()) != EObjectResult::Success)
	{
		return static_cast<int>(EEngineConsumerExitCode::ComponentBaseRegistrationFailed);
	}
	if (Registry.Register(AActor::StaticClassDescriptor()) != EObjectResult::Success)
	{
		return static_cast<int>(EEngineConsumerExitCode::ActorBaseRegistrationFailed);
	}
	if (Registry.Register(UWorld::StaticClassDescriptor()) != EObjectResult::Success)
	{
		return static_cast<int>(EEngineConsumerExitCode::WorldBaseRegistrationFailed);
	}
	const FClassDescriptor ActorDescriptor = MakeClassDescriptor<FConsumerActor>(
		MicroWorldConsumer::ConsumerActorTypeId, "ConsumerActor", Registry.Find(AActorClassId), &TraceManagedObjectReferences);
	const FClassDescriptor ComponentDescriptor = MakeClassDescriptor<FConsumerComponent>(
		MicroWorldConsumer::ConsumerComponentTypeId, "ConsumerComponent", Registry.Find(UActorComponentClassId), &TraceManagedObjectReferences);
	const bool bDerivedRegistered =
		Registry.Register(ActorDescriptor) == EObjectResult::Success && Registry.Register(ComponentDescriptor) == EObjectResult::Success;
	if (!bDerivedRegistered)
	{
		return static_cast<int>(EEngineConsumerExitCode::DerivedRegistrationFailed);
	}

	alignas(MicroWorldConsumer::EngineProbeSlotAlignmentBytes)
		std::byte SlotBytes[MicroWorldConsumer::EngineProbeSlotSizeBytes * MicroWorldConsumer::EngineProbeSlotCount]{};
	FObjectSlotMetadata Slots[MicroWorldConsumer::EngineProbeSlotCount]{};
	FObjectRootEntry Roots[MicroWorldConsumer::EngineProbeRootCapacity]{};
	FObjectStore Store(
		FObjectStoreStorage{
			SlotBytes,
			sizeof(SlotBytes),
			Slots,
			MicroWorldConsumer::EngineProbeSlotCount,
			MicroWorldConsumer::EngineProbeSlotSizeBytes,
			MicroWorldConsumer::EngineProbeSlotAlignmentBytes,
			Roots,
			MicroWorldConsumer::EngineProbeRootCapacity,
		},
		MakeClassRegistryView(Registry));
	if (Store.ConfigurationResult() != EObjectResult::Success)
	{
		return static_cast<int>(EEngineConsumerExitCode::StoreConfigurationFailed);
	}

	FWorldActorRegistry<MicroWorldConsumer::EngineProbeWorldActorCapacity> WorldActors;
	const TObjectPtr<UWorld> World = Store.NewObject<UWorld>(*Registry.Find(UWorldClassId), WorldActors.MakeReference()).Object;
	const TObjectPtr<FConsumerActor> Actor = Store.NewObject<FConsumerActor>(*Registry.Find(MicroWorldConsumer::ConsumerActorTypeId)).Object;
	const TObjectPtr<FConsumerComponent> Component =
		Store.NewObject<FConsumerComponent>(*Registry.Find(MicroWorldConsumer::ConsumerComponentTypeId)).Object;
	const bool bAllObjectsCreated = World.Get() != nullptr && Actor.Get() != nullptr && Component.Get() != nullptr;
	if (!bAllObjectsCreated)
	{
		return static_cast<int>(EEngineConsumerExitCode::ObjectCreationFailed);
	}

	if (Actor.Get()->RegisterComponent(Component) != EEngineResult::Success)
	{
		return static_cast<int>(EEngineConsumerExitCode::ComponentRegistrationFailed);
	}
	if (World.Get()->RegisterActor(TObjectPtr<AActor>{Actor}) != EEngineResult::Success)
	{
		return static_cast<int>(EEngineConsumerExitCode::ActorRegistrationFailed);
	}

	TStrongObjectPointerResult<UWorld> WorldRoot = Store.MakeStrongObjectPtr(World);
	if (WorldRoot.Result != EObjectResult::Success)
	{
		return static_cast<int>(EEngineConsumerExitCode::WorldRootFailed);
	}
	if (World.Get()->BeginPlay(0) != ERuntimeResult::Success)
	{
		return static_cast<int>(EEngineConsumerExitCode::BeginPlayFailed);
	}
	if (World.Get()->Advance(1) != ERuntimeResult::Success)
	{
		return static_cast<int>(EEngineConsumerExitCode::AdvanceFailed);
	}
	if (World.Get()->EndPlay() != ERuntimeResult::Success)
	{
		return static_cast<int>(EEngineConsumerExitCode::EndPlayFailed);
	}

	FObjectHandle Worklist[MicroWorldConsumer::EngineProbeSlotCount]{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist, MicroWorldConsumer::EngineProbeSlotCount});
	const FGarbageCollectionResult RootedCollection = Collector.CollectFull();
	const bool bRootedCollectionHeldObjects = RootedCollection.Result == ERuntimeResult::Success && RootedCollection.ObjectsReclaimed == 0;
	if (!bRootedCollectionHeldObjects)
	{
		return static_cast<int>(EEngineConsumerExitCode::RootedCollectionFailed);
	}

	WorldRoot.Pointer.Reset();
	const FGarbageCollectionResult UnrootedCollection = Collector.CollectFull();
	const FObjectStoreStats FinalStats = Store.Stats();
	const bool bUnrootedReclaimedAll = UnrootedCollection.Result == ERuntimeResult::Success
		&& UnrootedCollection.ObjectsReclaimed == MicroWorldConsumer::EngineProbeExpectedReclaimedObjectCount && FinalStats.OccupiedSlots == 0;
	if (!bUnrootedReclaimedAll)
	{
		return static_cast<int>(EEngineConsumerExitCode::UnrootedCollectionFailed);
	}

	// Exercises the bounded timer facility the same way a host application would: the caller owns the
	// manager value, supplies every clock reading, and the bound inline callback observes its dispatch.
	TTimerManager<MicroWorldConsumer::EngineProbeTimerCapacity, MicroWorldConsumer::EngineProbeTimerInlineBytes> TimerManager{
		MicroWorldConsumer::EngineProbeTimerInitialNow};
	std::uint32_t TimerFireCount{0};
	TDelegate<void(), MicroWorldConsumer::EngineProbeTimerInlineBytes> TimerCallback;
	(void)TimerCallback.Bind([&TimerFireCount]() noexcept { ++TimerFireCount; });

	FTimerHandle TimerHandle{};
	const ETimerResult TimerScheduleResult =
		TimerManager.Schedule(std::move(TimerCallback), MicroWorldConsumer::EngineProbeTimerDuration, ETimerMode::OneShot, TimerHandle);
	const bool bTimerScheduled = TimerScheduleResult == ETimerResult::Success && TimerHandle.IsValid();
	if (!bTimerScheduled)
	{
		return static_cast<int>(EEngineConsumerExitCode::TimerScheduleFailed);
	}

	const ETimerResult AdvanceAtDeadlineResult = TimerManager.Advance(MicroWorldConsumer::EngineProbeTimerInitialNow);
	const ETimerResult AdvancePastDeadlineResult =
		TimerManager.Advance(MicroWorldConsumer::EngineProbeTimerInitialNow + MicroWorldConsumer::EngineProbeTimerDuration);
	const bool bBothAdvancesSucceeded = AdvanceAtDeadlineResult == ETimerResult::Success && AdvancePastDeadlineResult == ETimerResult::Success;
	if (!bBothAdvancesSucceeded)
	{
		return static_cast<int>(EEngineConsumerExitCode::TimerAdvanceFailed);
	}
	if (TimerFireCount != MicroWorldConsumer::EngineProbeExpectedTimerFireCount)
	{
		return static_cast<int>(EEngineConsumerExitCode::TimerDidNotFireOnce);
	}

	const ETimerResult AdvanceAfterCompletionResult = TimerManager.Advance(
		MicroWorldConsumer::EngineProbeTimerInitialNow + MicroWorldConsumer::EngineProbeTimerDuration + MicroWorldConsumer::EngineProbeTimerDuration);
	const bool bNoExtraFire =
		AdvanceAfterCompletionResult == ETimerResult::Success && TimerFireCount == MicroWorldConsumer::EngineProbeExpectedTimerFireCount;
	if (!bNoExtraFire)
	{
		return static_cast<int>(EEngineConsumerExitCode::TimerFiredAfterCompletion);
	}

	const ETimerResult StaleCancelResult = TimerManager.Cancel(TimerHandle);
	if (StaleCancelResult != ETimerResult::StaleHandle)
	{
		return static_cast<int>(EEngineConsumerExitCode::TimerStaleCancelFailed);
	}

	return static_cast<int>(EEngineConsumerExitCode::Success);
}
