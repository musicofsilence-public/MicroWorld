#pragma once

#include "EngineTestSupport.h"
#include "TestSupport.h"

#include <MicroWorld/Core/TickContext.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/ClassRegistry.h>
#include <MicroWorld/Engine/ClassRegistryView.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/GarbageCollectorStorage.h>
#include <MicroWorld/Engine/ObjectMutationResult.h>
#include <MicroWorld/Engine/ObjectRootEntry.h>
#include <MicroWorld/Engine/ObjectSlotMetadata.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/ObjectStoreStats.h>
#include <MicroWorld/Engine/ObjectStoreStorage.h>
#include <MicroWorld/Engine/StrongObjectPtr.h>
#include <MicroWorld/Engine/WeakObjectPtr.h>
#include <MicroWorld/Engine/World.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace MicroWorld::Tests
{

using MicroWorld::Core::DurationMilliseconds;
using MicroWorld::Core::ERuntimeResult;
using MicroWorld::Core::FTickConfiguration;
using MicroWorld::Core::FTickContext;
using MicroWorld::Core::TimePointMilliseconds;
using MicroWorld::Engine::AActor;
using MicroWorld::Engine::EEngineResult;
using MicroWorld::Engine::EObjectResult;
using MicroWorld::Engine::FClassDescriptor;
using MicroWorld::Engine::FGarbageCollector;
using MicroWorld::Engine::FGarbageCollectorStorage;
using MicroWorld::Engine::FObjectHandle;
using MicroWorld::Engine::FObjectRootEntry;
using MicroWorld::Engine::FObjectSlotMetadata;
using MicroWorld::Engine::FObjectStore;
using MicroWorld::Engine::FObjectStoreStats;
using MicroWorld::Engine::FObjectStoreStorage;
using MicroWorld::Engine::FWorldActorRegistry;
using MicroWorld::Engine::MakeClassDescriptor;
using MicroWorld::Engine::MakeClassRegistryView;
using MicroWorld::Engine::TClassRegistry;
using MicroWorld::Engine::TObjectPtr;
using MicroWorld::Engine::TStrongObjectPtr;
using MicroWorld::Engine::TWeakObjectPtr;
using MicroWorld::Engine::UActorComponent;
using MicroWorld::Engine::UWorld;

/** Motivation: Tick configuration that lets ordering types tick on every advance. */
constexpr FTickConfiguration OrderingTickConfiguration{true, true, DurationMilliseconds{0}};

/** Motivation: Test-local type ids for the ordering, plain, and component descriptors. */
constexpr MicroWorld::Engine::FTypeId OrderingActorTypeId{0x00040001u};
constexpr MicroWorld::Engine::FTypeId OrderingComponentTypeId{0x00040002u};
constexpr MicroWorld::Engine::FTypeId PlainActorTypeId{0x00040003u};
constexpr MicroWorld::Engine::FTypeId PlainComponentTypeId{0x00040004u};

/** Motivation: Canonical monotonic baseline every BeginPlay call uses as its starting world time. */
constexpr TimePointMilliseconds BaselineTimeMilliseconds{0};

/** Motivation: World time at which ApplyPending flushes the queued spawn/destroy barrier in these tests. */
constexpr TimePointMilliseconds BarrierTimeMilliseconds{10};

/** Motivation: World time used to advance survivors after a destroy barrier in the order-preservation tests. */
constexpr TimePointMilliseconds SurvivorAdvanceTimeMilliseconds{20};

/** Motivation: Fixed capacity of the GC fixture worklist, large enough for every reachable object in these tests. */
constexpr std::uint32_t CollectorWorklistCapacity = 16;

/** Motivation: Per-call upper bound on objects the store destruction barrier reclaims in one call. */
constexpr std::uint32_t MaxObjectsReclaimedPerBarrier = 16;

/** Motivation: Environment sized for spawn/destroy tests with room for several actors and components. */
using FSpawnDestroyEnvironment = TEngineEnvironment<256, 16, 16, 4>;

/**
 * Motivation: A component that records begin/tick/end ordering into per-instance state.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FOrderingComponent final : public UActorComponent
{
public:
	/**
	 * Motivation: Binds this component to the shared sequence and its own observed event record.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FOrderingComponent(FSequenceCounter& InSequence, FComponentEventState& InEvents) noexcept
		: UActorComponent(OrderingTickConfiguration), Sequence(InSequence), Events(InEvents)
	{
	}

protected:
	/**
	 * Motivation: Records the sequence value and count of this component's begin hook.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void BeginPlay() noexcept override
	{
		Events.BeginOrder = Sequence.Next();
		++Events.BeginCount;
	}
	/**
	 * Motivation: Records the sequence value and count of this component's tick hook.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void TickComponent(const FTickContext&) noexcept override
	{
		Events.TickOrder = Sequence.Next();
		++Events.TickCount;
	}
	/**
	 * Motivation: Records the sequence value and count of this component's end hook.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void EndPlay() noexcept override
	{
		Events.EndOrder = Sequence.Next();
		++Events.EndCount;
	}

private:
	/** Motivation: Shares one monotonic order source with every observed type in the test. */
	FSequenceCounter& Sequence;
	/** Motivation: Receives this component's observed begin/tick/end ordering and counts. */
	FComponentEventState& Events;
};

/**
 * Motivation: An actor that records begin/tick/end ordering into per-instance state.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FOrderingActor final : public AActor
{
public:
	/**
	 * Motivation: Binds this actor to the shared sequence and its event record.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FOrderingActor(FSequenceCounter& InSequence, FActorEventState& InEvents) noexcept
		: AActor(OrderingTickConfiguration), Sequence(InSequence), Events(InEvents)
	{
	}

protected:
	/**
	 * Motivation: Records the sequence value and count of this actor's begin hook.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void BeginPlay() noexcept override
	{
		Events.BeginOrder = Sequence.Next();
		++Events.BeginCount;
	}
	/**
	 * Motivation: Records the sequence value and count of this actor's tick hook.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void Tick(const FTickContext&) noexcept override
	{
		Events.TickOrder = Sequence.Next();
		++Events.TickCount;
	}
	/**
	 * Motivation: Records the sequence value and count of this actor's end hook.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void EndPlay() noexcept override
	{
		Events.EndOrder = Sequence.Next();
		++Events.EndCount;
	}

private:
	/** Motivation: Shares one monotonic order source with every observed type in the test. */
	FSequenceCounter& Sequence;
	/** Motivation: Receives this actor's observed begin/tick/end ordering and counts. */
	FActorEventState& Events;
};

/**
 * Motivation: A minimal component used where lifetime ordering does not need observing.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FPlainComponent final : public UActorComponent
{
public:
	/**
	 * Motivation: Constructs a component with the default (non-ticking) configuration.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FPlainComponent() noexcept : UActorComponent() {}
};

/**
 * Motivation: A minimal actor used where lifetime ordering does not need observing.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FPlainActor final : public AActor
{
public:
	/**
	 * Motivation: Constructs a plain actor with direct fixed component storage.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	explicit FPlainActor() noexcept : AActor() {}
};

/**
 * Motivation: Builds one ordering actor through its derived descriptor in the environment.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
inline TObjectPtr<FOrderingActor> MakeOrderingActor(
	FSpawnDestroyEnvironment& InEnv, FSequenceCounter& InSequence, FActorEventState& InEvents) noexcept
{
	return InEnv.CreateDerivedObject<FOrderingActor>(OrderingActorTypeId, "OrderingActor", InSequence, InEvents);
}

/**
 * Motivation: Builds one ordering component through its derived descriptor in the environment.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
inline TObjectPtr<FOrderingComponent> MakeOrderingComponent(
	FSpawnDestroyEnvironment& InEnv, FSequenceCounter& InSequence, FComponentEventState& InEvents) noexcept
{
	return InEnv.CreateDerivedObject<FOrderingComponent>(OrderingComponentTypeId, "OrderingComponent", InSequence, InEvents);
}

/**
 * Motivation: Builds one plain actor through its derived descriptor in the environment.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
inline TObjectPtr<FPlainActor> MakePlainActor(FSpawnDestroyEnvironment& InEnv) noexcept
{
	return InEnv.CreateDerivedObject<FPlainActor>(PlainActorTypeId, "PlainActor");
}

/**
 * Motivation: Owns a fixed worklist and collector bound to an environment's store for GC assertions.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FCollectorFixture final
{
public:
	/**
	 * Motivation: Binds a collector to the store using this fixture's caller-owned worklist storage.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	explicit FCollectorFixture(FObjectStore& InStore) noexcept : Collector(InStore, FGarbageCollectorStorage{Worklist, CollectorWorklistCapacity}) {}

	/**
	 * Motivation: Tests can run a full cycle and read its stats.
	 * Responsibilities: Exposes the collector.
	 */
	FGarbageCollector& GetCollector() noexcept { return Collector; }

private:
	/** Motivation: Backs the collector's reachable-object queue without heap storage. */
	FObjectHandle Worklist[CollectorWorklistCapacity]{};
	/** Motivation: Owns the collector bound to this fixture's worklist for the test's lifetime. */
	FGarbageCollector Collector;
};

/**
 * Motivation: Builds a fresh standalone store so cross-store tests can use a foreign owner.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FSecondStore final
{
public:
	/**
	 * Motivation: Registers the engine base and plain test descriptors into this store's registry.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FSecondStore() noexcept : Store(MakeStorage(), MakeClassRegistryView(Registry))
	{
		(void)Registry.Register(UActorComponent::StaticClassDescriptor());
		(void)Registry.Register(AActor::StaticClassDescriptor());
		(void)Registry.Register(UWorld::StaticClassDescriptor());
		(void)Registry.Register(MakeClassDescriptor<FPlainActor>(
			PlainActorTypeId, "PlainActor", Registry.Find(MicroWorld::Engine::AActorClassId), &MicroWorld::Engine::TraceManagedObjectReferences));
		(void)Registry.Register(MakeClassDescriptor<FPlainComponent>(
			PlainComponentTypeId,
			"PlainComponent",
			Registry.Find(MicroWorld::Engine::UActorComponentClassId),
			&MicroWorld::Engine::TraceManagedObjectReferences));
	}

	/**
	 * Motivation: Returns the foreign store used to mint cross-store references.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FObjectStore& GetStore() noexcept { return Store; }

	/**
	 * Motivation: Tests can find its plain-actor descriptor.
	 * Responsibilities: Returns the foreign registry.
	 */
	TClassRegistry<8>& GetRegistry() noexcept { return Registry; }

private:
	/**
	 * Motivation: Describes this foreign store's complete caller-owned storage.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FObjectStoreStorage MakeStorage() noexcept
	{
		return FObjectStoreStorage{SlotBytes.data(), SlotBytes.size(), Slots.data(), SlotCount, 256, 16, Roots.data(), RootCapacity};
	}

	/** Motivation: Fixes the foreign store's object-slot count. */
	static constexpr std::uint32_t SlotCount{4};
	/** Motivation: Fixes the foreign store's root-table capacity. */
	static constexpr std::uint32_t RootCapacity{4};
	/** Motivation: Keeps every foreign slot aligned for placement construction. */
	alignas(16) std::array<std::byte, 256 * SlotCount> SlotBytes{};
	/** Motivation: Gives the foreign store one lifecycle record per slot. */
	std::array<FObjectSlotMetadata, SlotCount> Slots{};
	/** Motivation: Gives the foreign store its independent root entries. */
	std::array<FObjectRootEntry, RootCapacity> Roots{};
	/** Motivation: Owns the foreign class registry. */
	TClassRegistry<8> Registry;
	/** Motivation: Owns the foreign managed lifetimes for the test's duration. */
	FObjectStore Store;
};

} // namespace MicroWorld::Tests
