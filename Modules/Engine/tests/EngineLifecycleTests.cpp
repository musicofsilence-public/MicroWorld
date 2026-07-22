#include "EngineTestSupport.h"
#include "TestSupport.h"

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/World.h>

#include <cstdint>

namespace
{

using MicroWorld::AActor;
using MicroWorld::DurationMilliseconds;
using MicroWorld::EEngineResult;
using MicroWorld::EObjectResult;
using MicroWorld::ERuntimeResult;
using MicroWorld::FActorComponentRegistry;
using MicroWorld::FActorComponentRegistryBase;
using MicroWorld::FGarbageCollectionBudget;
using MicroWorld::FGarbageCollector;
using MicroWorld::FObjectStore;
using MicroWorld::FTickConfiguration;
using MicroWorld::FTickContext;
using MicroWorld::FWorldActorRegistry;
using MicroWorld::FWorldActorRegistryBase;
using MicroWorld::TObjectPtr;
using MicroWorld::UActorComponent;
using MicroWorld::UWorld;
using MicroWorld::Tests::FActorEventState;
using MicroWorld::Tests::FComponentEventState;
using MicroWorld::Tests::FSequenceCounter;
using MicroWorld::Tests::TEngineEnvironment;

/** Tick configuration that lets ordering types tick every advance by default. */
constexpr FTickConfiguration OrderingTickConfiguration{true, true, DurationMilliseconds{0}};

/** A component that records begin/tick/end ordering into per-instance state. */
class FOrderingComponent final : public UActorComponent
{
public:
	FOrderingComponent(FSequenceCounter& InSequence, FComponentEventState& InEvents) noexcept
		: UActorComponent(OrderingTickConfiguration), Sequence(InSequence), Events(InEvents)
	{
	}

protected:
	void BeginPlay() noexcept override
	{
		Events.BeginOrder = Sequence.Next();
		++Events.BeginCount;
	}
	void TickComponent(const FTickContext&) noexcept override
	{
		Events.TickOrder = Sequence.Next();
		++Events.TickCount;
	}
	void EndPlay() noexcept override
	{
		Events.EndOrder = Sequence.Next();
		++Events.EndCount;
	}

private:
	FSequenceCounter& Sequence;
	FComponentEventState& Events;
};

/** An actor that records begin/tick/end ordering into per-instance state. */
class FOrderingActor final : public AActor
{
public:
	FOrderingActor(
		FTickConfiguration TickConfiguration,
		FActorComponentRegistryBase Components,
		FSequenceCounter& InSequence,
		FActorEventState& InEvents) noexcept
		: AActor(std::move(Components), TickConfiguration), Sequence(InSequence), Events(InEvents)
	{
	}

protected:
	void BeginPlay() noexcept override
	{
		Events.BeginOrder = Sequence.Next();
		++Events.BeginCount;
	}
	void Tick(const FTickContext&) noexcept override
	{
		Events.TickOrder = Sequence.Next();
		++Events.TickCount;
	}
	void EndPlay() noexcept override
	{
		Events.EndOrder = Sequence.Next();
		++Events.EndCount;
	}

private:
	FSequenceCounter& Sequence;
	FActorEventState& Events;
};

/** Test-local type ids for the ordering actor and component descriptors. */
constexpr MicroWorld::FTypeId OrderingActorTypeId{0x00010001u};
constexpr MicroWorld::FTypeId OrderingComponentTypeId{0x00010002u};
constexpr MicroWorld::FTypeId MutationAttemptActorTypeId{0x00010003u};

/** Records every structural operation attempted from each lifecycle hook. */
struct FLifecycleMutationState final
{
	std::array<EObjectResult, 3> Construction{};
	std::array<EObjectResult, 3> MarkPending{};
	std::array<EObjectResult, 3> ApplyPending{};
	std::array<EObjectResult, 3> AddRoot{};
	std::array<ERuntimeResult, 3> RequestCollection{};
	std::array<ERuntimeResult, 3> AdvanceCollection{};
};

/** Attempts forbidden object-graph mutation from BeginPlay, Tick, and EndPlay. */
class FMutationAttemptActor final : public AActor
{
public:
	FMutationAttemptActor(
		FObjectStore& InStore,
		FActorComponentRegistryBase Components,
		FGarbageCollector& InCollector,
		const MicroWorld::FClassDescriptor& InComponentDescriptor,
		FLifecycleMutationState& InState) noexcept
		: AActor(std::move(Components), OrderingTickConfiguration)
		, Store(InStore)
		, Collector(InCollector)
		, ComponentDescriptor(InComponentDescriptor)
		, State(InState)
	{
	}

protected:
	void BeginPlay() noexcept override { AttemptMutations(0); }
	void Tick(const FTickContext&) noexcept override { AttemptMutations(1); }
	void EndPlay() noexcept override { AttemptMutations(2); }

private:
	void AttemptMutations(const std::size_t HookIndex) noexcept
	{
		State.Construction[HookIndex] = Store.NewObject<UActorComponent>(ComponentDescriptor).Result;
		State.MarkPending[HookIndex] = Store.MarkPendingDestroy(GetObjectHandle());
		State.ApplyPending[HookIndex] = Store.ApplyPendingDestroy(1).Result;
		State.AddRoot[HookIndex] = Store.AddRoot(GetObjectHandle());
		State.RequestCollection[HookIndex] = Collector.RequestCollection();
		State.AdvanceCollection[HookIndex] = Collector.Advance(FGarbageCollectionBudget{1, 1, 1}).Result;
	}

	FObjectStore& Store;
	FGarbageCollector& Collector;
	const MicroWorld::FClassDescriptor& ComponentDescriptor;
	FLifecycleMutationState& State;
};

/** Builds one ordering actor through its derived descriptor in the environment. */
template<std::size_t SlotSizeBytes, std::size_t SlotAlignmentBytes, std::uint32_t SlotCount, std::uint32_t RootCapacity>
TObjectPtr<FOrderingActor> MakeOrderingActor(
	TEngineEnvironment<SlotSizeBytes, SlotAlignmentBytes, SlotCount, RootCapacity>& Env,
	FActorComponentRegistryBase Components,
	FSequenceCounter& Sequence,
	FActorEventState& Events) noexcept
{
	return Env.template CreateDerivedObject<FOrderingActor>(
		OrderingActorTypeId, "OrderingActor", OrderingTickConfiguration, std::move(Components), Sequence, Events);
}

/** Builds one ordering component through its derived descriptor in the environment. */
template<std::size_t SlotSizeBytes, std::size_t SlotAlignmentBytes, std::uint32_t SlotCount, std::uint32_t RootCapacity>
TObjectPtr<FOrderingComponent> MakeOrderingComponent(
	TEngineEnvironment<SlotSizeBytes, SlotAlignmentBytes, SlotCount, RootCapacity>& Env,
	FSequenceCounter& Sequence,
	FComponentEventState& Events) noexcept
{
	return Env.template CreateDerivedObject<FOrderingComponent>(OrderingComponentTypeId, "OrderingComponent", Sequence, Events);
}

/** Convenience environment sized for the lifecycle ordering tests. */
using FLifecycleEnvironment = TEngineEnvironment<256, 16, 8, 4>;

/**
 * Proves BeginPlay visits actors in registration order and, for each actor,
 * begins its components in registration order before the actor's own hook.
 */
MW_TEST_CASE(EngineBeginPlayOrderIsActorsThenComponentsPerActor)
{
	FSequenceCounter Sequence{};
	FActorEventState ActorAEvents{};
	FActorEventState ActorBEvents{};
	FComponentEventState CompA1Events{};
	FComponentEventState CompA2Events{};
	FComponentEventState CompB1Events{};

	FLifecycleEnvironment Env{};

	FActorComponentRegistry<2> ActorAComponents;
	FActorComponentRegistry<2> ActorBComponents;
	FWorldActorRegistry<2> WorldActors;

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, WorldActors.MakeView());
	const TObjectPtr<FOrderingActor> ActorA = MakeOrderingActor(Env, ActorAComponents.MakeView(), Sequence, ActorAEvents);
	const TObjectPtr<FOrderingActor> ActorB = MakeOrderingActor(Env, ActorBComponents.MakeView(), Sequence, ActorBEvents);
	const TObjectPtr<FOrderingComponent> CompA1 = MakeOrderingComponent(Env, Sequence, CompA1Events);
	const TObjectPtr<FOrderingComponent> CompA2 = MakeOrderingComponent(Env, Sequence, CompA2Events);
	const TObjectPtr<FOrderingComponent> CompB1 = MakeOrderingComponent(Env, Sequence, CompB1Events);

	const EEngineResult ActorAComp1 = ActorA.Get()->RegisterComponent(CompA1);
	const EEngineResult ActorAComp2 = ActorA.Get()->RegisterComponent(CompA2);
	const EEngineResult ActorBComp1 = ActorB.Get()->RegisterComponent(CompB1);
	const EEngineResult WorldActorA = World.Get()->RegisterActor(TObjectPtr<AActor>{ActorA});
	const EEngineResult WorldActorB = World.Get()->RegisterActor(TObjectPtr<AActor>{ActorB});

	const ERuntimeResult BeginResult = World.Get()->BeginPlay(0);

	MW_EXPECT_EQ(Test, EEngineResult::Success, ActorAComp1, "ActorA should accept its first component");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ActorAComp2, "ActorA should accept its second component");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ActorBComp1, "ActorB should accept its component");
	MW_EXPECT_EQ(Test, EEngineResult::Success, WorldActorA, "The world should accept ActorA");
	MW_EXPECT_EQ(Test, EEngineResult::Success, WorldActorB, "The world should accept ActorB");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "BeginPlay should succeed");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, CompA1Events.BeginOrder, "ActorA's first component begins first");
	MW_EXPECT_EQ(Test, std::uint32_t{2}, CompA2Events.BeginOrder, "ActorA's second component begins second");
	MW_EXPECT_EQ(Test, std::uint32_t{3}, ActorAEvents.BeginOrder, "ActorA begins after both components");
	MW_EXPECT_EQ(Test, std::uint32_t{4}, CompB1Events.BeginOrder, "ActorB's component begins after ActorA");
	MW_EXPECT_EQ(Test, std::uint32_t{5}, ActorBEvents.BeginOrder, "ActorB begins last");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, ActorAEvents.BeginCount, "ActorA BeginPlay runs exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, ActorBEvents.BeginCount, "ActorB BeginPlay runs exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, CompA1Events.BeginCount, "Each registered component begins exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, CompA2Events.BeginCount, "Each registered component begins exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, CompB1Events.BeginCount, "Each registered component begins exactly once");
}

/**
 * Proves Advance ticks actors in registration order and, for each actor, ticks
 * its components before the actor's own Tick hook.
 */
MW_TEST_CASE(EngineTickOrderIsActorsThenComponentsPerActor)
{
	FSequenceCounter Sequence{};
	FActorEventState ActorAEvents{};
	FActorEventState ActorBEvents{};
	FComponentEventState CompAEvents{};
	FComponentEventState CompBEvents{};

	FLifecycleEnvironment Env{};

	FActorComponentRegistry<1> ActorAComponents;
	FActorComponentRegistry<1> ActorBComponents;
	FWorldActorRegistry<2> WorldActors;

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, WorldActors.MakeView());
	const TObjectPtr<FOrderingActor> ActorA = MakeOrderingActor(Env, ActorAComponents.MakeView(), Sequence, ActorAEvents);
	const TObjectPtr<FOrderingActor> ActorB = MakeOrderingActor(Env, ActorBComponents.MakeView(), Sequence, ActorBEvents);
	const TObjectPtr<FOrderingComponent> CompA = MakeOrderingComponent(Env, Sequence, CompAEvents);
	const TObjectPtr<FOrderingComponent> CompB = MakeOrderingComponent(Env, Sequence, CompBEvents);

	const EEngineResult ActorAComponentResult = ActorA.Get()->RegisterComponent(CompA);
	const EEngineResult ActorBComponentResult = ActorB.Get()->RegisterComponent(CompB);
	const EEngineResult ActorAResult = World.Get()->RegisterActor(TObjectPtr<AActor>{ActorA});
	const EEngineResult ActorBResult = World.Get()->RegisterActor(TObjectPtr<AActor>{ActorB});

	const ERuntimeResult BeginResult = World.Get()->BeginPlay(100);
	Sequence.Next(); // Delimits BeginPlay events from the exact tick sequence.
	const ERuntimeResult TickResult = World.Get()->Advance(100);

	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "BeginPlay should succeed");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, TickResult, "Advance should succeed");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ActorAComponentResult, "ActorA component setup succeeds");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ActorBComponentResult, "ActorB component setup succeeds");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ActorAResult, "ActorA setup succeeds");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ActorBResult, "ActorB setup succeeds");
	MW_EXPECT_EQ(Test, std::uint32_t{6}, CompAEvents.TickOrder, "ActorA's component ticks first");
	MW_EXPECT_EQ(Test, std::uint32_t{7}, ActorAEvents.TickOrder, "ActorA ticks after its component");
	MW_EXPECT_EQ(Test, std::uint32_t{8}, CompBEvents.TickOrder, "ActorB's component ticks after ActorA");
	MW_EXPECT_EQ(Test, std::uint32_t{9}, ActorBEvents.TickOrder, "ActorB ticks last");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, CompAEvents.TickCount, "ActorA's component ticks exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, ActorAEvents.TickCount, "ActorA ticks exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, CompBEvents.TickCount, "ActorB's component ticks exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, ActorBEvents.TickCount, "ActorB ticks exactly once");
}

/**
 * Proves EndPlay ends actors in reverse registration order and, for each actor,
 * runs the actor's EndPlay before its components' EndPlay in reverse order.
 */
MW_TEST_CASE(EngineEndPlayIsReverseRegistrationAndActorBeforeComponents)
{
	FSequenceCounter Sequence{};
	FActorEventState ActorAEvents{};
	FActorEventState ActorBEvents{};
	FComponentEventState CompA1Events{};
	FComponentEventState CompA2Events{};

	FLifecycleEnvironment Env{};

	FActorComponentRegistry<2> ActorAComponents;
	FActorComponentRegistry<2> ActorBComponents;
	FWorldActorRegistry<2> WorldActors;

	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, WorldActors.MakeView());
	const TObjectPtr<FOrderingActor> ActorA = MakeOrderingActor(Env, ActorAComponents.MakeView(), Sequence, ActorAEvents);
	const TObjectPtr<FOrderingActor> ActorB = MakeOrderingActor(Env, ActorBComponents.MakeView(), Sequence, ActorBEvents);
	const TObjectPtr<FOrderingComponent> CompA1 = MakeOrderingComponent(Env, Sequence, CompA1Events);
	const TObjectPtr<FOrderingComponent> CompA2 = MakeOrderingComponent(Env, Sequence, CompA2Events);

	const EEngineResult ComponentA1Result = ActorA.Get()->RegisterComponent(CompA1);
	const EEngineResult ComponentA2Result = ActorA.Get()->RegisterComponent(CompA2);
	const EEngineResult ActorAResult = World.Get()->RegisterActor(TObjectPtr<AActor>{ActorA});
	const EEngineResult ActorBResult = World.Get()->RegisterActor(TObjectPtr<AActor>{ActorB});

	const ERuntimeResult BeginResult = World.Get()->BeginPlay(0);
	Sequence.Next(); // Delimits BeginPlay events from the exact EndPlay sequence.
	const ERuntimeResult EndResult = World.Get()->EndPlay();
	const ERuntimeResult RepeatedEndResult = World.Get()->EndPlay();

	MW_EXPECT_EQ(Test, EEngineResult::Success, ComponentA1Result, "First component setup succeeds");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ComponentA2Result, "Second component setup succeeds");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ActorAResult, "ActorA setup succeeds");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ActorBResult, "ActorB setup succeeds");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "BeginPlay should succeed");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, EndResult, "EndPlay should succeed");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, RepeatedEndResult, "Repeated EndPlay remains idempotent");
	MW_EXPECT_EQ(Test, std::uint32_t{6}, ActorBEvents.EndOrder, "ActorB ends first");
	MW_EXPECT_EQ(Test, std::uint32_t{7}, ActorAEvents.EndOrder, "ActorA ends after ActorB");
	MW_EXPECT_EQ(Test, std::uint32_t{8}, CompA2Events.EndOrder, "ActorA's second component ends next");
	MW_EXPECT_EQ(Test, std::uint32_t{9}, CompA1Events.EndOrder, "ActorA's first component ends last");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, ActorAEvents.EndCount, "ActorA EndPlay runs exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, ActorBEvents.EndCount, "ActorB EndPlay runs exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, CompA1Events.EndCount, "First component EndPlay runs exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, CompA2Events.EndCount, "Second component EndPlay runs exactly once");
}

/**
 * Proves an interval actor ticks at most once per Advance even when the caller
 * jumps multiple intervals forward, and that time rollback is rejected.
 */
MW_TEST_CASE(EngineTickIntervalAndNoCatchUpBehavior)
{
	FSequenceCounter Sequence{};
	FActorEventState ActorEvents{};

	FLifecycleEnvironment Env{};

	FActorComponentRegistry<1> ActorComponents;
	FWorldActorRegistry<1> WorldActors;

	const FTickConfiguration IntervalConfiguration{true, true, DurationMilliseconds{50}};
	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, WorldActors.MakeView());
	const TObjectPtr<FOrderingActor> Actor = Env.CreateDerivedObject<FOrderingActor>(
		OrderingActorTypeId, "OrderingActor", IntervalConfiguration, ActorComponents.MakeView(), Sequence, ActorEvents);
	(void)World.Get()->RegisterActor(TObjectPtr<AActor>{Actor});

	(void)World.Get()->BeginPlay(100);

	const ERuntimeResult FirstAdvance = World.Get()->Advance(100); // due immediately after begin
	const std::uint32_t TicksAfterFirst = ActorEvents.TickCount;
	const ERuntimeResult JumpAdvance = World.Get()->Advance(300); // four intervals later, still one tick
	const std::uint32_t TicksAfterJump = ActorEvents.TickCount;
	const ERuntimeResult EarlyAdvance = World.Get()->Advance(310); // before next deadline, no tick
	const std::uint32_t TicksAfterEarly = ActorEvents.TickCount;
	const ERuntimeResult RollbackAdvance = World.Get()->Advance(200); // before last observed time

	MW_EXPECT_EQ(Test, ERuntimeResult::Success, FirstAdvance, "The first advance after begin should tick");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, JumpAdvance, "A forward jump should still succeed");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, EarlyAdvance, "An advance before the next deadline should succeed");
	MW_EXPECT_EQ(Test, ERuntimeResult::NonMonotonicTime, RollbackAdvance, "Time rollback must be rejected");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, TicksAfterFirst, "The interval actor ticks once on the first advance after begin");
	MW_EXPECT_EQ(Test, std::uint32_t{2}, TicksAfterJump, "A multi-interval jump fires at most one additional tick");
	MW_EXPECT_EQ(Test, std::uint32_t{2}, TicksAfterEarly, "An advance before the deadline does not tick");
}

/**
 * Proves the world dispatch guard rejects structural store/collector reentry
 * from every consumer lifecycle hook and preserves the live world graph.
 */
MW_TEST_CASE(EngineLifecycleHooksCannotMutateManagedGraph)
{
	FLifecycleEnvironment Env{};
	FObjectStore& Store = Env.GetStore();
	std::array<MicroWorld::FObjectHandle, 8> Worklist{};
	FGarbageCollector Collector{Store, MicroWorld::FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())}};
	FLifecycleMutationState MutationState{};
	FActorComponentRegistry<0> ActorComponents;
	FWorldActorRegistry<1> WorldActors;

	const MicroWorld::FClassDescriptor* const ComponentDescriptor = Env.FindDescriptor(MicroWorld::UActorComponentClassId);
	const TObjectPtr<UWorld> World = Env.CreateObject<UWorld>(MicroWorld::UWorldClassId, WorldActors.MakeView());
	const TObjectPtr<FMutationAttemptActor> Actor = Env.CreateDerivedObject<FMutationAttemptActor>(
		MutationAttemptActorTypeId, "MutationAttemptActor", Store, ActorComponents.MakeView(), Collector, *ComponentDescriptor, MutationState);
	const EEngineResult ActorRegistration = World.Get()->RegisterActor(Actor);
	const ERuntimeResult BeginResult = World.Get()->BeginPlay(0);
	const ERuntimeResult AdvanceResult = World.Get()->Advance(1);
	const ERuntimeResult EndResult = World.Get()->EndPlay();
	const MicroWorld::FObjectStoreStats FinalStats = Store.Stats();

	MW_EXPECT_EQ(Test, EEngineResult::Success, ActorRegistration, "Adversarial actor setup succeeds");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "BeginPlay completes despite rejected hook reentry");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, AdvanceResult, "Advance completes despite rejected hook reentry");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, EndResult, "EndPlay completes despite rejected hook reentry");
	for (std::size_t HookIndex = 0; HookIndex < MutationState.Construction.size(); ++HookIndex)
	{
		MW_EXPECT_EQ(Test, EObjectResult::LifecycleLocked, MutationState.Construction[HookIndex], "Lifecycle hook construction is rejected");
		MW_EXPECT_EQ(Test, EObjectResult::LifecycleLocked, MutationState.MarkPending[HookIndex], "Lifecycle hook destroy request is rejected");
		MW_EXPECT_EQ(Test, EObjectResult::LifecycleLocked, MutationState.ApplyPending[HookIndex], "Lifecycle hook destruction barrier is rejected");
		MW_EXPECT_EQ(Test, EObjectResult::LifecycleLocked, MutationState.AddRoot[HookIndex], "Lifecycle hook root mutation is rejected");
		MW_EXPECT_EQ(
			Test, ERuntimeResult::LifecycleLocked, MutationState.RequestCollection[HookIndex], "Lifecycle hook collection request is rejected");
		MW_EXPECT_EQ(
			Test, ERuntimeResult::LifecycleLocked, MutationState.AdvanceCollection[HookIndex], "Lifecycle hook collection advance is rejected");
	}
	MW_EXPECT_EQ(Test, std::uint32_t{2}, FinalStats.OccupiedSlots, "Rejected hook reentry preserves the world and actor");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, FinalStats.PendingDestroySlots, "Rejected hook reentry leaves no pending destruction");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, FinalStats.ActiveRoots, "Rejected hook reentry publishes no root");
	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "World remains live after all lifecycle hooks");
	MW_EXPECT_TRUE(Test, Actor.Get() != nullptr, "Actor remains live after all lifecycle hooks");
	MW_EXPECT_EQ(Test, World.Get(), Actor.Get()->GetOwnerWorld(), "Actor retains its original world parent");
}

} // namespace
