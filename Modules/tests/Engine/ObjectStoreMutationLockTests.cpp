#include "TestSupport.h"
#include "ObjectStoreTestHelpers.h"

#include <MicroWorld/Engine/ClassRegistry.h>
#include <MicroWorld/Engine/ClassRegistryView.h>
#include <MicroWorld/Engine/GarbageCollectionBudget.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/GarbageCollectorStorage.h>
#include <MicroWorld/Engine/ObjectCreationResult.h>
#include <MicroWorld/Engine/ObjectMutationResult.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/ObjectStoreStats.h>
#include <MicroWorld/Engine/ObjectStoreStorage.h>
#include <MicroWorld/Engine/StrongObjectPtr.h>
#include <MicroWorld/Engine/WeakObjectPtr.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace
{

using namespace ::MicroWorld::Tests;

/**
 * Motivation: Fill the store's single slot with one object and attempt to construct a second object.
 * Responsibilities: The second construction fails atomically with capacity exhaustion; the first object is not disturbed
 *   and no collection runs.
 */
MW_TEST_CASE(ObjectStoreCapacityFailureIsAtomicAndDoesNotCollect)
{
	// Arrange
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterTrackedDescriptor(Registry, Descriptor);
	TObjectStoreFixture<128, 16, 1, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FTrackedObject> FirstCreation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);

	// Act
	const TObjectCreationResult<FTrackedObject> SecondCreation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);

	// Assert
	const EObjectResult ExpectedSuccess = EObjectResult::Success;
	const EObjectResult ExpectedFailure = EObjectResult::CapacityExceeded;
	const std::uint32_t ExpectedConstructionCount = 1;
	const std::uint32_t ExpectedDestructionCount = 0;
	const std::uint32_t ExpectedOccupiedSlots = 1;
	const bool bFirstStillResolves = FirstCreation.Object.Get() != nullptr;
	const FObjectStoreStats StoreStats = Store.Stats();
	MW_EXPECT_EQ(Test, ExpectedSuccess, RegistrationResult, "The tracked class should register");
	MW_EXPECT_EQ(Test, ExpectedSuccess, FirstCreation.Result, "The first object should consume the available slot");
	MW_EXPECT_EQ(Test, ExpectedFailure, SecondCreation.Result, "A second object should report fixed capacity exhaustion");
	MW_EXPECT_EQ(Test, ExpectedConstructionCount, Lifetime.ConstructionCount, "Capacity rejection must not start another lifetime");
	MW_EXPECT_EQ(Test, ExpectedDestructionCount, Lifetime.DestructionCount, "Allocation failure must not trigger hidden collection");
	MW_EXPECT_EQ(Test, ExpectedOccupiedSlots, StoreStats.OccupiedSlots, "Capacity failure must preserve the live object");
	MW_EXPECT_TRUE(Test, bFirstStillResolves, "The original unrooted object should remain live until explicit collection");
}

/**
 * Motivation: Create one object, request pending destruction twice, and run the destruction barrier twice.
 * Responsibilities: The handle is hidden immediately and the repeated request is idempotent; BeginDestroy and the exact
 *   destructor each run exactly once.
 */
MW_TEST_CASE(ObjectStoreDeferredDestructionRunsLifecycleHooksOnce)
{
	// Arrange
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterTrackedDescriptor(Registry, Descriptor);
	TObjectStoreFixture<128, 16, 1, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FTrackedObject> Creation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);
	FTrackedObject* const RawObject = Creation.Object.Get();

	// Act
	const EObjectResult FirstPendingResult = Store.MarkPendingDestroy(Creation.Object.Handle());
	const EObjectResult SecondPendingResult = Store.MarkPendingDestroy(Creation.Object.Handle());
	const MicroWorld::Engine::FObjectMutationResult FirstBarrier = Store.ApplyPendingDestroy(1);
	const MicroWorld::Engine::FObjectMutationResult SecondBarrier = Store.ApplyPendingDestroy(1);

	// Assert
	const EObjectResult ExpectedSuccess = EObjectResult::Success;
	const EObjectResult ExpectedRepeatedResult = EObjectResult::AlreadyPendingDestroy;
	const std::uint32_t ExpectedOne = 1;
	const std::uint32_t ExpectedZero = 0;
	const bool bHiddenImmediately = Creation.Object.Get() == nullptr;
	const bool bPendingFlagWasVisible = RawObject != nullptr && RawObject->IsPendingDestroy();
	MW_EXPECT_EQ(Test, ExpectedSuccess, RegistrationResult, "The tracked class should register");
	MW_EXPECT_EQ(Test, ExpectedSuccess, Creation.Result, "The tracked object should be created");
	MW_EXPECT_EQ(Test, ExpectedSuccess, FirstPendingResult, "The first destruction request should succeed");
	MW_EXPECT_EQ(Test, ExpectedRepeatedResult, SecondPendingResult, "A repeated request should be explicitly idempotent");
	MW_EXPECT_TRUE(Test, bHiddenImmediately, "Pending destruction should make the handle non-resolvable immediately");
	MW_EXPECT_TRUE(Test, bPendingFlagWasVisible, "The still-alive object should expose pending state before the barrier");
	MW_EXPECT_EQ(Test, ExpectedOne, FirstBarrier.ObjectsDestroyed, "The first barrier should destroy the pending object");
	MW_EXPECT_EQ(Test, ExpectedZero, SecondBarrier.ObjectsDestroyed, "Later barriers must not repeat destruction");
	MW_EXPECT_EQ(Test, ExpectedOne, Lifetime.BeginDestroyCount, "BeginDestroy should run exactly once");
	MW_EXPECT_EQ(Test, ExpectedOne, Lifetime.DestructionCount, "The exact derived destructor should run exactly once");
}

/**
 * Motivation: Create two independent strong roots, attempt a third, then move the first root through construction
 *   and assignment.
 * Responsibilities: The third root fails with root capacity exhaustion; move construction and assignment each empty
 *   their source and transfer exactly one.
 */
MW_TEST_CASE(ObjectStoreStrongRootsAreIndependentAndMoveOnly)
{
	// Arrange
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterTrackedDescriptor(Registry, Descriptor);
	TObjectStoreFixture<128, 16, 1, 2> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FTrackedObject> Creation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);
	TStrongObjectPointerResult<FTrackedObject> FirstRoot = Store.MakeStrongObjectPtr(Creation.Object);
	TStrongObjectPointerResult<FTrackedObject> SecondRoot = Store.MakeStrongObjectPtr(Creation.Object);

	// Act
	const TStrongObjectPointerResult<FTrackedObject> RejectedRoot = Store.MakeStrongObjectPtr(Creation.Object);
	TStrongObjectPtr<FTrackedObject> MovedRoot(std::move(FirstRoot.Pointer));
	SecondRoot.Pointer = std::move(MovedRoot);

	// Assert
	const EObjectResult ExpectedSuccess = EObjectResult::Success;
	const EObjectResult ExpectedCapacityFailure = EObjectResult::RootCapacityExceeded;
	const std::uint32_t ExpectedActiveRoots = 1;
	const bool bFirstOwnerEmpty = FirstRoot.Pointer.Get() == nullptr;
	const bool bMovedFromOwnerEmpty = MovedRoot.Get() == nullptr;
	const bool bFinalOwnerResolves = SecondRoot.Pointer.Get() != nullptr;
	const FObjectStoreStats StoreStats = Store.Stats();
	MW_EXPECT_EQ(Test, ExpectedSuccess, RegistrationResult, "The tracked class should register");
	MW_EXPECT_EQ(Test, ExpectedSuccess, Creation.Result, "The rooted object should be created");
	MW_EXPECT_EQ(Test, ExpectedSuccess, FirstRoot.Result, "The first independent root should succeed");
	MW_EXPECT_EQ(Test, ExpectedSuccess, SecondRoot.Result, "A duplicate independent root should also succeed");
	MW_EXPECT_EQ(Test, ExpectedCapacityFailure, RejectedRoot.Result, "A third root should report fixed root capacity");
	MW_EXPECT_TRUE(Test, bFirstOwnerEmpty, "Move construction should empty the source owner");
	MW_EXPECT_TRUE(Test, bMovedFromOwnerEmpty, "Move assignment should empty its source owner");
	MW_EXPECT_TRUE(Test, bFinalOwnerResolves, "The transferred final token should continue to resolve");
	MW_EXPECT_EQ(Test, ExpectedActiveRoots, StoreStats.ActiveRoots, "Move assignment should release the replaced token exactly once");
}

/**
 * Motivation: Create a rooted object, mark it pending destruction, then attempt resolution, a new root, reset, and
 *   a stale root release.
 * Responsibilities: The pending object cannot resolve or gain a new root; the weak pointer expires; the stale release
 *   remains stale and no root token.
 */
MW_TEST_CASE(ObjectStorePendingObjectCannotBeResolvedOrResurrected)
{
	// Arrange
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterTrackedDescriptor(Registry, Descriptor);
	TObjectStoreFixture<128, 16, 1, 2> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FTrackedObject> Creation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);
	TWeakObjectPtr<FTrackedObject> WeakObject(Creation.Object);
	TStrongObjectPointerResult<FTrackedObject> StrongObject = Store.MakeStrongObjectPtr(Creation.Object);
	const FObjectHandle ObjectHandle = Creation.Object.Handle();

	// Act
	const EObjectResult PendingResult = Store.MarkPendingDestroy(ObjectHandle);
	const TStrongObjectPointerResult<FTrackedObject> RejectedRoot = Store.MakeStrongObjectPtr(Creation.Object);
	StrongObject.Pointer.Reset();
	const EObjectResult StaleReleaseResult = Store.RemoveRoot(ObjectHandle);

	// Assert
	const EObjectResult ExpectedSuccess = EObjectResult::Success;
	const EObjectResult ExpectedPending = EObjectResult::AlreadyPendingDestroy;
	const EObjectResult ExpectedStale = EObjectResult::StaleHandle;
	const std::uint32_t ExpectedActiveRoots = 0;
	const bool bTracedPointerHidden = Creation.Object.Get() == nullptr;
	const bool bWeakPointerExpired = WeakObject.IsExpired();
	const FObjectStoreStats StoreStats = Store.Stats();
	MW_EXPECT_EQ(Test, ExpectedSuccess, RegistrationResult, "The tracked class should register");
	MW_EXPECT_EQ(Test, ExpectedSuccess, Creation.Result, "The pending object should first be live");
	MW_EXPECT_EQ(Test, ExpectedSuccess, StrongObject.Result, "The live object should accept a root");
	MW_EXPECT_EQ(Test, ExpectedSuccess, PendingResult, "The object should enter pending state");
	MW_EXPECT_EQ(Test, ExpectedPending, RejectedRoot.Result, "Pending state should reject a new root token");
	MW_EXPECT_TRUE(Test, bTracedPointerHidden, "A traced pointer cannot resolve a pending object");
	MW_EXPECT_TRUE(Test, bWeakPointerExpired, "A weak pointer should expire as soon as destruction is pending");
	MW_EXPECT_EQ(Test, ExpectedStale, StaleReleaseResult, "Releasing an already removed token must remain stale");
	MW_EXPECT_EQ(Test, ExpectedActiveRoots, StoreStats.ActiveRoots, "Pending cleanup and reset must leave no root token");
}

/**
 * Motivation: Observation bundle captured after the adversarial destruction-reentry world runs its barrier. Every
 *   field is a copyable value so the store, collector, and fixture can be destroyed before the split
 *   tests assert on it — keeping each test isolated without duplicating the build sequence.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FDestructionReentryOutcome final
{
	/** Motivation: The publication result of the adversarial object before its barrier ran. */
	EObjectResult CreationResult{EObjectResult::Success};

	/** Motivation: One recorded result per mutation path BeginDestroy attempted during the barrier. */
	FReentryState Reentry{};

	/** Motivation: Detects any nested lifetime that escaped the mutation lock. */
	FObjectLifetimeState NestedLifetime{};

	/** Motivation: Final occupancy after the barrier reclaimed the destroyed slot. */
	FObjectStoreStats StoreStats{};
};

/**
 * Motivation: Builds the adversarial destruction-reentry world, drives it through its owning barrier, and returns
 *   the captured observations.
 * Responsibilities: Centralizing the build keeps both split tests isolated (each gets a fresh world) without duplicating
 *   the registry, fixture, collector, and barrier.
 */
FDestructionReentryOutcome RunDestructionReentryBarrierOnce() noexcept
{
	FObjectLifetimeState NestedLifetime{};
	FReentryState Reentry{};
	TClassRegistry<3> Registry;
	const FClassDescriptor NestedDescriptor = MakeClassDescriptor<FTrackedObject>(1, "NestedTracked");
	const FClassDescriptor OuterDescriptor = MakeClassDescriptor<FDestroyReentryObject>(2, "DestroyReentry");
	(void)Registry.Register(NestedDescriptor);
	(void)Registry.Register(OuterDescriptor);
	const FClassDescriptor* const RegisteredNestedDescriptor = Registry.Find(NestedDescriptor.TypeId);
	const FClassDescriptor* const RegisteredOuterDescriptor = Registry.Find(OuterDescriptor.TypeId);
	TObjectStoreFixture<256, 16, 2, 1> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	std::array<FObjectHandle, 2> Worklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())});

	const TObjectCreationResult<FDestroyReentryObject> Creation =
		Store.NewObject<FDestroyReentryObject>(*RegisteredOuterDescriptor, Store, *RegisteredNestedDescriptor, NestedLifetime, Collector, Reentry);
	TStrongObjectPointerResult<FDestroyReentryObject> Root = Store.MakeStrongObjectPtr(Creation.Object);
	(void)Store.MarkPendingDestroy(Creation.Object.Handle());
	(void)Store.ApplyPendingDestroy(2);
	Root.Pointer.Reset();

	return FDestructionReentryOutcome{Creation.Result, Reentry, NestedLifetime, Store.Stats()};
}

/**
 * Motivation: Scenario: Construct an object whose placement constructor attempts nested construction, the
 *   destruction barrier, and a collection request, then run the outer barrier.
 * Responsibilities: Expected: Every recursive mutation returns lifecycle-locked; the outer object publishes once and is
 *   destroyed once with no nested lifetime escaping.
 */
MW_TEST_CASE(ObjectStoreLocksMutationUntilPlacementConstructionPublishes)
{
	// Arrange
	FObjectLifetimeState NestedLifetime{};
	FReentryState Reentry{};
	TClassRegistry<3> Registry;
	FClassDescriptor NestedDescriptor = MakeClassDescriptor<FTrackedObject>(1, "NestedTracked");
	FClassDescriptor OuterDescriptor = MakeClassDescriptor<FConstructorReentryObject>(2, "ConstructorReentry");
	const EObjectResult NestedRegistration = Registry.Register(NestedDescriptor);
	const EObjectResult OuterRegistration = Registry.Register(OuterDescriptor);
	const FClassDescriptor* const RegisteredNestedDescriptor = Registry.Find(NestedDescriptor.TypeId);
	const FClassDescriptor* const RegisteredOuterDescriptor = Registry.Find(OuterDescriptor.TypeId);
	TObjectStoreFixture<256, 16, 2, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	std::array<FObjectHandle, 2> Worklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())});

	// Act
	const TObjectCreationResult<FConstructorReentryObject> Creation = Store.NewObject<FConstructorReentryObject>(
		*RegisteredOuterDescriptor, Store, *RegisteredNestedDescriptor, NestedLifetime, Collector, Reentry);
	const EObjectResult PendingResult = Store.MarkPendingDestroy(Creation.Object.Handle());
	const MicroWorld::Engine::FObjectMutationResult Barrier = Store.ApplyPendingDestroy(2);

	// Assert
	const EObjectResult ExpectedSuccess = EObjectResult::Success;
	const EObjectResult ExpectedLocked = EObjectResult::LifecycleLocked;
	const ERuntimeResult ExpectedCollectionLocked = ERuntimeResult::LifecycleLocked;
	MW_EXPECT_EQ(Test, ExpectedSuccess, NestedRegistration, "The nested tracked class should register");
	MW_EXPECT_EQ(Test, ExpectedSuccess, OuterRegistration, "The constructor-reentry class should register");
	MW_EXPECT_EQ(Test, ExpectedSuccess, Creation.Result, "The outer object should publish after its constructor returns");
	MW_EXPECT_EQ(Test, ExpectedLocked, Reentry.ConstructionResult, "Nested construction must remain locked before publication");
	MW_EXPECT_EQ(Test, ExpectedLocked, Reentry.BarrierResult, "A constructor cannot enter the destruction barrier");
	MW_EXPECT_EQ(Test, ExpectedCollectionLocked, Reentry.CollectionRequestResult, "A constructor cannot begin collection");
	MW_EXPECT_EQ(Test, ExpectedSuccess, PendingResult, "The published outer object should accept later destruction");
	MW_EXPECT_EQ(Test, ExpectedSuccess, Barrier.Result, "The later explicit destruction barrier should succeed");
	MW_EXPECT_EQ(Test, 0U, NestedLifetime.ConstructionCount, "No nested object may escape constructor reentry");
	MW_EXPECT_EQ(Test, 1U, Reentry.BeginDestroyCount, "The outer object should begin destruction once");
	MW_EXPECT_EQ(Test, 1U, Reentry.DestructionCount, "The outer object should be destroyed once");
}

/**
 * Motivation: Drive an adversarial object whose BeginDestroy attempts every mutation path, and observe the
 *   captured results after the barrier.
 * Responsibilities: Barrier reentry, publication, rooting, pending mutation, collection requests, and collection advance
 *   are each rejected; the object still.
 */
MW_TEST_CASE(BeginDestroyRejectsEveryRecursiveMutationPath)
{
	// Arrange
	const EObjectResult ExpectedLocked = EObjectResult::LifecycleLocked;
	const ERuntimeResult ExpectedCollectionLocked = ERuntimeResult::LifecycleLocked;
	const ERuntimeResult ExpectedInactiveCollection = ERuntimeResult::InvalidLifecycle;

	// Act — the adversarial BeginDestroy already ran during the destruction barrier,
	// recording one result per mutation path it attempted.
	const FDestructionReentryOutcome Outcome = RunDestructionReentryBarrierOnce();

	// Assert
	MW_EXPECT_EQ(Test, EObjectResult::Success, Outcome.CreationResult, "The adversarial object must publish before its barrier runs");
	MW_EXPECT_EQ(Test, ExpectedLocked, Outcome.Reentry.BarrierResult, "BeginDestroy cannot recursively enter its barrier");
	MW_EXPECT_EQ(Test, ExpectedLocked, Outcome.Reentry.ConstructionResult, "BeginDestroy cannot publish another object");
	MW_EXPECT_EQ(Test, ExpectedLocked, Outcome.Reentry.AddRootResult, "BeginDestroy cannot add a root");
	MW_EXPECT_EQ(Test, ExpectedLocked, Outcome.Reentry.MarkPendingResult, "BeginDestroy cannot repeat pending mutation");
	MW_EXPECT_EQ(Test, ExpectedCollectionLocked, Outcome.Reentry.CollectionRequestResult, "BeginDestroy cannot start collection");
	MW_EXPECT_EQ(
		Test, ExpectedInactiveCollection, Outcome.Reentry.CollectionAdvanceResult, "BeginDestroy cannot advance a collector without an active cycle");
}

/**
 * Motivation: Drive an adversarial object through its destruction barrier with an active root and observe the
 *   captured occupancy and counts.
 * Responsibilities: BeginDestroy and exact destruction run exactly once; the root is released safely; no nested lifetime
 *   escapes and no slot or root leaks.
 */
MW_TEST_CASE(DestructionReentryLeavesNoLeakedSlotsOrRoots)
{
	// Arrange — Act happens inside the barrier; observations are captured by value on return.
	const FDestructionReentryOutcome Outcome = RunDestructionReentryBarrierOnce();

	// Assert
	MW_EXPECT_EQ(Test, EObjectResult::Success, Outcome.CreationResult, "The adversarial object must publish before its barrier runs");
	MW_EXPECT_EQ(Test, 1U, Outcome.Reentry.BeginDestroyCount, "Recursive attempts must not repeat BeginDestroy");
	MW_EXPECT_EQ(Test, 1U, Outcome.Reentry.DestructionCount, "Recursive attempts must not repeat exact destruction");
	MW_EXPECT_EQ(Test, 0U, Outcome.NestedLifetime.ConstructionCount, "No nested object may escape destruction reentry");
	MW_EXPECT_EQ(Test, EObjectResult::Success, Outcome.Reentry.RemoveRootResult, "Exact destruction may release an existing root safely");
	MW_EXPECT_EQ(Test, 0U, Outcome.StoreStats.OccupiedSlots, "The destroyed slot must not leak an object");
	MW_EXPECT_EQ(Test, 0U, Outcome.StoreStats.ActiveRoots, "Destruction and stale reset must leave no root token");
}

// A derived-to-base conversion preserves store and generation; a base-to-derived
// conversion is rejected at compile time so a stale generation cannot be widened.
static_assert(
	std::is_constructible<TObjectPtr<UObject>, const TObjectPtr<FTrackedObject>&>::value,
	"A derived traced reference must convert to its managed base.");
static_assert(
	!std::is_constructible<TObjectPtr<FTrackedObject>, const TObjectPtr<UObject>&>::value,
	"A base traced reference must not widen to an arbitrary derived type.");

/**
 * Motivation: Create one tracked object and convert its derived reference to base, then to the same derived type.
 * Responsibilities: Each conversion preserves the handle identity, resolves the same object, and retains store
 *   membership.
 */
MW_TEST_CASE(TObjectPtrDerivedToBaseConversionPreservesStoreAndGeneration)
{
	// Base-to-derived conversion remains a compile-time error for any caller.
	static_assert(
		!std::is_convertible<const TObjectPtr<UObject>&, TObjectPtr<FTrackedObject>>::value,
		"Implicit base-to-derived conversion must stay disabled.");

	// Arrange
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterTrackedDescriptor(Registry, Descriptor);
	TObjectStoreFixture<128, 16, 2, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();

	// Act
	const TObjectCreationResult<FTrackedObject> Creation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);
	const TObjectPtr<FTrackedObject> Derived = Creation.Object;
	const TObjectPtr<UObject> Base = TObjectPtr<UObject>(Derived);
	const TObjectPtr<FTrackedObject> SameType = TObjectPtr<FTrackedObject>(Derived);

	// Assert
	const EObjectResult ExpectedSuccess = EObjectResult::Success;
	const FObjectHandle ExpectedHandle = Derived.Handle();
	const FTrackedObject* const DerivedResolved = Derived.Get();
	const UObject* const BaseResolved = Base.Get();
	const FTrackedObject* const SameTypeResolved = SameType.Get();
	const bool bBaseSharesStore = Base.BelongsTo(Store);
	const bool bSameTypeSharesStore = SameType.BelongsTo(Store);
	MW_EXPECT_EQ(Test, ExpectedSuccess, RegistrationResult, "The tracked class should register before conversion");
	MW_EXPECT_EQ(Test, ExpectedSuccess, Creation.Result, "One tracked object should publish for conversion");
	MW_EXPECT_EQ(Test, ExpectedHandle, Base.Handle(), "Derived-to-base conversion must preserve the handle identity");
	MW_EXPECT_EQ(Test, ExpectedHandle, SameType.Handle(), "Same-type conversion must preserve the handle identity");
	MW_EXPECT_EQ(Test, static_cast<const UObject*>(DerivedResolved), BaseResolved, "Conversion must resolve the same object");
	MW_EXPECT_EQ(Test, DerivedResolved, SameTypeResolved, "Same-type conversion must resolve the same object");
	MW_EXPECT_TRUE(Test, bBaseSharesStore, "Derived-to-base conversion must preserve store membership");
	MW_EXPECT_TRUE(Test, bSameTypeSharesStore, "Same-type conversion must preserve store membership");
}

/**
 * Motivation: Scenario: Manually occupy the preferred automatic ID, then automatically register the same candidate
 *   twice and attempt one registration into a full registry.
 * Responsibilities: Expected: Automatic registration probes past the occupied ID, returns a stable canonical descriptor
 *   on repeat, and leaves occupancy unchanged when the registry is full.
 */
MW_TEST_CASE(ObjectRegistryAutomaticRegistrationIsCanonicalAndBounded)
{
	// Arrange
	TClassRegistry<2> Registry;

	// Act
	const FClassDescriptor ManualDescriptor = MakeClassDescriptor<FWrongDestructorObject>(TClassRegistry<2>::FirstAutomaticTypeId, "ManualObject");
	const EObjectResult ManualResult = Registry.Register(ManualDescriptor);
	const FClassDescriptor Candidate = MakeClassDescriptor<FTrackedObject>(0, "TrackedObject");
	const FClassDescriptor* FirstDescriptor = nullptr;
	const FClassDescriptor* ReusedDescriptor = nullptr;
	const EObjectResult FirstResult = Registry.RegisterAutomatic(Candidate, FirstDescriptor);
	const EObjectResult ReusedResult = Registry.RegisterAutomatic(Candidate, ReusedDescriptor);

	const std::size_t CountBeforeFullRegistration = Registry.ClassCount();
	const FClassDescriptor AnotherCandidate = MakeClassDescriptor<FConstructorReentryObject>(0, "AnotherObject");
	const FClassDescriptor* FullDescriptor = nullptr;
	const EObjectResult FullResult = Registry.RegisterAutomatic(AnotherCandidate, FullDescriptor);

	// Assert
	MW_EXPECT_EQ(Test, EObjectResult::Success, ManualResult, "A manual descriptor occupies the preferred automatic ID");
	MW_EXPECT_EQ(Test, EObjectResult::Success, FirstResult, "Automatic registration probes past the occupied preferred ID");
	MW_EXPECT_TRUE(Test, FirstDescriptor != nullptr, "Automatic registration returns the registry-owned descriptor");
	MW_EXPECT_EQ(Test, EObjectResult::Success, ReusedResult, "The same type token reuses its canonical descriptor");
	MW_EXPECT_EQ(Test, FirstDescriptor, ReusedDescriptor, "Repeated automatic registration preserves canonical pointer identity");
	MW_EXPECT_EQ(Test, std::size_t{1}, Registry.ClassCount() - 1, "Reusing a type token does not consume another class slot");
	MW_EXPECT_EQ(Test, EObjectResult::CapacityExceeded, FullResult, "A full registry rejects automatic registration transactionally");
	MW_EXPECT_EQ(Test, static_cast<const FClassDescriptor*>(nullptr), FullDescriptor, "A rejected automatic registration exposes no descriptor");
	MW_EXPECT_EQ(Test, CountBeforeFullRegistration, Registry.ClassCount(), "A full registration leaves class occupancy unchanged");
}

} // namespace
