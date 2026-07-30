#include "EngineAllocationCounters.h"
#include "EngineTestSupport.h"
#include "TestSupport.h"

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Engine/GarbageCollector.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{
using MicroWorld::Tests::GlobalAllocationCount;

using MicroWorld::AActor;
using MicroWorld::EEngineResult;
using MicroWorld::ERuntimeResult;
using MicroWorld::FGarbageCollectionBudget;
using MicroWorld::FGarbageCollector;
using MicroWorld::FGarbageCollectorStorage;
using MicroWorld::FObjectHandle;
using MicroWorld::FObjectRootEntry;
using MicroWorld::FObjectSlotMetadata;
using MicroWorld::FObjectStore;
using MicroWorld::FObjectStoreStats;
using MicroWorld::FWorldActorRegistry;
using MicroWorld::FWorldActorRegistryReference;
using MicroWorld::TObjectPtr;
using MicroWorld::TStrongObjectPtr;
using MicroWorld::TWeakObjectPtr;
using MicroWorld::UActorComponent;
using MicroWorld::UWorld;
using MicroWorld::Tests::TEngineEnvironment;

/** A component that observes lifetime and exposes its actor parent for tests. */
class FTrackedComponent final : public UActorComponent
{
public:
	FTrackedComponent() noexcept : UActorComponent() {}
};

/** An actor that observes lifetime and exposes its world parent for tests. */
class FTrackedActor final : public AActor
{
public:
	explicit FTrackedActor() noexcept : AActor() {}
};

constexpr MicroWorld::FTypeId TrackedActorTypeId{0x00030001u};
constexpr MicroWorld::FTypeId TrackedComponentTypeId{0x00030002u};

/** Environment sized for GC tests with enough slots for world, actors, components, and roots. */
using FGarbageCollectionEnvironment = TEngineEnvironment<256, 16, 8, 4>;

/** Fixed capacity of the GC fixture worklist, large enough for every reachable object in these tests. */
constexpr std::uint32_t CollectorWorklistCapacity = 16;

/** Canonical monotonic baseline every BeginPlay call uses as its starting world time. */
constexpr MicroWorld::TimePointMilliseconds BaselineTimeMilliseconds{0};

/** Steady-state iterations the allocation probe runs to prove Advance never calls global new. */
constexpr std::uint32_t SteadyStateAdvanceIterations = 64;

/** Monotonic ms-per-step the allocation probe advances so each Advance lands on a later time. */
constexpr std::uint32_t SteadyStateAdvanceStepMilliseconds = 10;

/** Builds a tracked actor through its own derived descriptor. */
TObjectPtr<FTrackedActor> MakeTrackedActor(FGarbageCollectionEnvironment& InEnv) noexcept
{
	return InEnv.CreateDerivedObject<FTrackedActor>(TrackedActorTypeId, "TrackedActor");
}

/** Builds a tracked component through its own derived descriptor. */
TObjectPtr<FTrackedComponent> MakeTrackedComponent(FGarbageCollectionEnvironment& InEnv) noexcept
{
	return InEnv.CreateDerivedObject<FTrackedComponent>(TrackedComponentTypeId, "TrackedComponent");
}

/** Owns a fixed worklist and collector bound to an environment's store for GC tests. */
class FCollectorFixture final
{
public:
	explicit FCollectorFixture(FObjectStore& InStore) noexcept : Collector(InStore, FGarbageCollectorStorage{Worklist, CollectorWorklistCapacity}) {}

	FGarbageCollector& GetCollector() noexcept { return Collector; }

private:
	/** Backs the collector's reachable-object queue without heap storage. */
	FObjectHandle Worklist[CollectorWorklistCapacity]{};
	/** Owns the collector bound to this fixture's worklist for the test's lifetime. */
	FGarbageCollector Collector;
};

/**
 * Scenario: Root a world with one actor and component via a single strong pointer and run one full collection.
 * Expected: A single TStrongObjectPtr<UWorld> root retains the entire world graph (world, actors, components) through one full collection.
 */
MW_TEST_CASE(EngineRootedWorldRetainsActorsAndComponentsThroughFullGC)
{
	// Arrange
	FGarbageCollectionEnvironment Env{};
	FObjectStore& Store = Env.GetStore();
	FCollectorFixture Fixture{Store};
	FGarbageCollector& Collector = Fixture.GetCollector();

	FWorldActorRegistry<2> WorldActors;
	FWorldActorRegistryReference WorldActorsView = WorldActors.MakeReference();

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, std::move(WorldActorsView));
	const TObjectPtr<FTrackedActor> Actor = MakeTrackedActor(Env);
	const TObjectPtr<FTrackedComponent> Component = MakeTrackedComponent(Env);
	(void)Actor.Get()->RegisterComponent(Component);
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{Actor});

	TStrongObjectPtr<UWorld> WorldRoot = Env.MakeRoot(World);

	// Act
	const ERuntimeResult CollectResult = Collector.RequestCollection();
	const MicroWorld::FGarbageCollectionResult FullResult = Collector.CollectFull();
	const FObjectStoreStats Stats = Store.Stats();

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, CollectResult, "A rooted graph should accept a collection request");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, FullResult.Result, "A full collection on a rooted graph should succeed");
	MW_EXPECT_TRUE(Test, static_cast<bool>(WorldRoot), "The world root remains live");
	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "The rooted world remains reachable");
	MW_EXPECT_TRUE(Test, Actor.Get() != nullptr, "A rooted world retains its actor");
	MW_EXPECT_TRUE(Test, Component.Get() != nullptr, "A rooted actor retains its component");
	MW_EXPECT_EQ(Test, std::uint32_t{3}, Stats.OccupiedSlots, "A rooted world, actor, and component all survive collection");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, FullResult.ObjectsReclaimed, "Nothing is reclaimed while the world root holds");
}

/**
 * Scenario: Root a world graph with weak links observed, release the world root, then run a full collection.
 * Expected: Releasing the world root allows the collector to reclaim the complete graph, and weak parent links expire once the parent is reclaimed.
 */
MW_TEST_CASE(EngineReleasingWorldRootReclaimsEntireGraph)
{
	// Arrange
	FGarbageCollectionEnvironment Env{};
	FObjectStore& Store = Env.GetStore();
	FCollectorFixture Fixture{Store};
	FGarbageCollector& Collector = Fixture.GetCollector();

	FWorldActorRegistry<2> WorldActors;
	FWorldActorRegistryReference WorldActorsView = WorldActors.MakeReference();

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, std::move(WorldActorsView));
	const TObjectPtr<FTrackedActor> Actor = MakeTrackedActor(Env);
	const TObjectPtr<FTrackedComponent> Component = MakeTrackedComponent(Env);
	(void)Actor.Get()->RegisterComponent(Component);
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{Actor});

	TStrongObjectPtr<UWorld> WorldRoot = Env.MakeRoot(World);
	const TWeakObjectPtr<UWorld> WorldWeak{World};
	const TWeakObjectPtr<AActor> ActorWeak{TObjectPtr<AActor>{Actor}};
	const TWeakObjectPtr<UActorComponent> ComponentWeak{TObjectPtr<UActorComponent>{Component}};
	WorldRoot.Reset();

	// Act
	const ERuntimeResult CollectResult = Collector.RequestCollection();
	const MicroWorld::FGarbageCollectionResult FullResult = Collector.CollectFull();
	const FObjectStoreStats Stats = Store.Stats();

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, CollectResult, "An unrooted graph should accept a collection request");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, FullResult.Result, "A full collection should succeed");
	MW_EXPECT_EQ(Test, std::uint32_t{3}, FullResult.ObjectsReclaimed, "The world, actor, and component are all reclaimed");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, Stats.OccupiedSlots, "No objects remain after the graph is reclaimed");
	MW_EXPECT_TRUE(Test, WorldWeak.IsExpired(), "The world weak link expires after reclamation");
	MW_EXPECT_TRUE(Test, ActorWeak.IsExpired(), "The actor weak link expires after reclamation");
	MW_EXPECT_TRUE(Test, ComponentWeak.IsExpired(), "The component weak link expires after reclamation");
}

/**
 * Scenario: Independently root the world, actor, and component; reclaim the world while the actor survives, then reclaim the actor while the
 * component survives. Expected: The weak world parent link resolves to null after the world is reclaimed while the actor survives under its own root,
 * and the weak actor parent link resolves to null after the actor is reclaimed while the component survives under its own root, so a child never
 * observes a dangling parent pointer.
 */
MW_TEST_CASE(EngineWeakParentReferencesExpireCorrectly)
{
	// Arrange
	FGarbageCollectionEnvironment Env{};
	FObjectStore& Store = Env.GetStore();
	FCollectorFixture Fixture{Store};
	FGarbageCollector& Collector = Fixture.GetCollector();

	FWorldActorRegistry<2> WorldActors;
	FWorldActorRegistryReference WorldActorsView = WorldActors.MakeReference();

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, std::move(WorldActorsView));
	const TObjectPtr<FTrackedActor> Actor = MakeTrackedActor(Env);
	const TObjectPtr<FTrackedComponent> Component = MakeTrackedComponent(Env);
	const EEngineResult ComponentRegistration = Actor.Get()->RegisterComponent(Component);
	const EEngineResult ActorRegistration = World.Get()->RegisterActor(TObjectPtr<AActor>{Actor});
	const FObjectHandle OriginalWorldHandle = World.Handle();
	const FObjectHandle OriginalActorHandle = Actor.Handle();

	// Root the actor independently so it survives its world's reclamation; root
	// the component independently so it survives its actor's reclamation.
	TStrongObjectPtr<UWorld> WorldRoot = Env.MakeRoot(World);
	TStrongObjectPtr<AActor> ActorRoot = Env.MakeRoot(TObjectPtr<AActor>{Actor});
	TStrongObjectPtr<UActorComponent> ComponentRoot = Env.MakeRoot(TObjectPtr<UActorComponent>{Component});

	// Assert - rooted parents are still observable
	MW_EXPECT_TRUE(Test, Actor.Get()->GetOwnerWorld() != nullptr, "The actor observes its world while rooted");
	MW_EXPECT_TRUE(Test, Component.Get()->GetOwnerActor() != nullptr, "The component observes its actor while rooted");

	// Release only the world: the world is reclaimed, the actor survives, and
	// its weak world link now resolves to null.
	// Act - reclaim the world only
	WorldRoot.Reset();
	const ERuntimeResult WorldCollectionRequest = Collector.RequestCollection();
	const MicroWorld::FGarbageCollectionResult WorldCollection = Collector.CollectFull();
	FWorldActorRegistry<1> ReplacementWorldActors;
	const TObjectPtr<UWorld> ReplacementWorld = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, ReplacementWorldActors.MakeReference());
	TStrongObjectPtr<UWorld> ReplacementWorldRoot = Env.MakeRoot(ReplacementWorld);
	MW_EXPECT_EQ(Test, EEngineResult::Success, ComponentRegistration, "Component setup succeeds");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ActorRegistration, "Actor setup succeeds");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, WorldCollectionRequest, "World reclamation request succeeds");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, WorldCollection.Result, "World reclamation succeeds");
	MW_EXPECT_TRUE(Test, World.Get() == nullptr, "The unrooted world is reclaimed");
	MW_EXPECT_TRUE(Test, Actor.Get() != nullptr, "The independently rooted actor survives");
	MW_EXPECT_EQ(Test, OriginalWorldHandle.Index, ReplacementWorld.Handle().Index, "A replacement world reuses the reclaimed slot");
	MW_EXPECT_TRUE(Test, OriginalWorldHandle.Generation != ReplacementWorld.Handle().Generation, "Reused world slot publishes a fresh generation");
	MW_EXPECT_EQ(Test, nullptr, Actor.Get()->GetOwnerWorld(), "The actor's world parent link expires after reclamation");
	MW_EXPECT_TRUE(Test, Component.Get()->GetOwnerActor() != nullptr, "The component still observes its live actor");

	// Release only the actor: the actor is reclaimed, the component survives,
	// and its weak actor link now resolves to null.
	// Act - reclaim the actor only
	ActorRoot.Reset();
	const ERuntimeResult ActorCollectionRequest = Collector.RequestCollection();
	const MicroWorld::FGarbageCollectionResult ActorCollection = Collector.CollectFull();
	const TObjectPtr<FTrackedActor> ReplacementActor = MakeTrackedActor(Env);
	TStrongObjectPtr<AActor> ReplacementActorRoot = Env.MakeRoot(TObjectPtr<AActor>{ReplacementActor});
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ActorCollectionRequest, "Actor reclamation request succeeds");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ActorCollection.Result, "Actor reclamation succeeds");
	MW_EXPECT_TRUE(Test, Actor.Get() == nullptr, "The unrooted actor is reclaimed");
	MW_EXPECT_TRUE(Test, Component.Get() != nullptr, "The independently rooted component survives");
	MW_EXPECT_EQ(Test, OriginalActorHandle.Index, ReplacementActor.Handle().Index, "A replacement actor reuses the reclaimed slot");
	MW_EXPECT_TRUE(Test, OriginalActorHandle.Generation != ReplacementActor.Handle().Generation, "Reused actor slot publishes a fresh generation");
	MW_EXPECT_EQ(Test, nullptr, Component.Get()->GetOwnerActor(), "The component's actor parent link expires after reclamation");
}

/**
 * Scenario: Begin a rooted world and run a bounded loop of repeated Advance calls in steady state.
 * Expected: Repeated Advance calls in steady state invoke no scalar, array, or C++17 aligned global allocation and leave fixed object/root occupancy
 * unchanged.
 */
MW_TEST_CASE(EngineAdvancePerformsNoObservableAllocation)
{
	// Arrange
	FGarbageCollectionEnvironment Env{};
	FObjectStore& Store = Env.GetStore();

	FWorldActorRegistry<2> WorldActors;
	FWorldActorRegistryReference WorldActorsView = WorldActors.MakeReference();

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, std::move(WorldActorsView));
	const TObjectPtr<FTrackedActor> Actor = MakeTrackedActor(Env);
	const TObjectPtr<FTrackedComponent> Component = MakeTrackedComponent(Env);
	(void)Actor.Get()->RegisterComponent(Component);
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{Actor});

	TStrongObjectPtr<UWorld> WorldRoot = Env.MakeRoot(World);
	(void)World.Get()->BeginPlay(BaselineTimeMilliseconds);

	// Act - capture the steady-state baseline, then run the bounded advance loop
	const FObjectStoreStats BeforeStats = Store.Stats();
	const std::uint32_t AllocationsBeforeAdvance = GlobalAllocationCount;
	for (std::uint32_t Step = 0; Step < SteadyStateAdvanceIterations; ++Step)
	{
		const ERuntimeResult AdvanceResult = World.Get()->Advance(Step * SteadyStateAdvanceStepMilliseconds);
		if (AdvanceResult != ERuntimeResult::Success)
		{
			MW_EXPECT_EQ(Test, ERuntimeResult::Success, AdvanceResult, "Each steady-state advance should succeed");
			break;
		}
	}
	const FObjectStoreStats AfterStats = Store.Stats();
	const std::uint32_t AllocationsAfterAdvance = GlobalAllocationCount;

	// Assert

	MW_EXPECT_EQ(Test, AllocationsBeforeAdvance, AllocationsAfterAdvance, "Steady-state Advance must not call scalar, array, or aligned global new");
	MW_EXPECT_EQ(Test, BeforeStats.OccupiedSlots, AfterStats.OccupiedSlots, "Steady-state advance must not allocate object slots");
	MW_EXPECT_EQ(Test, BeforeStats.ActiveRoots, AfterStats.ActiveRoots, "Steady-state advance must not change root occupancy");
}

/**
 * Scenario: Begin, re-begin, advance, end, re-end, and advance-after-end a world in sequence.
 * Expected: EndPlay is idempotent and repeated BeginPlay/Advance/EndPlay calls outside the legal lifecycle match Core's InvalidLifecycle semantics.
 */
MW_TEST_CASE(EngineEndPlayIsIdempotentAndRepeatedLifecycleCallsMatchCore)
{
	// Arrange
	FGarbageCollectionEnvironment Env{};

	FWorldActorRegistry<2> WorldActors;
	FWorldActorRegistryReference WorldActorsView = WorldActors.MakeReference();

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, std::move(WorldActorsView));
	const TObjectPtr<FTrackedActor> Actor = MakeTrackedActor(Env);
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{Actor});

	// Act
	const ERuntimeResult FirstBegin = World.Get()->BeginPlay(BaselineTimeMilliseconds);
	const ERuntimeResult SecondBegin = World.Get()->BeginPlay(BaselineTimeMilliseconds);
	const ERuntimeResult AdvanceWhilePlaying = World.Get()->Advance(1);
	const ERuntimeResult FirstEnd = World.Get()->EndPlay();
	const ERuntimeResult SecondEnd = World.Get()->EndPlay();
	const ERuntimeResult AdvanceAfterEnd = World.Get()->Advance(2);

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, FirstBegin, "The first BeginPlay should succeed");
	MW_EXPECT_EQ(Test, ERuntimeResult::InvalidLifecycle, SecondBegin, "A second BeginPlay is rejected as out-of-lifecycle");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, AdvanceWhilePlaying, "Advance while playing should succeed");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, FirstEnd, "The first EndPlay should succeed");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, SecondEnd, "A repeated EndPlay is idempotent and succeeds");
	MW_EXPECT_EQ(Test, ERuntimeResult::InvalidLifecycle, AdvanceAfterEnd, "Advance after EndPlay is rejected as out-of-lifecycle");
}

} // namespace
