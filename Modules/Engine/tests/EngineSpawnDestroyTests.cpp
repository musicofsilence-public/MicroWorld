#include "EngineTestSupport.h"
#include "TestSupport.h"

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Object/ClassDescriptor.h>
#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/Object.h>
#include <MicroWorld/Object/ObjectPtr.h>
#include <MicroWorld/Object/ObjectStore.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{

using MicroWorld::AActor;
using MicroWorld::DurationMilliseconds;
using MicroWorld::EEngineResult;
using MicroWorld::EObjectResult;
using MicroWorld::ERuntimeResult;
using MicroWorld::FActorComponentRegistry;
using MicroWorld::FActorComponentRegistryBase;
using MicroWorld::FClassDescriptor;
using MicroWorld::FGarbageCollectionResult;
using MicroWorld::FGarbageCollector;
using MicroWorld::FGarbageCollectorStorage;
using MicroWorld::FObjectHandle;
using MicroWorld::FObjectMutationResult;
using MicroWorld::FObjectRootEntry;
using MicroWorld::FObjectSlotMetadata;
using MicroWorld::FObjectStore;
using MicroWorld::FObjectStoreStats;
using MicroWorld::FObjectStoreStorage;
using MicroWorld::FTickConfiguration;
using MicroWorld::FTickContext;
using MicroWorld::FWorldActorRegistry;
using MicroWorld::FWorldActorRegistryBase;
using MicroWorld::MakeClassDescriptor;
using MicroWorld::MakeClassRegistryView;
using MicroWorld::TClassRegistry;
using MicroWorld::TObjectPtr;
using MicroWorld::TStrongObjectPtr;
using MicroWorld::TWeakObjectPtr;
using MicroWorld::UActorComponent;
using MicroWorld::UWorld;
using MicroWorld::Tests::FActorEventState;
using MicroWorld::Tests::FComponentEventState;
using MicroWorld::Tests::FSequenceCounter;
using MicroWorld::Tests::TEngineEnvironment;

/** Tick configuration that lets ordering types tick on every advance. */
constexpr FTickConfiguration OrderingTickConfiguration{true, true, DurationMilliseconds{0}};

/** Test-local type ids for the ordering, plain, and component descriptors. */
constexpr MicroWorld::FTypeId OrderingActorTypeId{0x00040001u};
constexpr MicroWorld::FTypeId OrderingComponentTypeId{0x00040002u};
constexpr MicroWorld::FTypeId PlainActorTypeId{0x00040003u};
constexpr MicroWorld::FTypeId PlainComponentTypeId{0x00040004u};

/** A component that records begin/tick/end ordering into per-instance state. */
class FOrderingComponent final : public UActorComponent
{
public:
	/** Binds this component to the shared sequence and its own observed event record. */
	FOrderingComponent(FSequenceCounter& InSequence, FComponentEventState& InEvents) noexcept
		: UActorComponent(OrderingTickConfiguration), Sequence(InSequence), Events(InEvents)
	{
	}

protected:
	/** Records the sequence value and count of this component's begin hook. */
	void BeginPlay() noexcept override
	{
		Events.BeginOrder = Sequence.Next();
		++Events.BeginCount;
	}
	/** Records the sequence value and count of this component's tick hook. */
	void TickComponent(const FTickContext&) noexcept override
	{
		Events.TickOrder = Sequence.Next();
		++Events.TickCount;
	}
	/** Records the sequence value and count of this component's end hook. */
	void EndPlay() noexcept override
	{
		Events.EndOrder = Sequence.Next();
		++Events.EndCount;
	}

private:
	/** Shares one monotonic order source with every observed type in the test. */
	FSequenceCounter& Sequence;
	/** Receives this component's observed begin/tick/end ordering and counts. */
	FComponentEventState& Events;
};

/** An actor that records begin/tick/end ordering into per-instance state. */
class FOrderingActor final : public AActor
{
public:
	/** Binds this actor to its component lease, the shared sequence, and its event record. */
	FOrderingActor(FActorComponentRegistryBase Components, FSequenceCounter& InSequence, FActorEventState& InEvents) noexcept
		: AActor(std::move(Components), OrderingTickConfiguration), Sequence(InSequence), Events(InEvents)
	{
	}

protected:
	/** Records the sequence value and count of this actor's begin hook. */
	void BeginPlay() noexcept override
	{
		Events.BeginOrder = Sequence.Next();
		++Events.BeginCount;
	}
	/** Records the sequence value and count of this actor's tick hook. */
	void Tick(const FTickContext&) noexcept override
	{
		Events.TickOrder = Sequence.Next();
		++Events.TickCount;
	}
	/** Records the sequence value and count of this actor's end hook. */
	void EndPlay() noexcept override
	{
		Events.EndOrder = Sequence.Next();
		++Events.EndCount;
	}

private:
	/** Shares one monotonic order source with every observed type in the test. */
	FSequenceCounter& Sequence;
	/** Receives this actor's observed begin/tick/end ordering and counts. */
	FActorEventState& Events;
};

/** A minimal component used where lifetime ordering does not need observing. */
class FPlainComponent final : public UActorComponent
{
public:
	/** Constructs a component with the default (non-ticking) configuration. */
	FPlainComponent() noexcept : UActorComponent() {}
};

/** A minimal actor used where lifetime ordering does not need observing. */
class FPlainActor final : public AActor
{
public:
	/** Binds this actor to the caller-owned component registry lease. */
	explicit FPlainActor(FActorComponentRegistryBase Components) noexcept : AActor(std::move(Components)) {}
};

/** Environment sized for spawn/destroy tests with room for several actors and components. */
using FSpawnDestroyEnvironment = TEngineEnvironment<256, 16, 16, 4>;

/** Builds one ordering actor through its derived descriptor in the environment. */
TObjectPtr<FOrderingActor> MakeOrderingActor(
	FSpawnDestroyEnvironment& Env, FActorComponentRegistryBase Components, FSequenceCounter& Sequence, FActorEventState& Events) noexcept
{
	return Env.CreateDerivedObject<FOrderingActor>(OrderingActorTypeId, "OrderingActor", std::move(Components), Sequence, Events);
}

/** Builds one ordering component through its derived descriptor in the environment. */
TObjectPtr<FOrderingComponent> MakeOrderingComponent(FSpawnDestroyEnvironment& Env, FSequenceCounter& Sequence, FComponentEventState& Events) noexcept
{
	return Env.CreateDerivedObject<FOrderingComponent>(OrderingComponentTypeId, "OrderingComponent", Sequence, Events);
}

/** Builds one plain actor through its derived descriptor in the environment. */
TObjectPtr<FPlainActor> MakePlainActor(FSpawnDestroyEnvironment& Env, FActorComponentRegistryBase Components) noexcept
{
	return Env.CreateDerivedObject<FPlainActor>(PlainActorTypeId, "PlainActor", std::move(Components));
}

/** Owns a fixed worklist and collector bound to an environment's store for GC assertions. */
class FCollectorFixture final
{
public:
	/** Binds a collector to the store using this fixture's caller-owned worklist storage. */
	explicit FCollectorFixture(FObjectStore& Store) noexcept
		: Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())})
	{
	}

	/** Exposes the collector so tests can run a full cycle and read its stats. */
	FGarbageCollector& GetCollector() noexcept { return Collector; }

private:
	/** Backs the collector's reachable-object queue without heap storage. */
	std::array<FObjectHandle, 16> Worklist{};
	/** Owns the collector bound to this fixture's worklist for the test's lifetime. */
	FGarbageCollector Collector;
};

/** Builds a fresh standalone store so cross-store tests can use a foreign owner. */
class FSecondStore final
{
public:
	/** Registers the engine base and plain test descriptors into this store's registry. */
	FSecondStore() noexcept : Store(MakeStorage(), MakeClassRegistryView(Registry))
	{
		(void)Registry.Register(UActorComponent::StaticClassDescriptor());
		(void)Registry.Register(AActor::StaticClassDescriptor());
		(void)Registry.Register(UWorld::StaticClassDescriptor());
		(void)Registry.Register(MakeClassDescriptor<FPlainActor>(
			PlainActorTypeId, "PlainActor", Registry.Find(MicroWorld::AActorClassId), &MicroWorld::TraceManagedObjectReferences));
		(void)Registry.Register(MakeClassDescriptor<FPlainComponent>(
			PlainComponentTypeId, "PlainComponent", Registry.Find(MicroWorld::UActorComponentClassId), &MicroWorld::TraceManagedObjectReferences));
	}

	/** Returns the foreign store used to mint cross-store references. */
	FObjectStore& GetStore() noexcept { return Store; }

	/** Returns the foreign registry so tests can find its plain-actor descriptor. */
	TClassRegistry<8>& GetRegistry() noexcept { return Registry; }

private:
	/** Describes this foreign store's complete caller-owned storage. */
	FObjectStoreStorage MakeStorage() noexcept
	{
		return FObjectStoreStorage{SlotBytes.data(), SlotBytes.size(), Slots.data(), SlotCount, 256, 16, Roots.data(), RootCapacity};
	}

	/** Fixes the foreign store's object-slot count. */
	static constexpr std::uint32_t SlotCount{4};
	/** Fixes the foreign store's root-table capacity. */
	static constexpr std::uint32_t RootCapacity{4};
	/** Keeps every foreign slot aligned for placement construction. */
	alignas(16) std::array<std::byte, 256 * SlotCount> SlotBytes{};
	/** Gives the foreign store one lifecycle record per slot. */
	std::array<FObjectSlotMetadata, SlotCount> Slots{};
	/** Gives the foreign store its independent root entries. */
	std::array<FObjectRootEntry, RootCapacity> Roots{};
	/** Owns the foreign class registry. */
	TClassRegistry<8> Registry;
	/** Owns the foreign managed lifetimes for the test's duration. */
	FObjectStore Store;
};

/**
 * Proves SpawnActor only queues while playing and the queued actor receives its
 * BeginPlay at the next ApplyPending barrier, never at the SpawnActor call.
 */
MW_TEST_CASE(EngineSpawnActorBeginsAtNextBarrierNotImmediately)
{
	FSequenceCounter Sequence{};
	FActorEventState SpawnedEvents{};

	FSpawnDestroyEnvironment Env{};
	FWorldActorRegistry<2> WorldActors;
	FActorComponentRegistry<0> SpawnedComponents;

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, WorldActors.MakeView());
	const TObjectPtr<FOrderingActor> Spawned = MakeOrderingActor(Env, SpawnedComponents.MakeView(), Sequence, SpawnedEvents);

	const ERuntimeResult BeginResult = World.Get()->BeginPlay(0);
	const EEngineResult SpawnResult = World.Get()->SpawnActor(TObjectPtr<AActor>{Spawned});
	const std::uint32_t BeginCountAfterQueue = SpawnedEvents.BeginCount;
	const std::size_t PendingAfterQueue = World.Get()->PendingSpawnCount();
	const std::size_t LiveAfterQueue = WorldActors.GetCount();

	const ERuntimeResult ApplyResult = World.Get()->ApplyPending(10);
	const std::uint32_t BeginCountAfterBarrier = SpawnedEvents.BeginCount;
	const std::size_t PendingAfterBarrier = World.Get()->PendingSpawnCount();
	const std::size_t LiveAfterBarrier = WorldActors.GetCount();

	const ERuntimeResult AdvanceResult = World.Get()->Advance(20);

	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "BeginPlay should succeed on the empty world");
	MW_EXPECT_EQ(Test, EEngineResult::Success, SpawnResult, "A same-store unowned actor is accepted for deferred spawn");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, BeginCountAfterQueue, "A queued spawn must not begin at the SpawnActor call");
	MW_EXPECT_EQ(Test, std::size_t{1}, PendingAfterQueue, "The queued spawn is observable as one pending spawn");
	MW_EXPECT_EQ(Test, std::size_t{0}, LiveAfterQueue, "A queued spawn must not join the live registry before the barrier");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ApplyResult, "ApplyPending should succeed applying the spawn");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, BeginCountAfterBarrier, "The spawned actor begins exactly once at the barrier");
	MW_EXPECT_EQ(Test, std::size_t{0}, PendingAfterBarrier, "The barrier drains the pending-spawn queue");
	MW_EXPECT_EQ(Test, std::size_t{1}, LiveAfterBarrier, "The spawned actor joins the live registry at the barrier");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, AdvanceResult, "Advance after the barrier should succeed");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, SpawnedEvents.TickCount, "A spawned actor ticks as a live participant after the barrier");
}

/**
 * Proves DestroyActor only queues while playing and the queued actor ends at the
 * barrier, with its own EndPlay before its components end in reverse order.
 */
MW_TEST_CASE(EngineDestroyActorEndsAtBarrierWithReverseComponentShutdown)
{
	FSequenceCounter Sequence{};
	FActorEventState ActorEvents{};
	FComponentEventState FirstComponentEvents{};
	FComponentEventState SecondComponentEvents{};

	FSpawnDestroyEnvironment Env{};
	FWorldActorRegistry<1> WorldActors;
	FActorComponentRegistry<2> ActorComponents;

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, WorldActors.MakeView());
	const TObjectPtr<FOrderingActor> Actor = MakeOrderingActor(Env, ActorComponents.MakeView(), Sequence, ActorEvents);
	const TObjectPtr<FOrderingComponent> FirstComponent = MakeOrderingComponent(Env, Sequence, FirstComponentEvents);
	const TObjectPtr<FOrderingComponent> SecondComponent = MakeOrderingComponent(Env, Sequence, SecondComponentEvents);
	(void)Actor.Get()->RegisterComponent(FirstComponent);
	(void)Actor.Get()->RegisterComponent(SecondComponent);
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{Actor});
	(void)World.Get()->BeginPlay(0);

	const EEngineResult DestroyResult = World.Get()->DestroyActor(TObjectPtr<AActor>{Actor});
	const std::uint32_t EndCountAfterQueue = ActorEvents.EndCount;
	const std::size_t PendingAfterQueue = World.Get()->PendingDestroyCount();

	const ERuntimeResult ApplyResult = World.Get()->ApplyPending(10);

	MW_EXPECT_EQ(Test, EEngineResult::Success, DestroyResult, "A registered actor is accepted for deferred destroy");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, EndCountAfterQueue, "A queued destroy must not end the actor at the DestroyActor call");
	MW_EXPECT_EQ(Test, std::size_t{1}, PendingAfterQueue, "The queued destroy is observable as one pending destroy");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ApplyResult, "ApplyPending should succeed applying the destroy");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, ActorEvents.EndCount, "The destroyed actor ends exactly once at the barrier");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, FirstComponentEvents.EndCount, "The first component ends exactly once at the barrier");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, SecondComponentEvents.EndCount, "The second component ends exactly once at the barrier");
	MW_EXPECT_TRUE(Test, ActorEvents.EndOrder < SecondComponentEvents.EndOrder, "The actor ends before its components");
	MW_EXPECT_TRUE(Test, SecondComponentEvents.EndOrder < FirstComponentEvents.EndOrder, "Components end in reverse registration order");
	MW_EXPECT_EQ(Test, std::size_t{0}, World.Get()->PendingDestroyCount(), "The barrier drains the pending-destroy queue");
	MW_EXPECT_EQ(Test, std::size_t{0}, WorldActors.GetCount(), "The destroyed actor leaves the live registry at the barrier");
}

/**
 * Proves spawn capacity counts live plus pending-spawn actors together, both
 * before the barrier applies the queue and after it fills the live registry.
 */
MW_TEST_CASE(EngineSpawnCapacityCountsLiveAndPending)
{
	FSpawnDestroyEnvironment Env{};
	FWorldActorRegistry<2> WorldActors;
	FActorComponentRegistry<0> PreRegisteredComponents;
	FActorComponentRegistry<0> FirstSpawnComponents;
	FActorComponentRegistry<0> SecondSpawnComponents;

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, WorldActors.MakeView());
	const TObjectPtr<FPlainActor> PreRegistered = MakePlainActor(Env, PreRegisteredComponents.MakeView());
	const TObjectPtr<FPlainActor> FirstSpawn = MakePlainActor(Env, FirstSpawnComponents.MakeView());
	const TObjectPtr<FPlainActor> SecondSpawn = MakePlainActor(Env, SecondSpawnComponents.MakeView());
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{PreRegistered});
	(void)World.Get()->BeginPlay(0);

	// One live actor plus one pending spawn already reaches the capacity of two.
	const EEngineResult FirstSpawnResult = World.Get()->SpawnActor(TObjectPtr<AActor>{FirstSpawn});
	const EEngineResult OverCapacityBeforeBarrier = World.Get()->SpawnActor(TObjectPtr<AActor>{SecondSpawn});
	const std::size_t PendingAfterReject = World.Get()->PendingSpawnCount();
	const bool bSecondOwnedAfterReject = SecondSpawn.Get()->HasAssignedWorld();

	const ERuntimeResult ApplyResult = World.Get()->ApplyPending(10);
	// Two live actors still fill the capacity, so a further spawn is rejected.
	const EEngineResult OverCapacityAfterBarrier = World.Get()->SpawnActor(TObjectPtr<AActor>{SecondSpawn});

	MW_EXPECT_EQ(Test, EEngineResult::Success, FirstSpawnResult, "A spawn that reaches live-plus-pending capacity is accepted");
	MW_EXPECT_EQ(Test, EEngineResult::CapacityExceeded, OverCapacityBeforeBarrier, "Live plus pending-spawn at capacity rejects a further spawn");
	MW_EXPECT_EQ(Test, std::size_t{1}, PendingAfterReject, "A capacity-rejected spawn leaves the pending queue unchanged");
	MW_EXPECT_TRUE(Test, !bSecondOwnedAfterReject, "A capacity-rejected spawn must not bind a world identity");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ApplyResult, "ApplyPending should apply the accepted spawn");
	MW_EXPECT_EQ(Test, std::size_t{2}, WorldActors.GetCount(), "The barrier fills the live registry to capacity");
	MW_EXPECT_EQ(Test, EEngineResult::CapacityExceeded, OverCapacityAfterBarrier, "A full live registry still rejects further spawns");
}

/**
 * Proves a repeated spawn request is rejected as a duplicate both while the
 * first request is pending and after the barrier makes the actor live.
 */
MW_TEST_CASE(EngineDuplicateSpawnRejected)
{
	FSpawnDestroyEnvironment Env{};
	FWorldActorRegistry<4> WorldActors;
	FActorComponentRegistry<0> ActorComponents;

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, WorldActors.MakeView());
	const TObjectPtr<FPlainActor> Actor = MakePlainActor(Env, ActorComponents.MakeView());
	(void)World.Get()->BeginPlay(0);

	const EEngineResult FirstSpawn = World.Get()->SpawnActor(TObjectPtr<AActor>{Actor});
	const EEngineResult DuplicateWhilePending = World.Get()->SpawnActor(TObjectPtr<AActor>{Actor});
	const std::size_t PendingAfterDuplicate = World.Get()->PendingSpawnCount();

	(void)World.Get()->ApplyPending(10);
	const EEngineResult DuplicateWhileLive = World.Get()->SpawnActor(TObjectPtr<AActor>{Actor});

	MW_EXPECT_EQ(Test, EEngineResult::Success, FirstSpawn, "The first spawn request is accepted");
	MW_EXPECT_EQ(Test, EEngineResult::Duplicate, DuplicateWhilePending, "A second request for a pending-spawn actor is a duplicate");
	MW_EXPECT_EQ(Test, std::size_t{1}, PendingAfterDuplicate, "A duplicate spawn leaves the pending queue unchanged");
	MW_EXPECT_EQ(Test, EEngineResult::Duplicate, DuplicateWhileLive, "A spawn request for an already-live actor is a duplicate");
	MW_EXPECT_EQ(Test, std::size_t{1}, WorldActors.GetCount(), "The duplicate live request does not grow the registry");
}

/**
 * Proves destroying an actor that was never registered with this world is
 * rejected as an invalid reference and leaves the destroy queue unchanged.
 */
MW_TEST_CASE(EngineDestroyOfNeverRegisteredActorRejected)
{
	FSpawnDestroyEnvironment Env{};
	FWorldActorRegistry<2> WorldActors;
	FActorComponentRegistry<0> RegisteredComponents;
	FActorComponentRegistry<0> StrangerComponents;

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, WorldActors.MakeView());
	const TObjectPtr<FPlainActor> Registered = MakePlainActor(Env, RegisteredComponents.MakeView());
	const TObjectPtr<FPlainActor> Stranger = MakePlainActor(Env, StrangerComponents.MakeView());
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{Registered});
	(void)World.Get()->BeginPlay(0);

	const EEngineResult DestroyResult = World.Get()->DestroyActor(TObjectPtr<AActor>{Stranger});

	MW_EXPECT_EQ(Test, EEngineResult::InvalidReference, DestroyResult, "Destroying a never-registered actor is rejected as invalid");
	MW_EXPECT_EQ(Test, std::size_t{0}, World.Get()->PendingDestroyCount(), "A rejected destroy leaves the pending queue unchanged");
	MW_EXPECT_EQ(Test, std::size_t{1}, WorldActors.GetCount(), "A rejected destroy leaves the live registry unchanged");
}

/**
 * Proves spawn and destroy are lifecycle-locked outside the playing state: a
 * constructed world and an ended world both reject them without queueing.
 */
MW_TEST_CASE(EngineSpawnAndDestroyRejectedOutsidePlayingLifecycle)
{
	FSpawnDestroyEnvironment Env{};
	FWorldActorRegistry<2> ConstructedWorldActors;
	FWorldActorRegistry<2> EndedWorldActors;
	FActorComponentRegistry<0> ConstructedSpawnComponents;
	FActorComponentRegistry<0> EndedRegisteredComponents;
	FActorComponentRegistry<0> EndedSpawnComponents;

	// A world that never began play rejects both structural requests.
	const TObjectPtr<UWorld> ConstructedWorld = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, ConstructedWorldActors.MakeView());
	const TObjectPtr<FPlainActor> RegisteredBeforePlay = MakePlainActor(Env, ConstructedSpawnComponents.MakeView());
	(void)ConstructedWorld.Get()->RegisterActor(TObjectPtr<AActor>{RegisteredBeforePlay});
	const EEngineResult SpawnWhileConstructed = ConstructedWorld.Get()->SpawnActor(TObjectPtr<AActor>{RegisteredBeforePlay});
	const EEngineResult DestroyWhileConstructed = ConstructedWorld.Get()->DestroyActor(TObjectPtr<AActor>{RegisteredBeforePlay});

	// A world that ended play rejects both structural requests.
	const TObjectPtr<UWorld> EndedWorld = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, EndedWorldActors.MakeView());
	const TObjectPtr<FPlainActor> RegisteredActor = MakePlainActor(Env, EndedRegisteredComponents.MakeView());
	const TObjectPtr<FPlainActor> WouldSpawn = MakePlainActor(Env, EndedSpawnComponents.MakeView());
	(void)EndedWorld.Get()->RegisterActor(TObjectPtr<AActor>{RegisteredActor});
	(void)EndedWorld.Get()->BeginPlay(0);
	(void)EndedWorld.Get()->EndPlay();
	const EEngineResult SpawnAfterEnd = EndedWorld.Get()->SpawnActor(TObjectPtr<AActor>{WouldSpawn});
	const EEngineResult DestroyAfterEnd = EndedWorld.Get()->DestroyActor(TObjectPtr<AActor>{RegisteredActor});

	MW_EXPECT_EQ(Test, EEngineResult::LifecycleLocked, SpawnWhileConstructed, "SpawnActor before BeginPlay is lifecycle-locked");
	MW_EXPECT_EQ(Test, EEngineResult::LifecycleLocked, DestroyWhileConstructed, "DestroyActor before BeginPlay is lifecycle-locked");
	MW_EXPECT_EQ(Test, std::size_t{0}, ConstructedWorld.Get()->PendingSpawnCount(), "A rejected constructed-world spawn queues nothing");
	MW_EXPECT_EQ(Test, std::size_t{0}, ConstructedWorld.Get()->PendingDestroyCount(), "A rejected constructed-world destroy queues nothing");
	MW_EXPECT_EQ(Test, EEngineResult::LifecycleLocked, SpawnAfterEnd, "SpawnActor after EndPlay is lifecycle-locked");
	MW_EXPECT_EQ(Test, EEngineResult::LifecycleLocked, DestroyAfterEnd, "DestroyActor after EndPlay is lifecycle-locked");
	MW_EXPECT_EQ(Test, std::size_t{0}, EndedWorld.Get()->PendingSpawnCount(), "A rejected ended-world spawn queues nothing");
	MW_EXPECT_EQ(Test, std::size_t{0}, EndedWorld.Get()->PendingDestroyCount(), "A rejected ended-world destroy queues nothing");
	MW_EXPECT_TRUE(Test, !WouldSpawn.Get()->HasAssignedWorld(), "A rejected spawn candidate keeps no world identity");
}

/**
 * Proves every SpawnActor reference rejection (empty, cross-store, already-owned)
 * returns its exact code and leaves the pending queue and candidate unchanged.
 */
MW_TEST_CASE(EngineSpawnReferenceRejectionsLeaveStateUnchanged)
{
	FSpawnDestroyEnvironment Env{};
	FSecondStore ForeignStoreOwner{};
	FObjectStore& ForeignStore = ForeignStoreOwner.GetStore();

	FWorldActorRegistry<4> WorldActors;
	FWorldActorRegistry<2> OtherWorldActors;
	FActorComponentRegistry<0> OwnedActorComponents;
	FActorComponentRegistry<0> ForeignActorComponents;

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, WorldActors.MakeView());
	const TObjectPtr<UWorld> OtherWorld = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, OtherWorldActors.MakeView());
	const TObjectPtr<FPlainActor> OwnedByOther = MakePlainActor(Env, OwnedActorComponents.MakeView());
	const TObjectPtr<FPlainActor> ForeignActor =
		ForeignStore.NewObject<FPlainActor>(*ForeignStoreOwner.GetRegistry().Find(PlainActorTypeId), ForeignActorComponents.MakeView()).Object;
	// Bind OwnedByOther to a different world so the spawning world sees it as owned.
	(void)OtherWorld.Get()->RegisterActor(TObjectPtr<AActor>{OwnedByOther});
	(void)World.Get()->BeginPlay(0);

	const EEngineResult EmptyResult = World.Get()->SpawnActor(TObjectPtr<AActor>{});
	const EEngineResult CrossStoreResult = World.Get()->SpawnActor(TObjectPtr<AActor>{ForeignActor});
	const EEngineResult AlreadyOwnedResult = World.Get()->SpawnActor(TObjectPtr<AActor>{OwnedByOther});

	MW_EXPECT_EQ(Test, EEngineResult::InvalidReference, EmptyResult, "An empty spawn reference is rejected as invalid");
	MW_EXPECT_EQ(Test, EEngineResult::CrossStore, CrossStoreResult, "A foreign-store spawn reference is rejected as cross-store");
	MW_EXPECT_EQ(Test, EEngineResult::AlreadyOwned, AlreadyOwnedResult, "An actor owned by another world is rejected as already owned");
	MW_EXPECT_EQ(Test, std::size_t{0}, World.Get()->PendingSpawnCount(), "Every rejected spawn leaves the pending queue empty");
}

/**
 * Proves every DestroyActor reference rejection (empty, cross-store, repeated)
 * returns its exact code and leaves the pending-destroy queue accurate.
 */
MW_TEST_CASE(EngineDestroyReferenceRejectionsLeaveStateUnchanged)
{
	FSpawnDestroyEnvironment Env{};
	FSecondStore ForeignStoreOwner{};
	FObjectStore& ForeignStore = ForeignStoreOwner.GetStore();

	FWorldActorRegistry<2> WorldActors;
	FActorComponentRegistry<0> RegisteredComponents;
	FActorComponentRegistry<0> ForeignActorComponents;

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, WorldActors.MakeView());
	const TObjectPtr<FPlainActor> Registered = MakePlainActor(Env, RegisteredComponents.MakeView());
	const TObjectPtr<FPlainActor> ForeignActor =
		ForeignStore.NewObject<FPlainActor>(*ForeignStoreOwner.GetRegistry().Find(PlainActorTypeId), ForeignActorComponents.MakeView()).Object;
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{Registered});
	(void)World.Get()->BeginPlay(0);

	const EEngineResult EmptyResult = World.Get()->DestroyActor(TObjectPtr<AActor>{});
	const EEngineResult CrossStoreResult = World.Get()->DestroyActor(TObjectPtr<AActor>{ForeignActor});
	const EEngineResult FirstDestroy = World.Get()->DestroyActor(TObjectPtr<AActor>{Registered});
	const EEngineResult RepeatedDestroy = World.Get()->DestroyActor(TObjectPtr<AActor>{Registered});

	MW_EXPECT_EQ(Test, EEngineResult::InvalidReference, EmptyResult, "An empty destroy reference is rejected as invalid");
	MW_EXPECT_EQ(Test, EEngineResult::CrossStore, CrossStoreResult, "A foreign-store destroy reference is rejected as cross-store");
	MW_EXPECT_EQ(Test, EEngineResult::Success, FirstDestroy, "A registered actor is accepted for one deferred destroy");
	MW_EXPECT_EQ(Test, EEngineResult::Duplicate, RepeatedDestroy, "A repeated destroy of a pending actor is a duplicate");
	MW_EXPECT_EQ(Test, std::size_t{1}, World.Get()->PendingDestroyCount(), "Only the accepted destroy is queued after the rejections");
}

/**
 * Proves removing a middle actor at the barrier preserves the registration order
 * of the survivors, so their next tick dispatch stays in registration order.
 */
MW_TEST_CASE(EngineSurvivorDispatchOrderPreservedAfterMidListRemoval)
{
	FSequenceCounter Sequence{};
	FActorEventState FirstEvents{};
	FActorEventState MiddleEvents{};
	FActorEventState LastEvents{};

	FSpawnDestroyEnvironment Env{};
	FWorldActorRegistry<3> WorldActors;
	FActorComponentRegistry<0> FirstComponents;
	FActorComponentRegistry<0> MiddleComponents;
	FActorComponentRegistry<0> LastComponents;

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, WorldActors.MakeView());
	const TObjectPtr<FOrderingActor> First = MakeOrderingActor(Env, FirstComponents.MakeView(), Sequence, FirstEvents);
	const TObjectPtr<FOrderingActor> Middle = MakeOrderingActor(Env, MiddleComponents.MakeView(), Sequence, MiddleEvents);
	const TObjectPtr<FOrderingActor> Last = MakeOrderingActor(Env, LastComponents.MakeView(), Sequence, LastEvents);
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{First});
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{Middle});
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{Last});
	(void)World.Get()->BeginPlay(0);

	(void)World.Get()->DestroyActor(TObjectPtr<AActor>{Middle});
	const ERuntimeResult ApplyResult = World.Get()->ApplyPending(10);
	Sequence.Next(); // Delimits the barrier's end events from the survivor tick order.
	const ERuntimeResult AdvanceResult = World.Get()->Advance(20);

	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ApplyResult, "ApplyPending should remove the middle actor");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, AdvanceResult, "Advance over the survivors should succeed");
	MW_EXPECT_EQ(Test, std::size_t{2}, WorldActors.GetCount(), "The two survivors remain in the live registry");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, MiddleEvents.TickCount, "The removed middle actor never ticks after the barrier");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, FirstEvents.TickCount, "The first survivor ticks once on the advance");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, LastEvents.TickCount, "The last survivor ticks once on the advance");
	MW_EXPECT_TRUE(Test, FirstEvents.TickOrder < LastEvents.TickOrder, "Survivor dispatch keeps first-before-last registration order");
}

/**
 * Proves destroying an actor still queued to spawn in the same frame is rejected
 * as an invalid reference (it is not yet registered) and the spawn still applies.
 *
 * This is the documented behavior of the section-5 validation table: a destroy
 * targets only an actor registered with the world, and a pending-spawn actor is
 * not registered until the barrier begins it. Destroy does not cancel a spawn.
 */
MW_TEST_CASE(EngineSpawnThenDestroySameActorInOneFrame)
{
	FSequenceCounter Sequence{};
	FActorEventState ActorEvents{};

	FSpawnDestroyEnvironment Env{};
	FWorldActorRegistry<2> WorldActors;
	FActorComponentRegistry<0> ActorComponents;

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, WorldActors.MakeView());
	const TObjectPtr<FOrderingActor> Actor = MakeOrderingActor(Env, ActorComponents.MakeView(), Sequence, ActorEvents);
	(void)World.Get()->BeginPlay(0);

	const EEngineResult SpawnResult = World.Get()->SpawnActor(TObjectPtr<AActor>{Actor});
	const EEngineResult DestroyResult = World.Get()->DestroyActor(TObjectPtr<AActor>{Actor});
	const std::size_t PendingSpawnAfter = World.Get()->PendingSpawnCount();
	const std::size_t PendingDestroyAfter = World.Get()->PendingDestroyCount();

	const ERuntimeResult ApplyResult = World.Get()->ApplyPending(10);

	MW_EXPECT_EQ(Test, EEngineResult::Success, SpawnResult, "The spawn request is accepted");
	MW_EXPECT_EQ(Test, EEngineResult::InvalidReference, DestroyResult, "Destroying a still-pending-spawn actor is rejected as invalid");
	MW_EXPECT_EQ(Test, std::size_t{1}, PendingSpawnAfter, "The rejected destroy leaves the spawn queued");
	MW_EXPECT_EQ(Test, std::size_t{0}, PendingDestroyAfter, "The rejected destroy queues nothing");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ApplyResult, "ApplyPending should still apply the queued spawn");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, ActorEvents.BeginCount, "The spawn still begins at the barrier despite the rejected destroy");
	MW_EXPECT_EQ(Test, std::size_t{1}, WorldActors.GetCount(), "The spawned actor joins the live registry at the barrier");
}

/**
 * Proves a destroyed actor's handle is hidden at the barrier and becomes durably
 * stale after the store's destruction barrier reclaims its slot: the slot is
 * reused with a fresh generation, so the original handle can never resolve again.
 */
MW_TEST_CASE(EngineDestroyedActorHandleGoesStaleAfterReclamation)
{
	FSpawnDestroyEnvironment Env{};
	FObjectStore& Store = Env.GetStore();
	FWorldActorRegistry<1> WorldActors;
	FActorComponentRegistry<1> ActorComponents;
	FActorComponentRegistry<1> ReplacementComponents;

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, WorldActors.MakeView());
	const TObjectPtr<FPlainActor> Actor = MakePlainActor(Env, ActorComponents.MakeView());
	const TObjectPtr<FPlainComponent> Component = Env.CreateDerivedObject<FPlainComponent>(PlainComponentTypeId, "PlainComponent");
	(void)Actor.Get()->RegisterComponent(Component);
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{Actor});
	TStrongObjectPtr<UWorld> WorldRoot = Env.MakeRoot(World);
	(void)World.Get()->BeginPlay(0);

	const FObjectHandle OriginalHandle = Actor.Handle();
	const TWeakObjectPtr<AActor> ActorWeak{TObjectPtr<AActor>{Actor}};
	(void)World.Get()->DestroyActor(TObjectPtr<AActor>{Actor});
	(void)World.Get()->ApplyPending(10);
	const bool bHiddenAtBarrier = Actor.Get() == nullptr;
	const bool bWeakExpiredAtBarrier = ActorWeak.IsExpired();

	const FObjectMutationResult Reclaim = Store.ApplyPendingDestroy(16);
	// Reuse the vacated slot to prove the original handle's generation is stale.
	const TObjectPtr<FPlainActor> Replacement = MakePlainActor(Env, ReplacementComponents.MakeView());
	const FObjectHandle ReplacementHandle = Replacement.Handle();

	MW_EXPECT_TRUE(Test, bHiddenAtBarrier, "The destroyed actor is hidden immediately at the barrier");
	MW_EXPECT_TRUE(Test, bWeakExpiredAtBarrier, "A weak reference to the destroyed actor expires at the barrier");
	MW_EXPECT_EQ(Test, EObjectResult::Success, Reclaim.Result, "The store destruction barrier runs successfully");
	MW_EXPECT_EQ(Test, std::uint32_t{2}, Reclaim.ObjectsDestroyed, "The barrier reclaims the actor and its component");
	MW_EXPECT_EQ(Test, OriginalHandle.Index, ReplacementHandle.Index, "A replacement reuses the reclaimed actor slot");
	MW_EXPECT_TRUE(Test, OriginalHandle.Generation != ReplacementHandle.Generation, "The reused slot publishes a fresh generation");
	MW_EXPECT_TRUE(Test, ActorWeak.IsExpired(), "The original handle stays stale after reclamation");
}

/**
 * Proves that after a destroy barrier a full collection accounts every root and
 * keeps the worklist within capacity while correctly leaving the pending-destroy
 * actor and its components to the store's destruction barrier, which reclaims
 * exactly the actor and both components.
 */
MW_TEST_CASE(EngineDestroyReclaimsActorAndComponentsWithRootsAndWorklistAccounted)
{
	FSpawnDestroyEnvironment Env{};
	FObjectStore& Store = Env.GetStore();
	FCollectorFixture Fixture{Store};
	FGarbageCollector& Collector = Fixture.GetCollector();
	FWorldActorRegistry<1> WorldActors;
	FActorComponentRegistry<2> ActorComponents;

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, WorldActors.MakeView());
	const TObjectPtr<FPlainActor> Actor = MakePlainActor(Env, ActorComponents.MakeView());
	const TObjectPtr<FPlainComponent> FirstComponent = Env.CreateDerivedObject<FPlainComponent>(PlainComponentTypeId, "PlainComponent");
	const TObjectPtr<FPlainComponent> SecondComponent = Env.CreateDerivedObject<FPlainComponent>(PlainComponentTypeId, "PlainComponent");
	(void)Actor.Get()->RegisterComponent(FirstComponent);
	(void)Actor.Get()->RegisterComponent(SecondComponent);
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{Actor});
	TStrongObjectPtr<UWorld> WorldRoot = Env.MakeRoot(World);
	(void)World.Get()->BeginPlay(0);

	const TWeakObjectPtr<UActorComponent> FirstComponentWeak{TObjectPtr<UActorComponent>{FirstComponent}};
	const TWeakObjectPtr<UActorComponent> SecondComponentWeak{TObjectPtr<UActorComponent>{SecondComponent}};
	const std::uint32_t OccupiedBeforeDestroy = Store.Stats().OccupiedSlots;
	(void)World.Get()->DestroyActor(TObjectPtr<AActor>{Actor});
	(void)World.Get()->ApplyPending(10);
	const FObjectStoreStats AfterBarrierStats = Store.Stats();

	// A full collection accounts roots and worklist but must not reclaim the
	// still-pending-destroy actor or its components; the rooted world survives.
	const FGarbageCollectionResult FullResult = Collector.CollectFull();
	const FObjectStoreStats AfterCollectStats = Store.Stats();

	// The store destruction barrier is what reclaims pending-destroy objects.
	const FObjectMutationResult Reclaim = Store.ApplyPendingDestroy(16);
	const FObjectStoreStats AfterReclaimStats = Store.Stats();

	MW_EXPECT_EQ(Test, std::uint32_t{4}, OccupiedBeforeDestroy, "The world, actor, and two components occupy four slots");
	MW_EXPECT_EQ(Test, std::uint32_t{3}, AfterBarrierStats.PendingDestroySlots, "The barrier leaves the actor and both components pending destroy");
	MW_EXPECT_EQ(Test, std::uint32_t{4}, AfterBarrierStats.OccupiedSlots, "Pending-destroy objects still occupy their slots");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, FullResult.Result, "A full collection after the barrier succeeds");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, FullResult.ObjectsReclaimed, "Collection leaves pending-destroy objects to the store barrier");
	MW_EXPECT_EQ(Test, AfterCollectStats.RootCapacity, FullResult.RootOperations, "The collection accounts every root-table entry");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, Collector.Stats().WorklistOverflows, "The reachable set fits the worklist without overflow");
	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "The rooted world survives the collection");
	MW_EXPECT_EQ(Test, EObjectResult::Success, Reclaim.Result, "The store destruction barrier runs successfully");
	MW_EXPECT_EQ(Test, std::uint32_t{3}, Reclaim.ObjectsDestroyed, "The barrier reclaims the actor and both components");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, Reclaim.PendingObjectsRemaining, "No pending-destroy objects remain after the barrier");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, AfterReclaimStats.OccupiedSlots, "Only the rooted world remains occupied after reclamation");
	MW_EXPECT_TRUE(Test, FirstComponentWeak.IsExpired(), "The first component's weak reference expires after reclamation");
	MW_EXPECT_TRUE(Test, SecondComponentWeak.IsExpired(), "The second component's weak reference expires after reclamation");
}

} // namespace
