#include "EngineTestSupport.h"
#include "TestSupport.h"

#include <MicroWorld/Core/Delegates/Delegate.h>
#include <MicroWorld/Core/TickContext.h>
#include <MicroWorld/Engine/EngineClassIds.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/DefaultEngineTraits.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/GarbageCollectionBudget.h>
#include <MicroWorld/Engine/Object.h>
#include <MicroWorld/Engine/ObjectCreationResult.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/ObjectStoreStats.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Core/TimerHandle.h>
#include <MicroWorld/Core/TimerMode.h>
#include <MicroWorld/Core/TimerResult.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{
using MicroWorld::Core::ERuntimeResult;
using MicroWorld::Core::ETimerMode;
using MicroWorld::Core::ETimerResult;
using MicroWorld::Core::FTickConfiguration;
using MicroWorld::Core::FTimerHandle;
using MicroWorld::Core::TDelegate;
using MicroWorld::Engine::AActor;
using MicroWorld::Engine::AActorClassId;
using MicroWorld::Engine::EEngineResult;
using MicroWorld::Engine::EObjectResult;
using MicroWorld::Engine::FClassDescriptor;
using MicroWorld::Engine::FDefaultEngineTraits;
using MicroWorld::Engine::FGarbageCollectionBudget;
using MicroWorld::Engine::FObjectStoreStats;
using MicroWorld::Engine::FTypeId;
using MicroWorld::Engine::MakeClassDescriptor;
using MicroWorld::Engine::TEngine;
using MicroWorld::Engine::TObjectCreationResult;
using MicroWorld::Engine::TObjectPtr;
using MicroWorld::Engine::TraceManagedObjectReferences;
using MicroWorld::Engine::UActorComponent;
using MicroWorld::Engine::UActorComponentClassId;
using MicroWorld::Engine::UWorld;

using MicroWorld::Tests::FActorEventState;
using MicroWorld::Tests::FComponentEventState;
using MicroWorld::Tests::FSequenceCounter;

/** Motivation: Ticks every advance with a zero interval so the lifecycle test counts one tick per frame. */
constexpr FTickConfiguration HostTickConfiguration{true, true, MicroWorld::Core::DurationMilliseconds{0}};

/** Motivation: Stable type id for the recording actor managed through TEngine in this suite. */
constexpr FTypeId HostActorTypeId{0x00060001u};

/** Motivation: Stable type id for the recording component managed through TEngine in this suite. */
constexpr FTypeId HostComponentTypeId{0x00060002u};

/** Motivation: Stable type id for the plain unrooted component used as true garbage in the GC test. */
constexpr FTypeId HostPlainComponentTypeId{0x00060003u};

/** Motivation: Inline storage reserved for one timer callback bound through the host's delegate type. */
constexpr std::size_t HostTimerCallbackBytes = 64;

/**
 * Motivation: Carries the exact capacities FHost sized before the traits refactor, so the test store is unchanged.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FHostTraits : FDefaultEngineTraits
{
	static constexpr std::size_t MaxClasses = 6;
	static constexpr std::size_t MaxObjects = 8;
	static constexpr std::size_t SlotSizeBytes = 256;
	static constexpr std::size_t MaxRoots = 1;
	static constexpr std::size_t MaxActors = 2;
	static constexpr std::size_t MaxTimers = 4;
};

/** Motivation: Sizes a host large enough for the world, one actor, one component, and three garbage objects. */
using FHost = TEngine<FHostTraits>;

/** Motivation: Matches the host's timer manager delegate type so Schedule accepts the bound callback. */
using FHostDelegate = TDelegate<void(), HostTimerCallbackBytes>;

/**
 * Motivation: Records BeginPlay/TickComponent/EndPlay against a shared sequence so the host lifecycle test proves
 *   component-before-actor begin and actor-before-component end.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FHostComponent final : public UActorComponent
{
public:
	/**
	 * Motivation: Captures the shared sequence and per-component event sink the hooks will stamp.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FHostComponent(FSequenceCounter& InSequence, FComponentEventState& InEvents) noexcept
		: UActorComponent(HostTickConfiguration), Sequence(InSequence), Events(InEvents)
	{
	}

protected:
	/**
	 * Motivation: Stamps the component's begin sequence before any sibling actor hook runs.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void BeginPlay() noexcept override
	{
		Events.BeginOrder = Sequence.Next();
		++Events.BeginCount;
	}

	/**
	 * Motivation: Stamps the component's tick sequence after the timer slice in the same frame.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void TickComponent(const MicroWorld::Core::FTickContext&) noexcept override
	{
		Events.TickOrder = Sequence.Next();
		++Events.TickCount;
	}

	/**
	 * Motivation: Stamps the component's end sequence after the owning actor's end hook.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void EndPlay() noexcept override
	{
		Events.EndOrder = Sequence.Next();
		++Events.EndCount;
	}

private:
	/** Motivation: Shares the monotonic sequence across every observed object in this test. */
	FSequenceCounter& Sequence;

	/** Motivation: Holds the per-instance begin/tick/end counts and ordering stamps. */
	FComponentEventState& Events;
};

/**
 * Motivation: Records BeginPlay/Tick/EndPlay against a shared sequence so the host lifecycle test proves the
 *   actor's begin runs after its component and its end runs before.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FHostActor final : public AActor
{
public:
	/**
	 * Motivation: Captures the shared sequence and per-actor event sink.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FHostActor(FSequenceCounter& InSequence, FActorEventState& InEvents) noexcept
		: AActor(HostTickConfiguration), Sequence(InSequence), Events(InEvents)
	{
	}

protected:
	/**
	 * Motivation: Stamps the actor's begin sequence after every registered component has begun.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void BeginPlay() noexcept override
	{
		Events.BeginOrder = Sequence.Next();
		++Events.BeginCount;
	}

	/**
	 * Motivation: Stamps the actor's tick sequence after the timer slice in the same frame.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void Tick(const MicroWorld::Core::FTickContext&) noexcept override
	{
		Events.TickOrder = Sequence.Next();
		++Events.TickCount;
	}

	/**
	 * Motivation: Stamps the actor's end sequence before any registered component ends.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void EndPlay() noexcept override
	{
		Events.EndOrder = Sequence.Next();
		++Events.EndCount;
	}

private:
	/** Motivation: Shares the monotonic sequence across every observed object in this test. */
	FSequenceCounter& Sequence;

	/** Motivation: Holds the per-instance begin/tick/end counts and ordering stamps. */
	FActorEventState& Events;
};

/**
 * Motivation: A component with no hooks used as unreferenced garbage for the bounded-GC test.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FHostPlainComponent final : public UActorComponent
{
public:
	/**
	 * Motivation: The instance never dispatches.
	 * Responsibilities: Inherits the default tick-disabled configuration.
	 */
	FHostPlainComponent() noexcept = default;
};

/**
 * Motivation: Captures the order and firing state the host timer records against the shared sequence.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FTimerFireRecord final
{
	/** Motivation: Sequence value stamped when the timer callback fires in one frame. */
	std::uint32_t Order{0};

	/** Motivation: Signals whether the timer callback has run at least once. */
	bool bFired{false};
};

/**
 * Motivation: Registers the actor, recording component, and plain component descriptors on a fresh host so each
 *   test builds its graph through the host's own descriptor copies.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
bool RegisterHostTypes(FHost& InHost) noexcept
{
	const FClassDescriptor ActorDescriptor =
		MakeClassDescriptor<FHostActor>(HostActorTypeId, "HostActor", InHost.FindClass(AActorClassId), &TraceManagedObjectReferences);
	const FClassDescriptor ComponentDescriptor = MakeClassDescriptor<FHostComponent>(
		HostComponentTypeId, "HostComponent", InHost.FindClass(UActorComponentClassId), &TraceManagedObjectReferences);
	const FClassDescriptor PlainComponentDescriptor = MakeClassDescriptor<FHostPlainComponent>(
		HostPlainComponentTypeId, "HostPlainComponent", InHost.FindClass(UActorComponentClassId), &TraceManagedObjectReferences);
	return InHost.RegisterClass(ActorDescriptor) == EObjectResult::Success && InHost.RegisterClass(ComponentDescriptor) == EObjectResult::Success
		&& InHost.RegisterClass(PlainComponentDescriptor) == EObjectResult::Success;
}

/**
 * Motivation: Owns the shared per-test state and builds one registered, world-attached actor and component graph
 *   on a host so each case starts from the same baseline. The fixture owns the sequence, event sinks,
 *   and constructed handles so they outlive the host whose store retains the actor; declare it before
 *   the host in each test so destruction order drops the host first.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FHostFixture final
{
	/** Motivation: Shares the monotonic sequence across the actor and its component. */
	FSequenceCounter Sequence{};

	/** Motivation: Records the actor's begin/tick/end counts and ordering stamps. */
	FActorEventState ActorEvents{};

	/** Motivation: Records the component's begin/tick/end counts and ordering stamps. */
	FComponentEventState ComponentEvents{};

	/** Motivation: Holds the constructed actor handle so the test can drive and observe its lifecycle. */
	TObjectPtr<FHostActor> Actor{};

	/** Motivation: Holds the constructed component handle so the test can read its event state. */
	TObjectPtr<FHostComponent> Component{};

	/**
	 * Motivation: Registers the user types, creates the world, constructs the actor and component, and wires them onto
	 *   the host.
	 * Responsibilities: Returns false if any step fails so the caller can assert the common baseline without repeating the
	 *   graph construction inline.
	 */
	bool Build(FHost& InHost) noexcept
	{
		if (!RegisterHostTypes(InHost))
		{
			return false;
		}
		const TObjectPtr<UWorld> World = InHost.CreateWorld();
		if (World.Get() == nullptr)
		{
			return false;
		}
		Actor = InHost.NewObject<FHostActor>(*InHost.FindClass(HostActorTypeId), Sequence, ActorEvents).Object;
		Component = InHost.NewObject<FHostComponent>(*InHost.FindClass(HostComponentTypeId), Sequence, ComponentEvents).Object;
		const bool bBothObjectsCreated = Actor.Get() != nullptr && Component.Get() != nullptr;
		if (!bBothObjectsCreated)
		{
			return false;
		}
		if (Actor.Get()->RegisterComponent(Component) != EEngineResult::Success)
		{
			return false;
		}
		const EEngineResult RegisterResult = InHost.GetWorld().RegisterActor(TObjectPtr<AActor>{Actor});
		return RegisterResult == EEngineResult::Success;
	}
};

} // namespace

/**
 * Motivation: Build a registered actor-and-component graph on the host and drive one begin, three ticks, and an
 *   end through TEngine.
 * Responsibilities: The host runs begin, tick, and end through TEngine in the engine's deterministic order.
 */
MW_TEST_CASE(EngineHostLifecycleRunsBeginTickEndInOrder)
{
	// Arrange
	FHostFixture Fixture{};
	FHost Host{FGarbageCollectionBudget{1, 4, 8}};

	// Assert - the fixture builds the registered, world-attached actor and component
	MW_EXPECT_TRUE(Test, Fixture.Build(Host), "The fixture builds the registered, world-attached actor and component");

	// Act - drive one begin, three ticks, and an end through the host
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "BeginPlay reports success at the canonical baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(10), "Tick at 10 ms reports success");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(20), "Tick at 20 ms reports success");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(30), "Tick at 30 ms reports success");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.EndPlay(), "EndPlay reports success after the frame schedule");

	// Assert - the lifecycle hooks ran in the engine's deterministic order
	MW_EXPECT_EQ(Test, std::uint32_t{1}, Fixture.ActorEvents.BeginCount, "The actor begin hook runs exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{3}, Fixture.ActorEvents.TickCount, "The actor tick hook runs once per frame");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, Fixture.ActorEvents.EndCount, "The actor end hook runs exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, Fixture.ComponentEvents.BeginCount, "The component begin hook runs exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{3}, Fixture.ComponentEvents.TickCount, "The component tick hook runs once per frame");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, Fixture.ComponentEvents.EndCount, "The component end hook runs exactly once");
	MW_EXPECT_TRUE(Test, Fixture.ComponentEvents.BeginOrder < Fixture.ActorEvents.BeginOrder, "The component begins before its actor");
	MW_EXPECT_TRUE(Test, Fixture.ActorEvents.EndOrder < Fixture.ComponentEvents.EndOrder, "The actor ends before its component");
}

/**
 * Motivation: Build a graph, begin play, schedule a one-shot timer, then advance the frame to the timer deadline.
 * Responsibilities: The host frame order fires due timers before actor/component ticks in the same frame.
 */
MW_TEST_CASE(EngineHostFrameOrderRunsTimerBeforeActorTick)
{
	// Arrange
	FHostFixture Fixture{};
	FTimerFireRecord TimerRecord{};

	FHost Host{FGarbageCollectionBudget{1, 4, 8}};

	// Arrange - build the fixture and begin play, then schedule a one-shot timer
	MW_EXPECT_TRUE(Test, Fixture.Build(Host), "The fixture builds the registered, world-attached actor and component");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "BeginPlay reports success at the canonical baseline");

	FHostDelegate TimerCallback;
	(void)TimerCallback.Bind(
		[&Fixture, &TimerRecord]() noexcept
		{
			TimerRecord.Order = Fixture.Sequence.Next();
			TimerRecord.bFired = true;
		});
	FTimerHandle Handle{};
	MW_EXPECT_EQ(
		Test,
		ETimerResult::Success,
		Host.GetTimerManager().Schedule(std::move(TimerCallback), 10, ETimerMode::OneShot, Handle),
		"A one-shot timer schedules for the next frame deadline");
	MW_EXPECT_TRUE(Test, Handle.IsValid(), "A successful schedule publishes a valid handle");

	// Act - the frame advances to the timer deadline
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(10), "The frame advancing to the timer deadline reports success");

	// Assert
	MW_EXPECT_TRUE(Test, TimerRecord.bFired, "The timer callback fires at its deadline");
	MW_EXPECT_TRUE(Test, TimerRecord.Order < Fixture.ComponentEvents.TickOrder, "The timer slice runs before the component tick in the same frame");
}

/**
 * Motivation: Build the live graph, create three unreferenced garbage objects, begin play, then drive successive
 *   bounded GC slices.
 * Responsibilities: The host's idle-gated GC slice reclaims unreferenced garbage across multiple bounded ticks rather
 *   than all at once, while never touching.
 */
MW_TEST_CASE(EngineHostGarbageCollectorReclaimsUnrootedObjectsInBoundedSlices)
{
	// Arrange
	FHostFixture Fixture{};

	// MaxSweepOperations of 2 is deliberately smaller than the eight object slots, so one tick
	// cannot inspect every slot and the garbage must drain over successive bounded slices.
	FHost Host{FGarbageCollectionBudget{1, 1, 2}};
	MW_EXPECT_TRUE(Test, Fixture.Build(Host), "The fixture builds the registered, world-attached actor and component");

	// Arrange - three unreferenced plain components are true garbage: never registered, never rooted,
	// never reached through any traced edge, so only the GC sweep can reclaim them.
	const TObjectPtr<FHostPlainComponent> GarbageA = Host.NewObject<FHostPlainComponent>(*Host.FindClass(HostPlainComponentTypeId)).Object;
	const TObjectPtr<FHostPlainComponent> GarbageB = Host.NewObject<FHostPlainComponent>(*Host.FindClass(HostPlainComponentTypeId)).Object;
	const TObjectPtr<FHostPlainComponent> GarbageC = Host.NewObject<FHostPlainComponent>(*Host.FindClass(HostPlainComponentTypeId)).Object;
	MW_EXPECT_TRUE(
		Test,
		GarbageA.Get() != nullptr && GarbageB.Get() != nullptr && GarbageC.Get() != nullptr,
		"Garbage components construct through the host store");

	// Arrange - begin play and confirm the live graph plus garbage occupy six slots
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "BeginPlay reports success at the canonical baseline");
	MW_EXPECT_EQ(
		Test, std::uint32_t{6}, Host.GetObjectStore().Stats().OccupiedSlots, "World, actor, component, and three garbage objects occupy six slots");

	// Act - the first bounded GC slice cannot complete the mark phase (the {1,1,2} budget traces the
	// world root then one reachable object before exhausting its mark budget), so no slot is reclaimed yet
	// and all six occupied slots survive the first tick.
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(10), "The first bounded GC slice reports success");
	MW_EXPECT_EQ(
		Test,
		std::uint32_t{6},
		Host.GetObjectStore().Stats().OccupiedSlots,
		"One bounded slice cannot complete the mark phase, so no garbage is reclaimed on the first tick");

	// Act - successive bounded slices reclaim the remaining garbage
	for (MicroWorld::Core::TimePointMilliseconds Now = 20; Now <= 200; Now += 10)
	{
		(void)Host.Tick(Now);
	}

	// Assert
	MW_EXPECT_EQ(
		Test,
		std::uint32_t{3},
		Host.GetObjectStore().Stats().OccupiedSlots,
		"Bounded slices reclaim all garbage over successive frames while leaving the live graph intact");
}

/**
 * Motivation: Build a graph, advance the baseline to 150 ms, then issue an earlier tick at 149 ms.
 * Responsibilities: A rolled-back tick is rejected transactionally without advancing any observed state.
 */
MW_TEST_CASE(EngineHostRejectsNonMonotonicTickTransactionally)
{
	// Arrange
	FHostFixture Fixture{};
	FHost Host{FGarbageCollectionBudget{1, 4, 8}};

	// Arrange - build the fixture, then advance the baseline forward to 150 ms
	MW_EXPECT_TRUE(Test, Fixture.Build(Host), "The fixture builds the registered, world-attached actor and component");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(100), "BeginPlay reports success and records the tick baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(100), "A tick equal to the baseline is monotonic");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(150), "A later tick advances the frame");
	const std::uint32_t TickCountBeforeRollback = Fixture.ActorEvents.TickCount;

	// Act - an earlier tick is rejected as non-monotonic
	MW_EXPECT_EQ(Test, ERuntimeResult::NonMonotonicTime, Host.Tick(149), "An earlier tick is rejected as non-monotonic");

	// Assert
	MW_EXPECT_EQ(Test, TickCountBeforeRollback, Fixture.ActorEvents.TickCount, "A rejected tick advances no actor state");
}

/**
 * Motivation: Build a graph, begin play, tick once, then call EndPlay twice.
 * Responsibilities: EndPlay succeeds twice and runs the end hooks exactly once across both calls.
 */
MW_TEST_CASE(EngineHostEndPlayIsIdempotent)
{
	// Arrange
	FHostFixture Fixture{};
	FHost Host{FGarbageCollectionBudget{1, 4, 8}};

	// Arrange - build the fixture, begin play, and tick once
	MW_EXPECT_TRUE(Test, Fixture.Build(Host), "The fixture builds the registered, world-attached actor and component");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "BeginPlay reports success at the canonical baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(10), "Tick at 10 ms reports success");

	// Act - the first EndPlay reports success and captures the post-end counts
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.EndPlay(), "The first EndPlay reports success");
	const std::uint32_t ActorEndCountAfterFirst = Fixture.ActorEvents.EndCount;
	const std::uint32_t ComponentEndCountAfterFirst = Fixture.ComponentEvents.EndCount;

	// Act - a second EndPlay still reports success
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.EndPlay(), "A second EndPlay still reports success without changing state");

	// Assert - a repeated EndPlay does not re-run either end hook
	MW_EXPECT_EQ(Test, ActorEndCountAfterFirst, Fixture.ActorEvents.EndCount, "A repeated EndPlay does not re-run the actor end hook");
	MW_EXPECT_EQ(Test, ComponentEndCountAfterFirst, Fixture.ComponentEvents.EndCount, "A repeated EndPlay does not re-run the component end hook");
}

/**
 * Motivation: Construct a host without a world and call BeginPlay, Tick, and EndPlay before CreateWorld.
 * Responsibilities: BeginPlay, Tick, and EndPlay are rejected before CreateWorld constructs the world.
 */
MW_TEST_CASE(EngineHostRejectsLifecycleBeforeCreateWorld)
{
	// Arrange
	FHost Host{FGarbageCollectionBudget{1, 4, 8}};

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::InvalidLifecycle, Host.BeginPlay(0), "BeginPlay before CreateWorld is rejected as an invalid lifecycle");
	MW_EXPECT_EQ(Test, ERuntimeResult::InvalidLifecycle, Host.Tick(0), "Tick before CreateWorld is rejected as an invalid lifecycle");
	MW_EXPECT_EQ(Test, ERuntimeResult::InvalidLifecycle, Host.EndPlay(), "EndPlay before CreateWorld is rejected as an invalid lifecycle");
}

/**
 * Motivation: Call CreateWorld twice on the same host and read GetWorld after the second call.
 * Responsibilities: CreateWorld constructs the world exactly once and leaves GetWorld referring to that first world.
 */
MW_TEST_CASE(EngineHostCreateWorldIsSingleShot)
{
	// Arrange
	FHost Host{FGarbageCollectionBudget{1, 4, 8}};

	// Act
	const TObjectPtr<UWorld> FirstWorld = Host.CreateWorld();
	MW_EXPECT_TRUE(Test, FirstWorld.Get() != nullptr, "The first CreateWorld constructs and roots the world");

	// Act - a second CreateWorld returns an empty reference
	const TObjectPtr<UWorld> SecondWorld = Host.CreateWorld();

	// Assert
	MW_EXPECT_TRUE(Test, SecondWorld.Get() == nullptr, "A second CreateWorld returns an empty reference without replacing the world");
	MW_EXPECT_TRUE(Test, &Host.GetWorld() == FirstWorld.Get(), "GetWorld still refers to the first world after the rejected second creation");
}

/**
 * Motivation: Register user types through the helpers, construct the world and user objects, wire them, and drive
 *   a begin/three-tick/end schedule.
 * Responsibilities: The RegisterClass<T>/CreateObject<T> ergonomics helpers register, construct, and wire user types end
 *   to end so the descriptors they build.
 */
MW_TEST_CASE(EngineHostTemplateHelpersRegisterAndConstructUserTypes)
{
	// Arrange
	FSequenceCounter Sequence{};
	FActorEventState ActorEvents{};
	FComponentEventState ComponentEvents{};
	FHost Host{FGarbageCollectionBudget{1, 4, 8}};

	// Act - register the user actor and component classes
	MW_EXPECT_EQ(
		Test,
		EObjectResult::Success,
		Host.RegisterClass<FHostActor>(HostActorTypeId, "HostActor"),
		"RegisterClass<FHostActor> derives the actor parent and reports success");
	MW_EXPECT_EQ(
		Test,
		EObjectResult::Success,
		Host.RegisterClass<FHostComponent>(HostComponentTypeId, "HostComponent"),
		"RegisterClass<FHostComponent> derives the component parent and reports success");

	// Act - create the world, then construct the actor and component
	const TObjectPtr<UWorld> World = Host.CreateWorld();
	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "CreateWorld roots the world after the helpers register user types");

	const TObjectCreationResult<FHostActor> Actor = Host.CreateObject<FHostActor>(HostActorTypeId, Sequence, ActorEvents);
	const TObjectCreationResult<FHostComponent> Component = Host.CreateObject<FHostComponent>(HostComponentTypeId, Sequence, ComponentEvents);

	// Assert - both helper-constructed objects succeed and return non-null handles
	MW_EXPECT_EQ(
		Test, EObjectResult::Success, Actor.Result, "CreateObject<FHostActor> constructs the actor through the helper-registered descriptor");
	MW_EXPECT_EQ(
		Test,
		EObjectResult::Success,
		Component.Result,
		"CreateObject<FHostComponent> constructs the component through the helper-registered descriptor");
	MW_EXPECT_TRUE(Test, Actor.Object.Get() != nullptr, "CreateObject<FHostActor> returns a non-null actor handle");
	MW_EXPECT_TRUE(Test, Component.Object.Get() != nullptr, "CreateObject<FHostComponent> returns a non-null component handle");

	// Act - wire the component onto the actor and the actor onto the world
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		Actor.Object.Get()->RegisterComponent(Component.Object),
		"The helper-constructed component attaches to the helper-constructed actor");
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		Host.GetWorld().RegisterActor(TObjectPtr<AActor>{Actor.Object}),
		"The helper-constructed actor registers with the world");

	// Act - drive one begin, three ticks, and an end through the helper-built graph
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "BeginPlay reports success at the helper-driven baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(10), "Tick at 10 ms reports success through the helper-registered actor");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(20), "Tick at 20 ms reports success through the helper-registered actor");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(30), "Tick at 30 ms reports success through the helper-registered actor");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.EndPlay(), "EndPlay reports success after the helper-driven frame schedule");

	// Assert - the helper-built graph dispatched its lifecycle in the engine's deterministic order
	MW_EXPECT_EQ(Test, std::uint32_t{1}, ActorEvents.BeginCount, "The helper-registered actor begins exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{3}, ActorEvents.TickCount, "The helper-registered actor ticks once per frame");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, ActorEvents.EndCount, "The helper-registered actor ends exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, ComponentEvents.BeginCount, "The helper-registered component begins exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{3}, ComponentEvents.TickCount, "The helper-registered component ticks once per frame");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, ComponentEvents.EndCount, "The helper-registered component ends exactly once");
	MW_EXPECT_TRUE(Test, ComponentEvents.BeginOrder < ActorEvents.BeginOrder, "The helper-registered component begins before its actor");
	MW_EXPECT_TRUE(Test, ActorEvents.EndOrder < ComponentEvents.EndOrder, "The helper-registered actor ends before its component");
}

/**
 * Motivation: Construct a world and attempt CreateObject<T> with an id that was never registered.
 * Responsibilities: CreateObject<T> rejects an unregistered type id with UnknownClass and a null handle.
 */
MW_TEST_CASE(EngineHostCreateObjectRejectsUnregisteredType)
{
	// Arrange
	FHost Host{FGarbageCollectionBudget{1, 4, 8}};

	const TObjectPtr<UWorld> World = Host.CreateWorld();
	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "CreateWorld roots the world before an unregistered construction is attempted");

	// FHostPlainComponent is the suite's default-constructible component; the lookup below fails
	// before any construction, so the type only needs to satisfy the store's noexcept constructible
	// contract, not match the recording component's two-argument constructor.
	constexpr FTypeId UnregisteredComponentTypeId{0x00069999u};

	// Act
	const TObjectCreationResult<FHostPlainComponent> Component = Host.CreateObject<FHostPlainComponent>(UnregisteredComponentTypeId);

	// Assert
	MW_EXPECT_EQ(Test, EObjectResult::UnknownClass, Component.Result, "CreateObject reports UnknownClass for an id that was never registered");
	MW_EXPECT_TRUE(Test, Component.Object.Get() == nullptr, "CreateObject returns a null handle for an unregistered id");
}
