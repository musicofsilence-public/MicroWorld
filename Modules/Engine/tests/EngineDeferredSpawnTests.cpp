#include "EngineTestSupport.h"
#include "TestSupport.h"

#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineStorage.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{

using MicroWorld::AActor;
using MicroWorld::EActorSpawnRequestResult;
using MicroWorld::EActorSpawnState;
using MicroWorld::EObjectResult;
using MicroWorld::ERuntimeResult;
using MicroWorld::FActorComponentRegistry;
using MicroWorld::FDefaultEngineTraits;
using MicroWorld::FGarbageCollectionBudget;
using MicroWorld::FGarbageCollector;
using MicroWorld::FGarbageCollectorStorage;
using MicroWorld::FObjectHandle;
using MicroWorld::FObjectStore;
using MicroWorld::FWorldActorRegistry;
using MicroWorld::TDeferredActorSpawnStorage;
using MicroWorld::TEngine;
using MicroWorld::TObjectPtr;
using MicroWorld::UObject;
using MicroWorld::UWorld;
using MicroWorld::Tests::TEngineEnvironment;

/** Records public BeginPlay observation for one deferred actor instance. */
struct FDeferredSpawnState final
{
	/** Counts barrier-time BeginPlay calls without observing queue internals. */
	std::uint32_t BeginCount{0};
};

/** Deliberately exceeds the configured inline factory budget without side effects. */
struct FDeferredLargeCapture final
{
	/** Holds enough bytes to force public factory-layout rejection before argument movement. */
	std::array<std::byte, 96> Bytes{};
};

/** Gives tests enough class and object capacity for a World plus bounded deferred actors. */
struct FDeferredSpawnTraits : FDefaultEngineTraits
{
	static constexpr std::size_t MaxClasses = 6;
	static constexpr std::size_t MaxObjects = 6;
	static constexpr std::size_t MaxActors = 2;
	static constexpr std::size_t MaxRoots = 1;
	static constexpr std::size_t InlineDeferredSpawnFactoryBytes = 96;
};

using FDeferredSpawnHost = TEngine<FDeferredSpawnTraits>;

/** Reduces live actor capacity to one so a queued typed request exhausts it before publication. */
struct FCombinedSpawnCapacityTraits : FDeferredSpawnTraits
{
	static constexpr std::size_t MaxActors = 1;
};

using FCombinedSpawnCapacityHost = TEngine<FCombinedSpawnCapacityTraits>;

/** Keeps the manually constructed actor descriptor stable for the mixed-spawn capacity regression. */
constexpr MicroWorld::FTypeId DeferredActorTypeId{0x00070001u};

/** Provides a public fixed-store fixture with enough room for collection and deferred construction tests. */
using FDeferredSpawnEnvironment = TEngineEnvironment<256, 16, 8, 1>;

/** Restricts the store to a world and one blocking object for construction-failure coverage. */
using FDeferredSpawnCapacityEnvironment = TEngineEnvironment<256, 16, 2, 0>;

/** Owns the caller-provided collection worklist used by the direct deferred-spawn fixtures. */
class FDeferredSpawnCollectorFixture final
{
public:
	/** Binds collection to the fixture's store without borrowing test-local temporary storage. */
	explicit FDeferredSpawnCollectorFixture(FObjectStore& InStore) noexcept
		: Collector(InStore, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())})
	{
	}

	/** Exposes the collector so each test can drive its public collection lifecycle. */
	FGarbageCollector& GetCollector() noexcept { return Collector; }

private:
	/** Backs bounded mark traversal for the entire fixture lifetime. */
	std::array<FObjectHandle, 8> Worklist{};

	/** Owns the active-cycle capability for the fixture's object store. */
	FGarbageCollector Collector;
};

/** Detects any move performed while deferred request preflight is expected to reject. */
struct FDeferredMoveProbe final
{
	/** Keeps the caller-visible move state unchanged until factory capture actually begins. */
	bool bWasMovedFrom{false};

	FDeferredMoveProbe() noexcept = default;
	FDeferredMoveProbe(const FDeferredMoveProbe&) = delete;
	FDeferredMoveProbe& operator=(const FDeferredMoveProbe&) = delete;
	FDeferredMoveProbe(FDeferredMoveProbe&& InOther) noexcept { InOther.bWasMovedFrom = true; }
	FDeferredMoveProbe& operator=(FDeferredMoveProbe&&) = delete;
};

/** Begins only when World publishes it from the deferred barrier. */
class FDeferredActor final : public AActor
{
public:
	/** Takes caller-owned component storage and a per-test event sink through the deferred factory. */
	FDeferredActor(MicroWorld::FActorComponentRegistryReference InComponents, FDeferredSpawnState* const InState) noexcept
		: AActor(std::move(InComponents)), State(InState)
	{
	}

protected:
	/** Makes barrier publication directly observable through the actor lifecycle contract. */
	void BeginPlay() noexcept override { ++State->BeginCount; }

private:
	/** Belongs to the test and proves the world did not call BeginPlay at queue time. */
	FDeferredSpawnState* State{nullptr};
};

/** Accepts the large capture only so the request tests factory layout rather than constructor validity. */
class FLargeDeferredActor final : public AActor
{
public:
	/** Retains the same observable state dependency while intentionally ignoring the oversized value. */
	FLargeDeferredActor(MicroWorld::FActorComponentRegistryReference InComponents, FDeferredSpawnState* const InState, FDeferredLargeCapture) noexcept
		: AActor(std::move(InComponents)), State(InState)
	{
	}

protected:
	/** Makes any accidental admission observable through the normal actor lifecycle. */
	void BeginPlay() noexcept override { ++State->BeginCount; }

private:
	/** Belongs to the test and remains unchanged when layout preflight rejects. */
	FDeferredSpawnState* State{nullptr};
};

/** Forces an unsupported factory alignment while leaving actor construction otherwise ordinary. */
struct alignas(32) FOveralignedCapture final
{
	/** Makes the capture itself require stricter alignment than caller-provided inline storage guarantees. */
	std::array<std::byte, 32> Bytes{};
};

/** Accepts the over-aligned capture only to exercise public factory-layout preflight. */
class FOveralignedDeferredActor final : public AActor
{
public:
	/** Keeps the test actor constructible if a future storage policy supports this alignment. */
	FOveralignedDeferredActor(
		MicroWorld::FActorComponentRegistryReference InComponents, FDeferredSpawnState* const InState, FOveralignedCapture) noexcept
		: AActor(std::move(InComponents)), State(InState)
	{
	}

protected:
	/** Makes unexpected admission observable. */
	void BeginPlay() noexcept override { ++State->BeginCount; }

private:
	/** Belongs to this isolated test. */
	FDeferredSpawnState* State{nullptr};
};

/** Retains a caller-provided world pointer so typed factory value capture is observable after publication. */
class FLvaluePointerDeferredActor final : public AActor
{
public:
	/** Captures an lvalue managed pointer by value for later barrier-time construction. */
	FLvaluePointerDeferredActor(
		MicroWorld::FActorComponentRegistryReference InComponents, const MicroWorld::TObjectPtr<MicroWorld::UWorld> InCapturedWorld) noexcept
		: AActor(std::move(InComponents)), CapturedWorld(InCapturedWorld)
	{
	}

	/** Returns the managed pointer value received by the spawned actor constructor. */
	MicroWorld::TObjectPtr<MicroWorld::UWorld> GetCapturedWorld() const noexcept { return CapturedWorld; }

private:
	/** Preserves the constructor input so the lvalue factory-capture contract remains observable. */
	MicroWorld::TObjectPtr<MicroWorld::UWorld> CapturedWorld{};
};

/** Accepts a move probe so active collection can prove queue preflight happens before argument capture. */
class FMoveProbeDeferredActor final : public AActor
{
public:
	/** Takes ownership only if deferred request admission reaches factory construction. */
	FMoveProbeDeferredActor(MicroWorld::FActorComponentRegistryReference InComponents, FDeferredMoveProbe) noexcept : AActor(std::move(InComponents))
	{
	}
};

/** Retains a captured managed object so queued-factory tracing is directly observable through collection. */
class FObjectCaptureDeferredActor final : public AActor
{
public:
	/** Stores the direct managed capture until the World publishes this actor. */
	FObjectCaptureDeferredActor(MicroWorld::FActorComponentRegistryReference InComponents, const TObjectPtr<UObject> InCapturedObject) noexcept
		: AActor(std::move(InComponents)), CapturedObject(InCapturedObject)
	{
	}

private:
	/** Keeps the managed capture meaningful after barrier-time construction. */
	TObjectPtr<UObject> CapturedObject{};
};

/** Proves a typed factory remains queued until the next safe World barrier then yields a world-owned actor. */
MW_TEST_CASE(EngineDeferredSpawnReportsQueuedThenSpawnedAtBarrier)
{
	FDeferredSpawnHost Host{FGarbageCollectionBudget{8, 8, 8}};
	const auto World = Host.CreateWorld();
	FActorComponentRegistry<0> Components;
	FDeferredSpawnState State{};

	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "The composition root creates a configured world");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "The empty world begins before deferred spawn admission");

	const auto Request = Host.GetWorld().SpawnActor<FDeferredActor>(Components.MakeReference(), &State);
	const auto QueuedStatus = Host.GetWorld().GetSpawnStatus(Request.Handle);
	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::Queued, Request.Result, "Typed spawning queues a bounded factory during play");
	MW_EXPECT_EQ(Test, EActorSpawnState::Queued, QueuedStatus.State, "The completion handle stays queued before the barrier");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, State.BeginCount, "Queue admission never constructs or begins the actor immediately");

	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(10), "The next frame applies the deferred factory barrier");
	const auto SpawnedStatus = Host.GetWorld().GetSpawnStatus(Request.Handle);
	MW_EXPECT_EQ(Test, EActorSpawnState::Spawned, SpawnedStatus.State, "The handle becomes spawned after world publication");
	MW_EXPECT_TRUE(Test, SpawnedStatus.Actor.Get() != nullptr, "A spawned handle resolves to the live world-owned actor");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, State.BeginCount, "The actor begins exactly once at the barrier");
}

/** Proves factory byte preflight rejects before consuming an actor-storage reference or reserving a request. */
MW_TEST_CASE(EngineDeferredSpawnRejectsOversizedFactoryWithoutMutation)
{
	FDeferredSpawnHost Host{FGarbageCollectionBudget{8, 8, 8}};
	const auto World = Host.CreateWorld();
	FActorComponentRegistry<0> Components;
	FDeferredSpawnState State{};

	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "The composition root creates a configured world");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "The world enters play before queue preflight");
	const auto Request = Host.GetWorld().SpawnActor<FLargeDeferredActor>(Components.MakeReference(), &State, FDeferredLargeCapture{});
	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::FactoryTooLarge, Request.Result, "Oversized captures are rejected before factory construction");
	MW_EXPECT_TRUE(Test, !Request.Handle.IsValid(), "A rejected request returns no completion handle");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, State.BeginCount, "Layout rejection leaves no actor lifecycle side effect");
}

/** Proves an unsupported aligned factory is rejected before it consumes one deferred request slot. */
MW_TEST_CASE(EngineDeferredSpawnRejectsUnsupportedFactoryAlignment)
{
	FDeferredSpawnHost Host{FGarbageCollectionBudget{8, 8, 8}};
	const auto World = Host.CreateWorld();
	FActorComponentRegistry<0> Components;
	FDeferredSpawnState State{};

	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "The composition root creates a configured world");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "The world enters play before factory-layout preflight");
	const auto Request = Host.GetWorld().SpawnActor<FOveralignedDeferredActor>(Components.MakeReference(), &State, FOveralignedCapture{});
	MW_EXPECT_EQ(
		Test,
		EActorSpawnRequestResult::FactoryAlignmentUnsupported,
		Request.Result,
		"An over-aligned factory is rejected before capture construction");
	MW_EXPECT_TRUE(Test, !Request.Handle.IsValid(), "Alignment rejection returns no completion handle");
}

/** Proves a queued typed request consumes the same finite World capacity as a manual preconstructed spawn. */
MW_TEST_CASE(EngineDeferredSpawnRejectsManualSpawnWhenTypedRequestUsesRemainingCapacity)
{
	FCombinedSpawnCapacityHost Host{FGarbageCollectionBudget{8, 8, 8}};
	FActorComponentRegistry<0> TypedComponents;
	FActorComponentRegistry<0> ManualComponents;
	FDeferredSpawnState State{};

	const MicroWorld::EObjectResult RegisterResult = Host.RegisterClass<FDeferredActor>(DeferredActorTypeId, "DeferredActor");
	const auto World = Host.CreateWorld();
	const auto ManualCreation = Host.CreateObject<FDeferredActor>(DeferredActorTypeId, ManualComponents.MakeReference(), &State);
	const bool bWorldCreated = World.Get() != nullptr;
	const bool bManualActorCreated = ManualCreation.Object.Get() != nullptr;
	const ERuntimeResult BeginResult = Host.BeginPlay(0);
	MW_EXPECT_EQ(Test, MicroWorld::EObjectResult::Success, RegisterResult, "The manual actor class registers before world creation");
	MW_EXPECT_TRUE(Test, bWorldCreated, "The capacity test creates an isolated configured world");
	MW_EXPECT_EQ(Test, MicroWorld::EObjectResult::Success, ManualCreation.Result, "The manual actor is constructed in the same object store");
	MW_EXPECT_TRUE(Test, bManualActorCreated, "The manual actor has a valid managed reference before queueing");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "The empty capacity test world begins before spawn requests");

	const auto TypedRequest = Host.GetWorld().SpawnActor<FDeferredActor>(TypedComponents.MakeReference(), &State);
	const auto TypedStatus = Host.GetWorld().GetSpawnStatus(TypedRequest.Handle);
	const MicroWorld::EEngineResult ManualSpawnResult = Host.GetWorld().SpawnActor(MicroWorld::TObjectPtr<AActor>{ManualCreation.Object});
	const std::size_t PendingManualSpawnCount = Host.GetWorld().PendingSpawnCount();

	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::Queued, TypedRequest.Result, "The typed request reserves the only future world actor slot");
	MW_EXPECT_EQ(Test, EActorSpawnState::Queued, TypedStatus.State, "The typed request remains queued before the world barrier");
	MW_EXPECT_EQ(
		Test,
		MicroWorld::EEngineResult::CapacityExceeded,
		ManualSpawnResult,
		"The manual request is rejected when typed work uses remaining capacity");
	MW_EXPECT_EQ(Test, std::size_t{0}, PendingManualSpawnCount, "The rejected manual request does not enter the pending spawn registry");
}

/** Proves a typed factory accepts an lvalue managed pointer and delivers its value to the spawned actor. */
MW_TEST_CASE(EngineDeferredSpawnDeliversLvalueObjectPointerToSpawnedActor)
{
	FDeferredSpawnHost Host{FGarbageCollectionBudget{8, 8, 8}};
	const auto World = Host.CreateWorld();
	FActorComponentRegistry<0> Components;
	const MicroWorld::TObjectPtr<MicroWorld::UWorld> WorldReference = World;
	const bool bWorldCreated = World.Get() != nullptr;
	const ERuntimeResult BeginResult = Host.BeginPlay(0);
	MW_EXPECT_TRUE(Test, bWorldCreated, "The lvalue capture test creates an isolated configured world");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "The world begins before deferred lvalue capture admission");

	const auto Request = Host.GetWorld().SpawnActor<FLvaluePointerDeferredActor>(Components.MakeReference(), WorldReference);
	const ERuntimeResult TickResult = Host.Tick(10);
	const auto SpawnedStatus = Host.GetWorld().GetSpawnStatus(Request.Handle);
	FLvaluePointerDeferredActor* const SpawnedActor = static_cast<FLvaluePointerDeferredActor*>(SpawnedStatus.Actor.Get());
	const MicroWorld::TObjectPtr<MicroWorld::UWorld> CapturedWorld =
		SpawnedActor != nullptr ? SpawnedActor->GetCapturedWorld() : MicroWorld::TObjectPtr<MicroWorld::UWorld>{};
	const bool bCapturedWorldMatchesInput = CapturedWorld.Handle() == WorldReference.Handle();

	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::Queued, Request.Result, "The typed request accepts an lvalue managed pointer argument");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, TickResult, "The deferred lvalue factory constructs successfully at the barrier");
	MW_EXPECT_EQ(Test, EActorSpawnState::Spawned, SpawnedStatus.State, "The lvalue factory publishes a live world-owned actor");
	MW_EXPECT_TRUE(Test, SpawnedActor != nullptr, "The spawned lvalue-capturing actor resolves through the public handle");
	MW_EXPECT_TRUE(Test, bCapturedWorldMatchesInput, "The spawned actor receives the original lvalue managed pointer value");
}

/** Proves active collection rejects typed spawn before moving the caller's constructor argument. */
MW_TEST_CASE(EngineDeferredSpawnRejectsActiveCollectionBeforeMovingArguments)
{
	FDeferredSpawnEnvironment Env{};
	FObjectStore& Store = Env.GetStore();
	FDeferredSpawnCollectorFixture CollectorFixture{Store};
	FGarbageCollector& Collector = CollectorFixture.GetCollector();
	FWorldActorRegistry<1> WorldActors;
	TDeferredActorSpawnStorage<1, 96> DeferredSpawns;
	FActorComponentRegistry<0> Components;
	const auto WorldCreation = Store.NewObject<UWorld>(
		*Env.FindDescriptor(MicroWorld::UWorldClassId),
		WorldActors.MakeReference(),
		DeferredSpawns.MakeReference(),
		MicroWorld::MakeClassRegistryRegistrationView(Env.GetRegistry()));
	const TObjectPtr<UWorld> World = WorldCreation.Object;
	auto WorldRoot = Env.MakeRoot(World);
	const bool bWorldCreated = World.Get() != nullptr;
	const ERuntimeResult BeginResult = World.Get()->BeginPlay(0);
	const ERuntimeResult CollectionRequestResult = Collector.RequestCollection();
	FDeferredMoveProbe MoveProbe{};

	MW_EXPECT_EQ(Test, EObjectResult::Success, WorldCreation.Result, "The direct fixture creates a configured deferred-spawn world");
	MW_EXPECT_TRUE(Test, bWorldCreated, "The direct fixture roots a valid configured world");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "The configured world begins before collection admission");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, CollectionRequestResult, "The collector enters an active collection phase");

	const auto Request = World.Get()->SpawnActor<FMoveProbeDeferredActor>(Components.MakeReference(), std::move(MoveProbe));
	const bool bMoveProbeWasMoved = MoveProbe.bWasMovedFrom;
	const ERuntimeResult CancelResult = Collector.CancelCollection();

	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::LifecycleLocked, Request.Result, "Active collection rejects a typed spawn before factory capture");
	MW_EXPECT_TRUE(Test, !bMoveProbeWasMoved, "Lifecycle rejection leaves the caller move probe unchanged");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, CancelResult, "The direct fixture releases collection ownership after rejection");
}

/** Proves a queued typed factory traces an otherwise unrooted managed capture through full collection. */
MW_TEST_CASE(EngineDeferredSpawnTracesCapturedObjectThroughCollectionThenPublishes)
{
	FDeferredSpawnEnvironment Env{};
	FObjectStore& Store = Env.GetStore();
	FDeferredSpawnCollectorFixture CollectorFixture{Store};
	FGarbageCollector& Collector = CollectorFixture.GetCollector();
	FWorldActorRegistry<1> WorldActors;
	TDeferredActorSpawnStorage<1, 96> DeferredSpawns;
	FActorComponentRegistry<0> CaptureComponents;
	FActorComponentRegistry<0> TypedComponents;
	const auto WorldCreation = Store.NewObject<UWorld>(
		*Env.FindDescriptor(MicroWorld::UWorldClassId),
		WorldActors.MakeReference(),
		DeferredSpawns.MakeReference(),
		MicroWorld::MakeClassRegistryRegistrationView(Env.GetRegistry()));
	const TObjectPtr<UWorld> World = WorldCreation.Object;
	auto WorldRoot = Env.MakeRoot(World);
	const auto CaptureCreation = Store.NewObject<AActor>(*Env.FindDescriptor(MicroWorld::AActorClassId), CaptureComponents.MakeReference());
	const TObjectPtr<UObject> UnrootedCapture{CaptureCreation.Object};
	const bool bWorldCreated = World.Get() != nullptr;
	const bool bCaptureCreated = UnrootedCapture.Get() != nullptr;
	const ERuntimeResult BeginResult = World.Get()->BeginPlay(0);

	MW_EXPECT_EQ(Test, EObjectResult::Success, WorldCreation.Result, "The tracing fixture creates a configured deferred-spawn world");
	MW_EXPECT_TRUE(Test, bWorldCreated, "The tracing fixture roots its configured world");
	MW_EXPECT_EQ(Test, EObjectResult::Success, CaptureCreation.Result, "The fixture creates an otherwise unrooted capture object");
	MW_EXPECT_TRUE(Test, bCaptureCreated, "The unrooted capture resolves before it enters the factory");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "The tracing fixture begins the configured world");

	const auto Request = World.Get()->SpawnActor<FObjectCaptureDeferredActor>(TypedComponents.MakeReference(), UnrootedCapture);
	const auto CollectionResult = Collector.CollectFull();
	const bool bCaptureSurvivedCollection = UnrootedCapture.Get() != nullptr;
	const ERuntimeResult ApplyResult = World.Get()->ApplyPending(10);
	const auto SpawnStatus = World.Get()->GetSpawnStatus(Request.Handle);
	const bool bSpawnedActorPublished = SpawnStatus.Actor.Get() != nullptr;

	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::Queued, Request.Result, "The typed factory accepts the unrooted managed capture");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, CollectionResult.Result, "Full collection completes while the factory remains queued");
	MW_EXPECT_TRUE(Test, bCaptureSurvivedCollection, "The queued factory traces and retains its managed capture");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ApplyResult, "The captured factory publishes successfully after collection");
	MW_EXPECT_EQ(Test, EActorSpawnState::Spawned, SpawnStatus.State, "The queued factory transitions to a spawned public result");
	MW_EXPECT_TRUE(Test, bSpawnedActorPublished, "Publication returns the constructed actor through the public handle");
}

/** Proves barrier-time typed construction reports store exhaustion without publishing a partial actor. */
MW_TEST_CASE(EngineDeferredSpawnReportsStoreCapacityFailureWithoutPublication)
{
	FDeferredSpawnCapacityEnvironment Env{};
	FObjectStore& Store = Env.GetStore();
	FWorldActorRegistry<1> WorldActors;
	TDeferredActorSpawnStorage<1, 96> DeferredSpawns;
	FActorComponentRegistry<0> BlockingComponents;
	FActorComponentRegistry<0> TypedComponents;
	FDeferredSpawnState State{};
	const auto WorldCreation = Store.NewObject<UWorld>(
		*Env.FindDescriptor(MicroWorld::UWorldClassId),
		WorldActors.MakeReference(),
		DeferredSpawns.MakeReference(),
		MicroWorld::MakeClassRegistryRegistrationView(Env.GetRegistry()));
	const TObjectPtr<UWorld> World = WorldCreation.Object;
	const auto BlockingCreation = Store.NewObject<AActor>(*Env.FindDescriptor(MicroWorld::AActorClassId), BlockingComponents.MakeReference());
	const bool bWorldCreated = World.Get() != nullptr;
	const bool bBlockingObjectCreated = BlockingCreation.Object.Get() != nullptr;
	const ERuntimeResult BeginResult = World.Get()->BeginPlay(0);

	MW_EXPECT_EQ(Test, EObjectResult::Success, WorldCreation.Result, "The capacity fixture creates a configured deferred-spawn world");
	MW_EXPECT_TRUE(Test, bWorldCreated, "The capacity fixture has a valid world before filling its store");
	MW_EXPECT_EQ(Test, EObjectResult::Success, BlockingCreation.Result, "The blocking object fills the store's second and final slot");
	MW_EXPECT_TRUE(Test, bBlockingObjectCreated, "The blocking object occupies the store before the deferred barrier");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "The full-store fixture begins the world before typed queue admission");

	const auto Request = World.Get()->SpawnActor<FDeferredActor>(TypedComponents.MakeReference(), &State);
	const ERuntimeResult ApplyResult = World.Get()->ApplyPending(10);
	const auto SpawnStatus = World.Get()->GetSpawnStatus(Request.Handle);
	const bool bPublishedActorExists = SpawnStatus.Actor.Get() != nullptr;

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

/** Proves a live spawn handle is pinned through world ownership, released on destruction, then staled by deterministic reuse. */
MW_TEST_CASE(EngineDeferredSpawnPinsThenInvalidatesReusedHandle)
{
	FDeferredSpawnHost Host{FGarbageCollectionBudget{8, 8, 8}};
	const auto World = Host.CreateWorld();
	FActorComponentRegistry<0> FirstComponents;
	FActorComponentRegistry<0> SecondComponents;
	FDeferredSpawnState State{};

	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "The composition root creates a configured world");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "The world begins before the deferred request");
	const auto FirstRequest = Host.GetWorld().SpawnActor<FDeferredActor>(FirstComponents.MakeReference(), &State);
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(10), "The first deferred actor publishes at the barrier");
	const auto FirstStatus = Host.GetWorld().GetSpawnStatus(FirstRequest.Handle);
	MW_EXPECT_EQ(Test, EActorSpawnState::Spawned, FirstStatus.State, "A live actor pins its completion handle");
	MW_EXPECT_EQ(
		Test, MicroWorld::EEngineResult::Success, Host.GetWorld().DestroyActor(FirstStatus.Actor), "The live deferred actor queues for destruction");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(20), "The destruction barrier releases the live handle");
	MW_EXPECT_EQ(
		Test,
		EActorSpawnState::Released,
		Host.GetWorld().GetSpawnStatus(FirstRequest.Handle).State,
		"Destroyed actor releases but does not immediately reuse its handle");

	const auto SecondRequest = Host.GetWorld().SpawnActor<FDeferredActor>(SecondComponents.MakeReference(), &State);
	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::Queued, SecondRequest.Result, "A released terminal slot accepts the next request");
	MW_EXPECT_EQ(
		Test,
		EActorSpawnState::Stale,
		Host.GetWorld().GetSpawnStatus(FirstRequest.Handle).State,
		"Reusing a terminal slot invalidates the prior generation");
}

} // namespace
