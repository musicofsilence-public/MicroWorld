#include "EngineTestSupport.h"
#include "EngineDeferredSpawnTestHelpers.h"
#include "TestSupport.h"

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorSpawnRequest.h>
#include <MicroWorld/Engine/ActorSpawnRequestResult.h>
#include <MicroWorld/Engine/ActorSpawnState.h>
#include <MicroWorld/Engine/ActorSpawnStatus.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/TDeferredActorSpawnStorage.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{

using namespace ::MicroWorld::Tests;

using MicroWorld::Engine::AActor;
using MicroWorld::Engine::EActorSpawnRequestResult;
using MicroWorld::Engine::EActorSpawnState;
using MicroWorld::Engine::EObjectResult;
using MicroWorld::Engine::FActorSpawnRequest;
using MicroWorld::Engine::FActorSpawnStatus;
using MicroWorld::Engine::FWorldActorRegistry;
using MicroWorld::Engine::TDeferredActorSpawnStorage;
using MicroWorld::Engine::TObjectCreationResult;

/**
 * Motivation: Queue two pre-play requests competing for a single actor slot, then run BeginPlay.
 * Responsibilities: Composition-time requests reserve the same fixed actor capacity before they are constructed.
 */
MW_TEST_CASE(EngineDeferredSpawnBeforeBeginPlayRejectsCapacityExhaustion)
{
	// Arrange
	FCombinedSpawnCapacityHost Host{FGarbageCollectionBudget{8, 8, 8}};
	const TObjectPtr<UWorld> World = Host.CreateWorld();
	FDeferredSpawnState State{};

	// Assert - the capacity test creates a configured world
	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "The capacity test creates a configured world");

	// Act - two pre-play requests compete for the single actor slot
	const FActorSpawnRequest FirstRequest = Host.GetWorld().SpawnActor<FDeferredActor>(&State);
	const FActorSpawnRequest SecondRequest = Host.GetWorld().SpawnActor<FDeferredActor>(&State);

	// Assert - the first is admitted and the second is rejected at fixed capacity
	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::Queued, FirstRequest.Result, "The only actor slot admits the first pre-play request");
	MW_EXPECT_EQ(
		Test, EActorSpawnRequestResult::CapacityExceeded, SecondRequest.Result, "The second pre-play request is rejected at fixed actor capacity");
	MW_EXPECT_TRUE(Test, !SecondRequest.Handle.IsValid(), "A capacity-rejected request returns no completion handle");

	// Act - the admitted pre-play request still begins successfully
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "The admitted pre-play request still begins successfully");

	// Assert - the admitted request becomes a live actor and the rejected one never began
	MW_EXPECT_EQ(
		Test, EActorSpawnState::Spawned, Host.GetWorld().GetSpawnStatus(FirstRequest.Handle).State, "The admitted request becomes a live actor");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, State.BeginCount, "Only the admitted actor receives BeginPlay");
}

/**
 * Motivation: Begin and end a world, then attempt a typed spawn request.
 * Responsibilities: A terminal world never reopens typed actor admission after it has ended.
 */
MW_TEST_CASE(EngineDeferredSpawnRejectsEndedWorld)
{
	// Arrange
	FDeferredSpawnHost Host{FGarbageCollectionBudget{8, 8, 8}};
	const TObjectPtr<UWorld> World = Host.CreateWorld();
	FDeferredSpawnState State{};

	// Arrange - the world enters and then completes its terminal lifecycle transition
	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "The ended-world test creates a configured world");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "The world enters play before ending");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.EndPlay(), "The world completes its terminal lifecycle transition");

	// Act
	const FActorSpawnRequest Request = Host.GetWorld().SpawnActor<FDeferredActor>(&State);

	// Assert
	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::LifecycleLocked, Request.Result, "An ended world rejects typed spawn requests");
	MW_EXPECT_TRUE(Test, !Request.Handle.IsValid(), "An ended-world rejection returns no completion handle");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, State.BeginCount, "An ended world cannot dispatch another actor begin");
}

/**
 * Motivation: Begin a world and queue a typed request with an oversized factory capture.
 * Responsibilities: Factory byte preflight rejects before consuming an actor-storage reference or reserving a request.
 */
MW_TEST_CASE(EngineDeferredSpawnRejectsOversizedFactoryWithoutMutation)
{
	// Arrange
	FDeferredSpawnHost Host{FGarbageCollectionBudget{8, 8, 8}};
	const TObjectPtr<UWorld> World = Host.CreateWorld();
	FDeferredSpawnState State{};

	// Arrange - the world enters play before queue preflight
	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "The application entry point creates a configured world");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "The world enters play before queue preflight");

	// Act
	const FActorSpawnRequest Request = Host.GetWorld().SpawnActor<FLargeDeferredActor>(&State, FDeferredLargeCapture{});

	// Assert
	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::FactoryTooLarge, Request.Result, "Oversized captures are rejected before factory construction");
	MW_EXPECT_TRUE(Test, !Request.Handle.IsValid(), "A rejected request returns no completion handle");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, State.BeginCount, "Layout rejection leaves no actor lifecycle side effect");
}

/**
 * Motivation: Begin a world and queue a typed request with an over-aligned factory capture.
 * Responsibilities: An unsupported aligned factory is rejected before it consumes one deferred request slot.
 */
MW_TEST_CASE(EngineDeferredSpawnRejectsUnsupportedFactoryAlignment)
{
	// Arrange
	FDeferredSpawnHost Host{FGarbageCollectionBudget{8, 8, 8}};
	const TObjectPtr<UWorld> World = Host.CreateWorld();
	FDeferredSpawnState State{};

	// Arrange - the world enters play before factory-layout preflight
	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "The application entry point creates a configured world");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "The world enters play before factory-layout preflight");

	// Act
	const FActorSpawnRequest Request = Host.GetWorld().SpawnActor<FOveralignedDeferredActor>(&State, FOveralignedCapture{});

	// Assert
	MW_EXPECT_EQ(
		Test,
		EActorSpawnRequestResult::FactoryAlignmentUnsupported,
		Request.Result,
		"An over-aligned factory is rejected before capture construction");
	MW_EXPECT_TRUE(Test, !Request.Handle.IsValid(), "Alignment rejection returns no completion handle");
}

/**
 * Motivation: Reserve the only actor slot with a typed request, then attempt a manual preconstructed spawn against
 *   the same world.
 * Responsibilities: A queued typed request consumes the same finite World capacity as a manual preconstructed spawn.
 */
MW_TEST_CASE(EngineDeferredSpawnRejectsManualSpawnWhenTypedRequestUsesRemainingCapacity)
{
	// Arrange
	FCombinedSpawnCapacityHost Host{FGarbageCollectionBudget{8, 8, 8}};
	FDeferredSpawnState State{};

	const MicroWorld::Engine::EObjectResult RegisterResult = Host.RegisterClass<FDeferredActor>(DeferredActorTypeId, "DeferredActor");
	const TObjectPtr<UWorld> World = Host.CreateWorld();
	const TObjectCreationResult<FDeferredActor> ManualCreation = Host.CreateObject<FDeferredActor>(DeferredActorTypeId, &State);
	const bool bWorldCreated = World.Get() != nullptr;
	const bool bManualActorCreated = ManualCreation.Object.Get() != nullptr;
	const ERuntimeResult BeginResult = Host.BeginPlay(0);

	// Assert - the manual actor class, world, actor, and play baseline are ready
	MW_EXPECT_EQ(Test, MicroWorld::Engine::EObjectResult::Success, RegisterResult, "The manual actor class registers before world creation");
	MW_EXPECT_TRUE(Test, bWorldCreated, "The capacity test creates an isolated configured world");
	MW_EXPECT_EQ(Test, MicroWorld::Engine::EObjectResult::Success, ManualCreation.Result, "The manual actor is constructed in the same object store");
	MW_EXPECT_TRUE(Test, bManualActorCreated, "The manual actor has a valid managed reference before queueing");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "The empty capacity test world begins before spawn requests");

	// Act - the typed request reserves the only slot, then the manual request competes for it
	const FActorSpawnRequest TypedRequest = Host.GetWorld().SpawnActor<FDeferredActor>(&State);
	const FActorSpawnStatus TypedStatus = Host.GetWorld().GetSpawnStatus(TypedRequest.Handle);
	const MicroWorld::Engine::EEngineResult ManualSpawnResult =
		Host.GetWorld().SpawnActor(MicroWorld::Engine::TObjectPtr<AActor>{ManualCreation.Object});
	const std::size_t PendingManualSpawnCount = Host.GetWorld().PendingSpawnCount();

	// Assert
	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::Queued, TypedRequest.Result, "The typed request reserves the only future world actor slot");
	MW_EXPECT_EQ(Test, EActorSpawnState::Queued, TypedStatus.State, "The typed request remains queued before the world barrier");
	MW_EXPECT_EQ(
		Test,
		MicroWorld::Engine::EEngineResult::CapacityExceeded,
		ManualSpawnResult,
		"The manual request is rejected when typed work uses remaining capacity");
	MW_EXPECT_EQ(Test, std::size_t{0}, PendingManualSpawnCount, "The rejected manual request does not enter the pending spawn registry");
}

/**
 * Motivation: Begin a world, enter an active collection phase, then attempt a typed spawn with a move-probe
 *   argument.
 * Responsibilities: Active collection rejects typed spawn before moving the caller's constructor argument.
 */
MW_TEST_CASE(EngineDeferredSpawnRejectsActiveCollectionBeforeMovingArguments)
{
	// Arrange
	FDeferredSpawnEnvironment Env{};
	FObjectStore& Store = Env.GetStore();
	FDeferredSpawnCollectorFixture CollectorFixture{Store};
	FGarbageCollector& Collector = CollectorFixture.GetCollector();
	FWorldActorRegistry<1> WorldActors;
	TDeferredActorSpawnStorage<1, 96> DeferredSpawns;
	const TObjectCreationResult<UWorld> WorldCreation = Store.NewObject<UWorld>(
		*Env.FindDescriptor(MicroWorld::Engine::UWorldClassId),
		WorldActors.MakeReference(),
		DeferredSpawns.MakeReference(),
		MicroWorld::Engine::MakeClassRegistryRegistrationView(Env.GetRegistry()));
	const TObjectPtr<UWorld> World = WorldCreation.Object;
	const TStrongObjectPtr<UWorld> WorldRoot = Env.MakeRoot(World);
	const bool bWorldCreated = World.Get() != nullptr;
	const ERuntimeResult BeginResult = World.Get()->BeginPlay(0);
	const ERuntimeResult CollectionRequestResult = Collector.RequestCollection();
	FDeferredMoveProbe MoveProbe{};

	// Assert - the configured world is rooted, begun, and an active collection phase is entered
	MW_EXPECT_EQ(Test, EObjectResult::Success, WorldCreation.Result, "The direct fixture creates a configured deferred-spawn world");
	MW_EXPECT_TRUE(Test, bWorldCreated, "The direct fixture roots a valid configured world");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "The configured world begins before collection admission");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, CollectionRequestResult, "The collector enters an active collection phase");

	// Act - a typed spawn is attempted during collection, then the active phase is released
	const FActorSpawnRequest Request = World.Get()->SpawnActor<FMoveProbeDeferredActor>(std::move(MoveProbe));
	const bool bMoveProbeWasMoved = MoveProbe.bWasMovedFrom;
	const ERuntimeResult CancelResult = Collector.CancelCollection();

	// Assert
	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::LifecycleLocked, Request.Result, "Active collection rejects a typed spawn before factory capture");
	MW_EXPECT_TRUE(Test, !bMoveProbeWasMoved, "Lifecycle rejection leaves the caller move probe unchanged");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, CancelResult, "The direct fixture releases collection ownership after rejection");
}

} // namespace
