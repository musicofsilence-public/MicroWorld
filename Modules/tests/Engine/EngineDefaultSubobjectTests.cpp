#include "TestSupport.h"
#include "EngineMessagingTestHelpers.h"

#include <MicroWorld/Engine/ObjectInitializer.h>

#include <array>
#include <cstdint>

namespace
{

using namespace ::MicroWorld::Tests;

/**
 * Motivation: Records constructor and publication observations without retaining a managed pointer outside the actor graph.
 * Responsibilities: Keep the default-subobject visibility result observable after World startup.
 * Example: FDefaultSubobjectState State{};
 */
struct FDefaultSubobjectState final
{
	/** Motivation: Counts component construction so the test proves the initializer selected its actor constructor. */
	std::size_t ComponentConstructionCount{};
	/** Motivation: Records that a provisional component pointer cannot resolve inside the actor constructor. */
	bool bComponentResolvedDuringActorConstruction{};
	/** Motivation: Records that the retained pointer resolves only after the transaction commits. */
	bool bComponentResolvedAfterPublication{};
};

/**
 * Motivation: Provides one simple actor-owned component whose construction is observable through the public actor lifecycle.
 * Responsibilities: Increment only its supplied state during construction.
 * Example: FDefaultSubobjectComponent Component{&State};
 */
class FDefaultSubobjectComponent final : public MicroWorld::Engine::UActorComponent
{
public:
	/**
	 * Motivation: Records that the initializer constructed this component within its owner's private transaction.
	 * Responsibilities: Increment the supplied counter.
	 */
	explicit FDefaultSubobjectComponent(FDefaultSubobjectState* const InState) noexcept : UActorComponent(), State(InState)
	{
		++State->ComponentConstructionCount;
	}

private:
	/** Motivation: Keeps this test-only component's observation target alive outside managed storage. */
	FDefaultSubobjectState* State{nullptr};
};

/**
 * Motivation: Exercises initializer-first actor selection and constructor-time default component visibility.
 * Responsibilities: Request one component through the initializer and observe its pointer before and after publication.
 * Example: FInitializerActor Actor{Initializer, &State};
 */
class FInitializerActor final : public MicroWorld::Engine::AActor
{
public:
	/**
	 * Motivation: Builds the fixed component graph using the only permitted constructor-time managed creation capability.
	 * Responsibilities: Retain the returned pointer without resolving it successfully before commit.
	 */
	FInitializerActor(MicroWorld::Engine::FObjectInitializer& Initializer, FDefaultSubobjectState* const InState) noexcept : AActor(), State(InState)
	{
		Component = Initializer.CreateDefaultSubobject<FDefaultSubobjectComponent>(State);
		State->bComponentResolvedDuringActorConstruction = Component.Get() != nullptr;
	}

protected:
	/**
	 * Motivation: Observes the committed pointer only after World has published this actor graph.
	 * Responsibilities: Record the postcondition without mutating the graph.
	 */
	void BeginPlay() noexcept override { State->bComponentResolvedAfterPublication = Component.Get() != nullptr; }

private:
	/** Motivation: Holds the component's traced actor-owned reference after the transaction publishes it. */
	MicroWorld::Engine::TObjectPtr<FDefaultSubobjectComponent> Component{};
	/** Motivation: Carries test-owned observations without making their lifetime managed. */
	FDefaultSubobjectState* State{nullptr};
};

/**
 * Motivation: Records destruction order and factory-path selection without depending on ObjectStore internals.
 * Responsibilities: Preserve bounded test observations for rollback and constructor-selection assertions.
 * Example: FConstructionTransactionState State{};
 */
struct FConstructionTransactionState final
{
	/** Motivation: Holds the exact destructor sequence produced by one failed default-subobject transaction. */
	std::array<std::uint8_t, 5> DestructionOrder{};
	/** Motivation: Counts valid leading entries in DestructionOrder. */
	std::size_t DestructionCount{};
	/** Motivation: Distinguishes the initializer factory path from a legacy constructor fallback. */
	std::uint8_t FactorySelection{};
	/** Motivation: Records that a legacy actor reached its normal BeginPlay hook. */
	std::size_t LegacyBeginCount{};
};

/**
 * Motivation: Makes a pre-capacity sticky construction failure observable without exposing transaction internals.
 * Responsibilities: Count construction attempts that must be suppressed after the first failed default subobject.
 * Example: FStickyFailureState State{};
 */
struct FStickyFailureState final
{
	/** Motivation: Counts the component constructor that must not run after the transaction records its first failure. */
	std::size_t SuppressedComponentConstructionCount{};
	/** Motivation: Counts a later queued actor whose construction proves no suppressed component descriptor consumed registry capacity. */
	std::size_t ProofActorConstructionCount{};
};

/**
 * Motivation: Restricts a test Engine enough that one component reservation exhausts object slots before actor component capacity.
 * Responsibilities: Leave one class descriptor slot for the later proof actor while providing only World, actor, and first-component object slots.
 * Example: TEngine<FStickyFailureEngineTraits> Engine{Budget};
 */
struct FStickyFailureEngineTraits final : MicroWorld::Engine::FDefaultEngineTraits
{
	/** Motivation: Leaves one descriptor slot available only when the suppressed component never resolves. */
	static constexpr std::size_t MaxClasses = 8;
	/** Motivation: Makes the second default component reservation fail while the actor owns fewer than four components. */
	static constexpr std::size_t MaxObjects = 3;
};

/**
 * Motivation: Makes reverse default-component teardown observable when an actor constructor records a sticky error.
 * Responsibilities: Append its label only during destruction.
 * Example: FOrderedDefaultComponent Component{&State, 1};
 */
class FOrderedDefaultComponent final : public MicroWorld::Engine::UActorComponent
{
public:
	/**
	 * Motivation: Binds one stable test label to this default component.
	 * Responsibilities: Preserve the supplied state and label.
	 */
	FOrderedDefaultComponent(FConstructionTransactionState* const InState, const std::uint8_t InLabel) noexcept
		: UActorComponent(), State(InState), Label(InLabel)
	{
	}
	/**
	 * Motivation: Observes rollback destruction without adding behavior to the production transaction.
	 * Responsibilities: Append this component's label once.
	 */
	~FOrderedDefaultComponent() noexcept override
	{
		if (State->DestructionCount < State->DestructionOrder.size())
		{
			State->DestructionOrder[State->DestructionCount] = Label;
		}
		++State->DestructionCount;
	}

private:
	/** Motivation: Carries the caller-owned observation target. */
	FConstructionTransactionState* State{nullptr};
	/** Motivation: Identifies this component's required reverse destruction position. */
	std::uint8_t Label{};
};

/**
 * Motivation: Consumes the final available object slot before a later default-subobject reservation fails.
 * Responsibilities: Remain a constructible unique component type for the sticky-failure fixture.
 * Example: FFirstStickyComponent Component{};
 */
class FFirstStickyComponent final : public MicroWorld::Engine::UActorComponent
{
};

/**
 * Motivation: Resolves normally but fails to reserve an object slot before the actor reaches component capacity.
 * Responsibilities: Supply the existing storage-capacity failure seam for sticky-error behavior.
 * Example: FStorageLimitedComponent Component{};
 */
class FStorageLimitedComponent final : public MicroWorld::Engine::UActorComponent
{
};

/**
 * Motivation: Detects whether a later valid request constructs after another default-subobject request failed.
 * Responsibilities: Increment only when the transaction incorrectly attempts construction after its sticky failure.
 * Example: FSuppressedStickyComponent Component{&State};
 */
class FSuppressedStickyComponent final : public MicroWorld::Engine::UActorComponent
{
public:
	/**
	 * Motivation: Exposes an otherwise-valid constructor call after the required capacity failure.
	 * Responsibilities: Count only an incorrect post-failure construction attempt.
	 */
	explicit FSuppressedStickyComponent(FStickyFailureState* const InState) noexcept : UActorComponent()
	{
		++InState->SuppressedComponentConstructionCount;
	}
};

/**
 * Motivation: Produces a sticky default-subobject failure before an actor reaches its per-actor component limit.
 * Responsibilities: Request one successful component, one store-capacity failure, then one request that must be suppressed.
 * Example: FStickyFailureActor Actor{Initializer, &State};
 */
class FStickyFailureActor final : public MicroWorld::Engine::AActor
{
public:
	/**
	 * Motivation: Drives the first failing request and an otherwise-valid request after it.
	 * Responsibilities: Retain no component pointers and let the transaction enforce its sticky result.
	 */
	FStickyFailureActor(MicroWorld::Engine::FObjectInitializer& Initializer, FStickyFailureState* const InState) noexcept : AActor(Initializer)
	{
		(void)Initializer.CreateDefaultSubobject<FFirstStickyComponent>();
		(void)Initializer.CreateDefaultSubobject<FStorageLimitedComponent>();
		(void)Initializer.CreateDefaultSubobject<FSuppressedStickyComponent>(InState);
	}
};

/**
 * Motivation: Proves that a suppressed default-component request left the final class descriptor slot untouched.
 * Responsibilities: Increment only if its later queued factory can resolve and construct normally.
 * Example: FStickyProofActor Actor{&State};
 */
class FStickyProofActor final : public MicroWorld::Engine::AActor
{
public:
	/**
	 * Motivation: Makes successful later factory construction visible after the earlier transaction failed.
	 * Responsibilities: Increment the caller-owned proof counter.
	 */
	explicit FStickyProofActor(FStickyFailureState* const InState) noexcept : AActor() { ++InState->ProofActorConstructionCount; }
};

/**
 * Motivation: Forces the first default-subobject capacity failure after four valid component constructions.
 * Responsibilities: Record actor-first rollback before components unwind in reverse construction order.
 * Example: FFailingDefaultActor Actor{Initializer, &State};
 */
class FFailingDefaultActor final : public MicroWorld::Engine::AActor
{
public:
	/**
	 * Motivation: Exercises the transaction's sticky capacity error path.
	 * Responsibilities: Request five components against the actor's fixed four-component capacity.
	 */
	FFailingDefaultActor(MicroWorld::Engine::FObjectInitializer& Initializer, FConstructionTransactionState* const InState) noexcept
		: AActor(Initializer), State(InState)
	{
		(void)Initializer.CreateDefaultSubobject<FOrderedDefaultComponent>(State, std::uint8_t{1});
		(void)Initializer.CreateDefaultSubobject<FOrderedDefaultComponent>(State, std::uint8_t{2});
		(void)Initializer.CreateDefaultSubobject<FOrderedDefaultComponent>(State, std::uint8_t{3});
		(void)Initializer.CreateDefaultSubobject<FOrderedDefaultComponent>(State, std::uint8_t{4});
		(void)Initializer.CreateDefaultSubobject<FOrderedDefaultComponent>(State, std::uint8_t{5});
	}
	/**
	 * Motivation: Makes the required actor-first rollback order observable.
	 * Responsibilities: Append the actor marker before component teardown begins.
	 */
	~FFailingDefaultActor() noexcept override
	{
		if (State->DestructionCount < State->DestructionOrder.size())
		{
			State->DestructionOrder[State->DestructionCount] = 100;
		}
		++State->DestructionCount;
	}

private:
	/** Motivation: Retains the test observation target until transaction rollback destroys this actor. */
	FConstructionTransactionState* State{nullptr};
};

/**
 * Motivation: Preserves compatibility for existing actors that do not accept an object initializer.
 * Responsibilities: Mark legacy construction and normal BeginPlay through the unchanged deferred spawn API.
 * Example: FLegacyCompatibleActor Actor{&State};
 */
class FLegacyCompatibleActor final : public MicroWorld::Engine::AActor
{
public:
	/**
	 * Motivation: Uses the legacy constructor signature deliberately.
	 * Responsibilities: Mark legacy selection only.
	 */
	explicit FLegacyCompatibleActor(FConstructionTransactionState* const InState) noexcept : AActor(), State(InState) { State->FactorySelection = 2; }

protected:
	/**
	 * Motivation: Proves legacy construction still participates in normal World lifecycle dispatch.
	 * Responsibilities: Increment the legacy begin observation.
	 */
	void BeginPlay() noexcept override { ++State->LegacyBeginCount; }

private:
	/** Motivation: Retains legacy-path observations outside managed storage. */
	FConstructionTransactionState* State{nullptr};
};

/**
 * Motivation: Proves an initializer-aware constructor wins when a legacy constructor is also available.
 * Responsibilities: Mark only the selected constructor path.
 * Example: FInitializerWinsActor Actor{Initializer, &State};
 */
class FInitializerWinsActor final : public MicroWorld::Engine::AActor
{
public:
	/**
	 * Motivation: Represents the preferred initializer construction path.
	 * Responsibilities: Mark initializer selection.
	 */
	FInitializerWinsActor(MicroWorld::Engine::FObjectInitializer& Initializer, FConstructionTransactionState* const InState) noexcept
		: AActor(Initializer), State(InState)
	{
		State->FactorySelection = 1;
	}
	/**
	 * Motivation: Represents the compatibility fallback that must not win when the initializer constructor is valid.
	 * Responsibilities: Mark legacy selection if chosen.
	 */
	explicit FInitializerWinsActor(FConstructionTransactionState* const InState) noexcept : AActor(), State(InState) { State->FactorySelection = 2; }

private:
	/** Motivation: Retains the constructor-path observation target. */
	FConstructionTransactionState* State{nullptr};
};

/**
 * Motivation: Proves initializer-aware actors can create a private default graph without a special spawn API.
 * Responsibilities: Verify construction-time invisibility and post-commit resolution through the public World lifecycle.
 */
MW_TEST_CASE(EnginePublishesInitializerDefaultSubobjectsOnlyAfterActorConstructionCommits)
{
	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	FDefaultSubobjectState State{};
	const auto World = Engine.CreateWorld();
	const auto Request = World.Get()->SpawnActor<FInitializerActor>(&State);

	// Act
	const MicroWorld::Core::ERuntimeResult BeginResult = Engine.BeginPlay(0);

	// Assert
	MW_EXPECT_EQ(
		Test,
		MicroWorld::Engine::EActorSpawnRequestResult::Queued,
		Request.Result,
		"Initializer-aware actors should use the ordinary deferred spawn API");
	MW_EXPECT_EQ(Test, MicroWorld::Core::ERuntimeResult::Success, BeginResult, "A valid default-subobject graph should start with its World");
	MW_EXPECT_EQ(Test, std::size_t{1}, State.ComponentConstructionCount, "The initializer should construct exactly one default component");
	MW_EXPECT_TRUE(Test, !State.bComponentResolvedDuringActorConstruction, "Provisional default components must remain hidden during construction");
	MW_EXPECT_TRUE(Test, State.bComponentResolvedAfterPublication, "The retained default component pointer must resolve after commit");
}

/**
 * Motivation: Keeps both factory paths stable while adding initializer-first default-subobject support.
 * Responsibilities: Verify initializer selection wins when available and legacy-only actors retain lifecycle compatibility.
 */
MW_TEST_CASE(EngineSelectsInitializerFactoryBeforeLegacyFactoryAndRetainsLegacyCompatibility)
{
	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	FConstructionTransactionState InitializerState{};
	FConstructionTransactionState LegacyState{};
	const auto World = Engine.CreateWorld();
	const auto InitializerRequest = World.Get()->SpawnActor<FInitializerWinsActor>(&InitializerState);
	const auto LegacyRequest = World.Get()->SpawnActor<FLegacyCompatibleActor>(&LegacyState);

	// Act
	const MicroWorld::Core::ERuntimeResult BeginResult = Engine.BeginPlay(0);

	// Assert
	MW_EXPECT_EQ(
		Test,
		MicroWorld::Engine::EActorSpawnRequestResult::Queued,
		InitializerRequest.Result,
		"Both-compatible actors should use the ordinary spawn API");
	MW_EXPECT_EQ(Test, MicroWorld::Engine::EActorSpawnRequestResult::Queued, LegacyRequest.Result, "Legacy actors should remain spawnable");
	MW_EXPECT_EQ(Test, MicroWorld::Core::ERuntimeResult::Success, BeginResult, "Both factory paths should start the World");
	MW_EXPECT_EQ(Test, std::uint8_t{1}, InitializerState.FactorySelection, "The initializer constructor must win over a legacy alternative");
	MW_EXPECT_EQ(Test, std::uint8_t{2}, LegacyState.FactorySelection, "A legacy-only actor must retain its constructor path");
	MW_EXPECT_EQ(Test, std::size_t{1}, LegacyState.LegacyBeginCount, "Legacy actors must retain their normal BeginPlay behavior");
}

/**
 * Motivation: Makes capacity, sticky failure, descriptor reuse, destructor order, and object-count rollback observable at the public World boundary.
 * Responsibilities: Reject the fifth default component, abort startup, destroy actor then components in reverse order, and leave only the rooted
 * World object.
 */
MW_TEST_CASE(EngineRollsBackFailedDefaultSubobjectGraphAndRestoresObjectCount)
{
	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	FConstructionTransactionState State{};
	const auto World = Engine.CreateWorld();
	const auto Request = World.Get()->SpawnActor<FFailingDefaultActor>(&State);

	// Act
	const MicroWorld::Core::ERuntimeResult BeginResult = Engine.BeginPlay(0);
	const MicroWorld::Engine::FObjectStoreStats StoreStats = Engine.GetObjectStore().Stats();
	const MicroWorld::Engine::FActorSpawnStatus Status = World.Get()->GetSpawnStatus(Request.Handle);

	// Assert
	MW_EXPECT_EQ(
		Test,
		MicroWorld::Engine::EActorSpawnRequestResult::Queued,
		Request.Result,
		"The request should queue before constructor-time capacity is known");
	MW_EXPECT_EQ(
		Test,
		MicroWorld::Core::ERuntimeResult::InitializationFailed,
		BeginResult,
		"A required pre-play construction failure must abort World startup");
	MW_EXPECT_EQ(
		Test, MicroWorld::Engine::EActorSpawnState::Failed, Status.State, "The failed request should retain its terminal construction status");
	MW_EXPECT_EQ(
		Test, std::uint32_t{1}, StoreStats.OccupiedSlots, "Rollback must restore every actor and component slot while retaining only the World");
	MW_EXPECT_EQ(Test, std::size_t{5}, State.DestructionCount, "Rollback must destroy one actor and its four constructed components");
	MW_EXPECT_EQ(Test, std::uint8_t{100}, State.DestructionOrder[0], "Rollback must destroy the completed actor before its components");
	MW_EXPECT_EQ(Test, std::uint8_t{4}, State.DestructionOrder[1], "Rollback must destroy components in reverse construction order");
	MW_EXPECT_EQ(Test, std::uint8_t{3}, State.DestructionOrder[2], "Rollback must preserve the complete reverse component order");
	MW_EXPECT_EQ(Test, std::uint8_t{2}, State.DestructionOrder[3], "Rollback must preserve the complete reverse component order");
	MW_EXPECT_EQ(Test, std::uint8_t{1}, State.DestructionOrder[4], "Rollback must destroy the first component last");
}

/**
 * Motivation: Guards transaction stickiness independently from the actor's four-component capacity limit.
 * Responsibilities: Prove a later valid component is neither resolved nor constructed after a storage failure, while a later actor still resolves.
 */
MW_TEST_CASE(EngineSuppressesDefaultSubobjectsAfterPreCapacityStickyFailure)
{
	// Arrange
	MicroWorld::Engine::TEngine<FStickyFailureEngineTraits> Engine{EngineMessagingCollectionBudget};
	FStickyFailureState State{};
	const auto World = Engine.CreateWorld();
	const auto FailingRequest = World.Get()->SpawnActor<FStickyFailureActor>(&State);
	const auto ProofRequest = World.Get()->SpawnActor<FStickyProofActor>(&State);

	// Act
	const MicroWorld::Core::ERuntimeResult BeginResult = Engine.BeginPlay(0);

	// Assert
	MW_EXPECT_EQ(
		Test,
		MicroWorld::Engine::EActorSpawnRequestResult::Queued,
		FailingRequest.Result,
		"The sticky-failure actor should queue before construction starts");
	MW_EXPECT_EQ(
		Test,
		MicroWorld::Engine::EActorSpawnRequestResult::Queued,
		ProofRequest.Result,
		"The later proof actor should queue before class resolution occurs");
	MW_EXPECT_EQ(
		Test,
		MicroWorld::Core::ERuntimeResult::InitializationFailed,
		BeginResult,
		"The storage-limited default component must fail startup before actor component capacity is reached");
	MW_EXPECT_EQ(
		Test,
		std::size_t{0},
		State.SuppressedComponentConstructionCount,
		"A request after the sticky failure must not resolve far enough to construct its component");
	MW_EXPECT_EQ(
		Test,
		std::size_t{1},
		State.ProofActorConstructionCount,
		"The later actor must construct, proving the suppressed component left its descriptor slot unused");
}

} // namespace
