#include "TestSupport.h"
#include "GarbageCollectorTestGraph.h"

#include <MicroWorld/Engine/GarbageCollectionBudget.h>
#include <MicroWorld/Engine/GarbageCollectionPhase.h>
#include <MicroWorld/Engine/GarbageCollectionResult.h>
#include <MicroWorld/Engine/GarbageCollectionStats.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/ObjectMutationResult.h>

#include <array>
#include <cstdint>

namespace
{

using namespace ::MicroWorld::Tests;

/**
 * Motivation: Build a parent with two discovered children under one root and run bounded mark slices capped at one
 *   mark operation.
 * Responsibilities: The multi-reference visitor consumes only one mark per slice; the cycle completes and the complete
 *   two-edge graph survives.
 */
MW_TEST_CASE(GarbageCollectorMultiReferenceVisitorCountsOneMarkAndPreservesGraph)
{
	// Arrange
	FGraphLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterGraphDescriptor(Registry, Descriptor);
	TGraphStoreFixture<3, 1> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FGraphObject> Parent = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const TObjectCreationResult<FGraphObject> FirstChild = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const TObjectCreationResult<FGraphObject> SecondChild = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	Parent.Object.Get()->SetReference(0, FirstChild.Object);
	Parent.Object.Get()->SetReference(1, SecondChild.Object);
	TStrongObjectPointerResult<FGraphObject> Root = Store.MakeStrongObjectPtr(Parent.Object);
	static_cast<void>(Root);
	std::array<FObjectHandle, 3> Worklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())});
	const ERuntimeResult RequestResult = Collector.RequestCollection();

	// Act
	bool bMarkBudgetRespected = true;
	FGarbageCollectionResult FinalResult{};
	for (std::uint32_t Slice = 0; Slice < 16 && Collector.Phase() != EGarbageCollectionPhase::Idle; ++Slice)
	{
		const FGarbageCollectionResult SliceResult = Collector.Advance(FGarbageCollectionBudget{3, 1, 3});
		if (SliceResult.MarkOperations > 1)
		{
			bMarkBudgetRespected = false;
		}
		FinalResult = SliceResult;
	}

	// Assert
	const EObjectResult ExpectedObjectSuccess = EObjectResult::Success;
	const ERuntimeResult ExpectedCollectionSuccess = ERuntimeResult::Success;
	const std::uint32_t ExpectedOccupiedSlots = 3;
	const std::uint32_t ExpectedReclaimedObjects = 0;
	const FObjectStoreStats StoreStats = Store.Stats();
	const MicroWorld::Engine::FGarbageCollectionStats CollectionStats = Collector.Stats();
	MW_EXPECT_EQ(Test, ExpectedObjectSuccess, RegistrationResult, "The graph class should register");
	MW_EXPECT_EQ(Test, ExpectedCollectionSuccess, RequestResult, "The bounded traversal should start");
	MW_EXPECT_TRUE(Test, bMarkBudgetRespected, "A multi-reference visitor should consume only one mark operation");
	MW_EXPECT_TRUE(Test, FinalResult.bCycleComplete, "Bounded mark slices should finish the cycle");
	MW_EXPECT_EQ(Test, ExpectedReclaimedObjects, CollectionStats.ReclaimedObjects, "Both discovered children should remain reachable");
	MW_EXPECT_EQ(Test, ExpectedOccupiedSlots, StoreStats.OccupiedSlots, "The complete two-edge graph should survive");
}

/**
 * Motivation: Build a deep rooted chain, run one collection with the root held, then remove the root and collect
 *   again.
 * Responsibilities: The rooted deep graph survives the first cycle; removing the root reclaims the entire chain
 *   iteratively with no node remaining.
 */
MW_TEST_CASE(GarbageCollectorTraversesDeepGraphWithoutRecursion)
{
	// Arrange
	constexpr std::uint32_t NodeCount = 48;
	FGraphLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterGraphDescriptor(Registry, Descriptor);
	TGraphStoreFixture<NodeCount, 1> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	std::array<TObjectPtr<FGraphObject>, NodeCount> Nodes{};
	bool bAllCreated = true;
	for (std::uint32_t Index = 0; Index < NodeCount; ++Index)
	{
		const TObjectCreationResult<FGraphObject> Creation = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
		Nodes[Index] = Creation.Object;
		if (Creation.Result != EObjectResult::Success)
		{
			bAllCreated = false;
		}
	}
	for (std::uint32_t Index = 1; Index < NodeCount; ++Index)
	{
		Nodes[Index - 1].Get()->SetReference(0, Nodes[Index]);
	}
	TStrongObjectPointerResult<FGraphObject> Root = Store.MakeStrongObjectPtr(Nodes[0]);
	std::array<FObjectHandle, NodeCount> Worklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())});

	// Act
	const FGarbageCollectionResult RootedCollection = Collector.CollectFull();
	Root.Pointer.Reset();
	const FGarbageCollectionResult UnrootedCollection = Collector.CollectFull();

	// Assert
	const EObjectResult ExpectedObjectSuccess = EObjectResult::Success;
	const std::uint32_t ExpectedRootedReclaims = 0;
	const std::uint32_t ExpectedUnrootedReclaims = NodeCount;
	const std::uint32_t ExpectedRemainingObjects = 0;
	const FObjectStoreStats StoreStats = Store.Stats();
	MW_EXPECT_EQ(Test, ExpectedObjectSuccess, RegistrationResult, "The graph class should register");
	MW_EXPECT_TRUE(Test, bAllCreated, "Every node in the fixed deep graph should be created");
	MW_EXPECT_EQ(Test, ExpectedRootedReclaims, RootedCollection.ObjectsReclaimed, "The rooted deep graph should survive");
	MW_EXPECT_EQ(Test, ExpectedUnrootedReclaims, UnrootedCollection.ObjectsReclaimed, "Removing the root should reclaim the full deep graph");
	MW_EXPECT_EQ(Test, ExpectedRemainingObjects, StoreStats.OccupiedSlots, "No deep-graph node should remain after reclamation");
}

/**
 * Motivation: Scenario: Start one cycle, advance one root slice, then attempt construction, pending destruction, a
 *   new root, a barrier, and a second collector while the cycle owns the store.
 * Responsibilities: Expected: Every reachability-changing mutation and the second collector are rejected as
 *   lifecycle-locked; removing an existing root stays safe; cancellation releases ownership so mutation
 *   and a later collection resume.
 */
MW_TEST_CASE(GarbageCollectorLocksMutationAndSecondCollectorBetweenSlices)
{
	// Arrange
	FGraphLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterGraphDescriptor(Registry, Descriptor);
	MW_EXPECT_EQ(Test, EObjectResult::Success, RegistrationResult, "The graph descriptor should register");
	MW_EXPECT_TRUE(Test, Descriptor != nullptr, "The registry should expose its owned graph descriptor");
	if (Descriptor == nullptr)
	{
		return;
	}
	TGraphStoreFixture<2, 2> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FGraphObject> Rooted = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	TStrongObjectPointerResult<FGraphObject> Root = Store.MakeStrongObjectPtr(Rooted.Object);
	std::array<FObjectHandle, 2> FirstWorklist{};
	std::array<FObjectHandle, 2> SecondWorklist{};
	FGarbageCollector FirstCollector(Store, FGarbageCollectorStorage{FirstWorklist.data(), static_cast<std::uint32_t>(FirstWorklist.size())});
	FGarbageCollector SecondCollector(Store, FGarbageCollectorStorage{SecondWorklist.data(), static_cast<std::uint32_t>(SecondWorklist.size())});

	// Act
	const ERuntimeResult RequestResult = FirstCollector.RequestCollection();
	const FGarbageCollectionResult RootSlice = FirstCollector.Advance(FGarbageCollectionBudget{1, 0, 0});
	const TObjectCreationResult<FGraphObject> RejectedCreation = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const EObjectResult RejectedPending = Store.MarkPendingDestroy(Rooted.Object.Handle());
	TStrongObjectPointerResult<FGraphObject> RejectedRoot = Store.MakeStrongObjectPtr(Rooted.Object);
	const MicroWorld::Engine::FObjectMutationResult RejectedBarrier = Store.ApplyPendingDestroy(1);
	const ERuntimeResult RejectedSecondCollector = SecondCollector.RequestCollection();
	Root.Pointer.Reset();
	const FObjectStoreStats StatsAfterRootRemoval = Store.Stats();
	const ERuntimeResult CancelResult = FirstCollector.CancelCollection();
	const TObjectCreationResult<FGraphObject> CreationAfterCancel = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const FGarbageCollectionResult Cleanup = SecondCollector.CollectFull();

	// Assert
	MW_EXPECT_EQ(Test, EObjectResult::Success, RegistrationResult, "The graph class should register");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, RequestResult, "The first collector should acquire the store");
	MW_EXPECT_EQ(Test, EGarbageCollectionPhase::SeedRoots, RootSlice.Phase, "The first slice should pause after one root entry");
	MW_EXPECT_EQ(Test, EObjectResult::LifecycleLocked, RejectedCreation.Result, "Construction must wait until the cycle ends");
	MW_EXPECT_EQ(Test, EObjectResult::LifecycleLocked, RejectedPending, "Pending destruction must wait until the cycle ends");
	MW_EXPECT_EQ(Test, EObjectResult::LifecycleLocked, RejectedRoot.Result, "New roots must wait until the cycle ends");
	MW_EXPECT_EQ(Test, EObjectResult::LifecycleLocked, RejectedBarrier.Result, "Destruction barriers must wait until the cycle ends");
	MW_EXPECT_EQ(Test, ERuntimeResult::LifecycleLocked, RejectedSecondCollector, "A second collector cannot acquire an active store cycle");
	MW_EXPECT_EQ(Test, 0U, StatsAfterRootRemoval.ActiveRoots, "Removing an existing root remains safe between slices");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, CancelResult, "Explicit cancellation should release store ownership");
	MW_EXPECT_EQ(Test, EObjectResult::Success, CreationAfterCancel.Result, "Mutation should resume after cancellation");
	MW_EXPECT_TRUE(Test, Cleanup.bCycleComplete, "A later collector should complete after cancellation");
	MW_EXPECT_EQ(Test, 2U, Cleanup.ObjectsReclaimed, "The later cycle should reclaim both now-unrooted objects");
}

/**
 * Motivation: Let one collector acquire the store and pause mid-mark, destroy it, then start a fresh collector and
 *   run a full collection.
 * Responsibilities: The abandoned cycle's partial mark is cleared on destruction; a later collector acquires the
 *   released store and reclaims the formerly.
 */
MW_TEST_CASE(GarbageCollectorDestructorReleasesActiveCycleAndPartialMarks)
{
	// Arrange
	FGraphLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterGraphDescriptor(Registry, Descriptor);
	MW_EXPECT_EQ(Test, EObjectResult::Success, RegistrationResult, "The graph descriptor should register");
	MW_EXPECT_TRUE(Test, Descriptor != nullptr, "The registry should expose its owned graph descriptor");
	if (Descriptor == nullptr)
	{
		return;
	}
	TGraphStoreFixture<1, 1> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FGraphObject> Creation = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	TStrongObjectPointerResult<FGraphObject> Root = Store.MakeStrongObjectPtr(Creation.Object);
	ERuntimeResult RequestResult = ERuntimeResult::InvalidLifecycle;
	EGarbageCollectionPhase PausedPhase = EGarbageCollectionPhase::Idle;
	{
		std::array<FObjectHandle, 1> AbandonedWorklist{};
		FGarbageCollector AbandonedCollector(
			Store, FGarbageCollectorStorage{AbandonedWorklist.data(), static_cast<std::uint32_t>(AbandonedWorklist.size())});
		RequestResult = AbandonedCollector.RequestCollection();
		PausedPhase = AbandonedCollector.Advance(FGarbageCollectionBudget{1, 0, 0}).Phase;
	}

	// Act
	Root.Pointer.Reset();
	std::array<FObjectHandle, 1> FinalWorklist{};
	FGarbageCollector FinalCollector(Store, FGarbageCollectorStorage{FinalWorklist.data(), static_cast<std::uint32_t>(FinalWorklist.size())});
	const FGarbageCollectionResult FinalCollection = FinalCollector.CollectFull();

	// Assert
	MW_EXPECT_EQ(Test, EObjectResult::Success, RegistrationResult, "The graph class should register");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, RequestResult, "The abandoned collector should first acquire the store");
	MW_EXPECT_EQ(Test, EGarbageCollectionPhase::Mark, PausedPhase, "The abandoned cycle should retain one partial mark");
	MW_EXPECT_TRUE(Test, FinalCollection.bCycleComplete, "A later collector should acquire the released store");
	MW_EXPECT_EQ(Test, 1U, FinalCollection.ObjectsReclaimed, "Destructor cancellation must clear the abandoned mark");
	MW_EXPECT_EQ(Test, 1U, Lifetime.DestructionCount, "The formerly marked object should be destroyed exactly once");
}

/**
 * Motivation: Arm a rooted node's reference visitor to recursively advance the active collector, then start the
 *   cycle and advance it.
 * Responsibilities: The recursive advance is rejected as lifecycle-locked; the outer cycle still completes and the
 *   rooted object remains live.
 */
MW_TEST_CASE(GarbageCollectorRejectsRecursiveAdvanceFromReferenceVisitor)
{
	// Arrange
	FGraphLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterGraphDescriptor(Registry, Descriptor);
	MW_EXPECT_EQ(Test, EObjectResult::Success, RegistrationResult, "The graph descriptor should register");
	MW_EXPECT_TRUE(Test, Descriptor != nullptr, "The registry should expose its owned graph descriptor");
	if (Descriptor == nullptr)
	{
		return;
	}
	TGraphStoreFixture<1, 1> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FGraphObject> Creation = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	TStrongObjectPointerResult<FGraphObject> Root = Store.MakeStrongObjectPtr(Creation.Object);
	static_cast<void>(Root);
	std::array<FObjectHandle, 1> Worklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())});
	ERuntimeResult ReentrantResult = ERuntimeResult::InvalidLifecycle;
	Creation.Object.Get()->SetReentrantAdvance(Collector, ReentrantResult);

	// Act
	const ERuntimeResult RequestResult = Collector.RequestCollection();
	const FGarbageCollectionResult CollectionResult = Collector.Advance(FGarbageCollectionBudget{1, 1, 1});

	// Assert
	MW_EXPECT_EQ(Test, EObjectResult::Success, RegistrationResult, "The graph class should register");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, RequestResult, "The outer collection should start");
	MW_EXPECT_EQ(Test, ERuntimeResult::LifecycleLocked, ReentrantResult, "A managed visitor cannot recursively advance the active collector");
	MW_EXPECT_TRUE(Test, CollectionResult.bCycleComplete, "Rejecting reentry must not prevent the outer cycle from completing");
	MW_EXPECT_TRUE(Test, Creation.Object.Get() != nullptr, "The rooted object must remain live after rejected recursive advance");
}

/**
 * Motivation: Build a collector whose worklist is smaller than the store's slot capacity and request a collection.
 * Responsibilities: The request is rejected atomically with capacity exhaustion; the collector stays idle and
 *   observable, the object is not reclaimed and.
 */
MW_TEST_CASE(GarbageCollectorRejectsInsufficientWorklistAtomically)
{
	// Arrange
	FGraphLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterGraphDescriptor(Registry, Descriptor);
	TGraphStoreFixture<3, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FGraphObject> Creation = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	std::array<FObjectHandle, 2> TooSmallWorklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{TooSmallWorklist.data(), static_cast<std::uint32_t>(TooSmallWorklist.size())});

	// Act
	const ERuntimeResult RequestResult = Collector.RequestCollection();

	// Assert
	const EObjectResult ExpectedObjectSuccess = EObjectResult::Success;
	const ERuntimeResult ExpectedCapacityFailure = ERuntimeResult::CapacityExceeded;
	const std::uint32_t ExpectedRejectedRequests = 1;
	const std::uint32_t ExpectedDestructionCount = 0;
	const bool bObjectStillResolves = Creation.Object.Get() != nullptr;
	const EGarbageCollectionPhase CollectorPhase = Collector.Phase();
	const MicroWorld::Engine::FGarbageCollectionStats CollectionStats = Collector.Stats();
	MW_EXPECT_EQ(Test, ExpectedObjectSuccess, RegistrationResult, "The graph class should register");
	MW_EXPECT_EQ(Test, ExpectedCapacityFailure, RequestResult, "A worklist smaller than slot capacity should reject collection");
	MW_EXPECT_EQ(Test, EGarbageCollectionPhase::Idle, CollectorPhase, "Rejected collection should remain idle");
	MW_EXPECT_EQ(Test, ExpectedRejectedRequests, CollectionStats.RejectedRequests, "Rejected storage should be observable");
	MW_EXPECT_EQ(Test, ExpectedDestructionCount, Lifetime.DestructionCount, "Rejected collection must not reclaim an object");
	MW_EXPECT_TRUE(Test, bObjectStillResolves, "The object should remain live after atomic request rejection");
}

/**
 * Motivation: Scenario: Give a Store A holder a foreign reference to a Store B object whose handle matches a
 *   distinct unrelated Store A object, then run a full collection on Store A.
 * Responsibilities: Expected: The unrelated same-valued local object is reclaimed; the rooted holder survives and the
 *   foreign Store B object is unaffected.
 */
MW_TEST_CASE(GarbageCollectorIgnoresCrossStoreSameValuedReference)
{
	// Arrange
	FGraphLifetimeState StoreALifetime{};
	FGraphLifetimeState StoreBLifetime{};
	TClassRegistry<4> Registry;
	FClassDescriptor HolderDescriptor =
		MakeClassDescriptor<FCrossStoreReferenceHolder>(2, "CrossStoreHolder", nullptr, &TraceManagedObjectReferences);
	FClassDescriptor LeafDescriptor = MakeClassDescriptor<FCrossStoreLeaf>(3, "CrossStoreLeaf");
	const EObjectResult HolderRegistrationResult = Registry.Register(HolderDescriptor);
	const EObjectResult LeafRegistrationResult = Registry.Register(LeafDescriptor);
	const FClassDescriptor* const RegisteredHolderDescriptor = Registry.Find(HolderDescriptor.TypeId);
	const FClassDescriptor* const RegisteredLeafDescriptor = Registry.Find(LeafDescriptor.TypeId);
	TGraphStoreFixture<2, 1> StoreAFixture(MakeClassRegistryView(Registry));
	TGraphStoreFixture<2, 0> StoreBFixture(MakeClassRegistryView(Registry));
	FObjectStore& StoreA = StoreAFixture.GetStore();
	FObjectStore& StoreB = StoreBFixture.GetStore();
	MW_EXPECT_TRUE(Test, RegisteredHolderDescriptor != nullptr, "The registry should expose its owned holder descriptor");
	MW_EXPECT_TRUE(Test, RegisteredLeafDescriptor != nullptr, "The registry should expose its owned leaf descriptor");
	if (RegisteredHolderDescriptor == nullptr || RegisteredLeafDescriptor == nullptr)
	{
		return;
	}

	const TObjectCreationResult<FCrossStoreReferenceHolder> StoreAHolder =
		StoreA.NewObject<FCrossStoreReferenceHolder>(*RegisteredHolderDescriptor, StoreALifetime);
	const TObjectCreationResult<FCrossStoreLeaf> StoreAUnrelated = StoreA.NewObject<FCrossStoreLeaf>(*RegisteredLeafDescriptor, StoreALifetime);
	const TObjectCreationResult<FCrossStoreReferenceHolder> StoreBDummy =
		StoreB.NewObject<FCrossStoreReferenceHolder>(*RegisteredHolderDescriptor, StoreBLifetime);
	const TObjectCreationResult<FCrossStoreLeaf> StoreBReferenced = StoreB.NewObject<FCrossStoreLeaf>(*RegisteredLeafDescriptor, StoreBLifetime);
	MW_EXPECT_EQ(Test, EObjectResult::Success, StoreAHolder.Result, "Store A should create the holder before tracing it");
	MW_EXPECT_EQ(Test, EObjectResult::Success, StoreAUnrelated.Result, "Store A should create the unrelated leaf");
	MW_EXPECT_EQ(Test, EObjectResult::Success, StoreBDummy.Result, "Store B should align handle values for the regression");
	MW_EXPECT_EQ(Test, EObjectResult::Success, StoreBReferenced.Result, "Store B should create the foreign leaf");
	if (StoreAHolder.Object.Get() == nullptr || StoreAUnrelated.Object.Get() == nullptr || StoreBDummy.Object.Get() == nullptr
		|| StoreBReferenced.Object.Get() == nullptr)
	{
		return;
	}

	static_cast<void>(StoreBDummy);
	StoreAHolder.Object.Get()->SetReference(StoreBReferenced.Object);
	TStrongObjectPointerResult<FCrossStoreReferenceHolder> HolderRoot = StoreA.MakeStrongObjectPtr(StoreAHolder.Object);
	static_cast<void>(HolderRoot);
	std::array<FObjectHandle, 2> Worklist{};
	FGarbageCollector Collector(StoreA, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())});
	const bool bHandlesHaveSameValue = StoreAUnrelated.Object.Handle() == StoreBReferenced.Object.Handle();

	// Act
	const FGarbageCollectionResult CollectionResult = Collector.CollectFull();

	// Assert
	const EObjectResult ExpectedObjectSuccess = EObjectResult::Success;
	const std::uint32_t ExpectedReclaimedObjects = 1;
	const bool bHolderSurvives = StoreAHolder.Object.Get() != nullptr;
	const bool bUnrelatedWasReclaimed = StoreAUnrelated.Object.Get() == nullptr;
	const bool bForeignObjectUnaffected = StoreBReferenced.Object.Get() != nullptr;
	MW_EXPECT_EQ(Test, ExpectedObjectSuccess, HolderRegistrationResult, "The cross-store holder descriptor should register");
	MW_EXPECT_EQ(Test, ExpectedObjectSuccess, LeafRegistrationResult, "The cross-store leaf descriptor should register");
	MW_EXPECT_TRUE(Test, bHandlesHaveSameValue, "The regression requires identical index and generation values across stores");
	MW_EXPECT_EQ(Test, ExpectedReclaimedObjects, CollectionResult.ObjectsReclaimed, "The unrelated same-valued local object should be reclaimed");
	MW_EXPECT_TRUE(Test, bHolderSurvives, "The locally rooted holder should survive collection");
	MW_EXPECT_TRUE(Test, bUnrelatedWasReclaimed, "A foreign pointer must not retain the local same-valued lifetime");
	MW_EXPECT_TRUE(Test, bForeignObjectUnaffected, "Collecting Store A must not mutate Store B");
}

} // namespace
