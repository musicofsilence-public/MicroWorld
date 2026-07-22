#include "TestSupport.h"

#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/ObjectStore.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{

using MicroWorld::CanAdvanceObjectGeneration;
using MicroWorld::DestroyManagedObject;
using MicroWorld::EObjectResult;
using MicroWorld::ERuntimeResult;
using MicroWorld::FClassDescriptor;
using MicroWorld::FGarbageCollector;
using MicroWorld::FGarbageCollectorStorage;
using MicroWorld::FObjectHandle;
using MicroWorld::FObjectRootEntry;
using MicroWorld::FObjectSlotMetadata;
using MicroWorld::FObjectStore;
using MicroWorld::FObjectStoreStats;
using MicroWorld::FObjectStoreStorage;
using MicroWorld::MakeClassDescriptor;
using MicroWorld::MakeClassRegistryView;
using MicroWorld::ObjectGeneration;
using MicroWorld::TClassRegistry;
using MicroWorld::TObjectCreationResult;
using MicroWorld::TObjectPtr;
using MicroWorld::TStrongObjectPtr;
using MicroWorld::TWeakObjectPtr;
using MicroWorld::UObject;

/** Records managed lifetime hooks in fresh state owned by one test. */
struct FObjectLifetimeState final
{
	/** Proves rejected construction attempts do not start an object lifetime. */
	std::uint32_t ConstructionCount{0};

	/** Proves the deferred destruction hook runs exactly once per object. */
	std::uint32_t BeginDestroyCount{0};

	/** Proves exact descriptor destruction runs exactly once per object. */
	std::uint32_t DestructionCount{0};
};

/** Exposes construction and destruction through caller-owned counters. */
class FTrackedObject : public UObject
{
public:
	/** Begins one observable lifetime after every store validation succeeds. */
	explicit FTrackedObject(FObjectLifetimeState& InState) noexcept : State(InState) { ++State.ConstructionCount; }

	/** Records exact derived destruction selected by the registered descriptor. */
	~FTrackedObject() noexcept override { ++State.DestructionCount; }

protected:
	/** Records the one deferred lifecycle hook before exact destruction. */
	void BeginDestroy() noexcept override { ++State.BeginDestroyCount; }

private:
	/** Reports lifecycle events without global mutable test state. */
	FObjectLifetimeState& State;
};

/** Supplies an equally laid-out wrong type whose destructor may be linker-folded. */
class FWrongDestructorObject : public UObject
{
public:
	/** Keeps this type layout-equivalent to FTrackedObject for descriptor validation. */
	explicit FWrongDestructorObject(FObjectLifetimeState& InState) noexcept : State(InState) {}

	/** Deliberately matches tracked destructor work so type tokens carry identity. */
	~FWrongDestructorObject() noexcept override { ++State.DestructionCount; }

private:
	/** Preserves the same instance layout as the intended managed type. */
	FObjectLifetimeState& State;
};

/** Captures every store/collector operation attempted from one lifecycle callback. */
struct FReentryState final
{
	/** Records a nested construction attempt while the store is callback-locked. */
	EObjectResult ConstructionResult{EObjectResult::Success};

	/** Records a nested destruction barrier while the store is callback-locked. */
	EObjectResult BarrierResult{EObjectResult::Success};

	/** Records a nested root registration while the store is callback-locked. */
	EObjectResult AddRootResult{EObjectResult::Success};

	/** Records a nested destruction request while the store is callback-locked. */
	EObjectResult MarkPendingResult{EObjectResult::Success};

	/** Records the one root release that remains safe during exact destruction. */
	EObjectResult RemoveRootResult{EObjectResult::StaleHandle};

	/** Records a collection request attempted from construction or destruction. */
	ERuntimeResult CollectionRequestResult{ERuntimeResult::Success};

	/** Records collection advancement attempted from a destruction callback. */
	ERuntimeResult CollectionAdvanceResult{ERuntimeResult::Success};

	/** Counts exact destruction of the outer object. */
	std::uint32_t DestructionCount{0};

	/** Counts destruction hooks for the outer object. */
	std::uint32_t BeginDestroyCount{0};
};

/** Attempts forbidden store and collector operations from a placement constructor. */
class FConstructorReentryObject final : public UObject
{
public:
	/** Proves the slot remains unpublished and locked until construction completes. */
	FConstructorReentryObject(
		FObjectStore& Store,
		const FClassDescriptor& NestedDescriptor,
		FObjectLifetimeState& NestedLifetime,
		FGarbageCollector& Collector,
		FReentryState& InState) noexcept
		: State(InState)
	{
		State.ConstructionResult = Store.NewObject<FTrackedObject>(NestedDescriptor, NestedLifetime).Result;
		State.BarrierResult = Store.ApplyPendingDestroy(1).Result;
		State.CollectionRequestResult = Collector.RequestCollection();
	}

	/** Records exact outer destruction after successful publication. */
	~FConstructorReentryObject() noexcept override { ++State.DestructionCount; }

protected:
	/** Records the normal later destruction barrier independently from construction. */
	void BeginDestroy() noexcept override { ++State.BeginDestroyCount; }

private:
	/** Shares callback observations only with the owning test. */
	FReentryState& State;
};

/** Attempts recursive mutation while BeginDestroy owns the explicit barrier. */
class FDestroyReentryObject final : public UObject
{
public:
	/** Retains injected public boundaries used by the adversarial destruction hook. */
	FDestroyReentryObject(
		FObjectStore& InStore,
		const FClassDescriptor& InNestedDescriptor,
		FObjectLifetimeState& InNestedLifetime,
		FGarbageCollector& InCollector,
		FReentryState& InState) noexcept
		: Store(InStore), NestedDescriptor(InNestedDescriptor), NestedLifetime(InNestedLifetime), Collector(InCollector), State(InState)
	{
	}

	/** Records the one exact destructor after all recursive attempts are rejected. */
	~FDestroyReentryObject() noexcept override { ++State.DestructionCount; }

protected:
	/** Exercises every mutation path that must not reenter destruction callbacks. */
	void BeginDestroy() noexcept override
	{
		++State.BeginDestroyCount;
		State.BarrierResult = Store.ApplyPendingDestroy(1).Result;
		State.ConstructionResult = Store.NewObject<FTrackedObject>(NestedDescriptor, NestedLifetime).Result;
		State.AddRootResult = Store.AddRoot(GetObjectHandle());
		State.MarkPendingResult = Store.MarkPendingDestroy(GetObjectHandle());
		State.CollectionRequestResult = Collector.RequestCollection();
		State.CollectionAdvanceResult = Collector.Advance(MicroWorld::FGarbageCollectionBudget{1, 1, 1}).Result;
		State.RemoveRootResult = Store.RemoveRoot(GetObjectHandle());
	}

private:
	/** Identifies the store whose barrier owns this callback. */
	FObjectStore& Store;

	/** Supplies a valid nested type so rejection proves locking rather than validation. */
	const FClassDescriptor& NestedDescriptor;

	/** Detects any nested lifetime that escaped the mutation lock. */
	FObjectLifetimeState& NestedLifetime;

	/** Exercises collection-request rejection during the mutation barrier. */
	FGarbageCollector& Collector;

	/** Shares callback results only with the owning test. */
	FReentryState& State;
};

static_assert(sizeof(FTrackedObject) == sizeof(FWrongDestructorObject));
static_assert(alignof(FTrackedObject) == alignof(FWrongDestructorObject));
static_assert(!std::is_copy_constructible<TStrongObjectPtr<FTrackedObject>>::value);
static_assert(!std::is_copy_assignable<TStrongObjectPtr<FTrackedObject>>::value);
static_assert(std::is_move_constructible<TStrongObjectPtr<FTrackedObject>>::value);
static_assert(std::is_move_assignable<TStrongObjectPtr<FTrackedObject>>::value);

/** Owns all fixed store storage locally so every behavior test is isolated. */
template<std::size_t SlotSizeBytes, std::size_t SlotAlignmentBytes, std::uint32_t SlotCount, std::uint32_t RootCapacity>
class TObjectStoreFixture final
{
public:
	/** Binds a store to this fixture's aligned slots, metadata, and root entries. */
	explicit TObjectStoreFixture(const MicroWorld::FClassRegistryView Classes) noexcept : Store(MakeStorage(), Classes) {}

	/** Provides the public store under test without exposing fixture storage. */
	FObjectStore& GetStore() noexcept { return Store; }

private:
	static_assert(SlotCount > 0, "Object-store tests require at least one slot.");
	static_assert(SlotSizeBytes % SlotAlignmentBytes == 0, "Slot stride must preserve alignment.");

	/** Describes this fixture's complete caller-owned store storage. */
	FObjectStoreStorage MakeStorage() noexcept
	{
		return FObjectStoreStorage{
			SlotBytes.data(),
			SlotBytes.size(),
			Slots.data(),
			SlotCount,
			SlotSizeBytes,
			SlotAlignmentBytes,
			RootCapacity == 0 ? nullptr : Roots.data(),
			RootCapacity,
		};
	}

	/** Keeps every equal-size slot correctly aligned for placement construction. */
	alignas(SlotAlignmentBytes) std::array<std::byte, SlotSizeBytes * SlotCount> SlotBytes{};

	/** Gives the store one lifecycle record per fixed object slot. */
	std::array<FObjectSlotMetadata, SlotCount> Slots{};

	/** Gives each successful strong pointer one independently reusable token entry. */
	std::array<FObjectRootEntry, RootCapacity> Roots{};

	/** Owns all managed lifetimes while the fixture storage remains alive. */
	FObjectStore Store;
};

/** Registers one tracked-object descriptor and returns its registry-owned identity. */
EObjectResult RegisterTrackedDescriptor(
	TClassRegistry<2>& Registry, const FClassDescriptor*& OutDescriptor, const MicroWorld::FTypeId TypeId = 1) noexcept
{
	const FClassDescriptor Candidate = MakeClassDescriptor<FTrackedObject>(TypeId, "Tracked");
	const EObjectResult Result = Registry.Register(Candidate);
	OutDescriptor = Registry.Find(TypeId);
	return Result;
}

/** Proves malformed caller storage rejects construction without changing any lifetime state. */
MW_TEST_CASE(ObjectStoreRejectsInvalidStorageBeforeConstruction)
{
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterTrackedDescriptor(Registry, Descriptor);
	FObjectStoreStorage InvalidStorage{};
	FObjectStore Store(InvalidStorage, MakeClassRegistryView(Registry));

	const TObjectCreationResult<FTrackedObject> Creation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);

	const EObjectResult ExpectedRegistrationResult = EObjectResult::Success;
	const EObjectResult ExpectedConfigurationResult = EObjectResult::UnsupportedObjectLayout;
	const std::uint32_t ExpectedConstructionCount = 0;
	const EObjectResult ConfigurationResult = Store.ConfigurationResult();
	MW_EXPECT_EQ(Test, ExpectedRegistrationResult, RegistrationResult, "The valid class should register for the storage test");
	MW_EXPECT_EQ(Test, ExpectedConfigurationResult, ConfigurationResult, "Malformed store storage should be rejected explicitly");
	MW_EXPECT_EQ(Test, ExpectedConfigurationResult, Creation.Result, "Construction should return the store configuration failure");
	MW_EXPECT_EQ(Test, ExpectedConstructionCount, Lifetime.ConstructionCount, "Invalid storage must reject before placement construction");
}

/** Proves an object larger than the configured slots is rejected atomically. */
MW_TEST_CASE(ObjectStoreRejectsUnsupportedObjectLayoutBeforeConstruction)
{
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterTrackedDescriptor(Registry, Descriptor);
	TObjectStoreFixture<8, 8, 1, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();

	const TObjectCreationResult<FTrackedObject> Creation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);

	const EObjectResult ExpectedSuccess = EObjectResult::Success;
	const EObjectResult ExpectedFailure = EObjectResult::UnsupportedObjectLayout;
	const std::uint32_t ExpectedConstructionCount = 0;
	const std::uint32_t ExpectedOccupiedSlots = 0;
	const EObjectResult ConfigurationResult = Store.ConfigurationResult();
	const FObjectStoreStats StoreStats = Store.Stats();
	MW_EXPECT_EQ(Test, ExpectedSuccess, RegistrationResult, "The valid class should register before slot-layout validation");
	MW_EXPECT_EQ(Test, ExpectedSuccess, ConfigurationResult, "The small slot storage itself should remain structurally valid");
	MW_EXPECT_EQ(Test, ExpectedFailure, Creation.Result, "A class that cannot fit one slot should report layout failure");
	MW_EXPECT_EQ(Test, ExpectedConstructionCount, Lifetime.ConstructionCount, "Layout failure must happen before construction");
	MW_EXPECT_EQ(Test, ExpectedOccupiedSlots, StoreStats.OccupiedSlots, "Layout failure must not consume a slot");
}

/** Proves a descriptor outside the store registry cannot begin construction. */
MW_TEST_CASE(ObjectStoreRejectsUnknownClassWithoutConsumingCapacity)
{
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* RegisteredDescriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterTrackedDescriptor(Registry, RegisteredDescriptor);
	FClassDescriptor UnknownDescriptor = MakeClassDescriptor<FTrackedObject>(2, "Unknown");
	TObjectStoreFixture<128, 16, 1, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();

	const TObjectCreationResult<FTrackedObject> Creation = Store.NewObject<FTrackedObject>(UnknownDescriptor, Lifetime);

	const EObjectResult ExpectedSuccess = EObjectResult::Success;
	const EObjectResult ExpectedFailure = EObjectResult::UnknownClass;
	const std::uint32_t ExpectedConstructionCount = 0;
	const std::uint32_t ExpectedOccupiedSlots = 0;
	const FObjectStoreStats StoreStats = Store.Stats();
	MW_EXPECT_EQ(Test, ExpectedSuccess, RegistrationResult, "The registry setup should succeed");
	MW_EXPECT_EQ(Test, ExpectedFailure, Creation.Result, "An unregistered descriptor should be rejected as unknown");
	MW_EXPECT_EQ(Test, ExpectedConstructionCount, Lifetime.ConstructionCount, "Unknown-class rejection must precede construction");
	MW_EXPECT_EQ(Test, ExpectedOccupiedSlots, StoreStats.OccupiedSlots, "Unknown-class rejection must preserve slot capacity");
}

/** Proves descriptor identity includes the exact destructor even for equal layouts. */
MW_TEST_CASE(ObjectStoreRejectsSameLayoutDescriptorWithWrongExactDestructor)
{
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	FClassDescriptor WrongDescriptor = MakeClassDescriptor<FWrongDestructorObject>(1, "WrongDestructor");
	const EObjectResult RegistrationResult = Registry.Register(WrongDescriptor);
	const FClassDescriptor* const RegisteredWrongDescriptor = Registry.Find(WrongDescriptor.TypeId);
	TObjectStoreFixture<128, 16, 1, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();

	const TObjectCreationResult<FTrackedObject> Creation = Store.NewObject<FTrackedObject>(*RegisteredWrongDescriptor, Lifetime);

	const EObjectResult ExpectedSuccess = EObjectResult::Success;
	const EObjectResult ExpectedFailure = EObjectResult::UnsupportedObjectLayout;
	const std::uint32_t ExpectedConstructionCount = 0;
	const FObjectStoreStats StoreStats = Store.Stats();
	MW_EXPECT_EQ(Test, ExpectedSuccess, RegistrationResult, "A structurally valid descriptor should register");
	MW_EXPECT_EQ(Test, ExpectedFailure, Creation.Result, "The wrong exact destructor should reject typed construction");
	MW_EXPECT_EQ(Test, ExpectedConstructionCount, Lifetime.ConstructionCount, "Destructor mismatch must reject before construction");
	MW_EXPECT_EQ(Test, ExpectedConstructionCount, StoreStats.OccupiedSlots, "Destructor mismatch must not consume capacity");
}

/** Proves registry-owned descriptor state cannot be invalidated through its source copy. */
MW_TEST_CASE(ObjectStoreUsesImmutableRegistryOwnedDescriptorCopy)
{
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	FClassDescriptor SourceDescriptor = MakeClassDescriptor<FTrackedObject>(1, "TrackedSource");
	const EObjectResult RegistrationResult = Registry.Register(SourceDescriptor);
	const FClassDescriptor* const RegisteredDescriptor = Registry.Find(SourceDescriptor.TypeId);
	SourceDescriptor.Destroy = nullptr;
	SourceDescriptor.TypeToken = nullptr;
	SourceDescriptor.SizeBytes = 1;
	TObjectStoreFixture<128, 16, 1, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();

	const auto OwnedCreation = Store.NewObject<FTrackedObject>(*RegisteredDescriptor, Lifetime);
	const auto RejectedSourceCreation = Store.NewObject<FTrackedObject>(SourceDescriptor, Lifetime);
	const EObjectResult PendingResult = Store.MarkPendingDestroy(OwnedCreation.Object.Handle());
	const MicroWorld::FObjectMutationResult Barrier = Store.ApplyPendingDestroy(1);

	const EObjectResult ExpectedSuccess = EObjectResult::Success;
	const EObjectResult ExpectedUnknown = EObjectResult::UnknownClass;
	MW_EXPECT_EQ(Test, ExpectedSuccess, RegistrationResult, "The valid source descriptor should register");
	MW_EXPECT_EQ(Test, ExpectedSuccess, OwnedCreation.Result, "The registry-owned copy should retain exact safe callbacks");
	MW_EXPECT_EQ(Test, ExpectedUnknown, RejectedSourceCreation.Result, "The mutable source copy must not become store identity");
	MW_EXPECT_EQ(Test, ExpectedSuccess, PendingResult, "The registry-owned object should enter explicit destruction");
	MW_EXPECT_EQ(Test, 1U, Barrier.ObjectsDestroyed, "The registry-owned descriptor should destroy the object exactly once");
	MW_EXPECT_EQ(Test, 1U, Lifetime.ConstructionCount, "Mutating the source descriptor must not create a second object");
	MW_EXPECT_EQ(Test, 1U, Lifetime.DestructionCount, "The owned descriptor copy must retain exact destruction");
}

/** Proves full capacity rejects a second object without disturbing the first. */
MW_TEST_CASE(ObjectStoreCapacityFailureIsAtomicAndDoesNotCollect)
{
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterTrackedDescriptor(Registry, Descriptor);
	TObjectStoreFixture<128, 16, 1, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FTrackedObject> FirstCreation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);

	const TObjectCreationResult<FTrackedObject> SecondCreation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);

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

/** Proves deferred destruction hides immediately and runs both hooks exactly once. */
MW_TEST_CASE(ObjectStoreDeferredDestructionRunsLifecycleHooksOnce)
{
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterTrackedDescriptor(Registry, Descriptor);
	TObjectStoreFixture<128, 16, 1, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FTrackedObject> Creation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);
	FTrackedObject* const RawObject = Creation.Object.Get();

	const EObjectResult FirstPendingResult = Store.MarkPendingDestroy(Creation.Object.Handle());
	const EObjectResult SecondPendingResult = Store.MarkPendingDestroy(Creation.Object.Handle());
	const MicroWorld::FObjectMutationResult FirstBarrier = Store.ApplyPendingDestroy(1);
	const MicroWorld::FObjectMutationResult SecondBarrier = Store.ApplyPendingDestroy(1);

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

/** Proves slot reuse advances generation and never revives the stale identity. */
MW_TEST_CASE(ObjectStoreSlotReuseInvalidatesEveryOldHandle)
{
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterTrackedDescriptor(Registry, Descriptor);
	TObjectStoreFixture<128, 16, 1, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FTrackedObject> FirstCreation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);
	const FObjectHandle FirstHandle = FirstCreation.Object.Handle();
	const EObjectResult PendingResult = Store.MarkPendingDestroy(FirstHandle);
	const MicroWorld::FObjectMutationResult BarrierResult = Store.ApplyPendingDestroy(1);

	const TObjectCreationResult<FTrackedObject> SecondCreation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);

	const EObjectResult ExpectedSuccess = EObjectResult::Success;
	const EObjectResult ExpectedStale = EObjectResult::StaleHandle;
	const std::uint32_t ExpectedOne = 1;
	const FObjectHandle SecondHandle = SecondCreation.Object.Handle();
	const bool bSameSlotReused = FirstHandle.Index == SecondHandle.Index;
	const bool bGenerationAdvanced = FirstHandle.Generation != SecondHandle.Generation;
	const bool bOldPointerExpired = FirstCreation.Object.Get() == nullptr;
	const EObjectResult StaleRootRemoval = Store.RemoveRoot(FirstHandle);
	MW_EXPECT_EQ(Test, ExpectedSuccess, RegistrationResult, "The tracked class should register");
	MW_EXPECT_EQ(Test, ExpectedSuccess, FirstCreation.Result, "The first generation should be created");
	MW_EXPECT_EQ(Test, ExpectedSuccess, PendingResult, "The first generation should enter pending destruction");
	MW_EXPECT_EQ(Test, ExpectedOne, BarrierResult.ObjectsDestroyed, "The first generation should be reclaimed");
	MW_EXPECT_EQ(Test, ExpectedSuccess, SecondCreation.Result, "The reclaimed slot should publish a new generation");
	MW_EXPECT_TRUE(Test, bSameSlotReused, "The one-slot fixture should reuse the same index");
	MW_EXPECT_TRUE(Test, bGenerationAdvanced, "Reuse must publish a distinct generation");
	MW_EXPECT_TRUE(Test, bOldPointerExpired, "The old generation must remain stale after slot reuse");
	MW_EXPECT_EQ(Test, ExpectedStale, StaleRootRemoval, "A stale generation cannot release a current lifetime root");
}

/** Proves duplicate strong pointers own independent capacity and moves transfer one token. */
MW_TEST_CASE(ObjectStoreStrongRootsAreIndependentAndMoveOnly)
{
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterTrackedDescriptor(Registry, Descriptor);
	TObjectStoreFixture<128, 16, 1, 2> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FTrackedObject> Creation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);
	auto FirstRoot = Store.MakeStrongObjectPtr(Creation.Object);
	auto SecondRoot = Store.MakeStrongObjectPtr(Creation.Object);

	auto RejectedRoot = Store.MakeStrongObjectPtr(Creation.Object);
	TStrongObjectPtr<FTrackedObject> MovedRoot(std::move(FirstRoot.Pointer));
	SecondRoot.Pointer = std::move(MovedRoot);

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

/** Proves pending objects cannot resolve, gain roots, or be resurrected by stale releases. */
MW_TEST_CASE(ObjectStorePendingObjectCannotBeResolvedOrResurrected)
{
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterTrackedDescriptor(Registry, Descriptor);
	TObjectStoreFixture<128, 16, 1, 2> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FTrackedObject> Creation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);
	TWeakObjectPtr<FTrackedObject> WeakObject(Creation.Object);
	auto StrongObject = Store.MakeStrongObjectPtr(Creation.Object);
	const FObjectHandle ObjectHandle = Creation.Object.Handle();

	const EObjectResult PendingResult = Store.MarkPendingDestroy(ObjectHandle);
	auto RejectedRoot = Store.MakeStrongObjectPtr(Creation.Object);
	StrongObject.Pointer.Reset();
	const EObjectResult StaleReleaseResult = Store.RemoveRoot(ObjectHandle);

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

/** Proves the final generation is retired rather than wrapped into old identity. */
MW_TEST_CASE(ObjectHandleGenerationBoundaryRequiresRetirementBeforeWrap)
{
	const ObjectGeneration LastReusableGeneration = std::numeric_limits<ObjectGeneration>::max() - 1U;
	const ObjectGeneration ExhaustedGeneration = std::numeric_limits<ObjectGeneration>::max();

	const bool bLastGenerationCanAdvance = CanAdvanceObjectGeneration(LastReusableGeneration);
	const bool bExhaustedGenerationCanAdvance = CanAdvanceObjectGeneration(ExhaustedGeneration);

	MW_EXPECT_TRUE(Test, bLastGenerationCanAdvance, "The final distinct generation may be published once");
	MW_EXPECT_TRUE(Test, !bExhaustedGenerationCanAdvance, "An exhausted slot must retire before generation wrap");
}

/** Proves a noexcept constructor cannot recursively publish or collect through its store. */
MW_TEST_CASE(ObjectStoreLocksMutationUntilPlacementConstructionPublishes)
{
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

	const auto Creation = Store.NewObject<FConstructorReentryObject>(
		*RegisteredOuterDescriptor, Store, *RegisteredNestedDescriptor, NestedLifetime, Collector, Reentry);
	const EObjectResult PendingResult = Store.MarkPendingDestroy(Creation.Object.Handle());
	const MicroWorld::FObjectMutationResult Barrier = Store.ApplyPendingDestroy(2);

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

/** Proves BeginDestroy cannot recursively mutate, collect, or destroy its slot. */
MW_TEST_CASE(ObjectStoreRejectsDestructionCallbackReentryWithoutLeakingRoots)
{
	FObjectLifetimeState NestedLifetime{};
	FReentryState Reentry{};
	TClassRegistry<3> Registry;
	FClassDescriptor NestedDescriptor = MakeClassDescriptor<FTrackedObject>(1, "NestedTracked");
	FClassDescriptor OuterDescriptor = MakeClassDescriptor<FDestroyReentryObject>(2, "DestroyReentry");
	const EObjectResult NestedRegistration = Registry.Register(NestedDescriptor);
	const EObjectResult OuterRegistration = Registry.Register(OuterDescriptor);
	const FClassDescriptor* const RegisteredNestedDescriptor = Registry.Find(NestedDescriptor.TypeId);
	const FClassDescriptor* const RegisteredOuterDescriptor = Registry.Find(OuterDescriptor.TypeId);
	TObjectStoreFixture<256, 16, 2, 1> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	std::array<FObjectHandle, 2> Worklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())});
	const auto Creation =
		Store.NewObject<FDestroyReentryObject>(*RegisteredOuterDescriptor, Store, *RegisteredNestedDescriptor, NestedLifetime, Collector, Reentry);
	auto Root = Store.MakeStrongObjectPtr(Creation.Object);

	const EObjectResult PendingResult = Store.MarkPendingDestroy(Creation.Object.Handle());
	const MicroWorld::FObjectMutationResult Barrier = Store.ApplyPendingDestroy(2);
	Root.Pointer.Reset();

	const EObjectResult ExpectedSuccess = EObjectResult::Success;
	const EObjectResult ExpectedLocked = EObjectResult::LifecycleLocked;
	const ERuntimeResult ExpectedCollectionLocked = ERuntimeResult::LifecycleLocked;
	const ERuntimeResult ExpectedInactiveCollection = ERuntimeResult::InvalidLifecycle;
	const FObjectStoreStats StoreStats = Store.Stats();
	MW_EXPECT_EQ(Test, ExpectedSuccess, NestedRegistration, "The nested tracked class should register");
	MW_EXPECT_EQ(Test, ExpectedSuccess, OuterRegistration, "The destruction-reentry class should register");
	MW_EXPECT_EQ(Test, ExpectedSuccess, Creation.Result, "The adversarial object should first publish");
	MW_EXPECT_EQ(Test, ExpectedSuccess, Root.Result, "One root token should exist before destruction");
	MW_EXPECT_EQ(Test, ExpectedSuccess, PendingResult, "The adversarial object should enter pending state");
	MW_EXPECT_EQ(Test, ExpectedSuccess, Barrier.Result, "The owning destruction barrier should complete");
	MW_EXPECT_EQ(Test, ExpectedLocked, Reentry.BarrierResult, "BeginDestroy cannot recursively enter its barrier");
	MW_EXPECT_EQ(Test, ExpectedLocked, Reentry.ConstructionResult, "BeginDestroy cannot publish another object");
	MW_EXPECT_EQ(Test, ExpectedLocked, Reentry.AddRootResult, "BeginDestroy cannot add a root");
	MW_EXPECT_EQ(Test, ExpectedLocked, Reentry.MarkPendingResult, "BeginDestroy cannot repeat pending mutation");
	MW_EXPECT_EQ(Test, ExpectedCollectionLocked, Reentry.CollectionRequestResult, "BeginDestroy cannot start collection");
	MW_EXPECT_EQ(
		Test, ExpectedInactiveCollection, Reentry.CollectionAdvanceResult, "BeginDestroy cannot advance a collector without an active cycle");
	MW_EXPECT_EQ(Test, ExpectedSuccess, Reentry.RemoveRootResult, "Exact destruction may release an existing root safely");
	MW_EXPECT_EQ(Test, 1U, Reentry.BeginDestroyCount, "Recursive attempts must not repeat BeginDestroy");
	MW_EXPECT_EQ(Test, 1U, Reentry.DestructionCount, "Recursive attempts must not repeat exact destruction");
	MW_EXPECT_EQ(Test, 0U, NestedLifetime.ConstructionCount, "No nested object may escape destruction reentry");
	MW_EXPECT_EQ(Test, 0U, StoreStats.OccupiedSlots, "The destroyed slot must not leak an object");
	MW_EXPECT_EQ(Test, 0U, StoreStats.ActiveRoots, "Destruction and stale reset must leave no root token");
}

// A derived-to-base conversion preserves store and generation; a base-to-derived
// conversion is rejected at compile time so a stale generation cannot be widened.
static_assert(
	std::is_constructible<TObjectPtr<UObject>, const TObjectPtr<FTrackedObject>&>::value,
	"A derived traced reference must convert to its managed base.");
static_assert(
	!std::is_constructible<TObjectPtr<FTrackedObject>, const TObjectPtr<UObject>&>::value,
	"A base traced reference must not widen to an arbitrary derived type.");

/** Proves the derived-to-base conversion preserves store, generation, and resolution. */
MW_TEST_CASE(TObjectPtrDerivedToBaseConversionPreservesStoreAndGeneration)
{
	// Base-to-derived conversion remains a compile-time error for any caller.
	static_assert(
		!std::is_convertible<const TObjectPtr<UObject>&, TObjectPtr<FTrackedObject>>::value,
		"Implicit base-to-derived conversion must stay disabled.");

	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterTrackedDescriptor(Registry, Descriptor);
	TObjectStoreFixture<128, 16, 2, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();

	const TObjectCreationResult<FTrackedObject> Creation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);
	const TObjectPtr<FTrackedObject> Derived = Creation.Object;
	const TObjectPtr<UObject> Base = TObjectPtr<UObject>(Derived);
	const TObjectPtr<FTrackedObject> SameType = TObjectPtr<FTrackedObject>(Derived);

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

} // namespace
