#include "EngineSpawnDestroyTestHelpers.h"
#include "TestSupport.h"

#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/World.h>

#include <cstddef>

namespace
{

using namespace ::MicroWorld::Tests;

/**
 * Motivation: Pre-register one actor, queue a spawn that reaches capacity, then run ApplyPending and attempt
 *   another spawn.
 * Responsibilities: Spawn capacity counts live plus pending-spawn actors together, before the barrier applies the queue
 *   and after it fills the live registry.
 */
MW_TEST_CASE(EngineSpawnCapacityCountsLiveAndPending)
{
	// Arrange
	FSpawnDestroyEnvironment Env{};
	FWorldActorRegistry<2> WorldActors;
	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::Engine::UWorldClassId, WorldActors.MakeReference());
	const TObjectPtr<FPlainActor> PreRegistered = MakePlainActor(Env);
	const TObjectPtr<FPlainActor> FirstSpawn = MakePlainActor(Env);
	const TObjectPtr<FPlainActor> SecondSpawn = MakePlainActor(Env);
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{PreRegistered});
	(void)World.Get()->BeginPlay(BaselineTimeMilliseconds);

	// Act - one live actor plus one pending spawn already reaches the capacity of two
	const EEngineResult FirstSpawnResult = World.Get()->SpawnActor(TObjectPtr<AActor>{FirstSpawn});
	const EEngineResult OverCapacityBeforeBarrier = World.Get()->SpawnActor(TObjectPtr<AActor>{SecondSpawn});
	const std::size_t PendingAfterReject = World.Get()->PendingSpawnCount();
	const bool bSecondOwnedAfterReject = SecondSpawn.Get()->HasAssignedWorld();

	// Act - the barrier fills the registry; a further spawn is still rejected
	const ERuntimeResult ApplyResult = World.Get()->ApplyPending(BarrierTimeMilliseconds);
	const EEngineResult OverCapacityAfterBarrier = World.Get()->SpawnActor(TObjectPtr<AActor>{SecondSpawn});

	// Assert
	MW_EXPECT_EQ(Test, EEngineResult::Success, FirstSpawnResult, "A spawn that reaches live-plus-pending capacity is accepted");
	MW_EXPECT_EQ(Test, EEngineResult::CapacityExceeded, OverCapacityBeforeBarrier, "Live plus pending-spawn at capacity rejects a further spawn");
	MW_EXPECT_EQ(Test, std::size_t{1}, PendingAfterReject, "A capacity-rejected spawn leaves the pending queue unchanged");
	MW_EXPECT_TRUE(Test, !bSecondOwnedAfterReject, "A capacity-rejected spawn must not bind a world identity");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ApplyResult, "ApplyPending should apply the accepted spawn");
	MW_EXPECT_EQ(Test, std::size_t{2}, WorldActors.GetCount(), "The barrier fills the live registry to capacity");
	MW_EXPECT_EQ(Test, EEngineResult::CapacityExceeded, OverCapacityAfterBarrier, "A full live registry still rejects further spawns");
}

/**
 * Motivation: Queue a spawn, repeat it while pending, run the barrier, then repeat it again while the actor is
 *   live.
 * Responsibilities: A repeated spawn request is rejected as a duplicate both while the first request is pending and
 *   after the barrier makes the actor live.
 */
MW_TEST_CASE(EngineDuplicateSpawnRejected)
{
	// Arrange
	FSpawnDestroyEnvironment Env{};
	FWorldActorRegistry<4> WorldActors;
	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::Engine::UWorldClassId, WorldActors.MakeReference());
	const TObjectPtr<FPlainActor> Actor = MakePlainActor(Env);
	(void)World.Get()->BeginPlay(BaselineTimeMilliseconds);

	// Act
	const EEngineResult FirstSpawn = World.Get()->SpawnActor(TObjectPtr<AActor>{Actor});
	const EEngineResult DuplicateWhilePending = World.Get()->SpawnActor(TObjectPtr<AActor>{Actor});
	const std::size_t PendingAfterDuplicate = World.Get()->PendingSpawnCount();

	(void)World.Get()->ApplyPending(BarrierTimeMilliseconds);
	const EEngineResult DuplicateWhileLive = World.Get()->SpawnActor(TObjectPtr<AActor>{Actor});

	// Assert
	MW_EXPECT_EQ(Test, EEngineResult::Success, FirstSpawn, "The first spawn request is accepted");
	MW_EXPECT_EQ(Test, EEngineResult::Duplicate, DuplicateWhilePending, "A second request for a pending-spawn actor is a duplicate");
	MW_EXPECT_EQ(Test, std::size_t{1}, PendingAfterDuplicate, "A duplicate spawn leaves the pending queue unchanged");
	MW_EXPECT_EQ(Test, EEngineResult::Duplicate, DuplicateWhileLive, "A spawn request for an already-live actor is a duplicate");
	MW_EXPECT_EQ(Test, std::size_t{1}, WorldActors.GetCount(), "The duplicate live request does not grow the registry");
}

/**
 * Motivation: Register and begin one actor, then attempt to DestroyActor a never-registered stranger actor.
 * Responsibilities: Destroying an actor that was never registered with this world is rejected as an invalid reference
 *   and leaves the destroy queue unchanged.
 */
MW_TEST_CASE(EngineDestroyOfNeverRegisteredActorRejected)
{
	// Arrange
	FSpawnDestroyEnvironment Env{};
	FWorldActorRegistry<2> WorldActors;
	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::Engine::UWorldClassId, WorldActors.MakeReference());
	const TObjectPtr<FPlainActor> Registered = MakePlainActor(Env);
	const TObjectPtr<FPlainActor> Stranger = MakePlainActor(Env);
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{Registered});
	(void)World.Get()->BeginPlay(BaselineTimeMilliseconds);

	// Act
	const EEngineResult DestroyResult = World.Get()->DestroyActor(TObjectPtr<AActor>{Stranger});

	// Assert
	MW_EXPECT_EQ(Test, EEngineResult::InvalidReference, DestroyResult, "Destroying a never-registered actor is rejected as invalid");
	MW_EXPECT_EQ(Test, std::size_t{0}, World.Get()->PendingDestroyCount(), "A rejected destroy leaves the pending queue unchanged");
	MW_EXPECT_EQ(Test, std::size_t{1}, WorldActors.GetCount(), "A rejected destroy leaves the live registry unchanged");
}

/**
 * Motivation: Attempt SpawnActor and DestroyActor on a constructed (never-begun) world and on an ended world.
 * Responsibilities: Spawn and destroy are lifecycle-locked outside the playing state; a constructed world and an ended
 *   world both reject them without.
 */
MW_TEST_CASE(EngineSpawnAndDestroyRejectedOutsidePlayingLifecycle)
{
	// Arrange
	FSpawnDestroyEnvironment Env{};
	FWorldActorRegistry<2> ConstructedWorldActors;
	FWorldActorRegistry<2> EndedWorldActors;
	// A world that never began play rejects both structural requests.
	const TObjectPtr<UWorld> ConstructedWorld = Env.CreateObject<UWorld>(MicroWorld::Engine::UWorldClassId, ConstructedWorldActors.MakeReference());
	const TObjectPtr<FPlainActor> RegisteredBeforePlay = MakePlainActor(Env);
	(void)ConstructedWorld.Get()->RegisterActor(TObjectPtr<AActor>{RegisteredBeforePlay});

	// Act - a world that never began play rejects both structural requests
	const EEngineResult SpawnWhileConstructed = ConstructedWorld.Get()->SpawnActor(TObjectPtr<AActor>{RegisteredBeforePlay});
	const EEngineResult DestroyWhileConstructed = ConstructedWorld.Get()->DestroyActor(TObjectPtr<AActor>{RegisteredBeforePlay});

	// Arrange - a world that ended play rejects both structural requests
	const TObjectPtr<UWorld> EndedWorld = Env.CreateObject<UWorld>(MicroWorld::Engine::UWorldClassId, EndedWorldActors.MakeReference());
	const TObjectPtr<FPlainActor> RegisteredActor = MakePlainActor(Env);
	const TObjectPtr<FPlainActor> WouldSpawn = MakePlainActor(Env);
	(void)EndedWorld.Get()->RegisterActor(TObjectPtr<AActor>{RegisteredActor});
	(void)EndedWorld.Get()->BeginPlay(BaselineTimeMilliseconds);
	(void)EndedWorld.Get()->EndPlay();

	// Act
	const EEngineResult SpawnAfterEnd = EndedWorld.Get()->SpawnActor(TObjectPtr<AActor>{WouldSpawn});
	const EEngineResult DestroyAfterEnd = EndedWorld.Get()->DestroyActor(TObjectPtr<AActor>{RegisteredActor});

	// Assert
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
 * Motivation: Begin a world and attempt SpawnActor with empty, cross-store, and already-owned references.
 * Responsibilities: Every SpawnActor reference rejection (empty, cross-store, already-owned) returns its exact code,
 *   leaving the pending queue and candidate.
 */
MW_TEST_CASE(EngineSpawnReferenceRejectionsLeaveStateUnchanged)
{
	// Arrange
	FSpawnDestroyEnvironment Env{};
	FSecondStore ForeignStoreOwner{};
	FObjectStore& ForeignStore = ForeignStoreOwner.GetStore();

	FWorldActorRegistry<4> WorldActors;
	FWorldActorRegistry<2> OtherWorldActors;
	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::Engine::UWorldClassId, WorldActors.MakeReference());
	const TObjectPtr<UWorld> OtherWorld = Env.CreateObject<UWorld>(MicroWorld::Engine::UWorldClassId, OtherWorldActors.MakeReference());
	const TObjectPtr<FPlainActor> OwnedByOther = MakePlainActor(Env);
	const TObjectPtr<FPlainActor> ForeignActor = ForeignStore.NewObject<FPlainActor>(*ForeignStoreOwner.GetRegistry().Find(PlainActorTypeId)).Object;
	// Bind OwnedByOther to a different world so the spawning world sees it as owned.
	(void)OtherWorld.Get()->RegisterActor(TObjectPtr<AActor>{OwnedByOther});
	(void)World.Get()->BeginPlay(BaselineTimeMilliseconds);

	// Act
	const EEngineResult EmptyResult = World.Get()->SpawnActor(TObjectPtr<AActor>{});
	const EEngineResult CrossStoreResult = World.Get()->SpawnActor(TObjectPtr<AActor>{ForeignActor});
	const EEngineResult AlreadyOwnedResult = World.Get()->SpawnActor(TObjectPtr<AActor>{OwnedByOther});

	// Assert
	MW_EXPECT_EQ(Test, EEngineResult::InvalidReference, EmptyResult, "An empty spawn reference is rejected as invalid");
	MW_EXPECT_EQ(Test, EEngineResult::CrossStore, CrossStoreResult, "A foreign-store spawn reference is rejected as cross-store");
	MW_EXPECT_EQ(Test, EEngineResult::AlreadyOwned, AlreadyOwnedResult, "An actor owned by another world is rejected as already owned");
	MW_EXPECT_EQ(Test, std::size_t{0}, World.Get()->PendingSpawnCount(), "Every rejected spawn leaves the pending queue empty");
}

/**
 * Motivation: Begin a world and attempt DestroyActor with empty, cross-store, first, and repeated references.
 * Responsibilities: Every DestroyActor reference rejection (empty, cross-store, repeated) returns its exact code,
 *   leaving the pending-destroy queue accurate.
 */
MW_TEST_CASE(EngineDestroyReferenceRejectionsLeaveStateUnchanged)
{
	// Arrange
	FSpawnDestroyEnvironment Env{};
	FSecondStore ForeignStoreOwner{};
	FObjectStore& ForeignStore = ForeignStoreOwner.GetStore();

	FWorldActorRegistry<2> WorldActors;
	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::Engine::UWorldClassId, WorldActors.MakeReference());
	const TObjectPtr<FPlainActor> Registered = MakePlainActor(Env);
	const TObjectPtr<FPlainActor> ForeignActor = ForeignStore.NewObject<FPlainActor>(*ForeignStoreOwner.GetRegistry().Find(PlainActorTypeId)).Object;
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{Registered});
	(void)World.Get()->BeginPlay(BaselineTimeMilliseconds);

	// Act
	const EEngineResult EmptyResult = World.Get()->DestroyActor(TObjectPtr<AActor>{});
	const EEngineResult CrossStoreResult = World.Get()->DestroyActor(TObjectPtr<AActor>{ForeignActor});
	const EEngineResult FirstDestroy = World.Get()->DestroyActor(TObjectPtr<AActor>{Registered});
	const EEngineResult RepeatedDestroy = World.Get()->DestroyActor(TObjectPtr<AActor>{Registered});

	// Assert
	MW_EXPECT_EQ(Test, EEngineResult::InvalidReference, EmptyResult, "An empty destroy reference is rejected as invalid");
	MW_EXPECT_EQ(Test, EEngineResult::CrossStore, CrossStoreResult, "A foreign-store destroy reference is rejected as cross-store");
	MW_EXPECT_EQ(Test, EEngineResult::Success, FirstDestroy, "A registered actor is accepted for one deferred destroy");
	MW_EXPECT_EQ(Test, EEngineResult::Duplicate, RepeatedDestroy, "A repeated destroy of a pending actor is a duplicate");
	MW_EXPECT_EQ(Test, std::size_t{1}, World.Get()->PendingDestroyCount(), "Only the accepted destroy is queued after the rejections");
}

} // namespace
