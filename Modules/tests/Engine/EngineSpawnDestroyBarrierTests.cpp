#include "EngineSpawnDestroyTestHelpers.h"
#include "TestSupport.h"

#include <MicroWorld/Core/TickContext.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/World.h>

#include <cstddef>
#include <cstdint>

namespace
{

using namespace ::MicroWorld::Tests;

/**
 * Motivation: Begin an empty world, queue a SpawnActor, read the inert queue state, then run ApplyPending and
 *   Advance.
 * Responsibilities: SpawnActor only queues while playing; the queued actor receives its BeginPlay at the next
 *   ApplyPending barrier, never at the SpawnActor.
 */
MW_TEST_CASE(EngineSpawnActorBeginsAtNextBarrierNotImmediately)
{
	// Arrange
	FSequenceCounter Sequence{};
	FActorEventState SpawnedEvents{};

	FSpawnDestroyEnvironment Env{};
	FWorldActorRegistry<2> WorldActors;
	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::Engine::UWorldClassId, WorldActors.MakeReference());
	const TObjectPtr<FOrderingActor> Spawned = MakeOrderingActor(Env, Sequence, SpawnedEvents);

	// Act - BeginPlay, then the SpawnActor call itself queues without beginning
	const ERuntimeResult BeginResult = World.Get()->BeginPlay(BaselineTimeMilliseconds);
	const EEngineResult SpawnResult = World.Get()->SpawnActor(TObjectPtr<AActor>{Spawned});
	const std::uint32_t BeginCountAfterQueue = SpawnedEvents.BeginCount;
	const std::size_t PendingAfterQueue = World.Get()->PendingSpawnCount();
	const std::size_t LiveAfterQueue = WorldActors.GetCount();

	// Assert - the queued spawn is observable but inert
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "BeginPlay should succeed on the empty world");
	MW_EXPECT_EQ(Test, EEngineResult::Success, SpawnResult, "A same-store unowned actor is accepted for deferred spawn");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, BeginCountAfterQueue, "A queued spawn must not begin at the SpawnActor call");
	MW_EXPECT_EQ(Test, std::size_t{1}, PendingAfterQueue, "The queued spawn is observable as one pending spawn");
	MW_EXPECT_EQ(Test, std::size_t{0}, LiveAfterQueue, "A queued spawn must not join the live registry before the barrier");

	// Act - the barrier applies the queued spawn
	const ERuntimeResult ApplyResult = World.Get()->ApplyPending(BarrierTimeMilliseconds);
	const std::uint32_t BeginCountAfterBarrier = SpawnedEvents.BeginCount;
	const std::size_t PendingAfterBarrier = World.Get()->PendingSpawnCount();
	const std::size_t LiveAfterBarrier = WorldActors.GetCount();
	const ERuntimeResult AdvanceResult = World.Get()->Advance(SurvivorAdvanceTimeMilliseconds);

	// Assert - the actor begins at the barrier and ticks as a participant afterward
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ApplyResult, "ApplyPending should succeed applying the spawn");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, BeginCountAfterBarrier, "The spawned actor begins exactly once at the barrier");
	MW_EXPECT_EQ(Test, std::size_t{0}, PendingAfterBarrier, "The barrier drains the pending-spawn queue");
	MW_EXPECT_EQ(Test, std::size_t{1}, LiveAfterBarrier, "The spawned actor joins the live registry at the barrier");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, AdvanceResult, "Advance after the barrier should succeed");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, SpawnedEvents.TickCount, "A spawned actor ticks as a live participant after the barrier");
}

/**
 * Motivation: Register an actor with two components, queue a DestroyActor, then run ApplyPending.
 * Responsibilities: DestroyActor only queues while playing; the queued actor ends at the barrier, with its own EndPlay
 *   before its components end in reverse.
 */
MW_TEST_CASE(EngineDestroyActorEndsAtBarrierWithReverseComponentShutdown)
{
	// Arrange
	FSequenceCounter Sequence{};
	FActorEventState ActorEvents{};
	FComponentEventState FirstComponentEvents{};
	FComponentEventState SecondComponentEvents{};

	FSpawnDestroyEnvironment Env{};
	FWorldActorRegistry<1> WorldActors;
	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::Engine::UWorldClassId, WorldActors.MakeReference());
	const TObjectPtr<FOrderingActor> Actor = MakeOrderingActor(Env, Sequence, ActorEvents);
	const TObjectPtr<FOrderingComponent> FirstComponent = MakeOrderingComponent(Env, Sequence, FirstComponentEvents);
	const TObjectPtr<FOrderingComponent> SecondComponent = MakeOrderingComponent(Env, Sequence, SecondComponentEvents);
	(void)Actor.Get()->RegisterComponent(FirstComponent);
	(void)Actor.Get()->RegisterComponent(SecondComponent);
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{Actor});
	(void)World.Get()->BeginPlay(BaselineTimeMilliseconds);

	// Act - the DestroyActor call itself queues without ending
	const EEngineResult DestroyResult = World.Get()->DestroyActor(TObjectPtr<AActor>{Actor});
	const std::uint32_t EndCountAfterQueue = ActorEvents.EndCount;
	const std::size_t PendingAfterQueue = World.Get()->PendingDestroyCount();

	// Act - the barrier applies the queued destroy and ends the actor and components
	const ERuntimeResult ApplyResult = World.Get()->ApplyPending(BarrierTimeMilliseconds);

	// Assert - the queued destroy is observable but inert, then ends correctly at the barrier
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

} // namespace
