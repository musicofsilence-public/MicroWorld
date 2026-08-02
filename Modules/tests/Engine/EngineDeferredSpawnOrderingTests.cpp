#include "EngineTestSupport.h"
#include "EngineDeferredSpawnTestHelpers.h"
#include "TestSupport.h"

#include <MicroWorld/Engine/ActorSpawnRequest.h>
#include <MicroWorld/Engine/ActorSpawnRequestResult.h>
#include <MicroWorld/Engine/ActorSpawnState.h>
#include <MicroWorld/Engine/ActorSpawnStatus.h>
#include <MicroWorld/Engine/EngineResult.h>

#include <cstdint>

namespace
{

using namespace ::MicroWorld::Tests;

using MicroWorld::Engine::EActorSpawnRequestResult;
using MicroWorld::Engine::EActorSpawnState;
using MicroWorld::Engine::EObjectResult;
using MicroWorld::Engine::FActorSpawnRequest;
using MicroWorld::Engine::FActorSpawnStatus;
using MicroWorld::Engine::FWorldActorRegistry;

/**
 * Motivation: Begin an empty world, queue a typed factory request, then run the next-frame barrier.
 * Responsibilities: A typed factory remains queued until the next safe World barrier then yields a world-owned actor.
 */
MW_TEST_CASE(EngineDeferredSpawnReportsQueuedThenSpawnedAtBarrier)
{
	// Arrange
	FDeferredSpawnHost Host{FGarbageCollectionBudget{8, 8, 8}};
	const TObjectPtr<UWorld> World = Host.CreateWorld();
	FDeferredSpawnState State{};

	// Assert - the application entry point creates and begins the configured world
	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "The application entry point creates a configured world");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "The empty world begins before deferred spawn admission");

	// Act - the typed request queues a bounded factory without beginning the actor
	const FActorSpawnRequest Request = Host.GetWorld().SpawnActor<FDeferredActor>(&State);
	const FActorSpawnStatus QueuedStatus = Host.GetWorld().GetSpawnStatus(Request.Handle);

	// Assert - the completion handle stays queued and no construction ran at admission
	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::Queued, Request.Result, "Typed spawning queues a bounded factory during play");
	MW_EXPECT_EQ(Test, EActorSpawnState::Queued, QueuedStatus.State, "The completion handle stays queued before the barrier");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, State.BeginCount, "Queue admission never constructs or begins the actor immediately");

	// Act - the next frame applies the deferred factory barrier
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(10), "The next frame applies the deferred factory barrier");
	const FActorSpawnStatus SpawnedStatus = Host.GetWorld().GetSpawnStatus(Request.Handle);

	// Assert - the handle becomes spawned and the actor begins exactly once at publication
	MW_EXPECT_EQ(Test, EActorSpawnState::Spawned, SpawnedStatus.State, "The handle becomes spawned after world publication");
	MW_EXPECT_TRUE(Test, SpawnedStatus.Actor.Get() != nullptr, "A spawned handle resolves to the live world-owned actor");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, State.BeginCount, "The actor begins exactly once at the barrier");
}

/**
 * Motivation: Admit a typed request before play begins, then run BeginPlay.
 * Responsibilities: A typed request accepted during composition begins when the World enters play.
 */
MW_TEST_CASE(EngineDeferredSpawnBeforeBeginPlayStartsWhenPlayBegins)
{
	// Arrange
	FDeferredSpawnHost Host{FGarbageCollectionBudget{8, 8, 8}};
	const TObjectPtr<UWorld> World = Host.CreateWorld();
	FDeferredSpawnState State{};

	// Assert - the application entry point creates a configured world
	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "The application entry point creates a configured world");

	// Act - the typed request is admitted before play begins
	const FActorSpawnRequest Request = Host.GetWorld().SpawnActor<FDeferredActor>(&State);

	// Assert - the request queues without beginning the actor
	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::Queued, Request.Result, "A typed request is admitted before play begins");
	MW_EXPECT_EQ(
		Test, EActorSpawnState::Queued, Host.GetWorld().GetSpawnStatus(Request.Handle).State, "The pre-play request remains queued before BeginPlay");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, State.BeginCount, "Queue admission does not begin the actor during composition");

	// Act - BeginPlay drains the pre-play typed request
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "BeginPlay drains the pre-play typed request");

	// Assert - the pre-play request becomes a live, begun actor
	MW_EXPECT_EQ(
		Test,
		EActorSpawnState::Spawned,
		Host.GetWorld().GetSpawnStatus(Request.Handle).State,
		"The pre-play request becomes a live actor at BeginPlay");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, State.BeginCount, "The pre-play actor begins exactly once");
}

/**
 * Motivation: Queue two typed requests in order before play begins, then run BeginPlay.
 * Responsibilities: Pre-play typed requests preserve their FIFO queue order when BeginPlay starts them.
 */
MW_TEST_CASE(EngineDeferredSpawnBeforeBeginPlayStartsInQueueOrder)
{
	// Arrange
	FDeferredSpawnHost Host{FGarbageCollectionBudget{8, 8, 8}};
	const TObjectPtr<UWorld> World = Host.CreateWorld();
	FDeferredSpawnOrderState State{};

	// Assert - the queue-order test creates a configured world
	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "The queue-order test creates a configured world");

	// Act - two pre-play typed requests queue in order
	const FActorSpawnRequest FirstRequest = Host.GetWorld().SpawnActor<FOrderedDeferredActor>(&State, 1);
	const FActorSpawnRequest SecondRequest = Host.GetWorld().SpawnActor<FOrderedDeferredActor>(&State, 2);

	// Assert - both requests are queued
	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::Queued, FirstRequest.Result, "The first pre-play typed request is queued");
	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::Queued, SecondRequest.Result, "The second pre-play typed request is queued");

	// Act - BeginPlay starts both pre-play typed actors
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "BeginPlay starts both pre-play typed actors");

	// Assert - both queued actors begin in FIFO order and become live
	MW_EXPECT_EQ(Test, std::size_t{2}, State.BeginCount, "Both queued actors begin once");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, State.BeginOrder[0], "The first queued actor begins first");
	MW_EXPECT_EQ(Test, std::uint32_t{2}, State.BeginOrder[1], "The second queued actor begins second");
	MW_EXPECT_EQ(Test, EActorSpawnState::Spawned, Host.GetWorld().GetSpawnStatus(FirstRequest.Handle).State, "The first queued actor becomes live");
	MW_EXPECT_EQ(Test, EActorSpawnState::Spawned, Host.GetWorld().GetSpawnStatus(SecondRequest.Handle).State, "The second queued actor becomes live");
}

/**
 * Motivation: Register one actor, queue one typed request before play begins, then run BeginPlay.
 * Responsibilities: Registered actors retain their established priority over composition-time typed requests.
 */
MW_TEST_CASE(EngineDeferredSpawnBeforeBeginPlayBeginsAfterRegisteredActors)
{
	// Arrange
	FDeferredSpawnHost Host{FGarbageCollectionBudget{8, 8, 8}};
	FDeferredSpawnOrderState State{};
	constexpr MicroWorld::Engine::FTypeId OrderedDeferredActorTypeId{0x00070002u};

	const EObjectResult RegistrationResult = Host.RegisterClass<FOrderedDeferredActor>(OrderedDeferredActorTypeId, "OrderedDeferredActor");
	const TObjectPtr<UWorld> World = Host.CreateWorld();
	const MicroWorld::Engine::TObjectCreationResult<FOrderedDeferredActor> RegisteredActor =
		Host.CreateObject<FOrderedDeferredActor>(OrderedDeferredActorTypeId, &State, 1);
	const MicroWorld::Engine::EEngineResult RegisterActorResult =
		World.Get()->RegisterActor(MicroWorld::Engine::TObjectPtr<AActor>{RegisteredActor.Object});
	const FActorSpawnRequest QueuedRequest = Host.GetWorld().SpawnActor<FOrderedDeferredActor>(&State, 2);

	// Assert - the registered actor and queued request are both ready before play
	MW_EXPECT_EQ(Test, EObjectResult::Success, RegistrationResult, "The registered actor class is available before world creation");
	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "The ordering test creates a configured world");
	MW_EXPECT_EQ(Test, EObjectResult::Success, RegisteredActor.Result, "The registered actor constructs before play");
	MW_EXPECT_EQ(Test, MicroWorld::Engine::EEngineResult::Success, RegisterActorResult, "The registered actor attaches before the typed request");
	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::Queued, QueuedRequest.Result, "The composition-time typed request is queued");

	// Act - BeginPlay starts registered and queued actors
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "BeginPlay starts registered and queued actors");

	// Assert - the registered actor begins before the queued actor
	MW_EXPECT_EQ(Test, std::size_t{2}, State.BeginCount, "Both actors begin exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, State.BeginOrder[0], "The registered actor begins before queued actors");
	MW_EXPECT_EQ(Test, std::uint32_t{2}, State.BeginOrder[1], "The queued actor begins after the registered actor");
}

} // namespace
