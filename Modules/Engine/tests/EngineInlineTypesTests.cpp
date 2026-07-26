#include "EngineTestSupport.h"
#include "TestSupport.h"

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/InlineTypes.h>
#include <MicroWorld/Engine/World.h>

#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::AActor;
using MicroWorld::DurationMilliseconds;
using MicroWorld::EEngineResult;
using MicroWorld::ERuntimeResult;
using MicroWorld::FTickConfiguration;
using MicroWorld::FTickContext;
using MicroWorld::TInlineWorld;
using MicroWorld::TObjectPtr;
using MicroWorld::UActorComponent;
using MicroWorld::UWorld;
using MicroWorld::Tests::FActorEventState;
using MicroWorld::Tests::FComponentEventState;
using MicroWorld::Tests::FSequenceCounter;
using MicroWorld::Tests::TEngineEnvironment;

/** Tick configuration that lets the plain actor and component tick every advance. */
constexpr FTickConfiguration OrderingTickConfiguration{true, true, DurationMilliseconds{0}};

/** Test-local type ids for the inline world, actor, and component descriptors. */
constexpr MicroWorld::FTypeId InlineWorldTypeId{0x00050001u};
constexpr MicroWorld::FTypeId WorldActorTypeId{0x00050002u};
constexpr MicroWorld::FTypeId InlineComponentTypeId{0x00050003u};

/** A component that records begin/tick/end ordering without separate storage wrappers. */
class FInlineComponent final : public UActorComponent
{
public:
	/** Binds this component to the shared sequence and its own observed event record. */
	FInlineComponent(FSequenceCounter& InSequence, FComponentEventState& InEvents) noexcept
		: UActorComponent(OrderingTickConfiguration), Sequence(InSequence), Events(InEvents)
	{
	}

protected:
	/** Records the sequence value and count of this component's begin hook. */
	void BeginPlay() noexcept override
	{
		Events.BeginOrder = Sequence.Next();
		++Events.BeginCount;
	}
	/** Records the sequence value and count of this component's tick hook. */
	void TickComponent(const FTickContext&) noexcept override
	{
		Events.TickOrder = Sequence.Next();
		++Events.TickCount;
	}
	/** Records the sequence value and count of this component's end hook. */
	void EndPlay() noexcept override
	{
		Events.EndOrder = Sequence.Next();
		++Events.EndCount;
	}

private:
	/** Shares one monotonic order source with every observed type in the test. */
	FSequenceCounter& Sequence;
	/** Receives this component's observed begin/tick/end ordering and counts. */
	FComponentEventState& Events;
};

/** A plain actor that records lifecycle ordering through direct component storage. */
class FWorldActor final : public AActor
{
public:
	/** Uses direct actor component storage and binds the shared sequence and record. */
	FWorldActor(FSequenceCounter& InSequence, FActorEventState& InEvents) noexcept
		: AActor(OrderingTickConfiguration), Sequence(InSequence), Events(InEvents)
	{
	}

protected:
	/** Records the sequence value and count of this actor's begin hook. */
	void BeginPlay() noexcept override
	{
		Events.BeginOrder = Sequence.Next();
		++Events.BeginCount;
	}
	/** Records the sequence value and count of this actor's tick hook. */
	void Tick(const FTickContext&) noexcept override
	{
		Events.TickOrder = Sequence.Next();
		++Events.TickCount;
	}
	/** Records the sequence value and count of this actor's end hook. */
	void EndPlay() noexcept override
	{
		Events.EndOrder = Sequence.Next();
		++Events.EndCount;
	}

private:
	/** Shares one monotonic order source with every observed type in the test. */
	FSequenceCounter& Sequence;
	/** Receives this actor's observed begin/tick/end ordering and counts. */
	FActorEventState& Events;
};

/** Environment sized so the inline world's registry and its actors fit each slot. */
using FInlineEnvironment = TEngineEnvironment<512, 16, 8, 2>;

/**
 * Proves an inline world, a plain actor, and a component begin, tick, and end
 * in the same deterministic order without caller-composed component
 * storage.
 */
MW_TEST_CASE(EngineInlineWorldComposesAndDispatchesPlainActor)
{
	FSequenceCounter Sequence{};
	FActorEventState ActorEvents{};
	FComponentEventState ComponentEvents{};

	FInlineEnvironment Env{};

	// The inline world owns its registry while the actor uses direct component storage.
	const TObjectPtr<TInlineWorld<1>> World = Env.CreateDerivedObject<TInlineWorld<1>>(InlineWorldTypeId, "InlineWorld");
	const TObjectPtr<FWorldActor> Actor = Env.CreateDerivedObject<FWorldActor>(WorldActorTypeId, "WorldActor", Sequence, ActorEvents);
	const TObjectPtr<FInlineComponent> Component =
		Env.CreateDerivedObject<FInlineComponent>(InlineComponentTypeId, "InlineComponent", Sequence, ComponentEvents);

	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "The inline world constructs within one store slot");
	MW_EXPECT_TRUE(Test, Actor.Get() != nullptr, "The plain actor constructs within one store slot");
	MW_EXPECT_TRUE(Test, Component.Get() != nullptr, "The component constructs within one store slot");

	const EEngineResult ComponentRegistration = Actor.Get()->RegisterComponent(Component);
	const EEngineResult ActorRegistration = World.Get()->RegisterActor(TObjectPtr<AActor>{Actor});
	const ERuntimeResult BeginResult = World.Get()->BeginPlay(0);
	Sequence.Next(); // Delimits begin ordering from the exact tick ordering.
	const ERuntimeResult AdvanceResult = World.Get()->Advance(10);
	Sequence.Next(); // Delimits tick ordering from the exact end ordering.
	const ERuntimeResult EndResult = World.Get()->EndPlay();

	MW_EXPECT_EQ(Test, EEngineResult::Success, ComponentRegistration, "The plain actor accepts the component");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ActorRegistration, "The inline world's registry accepts the actor");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "BeginPlay over the inline world succeeds");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, AdvanceResult, "Advance over the inline world succeeds");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, EndResult, "EndPlay over the inline world succeeds");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, ComponentEvents.BeginCount, "The component begins exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, ActorEvents.BeginCount, "The plain actor begins exactly once");
	MW_EXPECT_TRUE(Test, ComponentEvents.BeginOrder < ActorEvents.BeginOrder, "The component begins before its actor");
	MW_EXPECT_TRUE(Test, ComponentEvents.TickOrder < ActorEvents.TickOrder, "The component ticks before its actor");
	MW_EXPECT_TRUE(Test, ActorEvents.EndOrder < ComponentEvents.EndOrder, "The actor ends before its component");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, ComponentEvents.EndCount, "The component ends exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, ActorEvents.EndCount, "The plain actor ends exactly once");
}

/**
 * Proves an inline world still spawns and destroys a plain actor through the deferred barrier.
 */
MW_TEST_CASE(EngineInlineWorldSpawnsAndDestroysPlainActor)
{
	FSequenceCounter Sequence{};
	FActorEventState ActorEvents{};

	FInlineEnvironment Env{};

	const TObjectPtr<TInlineWorld<1>> World = Env.CreateDerivedObject<TInlineWorld<1>>(InlineWorldTypeId, "InlineWorld");
	const TObjectPtr<FWorldActor> Actor = Env.CreateDerivedObject<FWorldActor>(WorldActorTypeId, "WorldActor", Sequence, ActorEvents);
	(void)World.Get()->BeginPlay(0);

	const EEngineResult SpawnResult = World.Get()->SpawnActor(TObjectPtr<AActor>{Actor});
	const std::uint32_t BeginCountAfterQueue = ActorEvents.BeginCount;
	const ERuntimeResult SpawnBarrier = World.Get()->ApplyPending(10);
	const std::uint32_t BeginCountAfterBarrier = ActorEvents.BeginCount;

	const EEngineResult DestroyResult = World.Get()->DestroyActor(TObjectPtr<AActor>{Actor});
	const std::uint32_t EndCountAfterQueue = ActorEvents.EndCount;
	const ERuntimeResult DestroyBarrier = World.Get()->ApplyPending(20);

	MW_EXPECT_EQ(Test, EEngineResult::Success, SpawnResult, "The plain actor is accepted for deferred spawn");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, BeginCountAfterQueue, "The deferred spawn does not begin before the barrier");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, SpawnBarrier, "The spawn barrier succeeds");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, BeginCountAfterBarrier, "The deferred spawn begins at the barrier");
	MW_EXPECT_EQ(Test, EEngineResult::Success, DestroyResult, "The plain actor is accepted for deferred destroy");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, EndCountAfterQueue, "The deferred destroy does not end before the barrier");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, DestroyBarrier, "The destroy barrier succeeds");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, ActorEvents.EndCount, "The deferred destroy ends at the barrier");
}

} // namespace
