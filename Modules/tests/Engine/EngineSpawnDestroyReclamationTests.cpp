#include "EngineSpawnDestroyTestHelpers.h"
#include "TestSupport.h"

#include <MicroWorld/Core/TickContext.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/GarbageCollectionResult.h>
#include <MicroWorld/Engine/World.h>

#include <cstddef>
#include <cstdint>

namespace
{

using namespace ::MicroWorld::Tests;

using MicroWorld::Engine::FGarbageCollectionResult;

/**
 * Motivation: Register three actors, destroy the middle one at the barrier, then Advance the survivors.
 * Responsibilities: Removing a middle actor at the barrier preserves the registration order of the survivors, so their
 *   next tick dispatch stays in.
 */
MW_TEST_CASE(EngineSurvivorDispatchOrderPreservedAfterMidListRemoval)
{
	// Arrange
	FSequenceCounter Sequence{};
	FActorEventState FirstEvents{};
	FActorEventState MiddleEvents{};
	FActorEventState LastEvents{};

	FSpawnDestroyEnvironment Env{};
	FWorldActorRegistry<3> WorldActors;
	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::Engine::UWorldClassId, WorldActors.MakeReference());
	const TObjectPtr<FOrderingActor> First = MakeOrderingActor(Env, Sequence, FirstEvents);
	const TObjectPtr<FOrderingActor> Middle = MakeOrderingActor(Env, Sequence, MiddleEvents);
	const TObjectPtr<FOrderingActor> Last = MakeOrderingActor(Env, Sequence, LastEvents);
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{First});
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{Middle});
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{Last});
	(void)World.Get()->BeginPlay(BaselineTimeMilliseconds);

	// Act - destroy the middle actor, apply the barrier, then advance the survivors
	(void)World.Get()->DestroyActor(TObjectPtr<AActor>{Middle});
	const ERuntimeResult ApplyResult = World.Get()->ApplyPending(BarrierTimeMilliseconds);
	Sequence.Next(); // Delimits the barrier's end events from the survivor tick order.
	const ERuntimeResult AdvanceResult = World.Get()->Advance(SurvivorAdvanceTimeMilliseconds);

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ApplyResult, "ApplyPending should remove the middle actor");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, AdvanceResult, "Advance over the survivors should succeed");
	MW_EXPECT_EQ(Test, std::size_t{2}, WorldActors.GetCount(), "The two survivors remain in the live registry");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, MiddleEvents.TickCount, "The removed middle actor never ticks after the barrier");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, FirstEvents.TickCount, "The first survivor ticks once on the advance");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, LastEvents.TickCount, "The last survivor ticks once on the advance");
	MW_EXPECT_TRUE(Test, FirstEvents.TickOrder < LastEvents.TickOrder, "Survivor dispatch keeps first-before-last registration order");
}

/**
 * Motivation: In one frame, queue a SpawnActor and then DestroyActor the same still-pending actor, then run the
 *   barrier.
 * Responsibilities: Destroying an actor still queued to spawn is rejected as invalid (it is not yet registered); the
 *   spawn still applies; destroy does not.
 */
MW_TEST_CASE(EngineSpawnThenDestroySameActorInOneFrame)
{
	// Arrange
	FSequenceCounter Sequence{};
	FActorEventState ActorEvents{};

	FSpawnDestroyEnvironment Env{};
	FWorldActorRegistry<2> WorldActors;
	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::Engine::UWorldClassId, WorldActors.MakeReference());
	const TObjectPtr<FOrderingActor> Actor = MakeOrderingActor(Env, Sequence, ActorEvents);
	(void)World.Get()->BeginPlay(BaselineTimeMilliseconds);

	// Act
	const EEngineResult SpawnResult = World.Get()->SpawnActor(TObjectPtr<AActor>{Actor});
	const EEngineResult DestroyResult = World.Get()->DestroyActor(TObjectPtr<AActor>{Actor});
	const std::size_t PendingSpawnAfter = World.Get()->PendingSpawnCount();
	const std::size_t PendingDestroyAfter = World.Get()->PendingDestroyCount();

	// Act - the barrier still applies the queued spawn
	const ERuntimeResult ApplyResult = World.Get()->ApplyPending(BarrierTimeMilliseconds);

	// Assert
	MW_EXPECT_EQ(Test, EEngineResult::Success, SpawnResult, "The spawn request is accepted");
	MW_EXPECT_EQ(Test, EEngineResult::InvalidReference, DestroyResult, "Destroying a still-pending-spawn actor is rejected as invalid");
	MW_EXPECT_EQ(Test, std::size_t{1}, PendingSpawnAfter, "The rejected destroy leaves the spawn queued");
	MW_EXPECT_EQ(Test, std::size_t{0}, PendingDestroyAfter, "The rejected destroy queues nothing");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ApplyResult, "ApplyPending should still apply the queued spawn");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, ActorEvents.BeginCount, "The spawn still begins at the barrier despite the rejected destroy");
	MW_EXPECT_EQ(Test, std::size_t{1}, WorldActors.GetCount(), "The spawned actor joins the live registry at the barrier");
}

/**
 * Motivation: Destroy an actor at the barrier, then run the store's destruction barrier and construct a
 *   replacement in the reclaimed slot.
 * Responsibilities: The destroyed actor's handle is hidden at the barrier and stales after reclamation; the slot is
 *   reused with a fresh generation that never.
 */
MW_TEST_CASE(EngineDestroyedActorHandleGoesStaleAfterReclamation)
{
	// Arrange
	FSpawnDestroyEnvironment Env{};
	FObjectStore& Store = Env.GetStore();
	FWorldActorRegistry<1> WorldActors;
	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::Engine::UWorldClassId, WorldActors.MakeReference());
	const TObjectPtr<FPlainActor> Actor = MakePlainActor(Env);
	const TObjectPtr<FPlainComponent> Component = Env.CreateDerivedObject<FPlainComponent>(PlainComponentTypeId, "PlainComponent");
	(void)Actor.Get()->RegisterComponent(Component);
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{Actor});
	TStrongObjectPtr<UWorld> WorldRoot = Env.MakeRoot(World);
	(void)World.Get()->BeginPlay(BaselineTimeMilliseconds);

	const FObjectHandle OriginalHandle = Actor.Handle();
	const TWeakObjectPtr<AActor> ActorWeak{TObjectPtr<AActor>{Actor}};

	// Act - the destroy barrier hides the actor without reclaiming its slot
	(void)World.Get()->DestroyActor(TObjectPtr<AActor>{Actor});
	(void)World.Get()->ApplyPending(BarrierTimeMilliseconds);
	const bool bHiddenAtBarrier = Actor.Get() == nullptr;
	const bool bWeakExpiredAtBarrier = ActorWeak.IsExpired();

	// Act - the store destruction barrier reclaims the slot, and a replacement reuses it
	const MicroWorld::Engine::FObjectMutationResult Reclaim = Store.ApplyPendingDestroy(MaxObjectsReclaimedPerBarrier);
	const TObjectPtr<FPlainActor> Replacement = MakePlainActor(Env);
	const FObjectHandle ReplacementHandle = Replacement.Handle();

	// Assert
	MW_EXPECT_TRUE(Test, bHiddenAtBarrier, "The destroyed actor is hidden immediately at the barrier");
	MW_EXPECT_TRUE(Test, bWeakExpiredAtBarrier, "A weak reference to the destroyed actor expires at the barrier");
	MW_EXPECT_EQ(Test, EObjectResult::Success, Reclaim.Result, "The store destruction barrier runs successfully");
	MW_EXPECT_EQ(Test, std::uint32_t{2}, Reclaim.ObjectsDestroyed, "The barrier reclaims the actor and its component");
	MW_EXPECT_EQ(Test, OriginalHandle.Index, ReplacementHandle.Index, "A replacement reuses the reclaimed actor slot");
	MW_EXPECT_TRUE(Test, OriginalHandle.Generation != ReplacementHandle.Generation, "The reused slot publishes a fresh generation");
	MW_EXPECT_TRUE(Test, ActorWeak.IsExpired(), "The original handle stays stale after reclamation");
}

/**
 * Motivation: Destroy a two-component actor at the barrier, run a full collection over the rooted world, then run
 *   the store destruction barrier.
 * Responsibilities: After the destroy barrier a full collection accounts every root and keeps the worklist within
 *   capacity while correctly leaving the.
 */
MW_TEST_CASE(EngineDestroyReclaimsActorAndComponentsWithRootsAndWorklistAccounted)
{
	// Arrange
	FSpawnDestroyEnvironment Env{};
	FObjectStore& Store = Env.GetStore();
	FCollectorFixture Fixture{Store};
	FGarbageCollector& Collector = Fixture.GetCollector();
	FWorldActorRegistry<1> WorldActors;
	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::Engine::UWorldClassId, WorldActors.MakeReference());
	const TObjectPtr<FPlainActor> Actor = MakePlainActor(Env);
	const TObjectPtr<FPlainComponent> FirstComponent = Env.CreateDerivedObject<FPlainComponent>(PlainComponentTypeId, "PlainComponent");
	const TObjectPtr<FPlainComponent> SecondComponent = Env.CreateDerivedObject<FPlainComponent>(PlainComponentTypeId, "PlainComponent");
	(void)Actor.Get()->RegisterComponent(FirstComponent);
	(void)Actor.Get()->RegisterComponent(SecondComponent);
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{Actor});
	TStrongObjectPtr<UWorld> WorldRoot = Env.MakeRoot(World);
	(void)World.Get()->BeginPlay(BaselineTimeMilliseconds);

	const TWeakObjectPtr<UActorComponent> FirstComponentWeak{TObjectPtr<UActorComponent>{FirstComponent}};
	const TWeakObjectPtr<UActorComponent> SecondComponentWeak{TObjectPtr<UActorComponent>{SecondComponent}};
	const std::uint32_t OccupiedBeforeDestroy = Store.Stats().OccupiedSlots;

	// Act - the destroy barrier leaves the actor and both components pending destroy
	(void)World.Get()->DestroyActor(TObjectPtr<AActor>{Actor});
	(void)World.Get()->ApplyPending(BarrierTimeMilliseconds);
	const FObjectStoreStats AfterBarrierStats = Store.Stats();

	// Act - a full collection accounts roots and worklist but must not reclaim the
	// still-pending-destroy actor or its components; the rooted world survives.
	const FGarbageCollectionResult FullResult = Collector.CollectFull();

	// Act - the store destruction barrier is what reclaims pending-destroy objects.
	const MicroWorld::Engine::FObjectMutationResult Reclaim = Store.ApplyPendingDestroy(MaxObjectsReclaimedPerBarrier);
	const FObjectStoreStats AfterReclaimStats = Store.Stats();

	// Assert
	MW_EXPECT_EQ(Test, std::uint32_t{4}, OccupiedBeforeDestroy, "The world, actor, and two components occupy four slots");
	MW_EXPECT_EQ(Test, std::uint32_t{3}, AfterBarrierStats.PendingDestroySlots, "The barrier leaves the actor and both components pending destroy");
	MW_EXPECT_EQ(Test, std::uint32_t{4}, AfterBarrierStats.OccupiedSlots, "Pending-destroy objects still occupy their slots");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, FullResult.Result, "A full collection after the barrier succeeds");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, FullResult.ObjectsReclaimed, "Collection leaves pending-destroy objects to the store barrier");
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
