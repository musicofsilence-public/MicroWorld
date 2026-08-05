#include "EngineTestSupport.h"
#include "TestSupport.h"

#include <MicroWorld/Core/RuntimeResult.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/DefaultEngineTraits.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Engine/WorldSubsystem.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::Core::ERuntimeResult;
using MicroWorld::Engine::AActor;
using MicroWorld::Engine::EEngineResult;
using MicroWorld::Engine::EObjectResult;
using MicroWorld::Engine::FDefaultEngineTraits;
using MicroWorld::Engine::FObjectStore;
using MicroWorld::Engine::FTypeId;
using MicroWorld::Engine::FWorldActorRegistry;
using MicroWorld::Engine::FWorldSubsystemRegistry;
using MicroWorld::Engine::TEngine;
using MicroWorld::Engine::TObjectPtr;
using MicroWorld::Engine::TStrongObjectPtr;
using MicroWorld::Engine::UActorComponent;
using MicroWorld::Engine::UWorld;
using MicroWorld::Engine::UWorldSubsystem;
using MicroWorld::Tests::FCollectorFixture;
using MicroWorld::Tests::FEngineEnvironmentSlots16;
using MicroWorld::Tests::FPlainComponent;
using MicroWorld::Tests::FSequenceCounter;

/** Motivation: Canonical time used by every subsystem startup test. */
constexpr MicroWorld::Core::TimePointMilliseconds BaselineTimeMilliseconds{0};

/** Motivation: Stable type ids for test-only managed classes. */
constexpr FTypeId FirstSubsystemTypeId{0x00070001u};
constexpr FTypeId SecondSubsystemTypeId{0x00070002u};
constexpr FTypeId MutationSubsystemTypeId{0x00070003u};
constexpr FTypeId LookupActorTypeId{0x00070004u};
constexpr FTypeId OrderingActorTypeId{0x00070005u};
constexpr FTypeId FailingActorTypeId{0x00070006u};
constexpr FTypeId PlainComponentTypeId{0x00070007u};
constexpr FTypeId DeferredLookupActorTypeId{0x00070008u};

/**
 * Motivation: Records observable subsystem lifecycle callbacks and their relative order.
 * Responsibilities: Store only public hook outcomes needed by subsystem behavior tests.
 * Example:
 *   FSubsystemEventState State{};
 */
struct FSubsystemEventState final
{
	/** Motivation: Counts successful Initialize hook calls. */
	std::uint32_t InitializeCount{0};
	/** Motivation: Counts successful Deinitialize hook calls. */
	std::uint32_t DeinitializeCount{0};
	/** Motivation: Records Initialize relative order. */
	std::uint32_t InitializeOrder{0};
	/** Motivation: Records Deinitialize relative order. */
	std::uint32_t DeinitializeOrder{0};
	/** Motivation: Exposes service readiness to actor-facing lookup tests. */
	bool bIsReady{false};
};

/**
 * Motivation: Gives tests distinct exact subsystem types with identical observable lifecycle behavior.
 * Responsibilities: Record initialize/deinitialize state without adding production-only seams.
 * Example:
 *   TRecordingWorldSubsystem<1> Subsystem(Sequence, State);
 */
template<std::uint32_t TypeTag>
class TRecordingWorldSubsystem final : public UWorldSubsystem
{
public:
	TRecordingWorldSubsystem(FSequenceCounter& InSequence, FSubsystemEventState& InState) noexcept : Sequence(InSequence), State(InState) {}

	bool IsReady() const noexcept { return State.bIsReady; }

protected:
	void Initialize() override
	{
		++State.InitializeCount;
		State.InitializeOrder = Sequence.Next();
		State.bIsReady = true;
	}

	void Deinitialize() override
	{
		++State.DeinitializeCount;
		State.DeinitializeOrder = Sequence.Next();
		State.bIsReady = false;
	}

private:
	FSequenceCounter& Sequence;
	FSubsystemEventState& State;
};

/** Motivation: First exact subsystem type used for success and duplicate tests. */
using FFirstWorldSubsystem = TRecordingWorldSubsystem<1>;

/** Motivation: Second exact subsystem type used for ordering and capacity tests. */
using FSecondWorldSubsystem = TRecordingWorldSubsystem<2>;

/**
 * Motivation: Records whether an actor can resolve a ready exact subsystem during its complete lifecycle.
 * Responsibilities: Query only through the public owning-World API during BeginPlay and EndPlay.
 * Example:
 *   FLookupActorState State{};
 */
struct FLookupActorState final
{
	/** Motivation: Records ready exact lookup during BeginPlay. */
	bool bReadyDuringBegin{false};
	/** Motivation: Records ready exact lookup during EndPlay. */
	bool bReadyDuringEnd{false};
	/** Motivation: Records actor BeginPlay relative order. */
	std::uint32_t BeginOrder{0};
	/** Motivation: Records actor EndPlay relative order. */
	std::uint32_t EndOrder{0};
};

/**
 * Motivation: Proves actors consume world subsystems through their weak owning-World link.
 * Responsibilities: Record exact ready lookup at begin and end without retaining a raw service pointer.
 * Example:
 *   FSubsystemLookupActor Actor(Sequence, State);
 */
class FSubsystemLookupActor final : public AActor
{
public:
	FSubsystemLookupActor(FSequenceCounter& InSequence, FLookupActorState& InState) noexcept : Sequence(InSequence), State(InState) {}

protected:
	void BeginPlay() noexcept override
	{
		UWorld* World = GetOwnerWorld();
		FFirstWorldSubsystem* Subsystem = World == nullptr ? nullptr : World->GetSubsystem<FFirstWorldSubsystem>();
		State.bReadyDuringBegin = Subsystem != nullptr && Subsystem->GetWorld() == World && Subsystem->IsReady();
		State.BeginOrder = Sequence.Next();
	}

	void EndPlay() noexcept override
	{
		UWorld* World = GetOwnerWorld();
		FFirstWorldSubsystem* Subsystem = World == nullptr ? nullptr : World->GetSubsystem<FFirstWorldSubsystem>();
		State.bReadyDuringEnd = Subsystem != nullptr && Subsystem->GetWorld() == World && Subsystem->IsReady();
		State.EndOrder = Sequence.Next();
	}

private:
	FSequenceCounter& Sequence;
	FLookupActorState& State;
};

/**
 * Motivation: Attempts World and object-store mutations from subsystem lifecycle hooks.
 * Responsibilities: Record both public rejection results without retaining private storage access.
 * Example:
 *   FMutationAttemptSubsystem Subsystem(Candidate, BeginRegistration, EndRegistration, BeginStore, EndStore);
 */
class FMutationAttemptSubsystem final : public UWorldSubsystem
{
public:
	FMutationAttemptSubsystem(
		TObjectPtr<UWorldSubsystem> InCandidate,
		EEngineResult& InInitializeRegistrationResult,
		EEngineResult& InEndRegistrationResult,
		EObjectResult& InInitializeStoreResult,
		EObjectResult& InEndStoreResult) noexcept
		: Candidate(InCandidate)
		, InitializeRegistrationResult(InInitializeRegistrationResult)
		, EndRegistrationResult(InEndRegistrationResult)
		, InitializeStoreResult(InInitializeStoreResult)
		, EndStoreResult(InEndStoreResult)
	{
	}

protected:
	void Initialize() override { AttemptMutations(InitializeRegistrationResult, InitializeStoreResult); }

	void Deinitialize() override { AttemptMutations(EndRegistrationResult, EndStoreResult); }

private:
	void AttemptMutations(EEngineResult& OutRegistrationResult, EObjectResult& OutStoreResult) noexcept
	{
		UWorld* World = GetWorld();
		OutRegistrationResult = World == nullptr ? EEngineResult::InvalidReference : World->RegisterSubsystem(Candidate);
		OutStoreResult = GetObjectStore()->MarkPendingDestroy(Candidate.Handle());
	}

	TObjectPtr<UWorldSubsystem> Candidate;
	EEngineResult& InitializeRegistrationResult;
	EEngineResult& EndRegistrationResult;
	EObjectResult& InitializeStoreResult;
	EObjectResult& EndStoreResult;
};

/** Motivation: Engine environment with enough slots for subsystem, actor, rollback, and second-World cases. */
using FSubsystemEnvironment = FEngineEnvironmentSlots16;

template<std::size_t MaxActors, std::size_t MaxSubsystems>
TObjectPtr<UWorld> MakeWorld(
	FSubsystemEnvironment& InEnvironment, FWorldActorRegistry<MaxActors>& InActors, FWorldSubsystemRegistry<MaxSubsystems>& InSubsystems) noexcept
{
	return InEnvironment.CreateObject<UWorld>(MicroWorld::Engine::UWorldClassId, InActors.MakeReference(), InSubsystems.MakeReference());
}

template<typename TSubsystem>
TObjectPtr<TSubsystem> MakeSubsystem(
	FSubsystemEnvironment& InEnvironment,
	const FTypeId InTypeId,
	const char* InName,
	FSequenceCounter& InSequence,
	FSubsystemEventState& InState) noexcept
{
	return InEnvironment.CreateDerivedObject<TSubsystem>(InTypeId, InName, InSequence, InState);
}

/**
 * Motivation: Register one derived subsystem and query exact, absent, and ancestor types.
 * Responsibilities: Successful registration publishes one owner and exact lookup never broadens to ancestry.
 */
MW_TEST_CASE(EngineWorldSubsystemRegistersAndExactLookupSucceeds)
{
	FSubsystemEnvironment Environment{};
	FWorldActorRegistry<1> Actors{};
	FWorldSubsystemRegistry<2> Subsystems{};
	FSequenceCounter Sequence{};
	FSubsystemEventState State{};
	const TObjectPtr<UWorld> World = MakeWorld(Environment, Actors, Subsystems);
	const TObjectPtr<FFirstWorldSubsystem> Subsystem =
		MakeSubsystem<FFirstWorldSubsystem>(Environment, FirstSubsystemTypeId, "FirstWorldSubsystem", Sequence, State);

	const EEngineResult Result = World.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{Subsystem});

	MW_EXPECT_EQ(Test, EEngineResult::Success, Result, "A live same-store subsystem registers successfully");
	MW_EXPECT_EQ(Test, std::size_t{1}, Subsystems.GetCount(), "Successful registration publishes one registry entry");
	MW_EXPECT_TRUE(Test, Subsystem.Get()->HasAssignedWorld(), "Successful registration assigns the weak World parent");
	MW_EXPECT_TRUE(Test, Subsystem.Get()->GetWorld() == World.Get(), "The weak World parent resolves to the registering World");
	MW_EXPECT_TRUE(Test, World.Get()->GetSubsystem<FFirstWorldSubsystem>() == Subsystem.Get(), "Exact lookup returns the registered instance");
	MW_EXPECT_TRUE(Test, World.Get()->GetSubsystem<FSecondWorldSubsystem>() == nullptr, "A missing exact type returns null");
	MW_EXPECT_TRUE(Test, World.Get()->GetSubsystem<UWorldSubsystem>() == nullptr, "Ancestor lookup does not match a derived instance");
}

/**
 * Motivation: Attempt repeated instance and repeated exact-type registration.
 * Responsibilities: Both duplicate forms are rejected without ownership or count changes.
 */
MW_TEST_CASE(EngineWorldSubsystemDuplicateInstanceAndTypeRejected)
{
	FSubsystemEnvironment Environment{};
	FWorldActorRegistry<1> Actors{};
	FWorldSubsystemRegistry<2> Subsystems{};
	FSequenceCounter Sequence{};
	FSubsystemEventState FirstState{};
	FSubsystemEventState SecondState{};
	const TObjectPtr<UWorld> World = MakeWorld(Environment, Actors, Subsystems);
	const TObjectPtr<FFirstWorldSubsystem> First =
		MakeSubsystem<FFirstWorldSubsystem>(Environment, FirstSubsystemTypeId, "FirstWorldSubsystem", Sequence, FirstState);
	const TObjectPtr<FFirstWorldSubsystem> Second =
		MakeSubsystem<FFirstWorldSubsystem>(Environment, FirstSubsystemTypeId, "FirstWorldSubsystem", Sequence, SecondState);
	MW_EXPECT_EQ(Test, EEngineResult::Success, World.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{First}), "Setup registration succeeds");

	const EEngineResult SameInstance = World.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{First});
	const EEngineResult SameType = World.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{Second});

	MW_EXPECT_EQ(Test, EEngineResult::Duplicate, SameInstance, "The same managed instance is rejected as duplicate");
	MW_EXPECT_EQ(Test, EEngineResult::Duplicate, SameType, "A second live exact type is rejected as duplicate");
	MW_EXPECT_EQ(Test, std::size_t{1}, Subsystems.GetCount(), "Duplicate attempts leave occupancy unchanged");
	MW_EXPECT_TRUE(Test, !Second.Get()->HasAssignedWorld(), "Rejected same-type candidate remains unowned");
}

/**
 * Motivation: Fill a one-slot registry and exercise a zero-capacity registry.
 * Responsibilities: Both capacity boundaries reject transactionally and report CapacityExceeded.
 */
MW_TEST_CASE(EngineWorldSubsystemFullAndZeroCapacityRejected)
{
	FSubsystemEnvironment Environment{};
	FWorldActorRegistry<1> FullActors{};
	FWorldActorRegistry<1> ZeroActors{};
	FWorldSubsystemRegistry<1> FullSubsystems{};
	FWorldSubsystemRegistry<0> ZeroSubsystems{};
	FSequenceCounter Sequence{};
	FSubsystemEventState FirstState{};
	FSubsystemEventState SecondState{};
	FSubsystemEventState ZeroState{};
	const TObjectPtr<UWorld> FullWorld = MakeWorld(Environment, FullActors, FullSubsystems);
	const TObjectPtr<UWorld> ZeroWorld = MakeWorld(Environment, ZeroActors, ZeroSubsystems);
	const TObjectPtr<FFirstWorldSubsystem> First =
		MakeSubsystem<FFirstWorldSubsystem>(Environment, FirstSubsystemTypeId, "FirstWorldSubsystem", Sequence, FirstState);
	const TObjectPtr<FSecondWorldSubsystem> Second =
		MakeSubsystem<FSecondWorldSubsystem>(Environment, SecondSubsystemTypeId, "SecondWorldSubsystem", Sequence, SecondState);
	const TObjectPtr<FSecondWorldSubsystem> ZeroCandidate =
		MakeSubsystem<FSecondWorldSubsystem>(Environment, SecondSubsystemTypeId, "SecondWorldSubsystem", Sequence, ZeroState);
	MW_EXPECT_EQ(Test, EEngineResult::Success, FullWorld.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{First}), "One-slot setup succeeds");

	const EEngineResult FullResult = FullWorld.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{Second});
	const EEngineResult ZeroResult = ZeroWorld.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{ZeroCandidate});

	MW_EXPECT_EQ(Test, EEngineResult::CapacityExceeded, FullResult, "A full registry rejects another exact type");
	MW_EXPECT_EQ(Test, EEngineResult::CapacityExceeded, ZeroResult, "A zero-capacity registry rejects its first candidate");
	MW_EXPECT_EQ(Test, std::size_t{1}, FullSubsystems.GetCount(), "Full rejection preserves its existing entry");
	MW_EXPECT_EQ(Test, std::size_t{0}, ZeroSubsystems.GetCount(), "Zero-capacity rejection publishes nothing");
	MW_EXPECT_TRUE(Test, !Second.Get()->HasAssignedWorld(), "Full rejection leaves the candidate unowned");
	MW_EXPECT_TRUE(Test, !ZeroCandidate.Get()->HasAssignedWorld(), "Zero-capacity rejection leaves the candidate unowned");
}

/**
 * Motivation: Attempt empty, reclaimed, and foreign subsystem registration.
 * Responsibilities: Reference and store validation return explicit results before publishing ownership.
 */
MW_TEST_CASE(EngineWorldSubsystemInvalidStaleAndCrossStoreRejected)
{
	FSubsystemEnvironment Environment{};
	FSubsystemEnvironment ForeignEnvironment{};
	FWorldActorRegistry<1> Actors{};
	FWorldSubsystemRegistry<3> Subsystems{};
	FSequenceCounter Sequence{};
	FSubsystemEventState StaleState{};
	FSubsystemEventState ForeignState{};
	const TObjectPtr<UWorld> World = MakeWorld(Environment, Actors, Subsystems);
	const TObjectPtr<FFirstWorldSubsystem> Stale =
		MakeSubsystem<FFirstWorldSubsystem>(Environment, FirstSubsystemTypeId, "FirstWorldSubsystem", Sequence, StaleState);
	const TObjectPtr<FFirstWorldSubsystem> Foreign =
		MakeSubsystem<FFirstWorldSubsystem>(ForeignEnvironment, FirstSubsystemTypeId, "FirstWorldSubsystem", Sequence, ForeignState);
	const EObjectResult MarkResult = Environment.GetStore().MarkPendingDestroy(Stale.Handle());
	const EObjectResult DestroyResult = Environment.GetStore().ApplyPendingDestroy(1).Result;

	const EEngineResult EmptyResult = World.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{});
	const EEngineResult StaleResult = World.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{Stale});
	const EEngineResult ForeignResult = World.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{Foreign});

	MW_EXPECT_EQ(Test, EObjectResult::Success, MarkResult, "Stale setup marks the candidate");
	MW_EXPECT_EQ(Test, EObjectResult::Success, DestroyResult, "Stale setup reclaims the candidate");
	MW_EXPECT_EQ(Test, EEngineResult::InvalidReference, EmptyResult, "An empty reference is rejected explicitly");
	MW_EXPECT_EQ(Test, EEngineResult::InvalidReference, StaleResult, "A stale reference is rejected explicitly");
	MW_EXPECT_EQ(Test, EEngineResult::CrossStore, ForeignResult, "A foreign-store reference is rejected explicitly");
	MW_EXPECT_EQ(Test, std::size_t{0}, Subsystems.GetCount(), "All rejected references leave occupancy unchanged");
	MW_EXPECT_TRUE(Test, !Foreign.Get()->HasAssignedWorld(), "The foreign candidate remains unowned");
}

/**
 * Motivation: Register one subsystem against two live Worlds.
 * Responsibilities: The first owner remains canonical and the second registry stays empty.
 */
MW_TEST_CASE(EngineWorldSubsystemSecondWorldOwnershipRejected)
{
	FSubsystemEnvironment Environment{};
	FWorldActorRegistry<1> FirstActors{};
	FWorldActorRegistry<1> SecondActors{};
	FWorldSubsystemRegistry<1> FirstSubsystems{};
	FWorldSubsystemRegistry<1> SecondSubsystems{};
	FSequenceCounter Sequence{};
	FSubsystemEventState State{};
	const TObjectPtr<UWorld> FirstWorld = MakeWorld(Environment, FirstActors, FirstSubsystems);
	const TObjectPtr<UWorld> SecondWorld = MakeWorld(Environment, SecondActors, SecondSubsystems);
	const TObjectPtr<FFirstWorldSubsystem> Subsystem =
		MakeSubsystem<FFirstWorldSubsystem>(Environment, FirstSubsystemTypeId, "FirstWorldSubsystem", Sequence, State);
	MW_EXPECT_EQ(Test, EEngineResult::Success, FirstWorld.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{Subsystem}), "First owner succeeds");

	const EEngineResult Result = SecondWorld.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{Subsystem});

	MW_EXPECT_EQ(Test, EEngineResult::AlreadyOwned, Result, "A second World cannot own the same subsystem");
	MW_EXPECT_TRUE(Test, Subsystem.Get()->GetWorld() == FirstWorld.Get(), "Rejected second ownership preserves the first parent");
	MW_EXPECT_EQ(Test, std::size_t{0}, SecondSubsystems.GetCount(), "The second registry remains empty");
}

/**
 * Motivation: Attempt registration after startup and during active collection.
 * Responsibilities: Both mutation locks reject before registry or parent state changes.
 */
MW_TEST_CASE(EngineWorldSubsystemRegistrationAfterBeginAndDuringCollectionRejected)
{
	FSubsystemEnvironment Environment{};
	FWorldActorRegistry<1> BegunActors{};
	FWorldActorRegistry<1> CollectingActors{};
	FWorldSubsystemRegistry<1> BegunSubsystems{};
	FWorldSubsystemRegistry<1> CollectingSubsystems{};
	FSequenceCounter Sequence{};
	FSubsystemEventState BegunState{};
	FSubsystemEventState CollectingState{};
	const TObjectPtr<UWorld> BegunWorld = MakeWorld(Environment, BegunActors, BegunSubsystems);
	const TObjectPtr<UWorld> CollectingWorld = MakeWorld(Environment, CollectingActors, CollectingSubsystems);
	const TObjectPtr<FFirstWorldSubsystem> BegunCandidate =
		MakeSubsystem<FFirstWorldSubsystem>(Environment, FirstSubsystemTypeId, "FirstWorldSubsystem", Sequence, BegunState);
	const TObjectPtr<FSecondWorldSubsystem> CollectingCandidate =
		MakeSubsystem<FSecondWorldSubsystem>(Environment, SecondSubsystemTypeId, "SecondWorldSubsystem", Sequence, CollectingState);
	FCollectorFixture CollectorFixture{Environment.GetStore()};
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BegunWorld.Get()->BeginPlay(BaselineTimeMilliseconds), "World startup succeeds");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, CollectorFixture.GetCollector().RequestCollection(), "Collection becomes active");

	const EEngineResult BegunResult = BegunWorld.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{BegunCandidate});
	const EEngineResult CollectionResult = CollectingWorld.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{CollectingCandidate});

	MW_EXPECT_EQ(Test, EEngineResult::LifecycleLocked, BegunResult, "Registration closes after BeginPlay");
	MW_EXPECT_EQ(Test, EEngineResult::LifecycleLocked, CollectionResult, "Active collection blocks registration");
	MW_EXPECT_EQ(Test, std::size_t{0}, BegunSubsystems.GetCount(), "Post-begin rejection publishes nothing");
	MW_EXPECT_EQ(Test, std::size_t{0}, CollectingSubsystems.GetCount(), "Collection rejection publishes nothing");
}

/**
 * Motivation: Let an actor query the service during BeginPlay and EndPlay.
 * Responsibilities: Subsystem readiness brackets the complete actor lifecycle.
 */
MW_TEST_CASE(EngineWorldSubsystemActorResolvesServiceDuringBeginPlay)
{
	FSubsystemEnvironment Environment{};
	FWorldActorRegistry<1> Actors{};
	FWorldSubsystemRegistry<1> Subsystems{};
	FSequenceCounter Sequence{};
	FSubsystemEventState SubsystemState{};
	FLookupActorState ActorState{};
	const TObjectPtr<UWorld> World = MakeWorld(Environment, Actors, Subsystems);
	const TObjectPtr<FFirstWorldSubsystem> Subsystem =
		MakeSubsystem<FFirstWorldSubsystem>(Environment, FirstSubsystemTypeId, "FirstWorldSubsystem", Sequence, SubsystemState);
	const TObjectPtr<FSubsystemLookupActor> Actor =
		Environment.CreateDerivedObject<FSubsystemLookupActor>(LookupActorTypeId, "SubsystemLookupActor", Sequence, ActorState);
	MW_EXPECT_EQ(Test, EEngineResult::Success, World.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{Subsystem}), "Subsystem setup succeeds");
	MW_EXPECT_EQ(Test, EEngineResult::Success, World.Get()->RegisterActor(TObjectPtr<AActor>{Actor}), "Actor setup succeeds");

	const ERuntimeResult BeginResult = World.Get()->BeginPlay(BaselineTimeMilliseconds);
	const ERuntimeResult EndResult = World.Get()->EndPlay();

	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "World startup succeeds");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, EndResult, "World shutdown succeeds");
	MW_EXPECT_TRUE(Test, ActorState.bReadyDuringBegin, "Actor BeginPlay resolves the initialized exact subsystem");
	MW_EXPECT_TRUE(Test, ActorState.bReadyDuringEnd, "Actor EndPlay resolves the still-initialized exact subsystem");
	MW_EXPECT_TRUE(Test, !SubsystemState.bIsReady, "Subsystem is no longer ready after actor shutdown");
}

/**
 * Motivation: Attempt World and object-store mutation from Initialize and Deinitialize.
 * Responsibilities: Both lifecycle locks reject mutation and preserve the candidate and registry.
 */
MW_TEST_CASE(EngineWorldSubsystemHooksRejectStructuralMutation)
{
	FSubsystemEnvironment Environment{};
	FWorldActorRegistry<1> Actors{};
	FWorldSubsystemRegistry<2> Subsystems{};
	FSequenceCounter Sequence{};
	FSubsystemEventState CandidateState{};
	EEngineResult InitializeResult = EEngineResult::Success;
	EEngineResult EndResult = EEngineResult::Success;
	EObjectResult InitializeStoreResult = EObjectResult::Success;
	EObjectResult EndStoreResult = EObjectResult::Success;
	const TObjectPtr<UWorld> World = MakeWorld(Environment, Actors, Subsystems);
	const TObjectPtr<FSecondWorldSubsystem> Candidate =
		MakeSubsystem<FSecondWorldSubsystem>(Environment, SecondSubsystemTypeId, "SecondWorldSubsystem", Sequence, CandidateState);
	const TObjectPtr<FMutationAttemptSubsystem> Mutator = Environment.CreateDerivedObject<FMutationAttemptSubsystem>(
		MutationSubsystemTypeId,
		"MutationAttemptSubsystem",
		TObjectPtr<UWorldSubsystem>{Candidate},
		InitializeResult,
		EndResult,
		InitializeStoreResult,
		EndStoreResult);
	MW_EXPECT_EQ(Test, EEngineResult::Success, World.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{Mutator}), "Mutator setup succeeds");

	const ERuntimeResult BeginResult = World.Get()->BeginPlay(BaselineTimeMilliseconds);
	const ERuntimeResult ShutdownResult = World.Get()->EndPlay();
	const UWorldSubsystem* CandidateObject = Candidate.Get();

	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "World startup completes despite rejected reentry");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ShutdownResult, "World shutdown completes despite rejected reentry");
	MW_EXPECT_EQ(Test, EEngineResult::LifecycleLocked, InitializeResult, "Initialize cannot register another subsystem");
	MW_EXPECT_EQ(Test, EEngineResult::LifecycleLocked, EndResult, "Deinitialize cannot register another subsystem");
	MW_EXPECT_EQ(Test, EObjectResult::LifecycleLocked, InitializeStoreResult, "Initialize cannot mark a managed object pending destroy");
	MW_EXPECT_EQ(Test, EObjectResult::LifecycleLocked, EndStoreResult, "Deinitialize cannot mark a managed object pending destroy");
	MW_EXPECT_EQ(Test, std::size_t{1}, Subsystems.GetCount(), "Rejected hook mutation leaves occupancy unchanged");
	MW_EXPECT_TRUE(Test, CandidateObject != nullptr, "Rejected store mutation keeps the candidate live");
	MW_EXPECT_TRUE(Test, CandidateObject == nullptr || !CandidateObject->HasAssignedWorld(), "Rejected hook candidate remains unowned");
	MW_EXPECT_TRUE(Test, CandidateObject == nullptr || !CandidateObject->IsPendingDestroy(), "Rejected store mutation does not set pending destroy");
}

/**
 * Motivation: Exercise both subsystem-aware and legacy World construction.
 * Responsibilities: External storage remains usable while legacy Worlds reject registration safely.
 */
MW_TEST_CASE(EngineWorldSubsystemExternalRegistryAndLegacyWorldLifetimesHold)
{
	FSubsystemEnvironment Environment{};
	FWorldActorRegistry<1> ExternalActors{};
	FWorldActorRegistry<1> LegacyActors{};
	FWorldSubsystemRegistry<1> ExternalSubsystems{};
	FSequenceCounter Sequence{};
	FSubsystemEventState ExternalState{};
	FSubsystemEventState LegacyState{};
	const TObjectPtr<UWorld> ExternalWorld = MakeWorld(Environment, ExternalActors, ExternalSubsystems);
	const TObjectPtr<UWorld> LegacyWorld = Environment.CreateObject<UWorld>(MicroWorld::Engine::UWorldClassId, LegacyActors.MakeReference());
	const TObjectPtr<FFirstWorldSubsystem> ExternalCandidate =
		MakeSubsystem<FFirstWorldSubsystem>(Environment, FirstSubsystemTypeId, "FirstWorldSubsystem", Sequence, ExternalState);
	const TObjectPtr<FSecondWorldSubsystem> LegacyCandidate =
		MakeSubsystem<FSecondWorldSubsystem>(Environment, SecondSubsystemTypeId, "SecondWorldSubsystem", Sequence, LegacyState);

	const EEngineResult ExternalResult = ExternalWorld.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{ExternalCandidate});
	const EEngineResult LegacyResult = LegacyWorld.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{LegacyCandidate});

	MW_EXPECT_EQ(Test, EEngineResult::Success, ExternalResult, "Subsystem-aware World publishes to caller-owned storage");
	MW_EXPECT_EQ(Test, EEngineResult::CapacityExceeded, LegacyResult, "Legacy World safely rejects subsystem registration");
	MW_EXPECT_EQ(Test, std::size_t{1}, ExternalSubsystems.GetCount(), "External storage remains observable after publish");
	MW_EXPECT_TRUE(Test, ExternalWorld.Get()->GetSubsystem<FFirstWorldSubsystem>() == ExternalCandidate.Get(), "External registry remains valid");
	MW_EXPECT_TRUE(Test, !LegacyCandidate.Get()->HasAssignedWorld(), "Legacy rejection leaves the candidate unowned");
}

/**
 * Motivation: Bound the portable zero-capacity metadata and constrained World slot contract.
 * Responsibilities: Capacity zero stores no subsystem entries and UWorld remains within 256 bytes.
 */
MW_TEST_CASE(EngineWorldSubsystemMemoryOverheadStaysWithinBudget)
{
	constexpr std::size_t ZeroRegistryMetadataBudget =
		sizeof(std::array<TObjectPtr<UWorldSubsystem>, 0>) + sizeof(std::size_t) + sizeof(bool) + alignof(std::max_align_t);

	MW_EXPECT_EQ(Test, std::size_t{0}, FDefaultEngineTraits::MaxSubsystems, "Default subsystem pointer capacity remains opt-in");
	MW_EXPECT_EQ(Test, std::size_t{0}, FWorldSubsystemRegistry<0>::GetCapacity(), "Zero-capacity registry exposes no entries");
	MW_EXPECT_TRUE(Test, sizeof(FWorldSubsystemRegistry<0>) <= ZeroRegistryMetadataBudget, "Zero-capacity registry stays metadata-only");
	MW_EXPECT_TRUE(Test, sizeof(UWorld) <= std::size_t{256}, "UWorld remains compatible with constrained 256-byte slots");
}

/**
 * Motivation: Run two subsystems and one actor through a complete lifecycle.
 * Responsibilities: Initialization is forward before actor begin; deinitialization is reverse after actor end.
 */
MW_TEST_CASE(EngineWorldSubsystemLifecycleBracketsActorsInDeterministicOrder)
{
	FSubsystemEnvironment Environment{};
	FWorldActorRegistry<1> Actors{};
	FWorldSubsystemRegistry<2> Subsystems{};
	FSequenceCounter Sequence{};
	FSubsystemEventState FirstState{};
	FSubsystemEventState SecondState{};
	FLookupActorState ActorState{};
	const TObjectPtr<UWorld> World = MakeWorld(Environment, Actors, Subsystems);
	const TObjectPtr<FFirstWorldSubsystem> First =
		MakeSubsystem<FFirstWorldSubsystem>(Environment, FirstSubsystemTypeId, "FirstWorldSubsystem", Sequence, FirstState);
	const TObjectPtr<FSecondWorldSubsystem> Second =
		MakeSubsystem<FSecondWorldSubsystem>(Environment, SecondSubsystemTypeId, "SecondWorldSubsystem", Sequence, SecondState);
	const TObjectPtr<FSubsystemLookupActor> Actor =
		Environment.CreateDerivedObject<FSubsystemLookupActor>(OrderingActorTypeId, "OrderingActor", Sequence, ActorState);
	MW_EXPECT_EQ(Test, EEngineResult::Success, World.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{First}), "First subsystem setup succeeds");
	MW_EXPECT_EQ(
		Test, EEngineResult::Success, World.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{Second}), "Second subsystem setup succeeds");
	MW_EXPECT_EQ(Test, EEngineResult::Success, World.Get()->RegisterActor(TObjectPtr<AActor>{Actor}), "Actor setup succeeds");

	const ERuntimeResult BeginResult = World.Get()->BeginPlay(BaselineTimeMilliseconds);
	const ERuntimeResult EndResult = World.Get()->EndPlay();
	const ERuntimeResult RepeatedEndResult = World.Get()->EndPlay();

	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "World startup succeeds");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, EndResult, "World shutdown succeeds");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, RepeatedEndResult, "Repeated World shutdown remains idempotent");
	MW_EXPECT_TRUE(Test, FirstState.InitializeOrder < SecondState.InitializeOrder, "Subsystems initialize in registration order");
	MW_EXPECT_TRUE(Test, SecondState.InitializeOrder < ActorState.BeginOrder, "Every subsystem initializes before actor BeginPlay");
	MW_EXPECT_TRUE(Test, ActorState.EndOrder < SecondState.DeinitializeOrder, "Actor EndPlay precedes subsystem shutdown");
	MW_EXPECT_TRUE(Test, SecondState.DeinitializeOrder < FirstState.DeinitializeOrder, "Subsystems deinitialize in reverse order");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, FirstState.DeinitializeCount, "Repeated shutdown does not deinitialize the first subsystem again");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, SecondState.DeinitializeCount, "Repeated shutdown does not deinitialize the second subsystem again");
}

/**
 * Motivation: Reclaim the later registered subsystem before World startup.
 * Responsibilities: Startup failure deinitializes the already initialized prefix in reverse.
 */
MW_TEST_CASE(EngineWorldSubsystemBeginFailureRollsBackInitializedPrefix)
{
	FSubsystemEnvironment Environment{};
	FWorldActorRegistry<1> Actors{};
	FWorldSubsystemRegistry<2> Subsystems{};
	FSequenceCounter Sequence{};
	FSubsystemEventState FirstState{};
	FSubsystemEventState SecondState{};
	const TObjectPtr<UWorld> World = MakeWorld(Environment, Actors, Subsystems);
	const TObjectPtr<FFirstWorldSubsystem> First =
		MakeSubsystem<FFirstWorldSubsystem>(Environment, FirstSubsystemTypeId, "FirstWorldSubsystem", Sequence, FirstState);
	const TObjectPtr<FSecondWorldSubsystem> Second =
		MakeSubsystem<FSecondWorldSubsystem>(Environment, SecondSubsystemTypeId, "SecondWorldSubsystem", Sequence, SecondState);
	MW_EXPECT_EQ(Test, EEngineResult::Success, World.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{First}), "First subsystem setup succeeds");
	MW_EXPECT_EQ(
		Test, EEngineResult::Success, World.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{Second}), "Second subsystem setup succeeds");
	MW_EXPECT_EQ(Test, EObjectResult::Success, Environment.GetStore().MarkPendingDestroy(Second.Handle()), "Later subsystem is marked stale");
	MW_EXPECT_EQ(Test, EObjectResult::Success, Environment.GetStore().ApplyPendingDestroy(1).Result, "Later subsystem is reclaimed");

	const ERuntimeResult Result = World.Get()->BeginPlay(BaselineTimeMilliseconds);

	MW_EXPECT_EQ(Test, ERuntimeResult::InvalidLifecycle, Result, "A stale registered subsystem fails startup");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, FirstState.InitializeCount, "The live prefix initializes before the failure");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, FirstState.DeinitializeCount, "The initialized prefix rolls back exactly once");
	MW_EXPECT_TRUE(Test, !FirstState.bIsReady, "Rollback leaves the initialized prefix not ready");
}

/**
 * Motivation: Fail actor startup through a stale registered component after subsystem initialization.
 * Responsibilities: Actor failure deinitializes every initialized subsystem.
 */
MW_TEST_CASE(EngineWorldSubsystemActorBeginFailureDeinitializesSubsystems)
{
	FSubsystemEnvironment Environment{};
	FWorldActorRegistry<1> Actors{};
	FWorldSubsystemRegistry<1> Subsystems{};
	FSequenceCounter Sequence{};
	FSubsystemEventState SubsystemState{};
	FLookupActorState ActorState{};
	const TObjectPtr<UWorld> World = MakeWorld(Environment, Actors, Subsystems);
	const TObjectPtr<FFirstWorldSubsystem> Subsystem =
		MakeSubsystem<FFirstWorldSubsystem>(Environment, FirstSubsystemTypeId, "FirstWorldSubsystem", Sequence, SubsystemState);
	const TObjectPtr<FSubsystemLookupActor> Actor =
		Environment.CreateDerivedObject<FSubsystemLookupActor>(FailingActorTypeId, "FailingActor", Sequence, ActorState);
	const TObjectPtr<FPlainComponent> Component = Environment.CreateDerivedObject<FPlainComponent>(PlainComponentTypeId, "PlainComponent");
	MW_EXPECT_EQ(Test, EEngineResult::Success, World.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{Subsystem}), "Subsystem setup succeeds");
	MW_EXPECT_EQ(Test, EEngineResult::Success, Actor.Get()->RegisterComponent(TObjectPtr<UActorComponent>{Component}), "Component setup succeeds");
	MW_EXPECT_EQ(Test, EEngineResult::Success, World.Get()->RegisterActor(TObjectPtr<AActor>{Actor}), "Actor setup succeeds");
	MW_EXPECT_EQ(Test, EObjectResult::Success, Environment.GetStore().MarkPendingDestroy(Component.Handle()), "Component is marked stale");
	MW_EXPECT_EQ(Test, EObjectResult::Success, Environment.GetStore().ApplyPendingDestroy(1).Result, "Component is reclaimed");

	const ERuntimeResult Result = World.Get()->BeginPlay(BaselineTimeMilliseconds);

	MW_EXPECT_EQ(Test, ERuntimeResult::InvalidLifecycle, Result, "Stale actor graph fails World startup");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, SubsystemState.InitializeCount, "Subsystem initializes before actor startup");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, SubsystemState.DeinitializeCount, "Actor startup failure deinitializes the subsystem");
	MW_EXPECT_TRUE(Test, !SubsystemState.bIsReady, "Actor startup rollback leaves the subsystem not ready");
}

/**
 * Motivation: Root only the World and collect the managed graph.
 * Responsibilities: World tracing keeps its registered subsystem live without tracing the weak parent upward.
 */
MW_TEST_CASE(EngineWorldSubsystemWorldTracingKeepsChildReachable)
{
	FSubsystemEnvironment Environment{};
	FWorldActorRegistry<1> Actors{};
	FWorldSubsystemRegistry<1> Subsystems{};
	FSequenceCounter Sequence{};
	FSubsystemEventState State{};
	const TObjectPtr<UWorld> World = MakeWorld(Environment, Actors, Subsystems);
	const TObjectPtr<FFirstWorldSubsystem> Subsystem =
		MakeSubsystem<FFirstWorldSubsystem>(Environment, FirstSubsystemTypeId, "FirstWorldSubsystem", Sequence, State);
	MW_EXPECT_EQ(Test, EEngineResult::Success, World.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{Subsystem}), "Subsystem setup succeeds");
	TStrongObjectPtr<UWorld> WorldRoot = Environment.MakeRoot(World);
	FCollectorFixture CollectorFixture{Environment.GetStore()};

	const ERuntimeResult RequestResult = CollectorFixture.GetCollector().RequestCollection();
	const MicroWorld::Engine::FGarbageCollectionResult Collection = CollectorFixture.GetCollector().CollectFull();

	MW_EXPECT_EQ(Test, ERuntimeResult::Success, RequestResult, "Rooted graph accepts collection");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Collection.Result, "Rooted graph collection succeeds");
	MW_EXPECT_TRUE(Test, static_cast<bool>(WorldRoot), "World root remains live");
	MW_EXPECT_TRUE(Test, Subsystem.Get() != nullptr, "World tracing keeps the subsystem live");
	MW_EXPECT_TRUE(Test, Subsystem.Get()->GetWorld() == World.Get(), "The weak parent still resolves after collection");
}

/**
 * Motivation: Root only a registered subsystem and collect the managed graph.
 * Responsibilities: The weak parent does not keep its World alive and safely expires after collection.
 */
MW_TEST_CASE(EngineWorldSubsystemWeakParentDoesNotKeepWorldReachable)
{
	FSubsystemEnvironment Environment{};
	FWorldActorRegistry<1> Actors{};
	FWorldSubsystemRegistry<1> Subsystems{};
	FSequenceCounter Sequence{};
	FSubsystemEventState State{};
	const TObjectPtr<UWorld> World = MakeWorld(Environment, Actors, Subsystems);
	const TObjectPtr<FFirstWorldSubsystem> Subsystem =
		MakeSubsystem<FFirstWorldSubsystem>(Environment, FirstSubsystemTypeId, "FirstWorldSubsystem", Sequence, State);
	MW_EXPECT_EQ(Test, EEngineResult::Success, World.Get()->RegisterSubsystem(TObjectPtr<UWorldSubsystem>{Subsystem}), "Subsystem setup succeeds");
	TStrongObjectPtr<FFirstWorldSubsystem> SubsystemRoot = Environment.MakeRoot(Subsystem);
	FCollectorFixture CollectorFixture{Environment.GetStore()};

	const ERuntimeResult RequestResult = CollectorFixture.GetCollector().RequestCollection();
	const MicroWorld::Engine::FGarbageCollectionResult Collection = CollectorFixture.GetCollector().CollectFull();

	MW_EXPECT_EQ(Test, ERuntimeResult::Success, RequestResult, "Subsystem-rooted graph accepts collection");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Collection.Result, "Subsystem-rooted graph collection succeeds");
	MW_EXPECT_TRUE(Test, static_cast<bool>(SubsystemRoot), "The rooted subsystem remains live");
	MW_EXPECT_TRUE(Test, World.Get() == nullptr, "The weak parent does not keep the World live");
	MW_EXPECT_TRUE(
		Test, SubsystemRoot.Get() == nullptr || SubsystemRoot.Get()->GetWorld() == nullptr, "The subsystem observes its expired weak World parent");
}

/**
 * Motivation: Gives host tests measured capacity for one subsystem and one deferred actor.
 * Responsibilities: Opt into only the class, object, root, actor, and subsystem slots these tests consume.
 * Example:
 *   using FHost = TEngine<FSubsystemHostTraits>;
 */
struct FSubsystemHostTraits : FDefaultEngineTraits
{
	static constexpr std::size_t MaxClasses = 6;
	static constexpr std::size_t MaxSubsystems = 1;
	static constexpr std::size_t MaxObjects = 3;
	static constexpr std::size_t SlotSizeBytes = 256;
	static constexpr std::size_t MaxRoots = 1;
	static constexpr std::size_t MaxActors = 1;
};

/** Motivation: Host shape used by derived-parent and deferred-order tests. */
using FSubsystemHost = TEngine<FSubsystemHostTraits>;

/**
 * Motivation: Records whether a typed deferred actor observes the ready subsystem at BeginPlay.
 * Responsibilities: Query its owning World without retaining a raw service pointer.
 * Example:
 *   FDeferredLookupActor Actor(&State);
 */
class FDeferredLookupActor final : public AActor
{
public:
	FDeferredLookupActor(FSequenceCounter* InSequence, FLookupActorState* InState) noexcept : Sequence(InSequence), State(InState) {}

protected:
	void BeginPlay() noexcept override
	{
		UWorld* World = GetOwnerWorld();
		FFirstWorldSubsystem* Subsystem = World == nullptr ? nullptr : World->GetSubsystem<FFirstWorldSubsystem>();
		State->bReadyDuringBegin = Subsystem != nullptr && Subsystem->IsReady();
		State->BeginOrder = Sequence->Next();
	}

private:
	FSequenceCounter* Sequence;
	FLookupActorState* State;
};

/**
 * Motivation: Register a derived subsystem through TEngine and inspect the constructed descriptor.
 * Responsibilities: TEngine assigns the canonical UWorldSubsystem base as the derived parent.
 */
MW_TEST_CASE(EngineHostRegistersSubsystemBaseAndDerivedParent)
{
	FSequenceCounter Sequence{};
	FSubsystemEventState State{};
	FSubsystemHost Host{MicroWorld::Engine::FGarbageCollectionBudget{1, 3, 4}};

	const EObjectResult RegisterResult = Host.RegisterClass<FFirstWorldSubsystem>(FirstSubsystemTypeId, "FirstWorldSubsystem");
	const TObjectPtr<UWorld> World = Host.CreateWorld();
	const MicroWorld::Engine::TObjectCreationResult<FFirstWorldSubsystem> Creation =
		Host.CreateObject<FFirstWorldSubsystem>(FirstSubsystemTypeId, Sequence, State);
	const MicroWorld::Engine::FClassDescriptor* Parent =
		Creation.Object.Get() == nullptr ? nullptr : Creation.Object.Get()->GetClassDescriptor().Parent;

	MW_EXPECT_EQ(Test, EObjectResult::Success, RegisterResult, "TEngine registers a derived subsystem descriptor");
	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "Host constructs its World with subsystem storage");
	MW_EXPECT_EQ(Test, EObjectResult::Success, Creation.Result, "Host constructs the registered subsystem type");
	MW_EXPECT_TRUE(Test, Parent != nullptr, "Derived subsystem descriptor has a registered parent");
	MW_EXPECT_EQ(Test, MicroWorld::Engine::UWorldSubsystemClassId, Parent->TypeId, "Derived subsystem parent is UWorldSubsystem");
}

/**
 * Motivation: Queue a typed actor before play and begin the Host.
 * Responsibilities: Subsystem initialization precedes the deferred actor's public BeginPlay observation.
 */
MW_TEST_CASE(EngineWorldSubsystemInitializesBeforeDeferredActorBegins)
{
	FSequenceCounter Sequence{};
	FSubsystemEventState SubsystemState{};
	FLookupActorState ActorState{};
	FSubsystemHost Host{MicroWorld::Engine::FGarbageCollectionBudget{1, 3, 4}};
	MW_EXPECT_EQ(
		Test,
		EObjectResult::Success,
		Host.RegisterClass<FFirstWorldSubsystem>(FirstSubsystemTypeId, "FirstWorldSubsystem"),
		"Subsystem descriptor setup succeeds");
	MW_EXPECT_EQ(
		Test,
		EObjectResult::Success,
		Host.RegisterClass<FDeferredLookupActor>(DeferredLookupActorTypeId, "DeferredLookupActor"),
		"Deferred actor descriptor setup succeeds");
	const TObjectPtr<UWorld> World = Host.CreateWorld();
	const MicroWorld::Engine::TObjectCreationResult<FFirstWorldSubsystem> SubsystemCreation =
		Host.CreateObject<FFirstWorldSubsystem>(FirstSubsystemTypeId, Sequence, SubsystemState);
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		Host.GetWorld().RegisterSubsystem(TObjectPtr<UWorldSubsystem>{SubsystemCreation.Object}),
		"Subsystem registration succeeds");
	const MicroWorld::Engine::FActorSpawnRequest Request = Host.GetWorld().SpawnActor<FDeferredLookupActor>(&Sequence, &ActorState);

	const ERuntimeResult BeginResult = Host.BeginPlay(BaselineTimeMilliseconds);

	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "Host World exists before startup");
	MW_EXPECT_EQ(
		Test, MicroWorld::Engine::EActorSpawnState::Spawned, Host.GetWorld().GetSpawnStatus(Request.Handle).State, "Deferred actor becomes live");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "Host startup drains the deferred request");
	MW_EXPECT_TRUE(Test, ActorState.bReadyDuringBegin, "Deferred actor resolves the initialized subsystem");
	MW_EXPECT_TRUE(Test, SubsystemState.InitializeOrder < ActorState.BeginOrder, "Subsystem initializes before deferred actor BeginPlay");
}

} // namespace
