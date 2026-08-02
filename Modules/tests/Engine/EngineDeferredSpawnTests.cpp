#include "EngineTestSupport.h"
#include "TestSupport.h"

#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/DefaultEngineTraits.h>
#include <MicroWorld/Engine/ActorSpawnRequest.h>
#include <MicroWorld/Engine/ActorSpawnRequestResult.h>
#include <MicroWorld/Engine/ActorSpawnState.h>
#include <MicroWorld/Engine/ActorSpawnStatus.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/TDeferredActorSpawnStorage.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{

using MicroWorld::Core::ERuntimeResult;
using MicroWorld::Engine::AActor;
using MicroWorld::Engine::EActorSpawnRequestResult;
using MicroWorld::Engine::EActorSpawnState;
using MicroWorld::Engine::EObjectResult;
using MicroWorld::Engine::FActorSpawnRequest;
using MicroWorld::Engine::FActorSpawnStatus;
using MicroWorld::Engine::FDefaultEngineTraits;
using MicroWorld::Engine::FGarbageCollectionBudget;
using MicroWorld::Engine::FGarbageCollectionResult;
using MicroWorld::Engine::FGarbageCollector;
using MicroWorld::Engine::FGarbageCollectorStorage;
using MicroWorld::Engine::FObjectHandle;
using MicroWorld::Engine::FObjectStore;
using MicroWorld::Engine::FWorldActorRegistry;
using MicroWorld::Engine::TDeferredActorSpawnStorage;
using MicroWorld::Engine::TEngine;
using MicroWorld::Engine::TObjectCreationResult;
using MicroWorld::Engine::TObjectPtr;
using MicroWorld::Engine::TStrongObjectPtr;
using MicroWorld::Engine::UObject;
using MicroWorld::Engine::UWorld;
using MicroWorld::Tests::TEngineEnvironment;

/**
 * Motivation: Records public BeginPlay observation for one deferred actor instance.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FDeferredSpawnState final
{
	/** Motivation: Counts barrier-time BeginPlay calls without observing queue internals. */
	std::uint32_t BeginCount{0};
};

/**
 * Motivation: Records BeginPlay order so composition-time and registered actor order stays externally observable.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FDeferredSpawnOrderState final
{
	/** Motivation: Holds the actor labels in the order the World began them. */
	std::array<std::uint32_t, 2> BeginOrder{};

	/** Motivation: Identifies the next observation slot in the fixed test sequence. */
	std::size_t BeginCount{0};
};

/**
 * Motivation: Deliberately exceeds the configured inline factory budget without side effects.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FDeferredLargeCapture final
{
	/** Motivation: Holds enough bytes to force public factory-layout rejection before argument movement. */
	std::array<std::byte, 96> Bytes{};
};

/**
 * Motivation: Gives tests enough class and object capacity for a World plus bounded deferred actors.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FDeferredSpawnTraits : FDefaultEngineTraits
{
	static constexpr std::size_t MaxClasses = 6;
	static constexpr std::size_t MaxObjects = 6;
	static constexpr std::size_t MaxActors = 2;
	static constexpr std::size_t MaxRoots = 1;
	static constexpr std::size_t InlineDeferredSpawnFactoryBytes = 96;
};

using FDeferredSpawnHost = TEngine<FDeferredSpawnTraits>;

/**
 * Motivation: Reduces live actor capacity to one so a queued typed request exhausts it before publication.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FCombinedSpawnCapacityTraits : FDeferredSpawnTraits
{
	static constexpr std::size_t MaxActors = 1;
};

using FCombinedSpawnCapacityHost = TEngine<FCombinedSpawnCapacityTraits>;

/** Motivation: Keeps the manually constructed actor descriptor stable for the mixed-spawn capacity regression. */
constexpr MicroWorld::Engine::FTypeId DeferredActorTypeId{0x00070001u};

/** Motivation: Provides a public fixed-store fixture with enough room for collection and deferred construction tests. */
using FDeferredSpawnEnvironment = TEngineEnvironment<256, 16, 8, 1>;

/** Motivation: Restricts the store to a world and one blocking object for construction-failure coverage. */
using FDeferredSpawnCapacityEnvironment = TEngineEnvironment<256, 16, 2, 0>;

/**
 * Motivation: Owns the caller-provided collection worklist used by the direct deferred-spawn fixtures.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FDeferredSpawnCollectorFixture final
{
public:
	/**
	 * Motivation: Binds collection to the fixture's store without borrowing test-local temporary storage.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	explicit FDeferredSpawnCollectorFixture(FObjectStore& InStore) noexcept
		: Collector(InStore, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())})
	{
	}

	/**
	 * Motivation: Each test can drive its public collection lifecycle.
	 * Responsibilities: Exposes the collector.
	 */
	FGarbageCollector& GetCollector() noexcept { return Collector; }

private:
	/** Motivation: Backs bounded mark traversal for the entire fixture lifetime. */
	std::array<FObjectHandle, 8> Worklist{};

	/** Motivation: Owns the active-cycle capability for the fixture's object store. */
	FGarbageCollector Collector;
};

/**
 * Motivation: Detects any move performed while deferred request preflight is expected to reject.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FDeferredMoveProbe final
{
	/** Motivation: Keeps the caller-visible move state unchanged until factory capture actually begins. */
	bool bWasMovedFrom{false};

	FDeferredMoveProbe() noexcept = default;
	FDeferredMoveProbe(const FDeferredMoveProbe&) = delete;
	FDeferredMoveProbe& operator=(const FDeferredMoveProbe&) = delete;
	FDeferredMoveProbe(FDeferredMoveProbe&& InOther) noexcept { InOther.bWasMovedFrom = true; }
	FDeferredMoveProbe& operator=(FDeferredMoveProbe&&) = delete;
};

/**
 * Motivation: Begins only when World publishes it from the deferred barrier.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FDeferredActor final : public AActor
{
public:
	/**
	 * Motivation: Binds the per-test event sink through the deferred factory.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FDeferredActor(FDeferredSpawnState* const InState) noexcept : AActor(), State(InState) {}

protected:
	/**
	 * Motivation: Makes barrier publication directly observable through the actor lifecycle contract.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void BeginPlay() noexcept override { ++State->BeginCount; }

private:
	/** Motivation: Belongs to the test and proves the world did not call BeginPlay at queue time. */
	FDeferredSpawnState* State{nullptr};
};

/**
 * Motivation: Records a caller-selected label when World dispatches this actor's BeginPlay.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FOrderedDeferredActor final : public AActor
{
public:
	/**
	 * Motivation: Retains isolated component storage, the shared order sink, and this actor's expected label.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FOrderedDeferredActor(FDeferredSpawnOrderState* const InState, const std::uint32_t InLabel) noexcept : AActor(), State(InState), Label(InLabel) {}

protected:
	/**
	 * Motivation: Makes the public actor lifecycle order directly observable without reading World internals.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void BeginPlay() noexcept override
	{
		if (State->BeginCount < State->BeginOrder.size())
		{
			State->BeginOrder[State->BeginCount] = Label;
		}
		++State->BeginCount;
	}

private:
	/** Motivation: Belongs to the test and records dispatch order across registered and queued actors. */
	FDeferredSpawnOrderState* State{nullptr};

	/** Motivation: Distinguishes the actor in the compact fixed-size observation sequence. */
	std::uint32_t Label{0};
};

/**
 * Motivation: Accepts the large capture only so the request tests factory layout rather than constructor validity.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FLargeDeferredActor final : public AActor
{
public:
	/**
	 * Motivation: Retains the same observable state dependency while intentionally ignoring the oversized value.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FLargeDeferredActor(FDeferredSpawnState* const InState, FDeferredLargeCapture) noexcept : AActor(), State(InState) {}

protected:
	/**
	 * Motivation: Makes any accidental admission observable through the normal actor lifecycle.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void BeginPlay() noexcept override { ++State->BeginCount; }

private:
	/** Motivation: Belongs to the test and remains unchanged when layout preflight rejects. */
	FDeferredSpawnState* State{nullptr};
};

/**
 * Motivation: Forces an unsupported factory alignment while leaving actor construction otherwise ordinary.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct alignas(32) FOveralignedCapture final
{
	/** Motivation: Makes the capture itself require stricter alignment than caller-provided inline storage guarantees. */
	std::array<std::byte, 32> Bytes{};
};

/**
 * Motivation: Accepts the over-aligned capture only to exercise public factory-layout preflight.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FOveralignedDeferredActor final : public AActor
{
public:
	/**
	 * Motivation: Keeps the test actor constructible if a future storage policy supports this alignment.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FOveralignedDeferredActor(FDeferredSpawnState* const InState, FOveralignedCapture) noexcept : AActor(), State(InState) {}

protected:
	/**
	 * Motivation: Makes unexpected admission observable.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void BeginPlay() noexcept override { ++State->BeginCount; }

private:
	/** Motivation: Belongs to this isolated test. */
	FDeferredSpawnState* State{nullptr};
};

/**
 * Motivation: Retains a caller-provided world pointer so typed factory value capture is observable after
 *   publication.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FLvaluePointerDeferredActor final : public AActor
{
public:
	/**
	 * Motivation: Captures an lvalue managed pointer by value for later barrier-time construction.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FLvaluePointerDeferredActor(const MicroWorld::Engine::TObjectPtr<MicroWorld::Engine::UWorld> InCapturedWorld) noexcept
		: AActor(), CapturedWorld(InCapturedWorld)
	{
	}

	/**
	 * Motivation: Returns the managed pointer value received by the spawned actor constructor.
	 * Responsibilities: Return the stored value and touch nothing else.
	 */
	MicroWorld::Engine::TObjectPtr<MicroWorld::Engine::UWorld> GetCapturedWorld() const noexcept { return CapturedWorld; }

private:
	/** Motivation: Preserves the constructor input so the lvalue factory-capture contract remains observable. */
	MicroWorld::Engine::TObjectPtr<MicroWorld::Engine::UWorld> CapturedWorld{};
};

/**
 * Motivation: Accepts a move probe so active collection can prove queue preflight happens before argument capture.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FMoveProbeDeferredActor final : public AActor
{
public:
	/**
	 * Motivation: Takes ownership only if deferred request admission reaches factory construction.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FMoveProbeDeferredActor(FDeferredMoveProbe) noexcept : AActor() {}
};

/**
 * Motivation: Retains a captured managed object so queued-factory tracing is directly observable through
 *   collection.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FObjectCaptureDeferredActor final : public AActor
{
public:
	/**
	 * Motivation: Stores the direct managed capture until the World publishes this actor.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FObjectCaptureDeferredActor(const TObjectPtr<UObject> InCapturedObject) noexcept : AActor(), CapturedObject(InCapturedObject) {}

private:
	/** Motivation: Keeps the managed capture meaningful after barrier-time construction. */
	TObjectPtr<UObject> CapturedObject{};
};

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
	const TObjectCreationResult<FOrderedDeferredActor> RegisteredActor =
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
 * Motivation: Begin a world, queue a typed factory capturing an lvalue managed pointer by value, then run the
 *   barrier.
 * Responsibilities: A typed factory accepts an lvalue managed pointer and delivers its value to the spawned actor.
 */
MW_TEST_CASE(EngineDeferredSpawnDeliversLvalueObjectPointerToSpawnedActor)
{
	// Arrange
	FDeferredSpawnHost Host{FGarbageCollectionBudget{8, 8, 8}};
	const TObjectPtr<UWorld> World = Host.CreateWorld();
	const MicroWorld::Engine::TObjectPtr<MicroWorld::Engine::UWorld> WorldReference = World;
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
	const MicroWorld::Engine::TObjectPtr<MicroWorld::Engine::UWorld> CapturedWorld =
		SpawnedActor != nullptr ? SpawnedActor->GetCapturedWorld() : MicroWorld::Engine::TObjectPtr<MicroWorld::Engine::UWorld>{};
	const bool bCapturedWorldMatchesInput = CapturedWorld.Handle() == WorldReference.Handle();

	// Assert
	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::Queued, Request.Result, "The typed request accepts an lvalue managed pointer argument");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, TickResult, "The deferred lvalue factory constructs successfully at the barrier");
	MW_EXPECT_EQ(Test, EActorSpawnState::Spawned, SpawnedStatus.State, "The lvalue factory publishes a live world-owned actor");
	MW_EXPECT_TRUE(Test, SpawnedActor != nullptr, "The spawned lvalue-capturing actor resolves through the public handle");
	MW_EXPECT_TRUE(Test, bCapturedWorldMatchesInput, "The spawned actor receives the original lvalue managed pointer value");
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
	const TObjectCreationResult<AActor> CaptureCreation = Store.NewObject<AActor>(*Env.FindDescriptor(MicroWorld::Engine::AActorClassId));
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
	const TObjectCreationResult<AActor> BlockingCreation = Store.NewObject<AActor>(*Env.FindDescriptor(MicroWorld::Engine::AActorClassId));
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
