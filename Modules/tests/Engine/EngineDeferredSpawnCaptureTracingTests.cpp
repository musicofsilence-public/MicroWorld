#include "EngineTestSupport.h"
#include "EngineDeferredSpawnTestHelpers.h"
#include "TestSupport.h"

#include <MicroWorld/Engine/ActorSpawnRequest.h>
#include <MicroWorld/Engine/ActorSpawnRequestResult.h>
#include <MicroWorld/Engine/ActorSpawnState.h>
#include <MicroWorld/Engine/ActorSpawnStatus.h>
#include <MicroWorld/Engine/GarbageCollectionResult.h>
#include <MicroWorld/Engine/TDeferredActorSpawnStorage.h>

#include <cstdint>
#include <utility>

namespace
{

using namespace ::MicroWorld::Tests;

using MicroWorld::Engine::EActorSpawnRequestResult;
using MicroWorld::Engine::EActorSpawnState;
using MicroWorld::Engine::EObjectResult;
using MicroWorld::Engine::FActorSpawnRequest;
using MicroWorld::Engine::FActorSpawnStatus;
using MicroWorld::Engine::FGarbageCollectionResult;
using MicroWorld::Engine::FWorldActorRegistry;
using MicroWorld::Engine::TDeferredActorSpawnStorage;
using MicroWorld::Engine::TObjectCreationResult;

/**
 * Motivation: Begin a world, queue a typed factory capturing an lvalue managed pointer by value, then run the
 *   barrier.
 * Responsibilities: A typed factory accepts an lvalue managed pointer and delivers its value to the spawned actor.
 */
MW_TEST_CASE(EngineDeferredSpawnDeliversLvalueObjectPointerToSpawnedActor)
{
	// Arrange
	FDeferredSpawnHost Host{FGarbageCollectionBudget{8, 8, 8}};
	const TObjectPtr<UWorld> World = Host.CreateWorld();
	const TObjectPtr<UWorld> WorldReference = World;
	const bool bWorldCreated = World.Get() != nullptr;
	const ERuntimeResult BeginResult = Host.BeginPlay(0);

	// Assert - the world is created and begun before lvalue capture admission
	MW_EXPECT_TRUE(Test, bWorldCreated, "The lvalue capture test creates an isolated configured world");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "The world begins before deferred lvalue capture admission");

	// Act - the typed request captures the lvalue pointer, then the barrier constructs the actor
	const FActorSpawnRequest Request = Host.GetWorld().SpawnActor<FLvaluePointerDeferredActor>(WorldReference);
	const ERuntimeResult TickResult = Host.Tick(10);
	const FActorSpawnStatus SpawnedStatus = Host.GetWorld().GetSpawnStatus(Request.Handle);
	FLvaluePointerDeferredActor* const SpawnedActor = static_cast<FLvaluePointerDeferredActor*>(SpawnedStatus.Actor.Get());
	const TObjectPtr<UWorld> CapturedWorld = SpawnedActor != nullptr ? SpawnedActor->GetCapturedWorld() : TObjectPtr<UWorld>{};
	const bool bCapturedWorldMatchesInput = CapturedWorld.Handle() == WorldReference.Handle();

	// Assert
	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::Queued, Request.Result, "The typed request accepts an lvalue managed pointer argument");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, TickResult, "The deferred lvalue factory constructs successfully at the barrier");
	MW_EXPECT_EQ(Test, EActorSpawnState::Spawned, SpawnedStatus.State, "The lvalue factory publishes a live world-owned actor");
	MW_EXPECT_TRUE(Test, SpawnedActor != nullptr, "The spawned lvalue-capturing actor resolves through the public handle");
	MW_EXPECT_TRUE(Test, bCapturedWorldMatchesInput, "The spawned actor receives the original lvalue managed pointer value");
}

/**
 * Motivation: Queue a typed factory capturing an unrooted managed object, run a full collection, then run the
 *   barrier to publish.
 * Responsibilities: A queued typed factory traces an otherwise unrooted managed capture through full collection.
 */
MW_TEST_CASE(EngineDeferredSpawnTracesCapturedObjectThroughCollectionThenPublishes)
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
	const TObjectCreationResult<MicroWorld::Engine::AActor> CaptureCreation =
		Store.NewObject<MicroWorld::Engine::AActor>(*Env.FindDescriptor(MicroWorld::Engine::AActorClassId));
	const TObjectPtr<UObject> UnrootedCapture{CaptureCreation.Object};
	const bool bWorldCreated = World.Get() != nullptr;
	const bool bCaptureCreated = UnrootedCapture.Get() != nullptr;
	const ERuntimeResult BeginResult = World.Get()->BeginPlay(0);

	// Assert - the tracing fixture creates and roots its world and capture before queueing
	MW_EXPECT_EQ(Test, EObjectResult::Success, WorldCreation.Result, "The tracing fixture creates a configured deferred-spawn world");
	MW_EXPECT_TRUE(Test, bWorldCreated, "The tracing fixture roots its configured world");
	MW_EXPECT_EQ(Test, EObjectResult::Success, CaptureCreation.Result, "The fixture creates an otherwise unrooted capture object");
	MW_EXPECT_TRUE(Test, bCaptureCreated, "The unrooted capture resolves before it enters the factory");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "The tracing fixture begins the configured world");

	// Act - queue the factory, run collection over the queued capture, then publish
	const FActorSpawnRequest Request = World.Get()->SpawnActor<FObjectCaptureDeferredActor>(UnrootedCapture);
	const FGarbageCollectionResult CollectionResult = Collector.CollectFull();
	const bool bCaptureSurvivedCollection = UnrootedCapture.Get() != nullptr;
	const ERuntimeResult ApplyResult = World.Get()->ApplyPending(10);
	const FActorSpawnStatus SpawnStatus = World.Get()->GetSpawnStatus(Request.Handle);
	const bool bSpawnedActorPublished = SpawnStatus.Actor.Get() != nullptr;

	// Assert
	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::Queued, Request.Result, "The typed factory accepts the unrooted managed capture");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, CollectionResult.Result, "Full collection completes while the factory remains queued");
	MW_EXPECT_TRUE(Test, bCaptureSurvivedCollection, "The queued factory traces and retains its managed capture");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ApplyResult, "The captured factory publishes successfully after collection");
	MW_EXPECT_EQ(Test, EActorSpawnState::Spawned, SpawnStatus.State, "The queued factory transitions to a spawned public result");
	MW_EXPECT_TRUE(Test, bSpawnedActorPublished, "Publication returns the constructed actor through the public handle");
}

/**
 * Motivation: Fill the store with a world and a blocking object, queue a typed request, then run the barrier that
 *   fails construction.
 * Responsibilities: Barrier-time typed construction reports store exhaustion without publishing a partial actor.
 */
MW_TEST_CASE(EngineDeferredSpawnReportsStoreCapacityFailureWithoutPublication)
{
	// Arrange
	FDeferredSpawnCapacityEnvironment Env{};
	FObjectStore& Store = Env.GetStore();
	FWorldActorRegistry<1> WorldActors;
	TDeferredActorSpawnStorage<1, 96> DeferredSpawns;
	FDeferredSpawnState State{};
	const TObjectCreationResult<UWorld> WorldCreation = Store.NewObject<UWorld>(
		*Env.FindDescriptor(MicroWorld::Engine::UWorldClassId),
		WorldActors.MakeReference(),
		DeferredSpawns.MakeReference(),
		MicroWorld::Engine::MakeClassRegistryRegistrationView(Env.GetRegistry()));
	const TObjectPtr<UWorld> World = WorldCreation.Object;
	const TObjectCreationResult<MicroWorld::Engine::AActor> BlockingCreation =
		Store.NewObject<MicroWorld::Engine::AActor>(*Env.FindDescriptor(MicroWorld::Engine::AActorClassId));
	const bool bWorldCreated = World.Get() != nullptr;
	const bool bBlockingObjectCreated = BlockingCreation.Object.Get() != nullptr;
	const ERuntimeResult BeginResult = World.Get()->BeginPlay(0);

	// Assert - the capacity fixture fills its store and begins before queue admission
	MW_EXPECT_EQ(Test, EObjectResult::Success, WorldCreation.Result, "The capacity fixture creates a configured deferred-spawn world");
	MW_EXPECT_TRUE(Test, bWorldCreated, "The capacity fixture has a valid world before filling its store");
	MW_EXPECT_EQ(Test, EObjectResult::Success, BlockingCreation.Result, "The blocking object fills the store's second and final slot");
	MW_EXPECT_TRUE(Test, bBlockingObjectCreated, "The blocking object occupies the store before the deferred barrier");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "The full-store fixture begins the world before typed queue admission");

	// Act - queue the request, then run the barrier that fails construction
	const FActorSpawnRequest Request = World.Get()->SpawnActor<FDeferredActor>(&State);
	const ERuntimeResult ApplyResult = World.Get()->ApplyPending(10);
	const FActorSpawnStatus SpawnStatus = World.Get()->GetSpawnStatus(Request.Handle);
	const bool bPublishedActorExists = SpawnStatus.Actor.Get() != nullptr;

	// Assert
	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::Queued, Request.Result, "Queue admission succeeds before barrier-time store construction");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ApplyResult, "The barrier completes after recording the typed construction failure");
	MW_EXPECT_EQ(Test, EActorSpawnState::Failed, SpawnStatus.State, "The completion handle reports terminal construction failure");
	MW_EXPECT_EQ(
		Test,
		EObjectResult::CapacityExceeded,
		SpawnStatus.CompletionResult,
		"The full object store reports exact typed construction capacity failure");
	MW_EXPECT_TRUE(Test, !bPublishedActorExists, "A failed typed construction never publishes an actor through its handle");
}

/**
 * Motivation: Publish a deferred actor, destroy it at the barrier, then queue a second request that reuses the
 *   terminal slot.
 * Responsibilities: A live spawn handle is pinned through world ownership, released on destruction, then staled by
 *   deterministic reuse.
 */
MW_TEST_CASE(EngineDeferredSpawnPinsThenInvalidatesReusedHandle)
{
	// Arrange
	FDeferredSpawnHost Host{FGarbageCollectionBudget{8, 8, 8}};
	const TObjectPtr<UWorld> World = Host.CreateWorld();
	FDeferredSpawnState State{};

	// Arrange - the world is created and begun before the deferred request
	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "The application entry point creates a configured world");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "The world begins before the deferred request");

	// Act - the first deferred actor publishes at the barrier and pins its handle
	const FActorSpawnRequest FirstRequest = Host.GetWorld().SpawnActor<FDeferredActor>(&State);
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(10), "The first deferred actor publishes at the barrier");
	const FActorSpawnStatus FirstStatus = Host.GetWorld().GetSpawnStatus(FirstRequest.Handle);

	// Assert - a live actor pins its completion handle
	MW_EXPECT_EQ(Test, EActorSpawnState::Spawned, FirstStatus.State, "A live actor pins its completion handle");

	// Act - the live deferred actor queues for destruction and the barrier releases its handle
	MW_EXPECT_EQ(
		Test,
		MicroWorld::Engine::EEngineResult::Success,
		Host.GetWorld().DestroyActor(FirstStatus.Actor),
		"The live deferred actor queues for destruction");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(20), "The destruction barrier releases the live handle");

	// Assert - the destroyed actor releases but does not immediately reuse its handle
	MW_EXPECT_EQ(
		Test,
		EActorSpawnState::Released,
		Host.GetWorld().GetSpawnStatus(FirstRequest.Handle).State,
		"Destroyed actor releases but does not immediately reuse its handle");

	// Act - a released terminal slot accepts the next request
	const FActorSpawnRequest SecondRequest = Host.GetWorld().SpawnActor<FDeferredActor>(&State);

	// Assert
	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::Queued, SecondRequest.Result, "A released terminal slot accepts the next request");
	MW_EXPECT_EQ(
		Test,
		EActorSpawnState::Stale,
		Host.GetWorld().GetSpawnStatus(FirstRequest.Handle).State,
		"Reusing a terminal slot invalidates the prior generation");
}

} // namespace
