#include "EngineTestSupport.h"
#include "TestSupport.h"

#include <MicroWorld/Delegates/Delegate.h>
#include <MicroWorld/Engine/EngineClassIds.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/Timer.h>
#include <MicroWorld/Object/ClassDescriptor.h>
#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/Object.h>
#include <MicroWorld/Object/ObjectStore.h>
#include <MicroWorld/Time.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{
using MicroWorld::AActor;
using MicroWorld::AActorClassId;
using MicroWorld::EEngineResult;
using MicroWorld::EObjectResult;
using MicroWorld::ERuntimeResult;
using MicroWorld::ETimerMode;
using MicroWorld::ETimerResult;
using MicroWorld::FClassDescriptor;
using MicroWorld::FGarbageCollectionBudget;
using MicroWorld::FObjectStoreStats;
using MicroWorld::FTickConfiguration;
using MicroWorld::FTimerHandle;
using MicroWorld::FTypeId;
using MicroWorld::MakeClassDescriptor;
using MicroWorld::TDelegate;
using MicroWorld::TEngineHost;
using MicroWorld::TObjectPtr;
using MicroWorld::TObjectCreationResult;
using MicroWorld::TraceManagedObjectReferences;
using MicroWorld::UActorComponent;
using MicroWorld::UActorComponentClassId;
using MicroWorld::UWorld;

using MicroWorld::Tests::FActorEventState;
using MicroWorld::Tests::FComponentEventState;
using MicroWorld::Tests::FSequenceCounter;

/** Ticks every advance with a zero interval so the lifecycle test counts one tick per frame. */
constexpr FTickConfiguration HostTickConfiguration{true, true, MicroWorld::DurationMilliseconds{0}};

/** Stable type id for the recording actor managed through TEngineHost in this suite. */
constexpr FTypeId HostActorTypeId{0x00060001u};

/** Stable type id for the recording component managed through TEngineHost in this suite. */
constexpr FTypeId HostComponentTypeId{0x00060002u};

/** Stable type id for the plain unrooted component used as true garbage in the GC test. */
constexpr FTypeId HostPlainComponentTypeId{0x00060003u};

/** Inline storage reserved for one timer callback bound through the host's delegate type. */
constexpr std::size_t HostTimerCallbackBytes = 64;

/** Sizes a host large enough for the world, one actor, one component, and three garbage objects. */
using FHost = TEngineHost<6, 8, 256, 16, 1, 2, 4, HostTimerCallbackBytes>;

/** Matches the host's timer manager delegate type so Schedule accepts the bound callback. */
using FHostDelegate = TDelegate<void(), HostTimerCallbackBytes>;

/**
 * Records BeginPlay/TickComponent/EndPlay against a shared sequence so the host
 * lifecycle test proves component-before-actor begin and actor-before-component end.
 */
class FHostComponent final : public UActorComponent
{
public:
	/** Captures the shared sequence and per-component event sink the hooks will stamp. */
	FHostComponent(FSequenceCounter& InSequence, FComponentEventState& InEvents) noexcept
		: UActorComponent(HostTickConfiguration), Sequence(InSequence), Events(InEvents)
	{
	}

protected:
	/** Stamps the component's begin sequence before any sibling actor hook runs. */
	void BeginPlay() noexcept override
	{
		Events.BeginOrder = Sequence.Next();
		++Events.BeginCount;
	}

	/** Stamps the component's tick sequence after the timer slice in the same frame. */
	void TickComponent(const MicroWorld::FTickContext&) noexcept override
	{
		Events.TickOrder = Sequence.Next();
		++Events.TickCount;
	}

	/** Stamps the component's end sequence after the owning actor's end hook. */
	void EndPlay() noexcept override
	{
		Events.EndOrder = Sequence.Next();
		++Events.EndCount;
	}

private:
	/** Shares the monotonic sequence across every observed object in this test. */
	FSequenceCounter& Sequence;

	/** Holds the per-instance begin/tick/end counts and ordering stamps. */
	FComponentEventState& Events;
};

/**
 * Records BeginPlay/Tick/EndPlay against a shared sequence so the host lifecycle
 * test proves the actor's begin runs after its component and its end runs before.
 */
class FHostActor final : public AActor
{
public:
	/** Captures the component lease, shared sequence, and per-actor event sink. */
	FHostActor(MicroWorld::FActorComponentRegistryBase Components, FSequenceCounter& InSequence, FActorEventState& InEvents) noexcept
		: AActor(std::move(Components), HostTickConfiguration), Sequence(InSequence), Events(InEvents)
	{
	}

protected:
	/** Stamps the actor's begin sequence after every registered component has begun. */
	void BeginPlay() noexcept override
	{
		Events.BeginOrder = Sequence.Next();
		++Events.BeginCount;
	}

	/** Stamps the actor's tick sequence after the timer slice in the same frame. */
	void Tick(const MicroWorld::FTickContext&) noexcept override
	{
		Events.TickOrder = Sequence.Next();
		++Events.TickCount;
	}

	/** Stamps the actor's end sequence before any registered component ends. */
	void EndPlay() noexcept override
	{
		Events.EndOrder = Sequence.Next();
		++Events.EndCount;
	}

private:
	/** Shares the monotonic sequence across every observed object in this test. */
	FSequenceCounter& Sequence;

	/** Holds the per-instance begin/tick/end counts and ordering stamps. */
	FActorEventState& Events;
};

/** A component with no hooks used as unreferenced garbage for the bounded-GC test. */
class FHostPlainComponent final : public UActorComponent
{
public:
	/** Inherits the default tick-disabled configuration so the instance never dispatches. */
	FHostPlainComponent() noexcept = default;
};

/** Captures the order and firing state the host timer records against the shared sequence. */
struct FTimerFireRecord final
{
	/** Sequence value stamped when the timer callback fires in one frame. */
	std::uint32_t Order{0};

	/** Signals whether the timer callback has run at least once. */
	bool bFired{false};
};

/**
 * Registers the actor, recording component, and plain component descriptors on a
 * fresh host so each test builds its graph through the host's own descriptor copies.
 */
bool RegisterHostTypes(FHost& Host) noexcept
{
	const FClassDescriptor ActorDescriptor =
		MakeClassDescriptor<FHostActor>(HostActorTypeId, "HostActor", Host.FindClass(AActorClassId), &TraceManagedObjectReferences);
	const FClassDescriptor ComponentDescriptor = MakeClassDescriptor<FHostComponent>(
		HostComponentTypeId, "HostComponent", Host.FindClass(UActorComponentClassId), &TraceManagedObjectReferences);
	const FClassDescriptor PlainComponentDescriptor = MakeClassDescriptor<FHostPlainComponent>(
		HostPlainComponentTypeId, "HostPlainComponent", Host.FindClass(UActorComponentClassId), &TraceManagedObjectReferences);
	return Host.RegisterClass(ActorDescriptor) == EObjectResult::Success && Host.RegisterClass(ComponentDescriptor) == EObjectResult::Success
		&& Host.RegisterClass(PlainComponentDescriptor) == EObjectResult::Success;
}

/**
 * Owns the shared per-test state and builds one registered, world-attached actor
 * and component graph on a host so each case starts from the same baseline.
 *
 * The fixture owns the sequence, event sinks, component registry, and constructed
 * handles so they outlive the host whose store retains the actor; declare it
 * before the host in each test so destruction order drops the host first.
 */
struct FHostFixture final
{
	/** Shares the monotonic sequence across the actor and its component. */
	FSequenceCounter Sequence{};

	/** Records the actor's begin/tick/end counts and ordering stamps. */
	FActorEventState ActorEvents{};

	/** Records the component's begin/tick/end counts and ordering stamps. */
	FComponentEventState ComponentEvents{};

	/** Owns the component registry lease the actor holds a view into for its lifetime. */
	MicroWorld::FActorComponentRegistry<2> ActorComponents{};

	/** Holds the constructed actor handle so the test can drive and observe its lifecycle. */
	TObjectPtr<FHostActor> Actor{};

	/** Holds the constructed component handle so the test can read its event state. */
	TObjectPtr<FHostComponent> Component{};

	/**
	 * Registers the user types, creates the world, constructs the actor and component,
	 * and wires them onto the host. Returns false if any step fails so the caller can
	 * assert the common baseline without repeating the graph construction inline.
	 */
	bool Build(FHost& Host) noexcept
	{
		if (!RegisterHostTypes(Host))
		{
			return false;
		}
		const TObjectPtr<UWorld> World = Host.CreateWorld();
		if (World.Get() == nullptr)
		{
			return false;
		}
		Actor = Host.NewObject<FHostActor>(*Host.FindClass(HostActorTypeId), ActorComponents.MakeView(), Sequence, ActorEvents).Object;
		Component = Host.NewObject<FHostComponent>(*Host.FindClass(HostComponentTypeId), Sequence, ComponentEvents).Object;
		if (Actor.Get() == nullptr || Component.Get() == nullptr)
		{
			return false;
		}
		if (Actor.Get()->RegisterComponent(Component) != EEngineResult::Success)
		{
			return false;
		}
		return Host.GetWorld().RegisterActor(TObjectPtr<AActor>{Actor}) == EEngineResult::Success;
	}
};

} // namespace

/** Proves the host runs begin, tick, and end through TEngineHost in the engine's deterministic order. */
MW_TEST_CASE(EngineHostLifecycleRunsBeginTickEndInOrder)
{
	FHostFixture Fixture{};
	FHost Host{FGarbageCollectionBudget{1, 4, 8}};
	MW_EXPECT_TRUE(Test, Fixture.Build(Host), "The fixture builds the registered, world-attached actor and component");

	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "BeginPlay reports success at the canonical baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(10), "Tick at 10 ms reports success");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(20), "Tick at 20 ms reports success");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(30), "Tick at 30 ms reports success");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.EndPlay(), "EndPlay reports success after the frame schedule");

	MW_EXPECT_EQ(Test, std::uint32_t{1}, Fixture.ActorEvents.BeginCount, "The actor begin hook runs exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{3}, Fixture.ActorEvents.TickCount, "The actor tick hook runs once per frame");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, Fixture.ActorEvents.EndCount, "The actor end hook runs exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, Fixture.ComponentEvents.BeginCount, "The component begin hook runs exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{3}, Fixture.ComponentEvents.TickCount, "The component tick hook runs once per frame");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, Fixture.ComponentEvents.EndCount, "The component end hook runs exactly once");
	MW_EXPECT_TRUE(Test, Fixture.ComponentEvents.BeginOrder < Fixture.ActorEvents.BeginOrder, "The component begins before its actor");
	MW_EXPECT_TRUE(Test, Fixture.ActorEvents.EndOrder < Fixture.ComponentEvents.EndOrder, "The actor ends before its component");
}

/** Proves the host frame order fires due timers before actor/component ticks in the same frame. */
MW_TEST_CASE(EngineHostFrameOrderRunsTimerBeforeActorTick)
{
	FHostFixture Fixture{};
	FTimerFireRecord TimerRecord{};

	FHost Host{FGarbageCollectionBudget{1, 4, 8}};
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

	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(10), "The frame advancing to the timer deadline reports success");
	MW_EXPECT_TRUE(Test, TimerRecord.bFired, "The timer callback fires at its deadline");
	MW_EXPECT_TRUE(Test, TimerRecord.Order < Fixture.ComponentEvents.TickOrder, "The timer slice runs before the component tick in the same frame");
}

/**
 * Proves the host's idle-gated GC slice reclaims unreferenced garbage across multiple bounded ticks
 * rather than all at once, while never touching the live world/actor/component graph.
 */
MW_TEST_CASE(EngineHostGarbageCollectorReclaimsUnrootedObjectsInBoundedSlices)
{
	FHostFixture Fixture{};

	// MaxSweepOperations of 2 is deliberately smaller than the eight object slots, so one tick
	// cannot inspect every slot and the garbage must drain over successive bounded slices.
	FHost Host{FGarbageCollectionBudget{1, 1, 2}};
	MW_EXPECT_TRUE(Test, Fixture.Build(Host), "The fixture builds the registered, world-attached actor and component");

	// Three unreferenced plain components are true garbage: never registered, never rooted, never
	// reached through any traced edge, so only the GC sweep can reclaim them.
	const TObjectPtr<FHostPlainComponent> GarbageA = Host.NewObject<FHostPlainComponent>(*Host.FindClass(HostPlainComponentTypeId)).Object;
	const TObjectPtr<FHostPlainComponent> GarbageB = Host.NewObject<FHostPlainComponent>(*Host.FindClass(HostPlainComponentTypeId)).Object;
	const TObjectPtr<FHostPlainComponent> GarbageC = Host.NewObject<FHostPlainComponent>(*Host.FindClass(HostPlainComponentTypeId)).Object;
	MW_EXPECT_TRUE(
		Test,
		GarbageA.Get() != nullptr && GarbageB.Get() != nullptr && GarbageC.Get() != nullptr,
		"Garbage components construct through the host store");

	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "BeginPlay reports success at the canonical baseline");
	MW_EXPECT_EQ(
		Test, std::uint32_t{6}, Host.GetObjectStore().Stats().OccupiedSlots, "World, actor, component, and three garbage objects occupy six slots");

	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(10), "The first bounded GC slice reports success");
	MW_EXPECT_TRUE(Test, Host.GetObjectStore().Stats().OccupiedSlots > std::uint32_t{3}, "One bounded slice cannot reclaim all garbage at once");

	for (MicroWorld::TimePointMilliseconds Now = 20; Now <= 200; Now += 10)
	{
		(void)Host.Tick(Now);
	}
	MW_EXPECT_EQ(
		Test,
		std::uint32_t{3},
		Host.GetObjectStore().Stats().OccupiedSlots,
		"Bounded slices reclaim all garbage over successive frames while leaving the live graph intact");
}

/** Proves a rolled-back tick is rejected transactionally without advancing any observed state. */
MW_TEST_CASE(EngineHostRejectsNonMonotonicTickTransactionally)
{
	FHostFixture Fixture{};
	FHost Host{FGarbageCollectionBudget{1, 4, 8}};
	MW_EXPECT_TRUE(Test, Fixture.Build(Host), "The fixture builds the registered, world-attached actor and component");

	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(100), "BeginPlay reports success and records the tick baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(100), "A tick equal to the baseline is monotonic");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(150), "A later tick advances the frame");
	const std::uint32_t TickCountBeforeRollback = Fixture.ActorEvents.TickCount;

	MW_EXPECT_EQ(Test, ERuntimeResult::NonMonotonicTime, Host.Tick(149), "An earlier tick is rejected as non-monotonic");
	MW_EXPECT_EQ(Test, TickCountBeforeRollback, Fixture.ActorEvents.TickCount, "A rejected tick advances no actor state");
}

/** Proves EndPlay succeeds twice and runs the end hooks exactly once across both calls. */
MW_TEST_CASE(EngineHostEndPlayIsIdempotent)
{
	FHostFixture Fixture{};
	FHost Host{FGarbageCollectionBudget{1, 4, 8}};
	MW_EXPECT_TRUE(Test, Fixture.Build(Host), "The fixture builds the registered, world-attached actor and component");

	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "BeginPlay reports success at the canonical baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(10), "Tick at 10 ms reports success");

	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.EndPlay(), "The first EndPlay reports success");
	const std::uint32_t ActorEndCountAfterFirst = Fixture.ActorEvents.EndCount;
	const std::uint32_t ComponentEndCountAfterFirst = Fixture.ComponentEvents.EndCount;

	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.EndPlay(), "A second EndPlay still reports success without changing state");
	MW_EXPECT_EQ(Test, ActorEndCountAfterFirst, Fixture.ActorEvents.EndCount, "A repeated EndPlay does not re-run the actor end hook");
	MW_EXPECT_EQ(Test, ComponentEndCountAfterFirst, Fixture.ComponentEvents.EndCount, "A repeated EndPlay does not re-run the component end hook");
}

/** Proves BeginPlay, Tick, and EndPlay are rejected before CreateWorld constructs the world. */
MW_TEST_CASE(EngineHostRejectsLifecycleBeforeCreateWorld)
{
	FHost Host{FGarbageCollectionBudget{1, 4, 8}};
	MW_EXPECT_EQ(Test, ERuntimeResult::InvalidLifecycle, Host.BeginPlay(0), "BeginPlay before CreateWorld is rejected as an invalid lifecycle");
	MW_EXPECT_EQ(Test, ERuntimeResult::InvalidLifecycle, Host.Tick(0), "Tick before CreateWorld is rejected as an invalid lifecycle");
	MW_EXPECT_EQ(Test, ERuntimeResult::InvalidLifecycle, Host.EndPlay(), "EndPlay before CreateWorld is rejected as an invalid lifecycle");
}

/** Proves CreateWorld constructs the world exactly once and leaves GetWorld referring to that first world. */
MW_TEST_CASE(EngineHostCreateWorldIsSingleShot)
{
	FHost Host{FGarbageCollectionBudget{1, 4, 8}};

	const TObjectPtr<UWorld> FirstWorld = Host.CreateWorld();
	MW_EXPECT_TRUE(Test, FirstWorld.Get() != nullptr, "The first CreateWorld constructs and roots the world");

	const TObjectPtr<UWorld> SecondWorld = Host.CreateWorld();
	MW_EXPECT_TRUE(Test, SecondWorld.Get() == nullptr, "A second CreateWorld returns an empty reference without replacing the world");

	MW_EXPECT_TRUE(Test, &Host.GetWorld() == FirstWorld.Get(), "GetWorld still refers to the first world after the rejected second creation");
}

/**
 * Proves the RegisterClass<T>/CreateObject<T> ergonomics helpers register, construct,
 * and wire user types end to end so the descriptors they build participate in both
 * validation and reference tracing just like hand-built descriptors do.
 */
MW_TEST_CASE(EngineHostTemplateHelpersRegisterAndConstructUserTypes)
{
	FSequenceCounter Sequence{};
	FActorEventState ActorEvents{};
	FComponentEventState ComponentEvents{};
	MicroWorld::FActorComponentRegistry<2> ActorComponents{};
	FHost Host{FGarbageCollectionBudget{1, 4, 8}};

	MW_EXPECT_EQ(Test, EObjectResult::Success, Host.RegisterClass<FHostActor>(HostActorTypeId, "HostActor"), "RegisterClass<FHostActor> derives the actor parent and reports success");
	MW_EXPECT_EQ(Test, EObjectResult::Success, Host.RegisterClass<FHostComponent>(HostComponentTypeId, "HostComponent"), "RegisterClass<FHostComponent> derives the component parent and reports success");

	const TObjectPtr<UWorld> World = Host.CreateWorld();
	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "CreateWorld roots the world after the helpers register user types");

	const TObjectCreationResult<FHostActor> Actor =
		Host.CreateObject<FHostActor>(HostActorTypeId, ActorComponents.MakeView(), Sequence, ActorEvents);
	const TObjectCreationResult<FHostComponent> Component =
		Host.CreateObject<FHostComponent>(HostComponentTypeId, Sequence, ComponentEvents);
	MW_EXPECT_EQ(Test, EObjectResult::Success, Actor.Result, "CreateObject<FHostActor> constructs the actor through the helper-registered descriptor");
	MW_EXPECT_EQ(Test, EObjectResult::Success, Component.Result, "CreateObject<FHostComponent> constructs the component through the helper-registered descriptor");
	MW_EXPECT_TRUE(Test, Actor.Object.Get() != nullptr, "CreateObject<FHostActor> returns a non-null actor handle");
	MW_EXPECT_TRUE(Test, Component.Object.Get() != nullptr, "CreateObject<FHostComponent> returns a non-null component handle");

	MW_EXPECT_EQ(Test, EEngineResult::Success, Actor.Object.Get()->RegisterComponent(Component.Object), "The helper-constructed component attaches to the helper-constructed actor");
	MW_EXPECT_EQ(Test, EEngineResult::Success, Host.GetWorld().RegisterActor(TObjectPtr<AActor>{Actor.Object}), "The helper-constructed actor registers with the world");

	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "BeginPlay reports success at the helper-driven baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(10), "Tick at 10 ms reports success through the helper-registered actor");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(20), "Tick at 20 ms reports success through the helper-registered actor");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(30), "Tick at 30 ms reports success through the helper-registered actor");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.EndPlay(), "EndPlay reports success after the helper-driven frame schedule");

	MW_EXPECT_EQ(Test, std::uint32_t{1}, ActorEvents.BeginCount, "The helper-registered actor begins exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{3}, ActorEvents.TickCount, "The helper-registered actor ticks once per frame");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, ActorEvents.EndCount, "The helper-registered actor ends exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, ComponentEvents.BeginCount, "The helper-registered component begins exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{3}, ComponentEvents.TickCount, "The helper-registered component ticks once per frame");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, ComponentEvents.EndCount, "The helper-registered component ends exactly once");
	MW_EXPECT_TRUE(Test, ComponentEvents.BeginOrder < ActorEvents.BeginOrder, "The helper-registered component begins before its actor");
	MW_EXPECT_TRUE(Test, ActorEvents.EndOrder < ComponentEvents.EndOrder, "The helper-registered actor ends before its component");
}

/** Proves CreateObject<T> rejects an unregistered type id with UnknownClass and a null handle. */
MW_TEST_CASE(EngineHostCreateObjectRejectsUnregisteredType)
{
	FHost Host{FGarbageCollectionBudget{1, 4, 8}};

	const TObjectPtr<UWorld> World = Host.CreateWorld();
	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "CreateWorld roots the world before an unregistered construction is attempted");

	// FHostPlainComponent is the suite's default-constructible component; the lookup below fails
	// before any construction, so the type only needs to satisfy the store's noexcept constructible
	// contract, not match the recording component's two-argument constructor.
	constexpr FTypeId UnregisteredComponentTypeId{0x00069999u};
	const TObjectCreationResult<FHostPlainComponent> Component = Host.CreateObject<FHostPlainComponent>(UnregisteredComponentTypeId);
	MW_EXPECT_EQ(Test, EObjectResult::UnknownClass, Component.Result, "CreateObject reports UnknownClass for an id that was never registered");
	MW_EXPECT_TRUE(Test, Component.Object.Get() == nullptr, "CreateObject returns a null handle for an unregistered id");
}
