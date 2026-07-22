#include "TestSupport.h"

#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/ObjectStore.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::EGarbageCollectionPhase;
using MicroWorld::EObjectResult;
using MicroWorld::ERuntimeResult;
using MicroWorld::FClassDescriptor;
using MicroWorld::FGarbageCollectionBudget;
using MicroWorld::FGarbageCollectionResult;
using MicroWorld::FGarbageCollector;
using MicroWorld::FGarbageCollectorStorage;
using MicroWorld::FObjectHandle;
using MicroWorld::FObjectRootEntry;
using MicroWorld::FObjectSlotMetadata;
using MicroWorld::FObjectStore;
using MicroWorld::FObjectStoreStats;
using MicroWorld::FObjectStoreStorage;
using MicroWorld::FReferenceCollector;
using MicroWorld::MakeClassDescriptor;
using MicroWorld::MakeClassRegistryView;
using MicroWorld::TClassRegistry;
using MicroWorld::TObjectPtr;
using MicroWorld::TraceManagedObjectReferences;
using MicroWorld::TWeakObjectPtr;
using MicroWorld::UObject;

/** Records collector-visible lifetime completion in fresh per-test state. */
struct FGraphLifetimeState final
{
	/** Counts successfully constructed graph nodes. */
	std::uint32_t ConstructionCount{0};

	/** Counts nodes entering managed destruction. */
	std::uint32_t BeginDestroyCount{0};

	/** Counts exact graph-node destructor executions. */
	std::uint32_t DestructionCount{0};
};

/** Provides two explicit outgoing handles for graph and bounded-visitor tests. */
class FGraphObject final : public UObject
{
public:
	/** Begins one observable graph-node lifetime. */
	explicit FGraphObject(FGraphLifetimeState& InState) noexcept : State(InState) { ++State.ConstructionCount; }

	/** Records exact derived destruction after collector reclamation. */
	~FGraphObject() noexcept override { ++State.DestructionCount; }

	/** Replaces one bounded outgoing edge without changing target lifetime. */
	void SetReference(const std::size_t Index, const TObjectPtr<FGraphObject> Reference) noexcept
	{
		if (Index >= References.size())
		{
			return;
		}

		References[Index] = Reference;
		if (ReferenceCount <= Index)
		{
			ReferenceCount = Index + 1;
		}
	}

	/** Requests one deliberately recursive advance from the next reference visit. */
	void SetReentrantAdvance(FGarbageCollector& InCollector, ERuntimeResult& OutResult) noexcept
	{
		ReentrantCollector = &InCollector;
		ReentrantResult = &OutResult;
	}

protected:
	/** Presents every configured edge to the active iterative collector. */
	void VisitReferences(FReferenceCollector& Collector) noexcept override
	{
		if (ReentrantCollector != nullptr && ReentrantResult != nullptr)
		{
			*ReentrantResult = ReentrantCollector->Advance(FGarbageCollectionBudget{1, 1, 1}).Result;
		}

		for (std::size_t Index = 0; Index < ReferenceCount; ++Index)
		{
			Collector.AddReferencedObject(References[Index]);
		}
	}

	/** Records the lifecycle barrier before exact destruction. */
	void BeginDestroy() noexcept override { ++State.BeginDestroyCount; }

private:
	/** Shares observations only with the test that owns this node. */
	FGraphLifetimeState& State;

	/** Holds a small same-store typed graph without dynamic storage. */
	std::array<TObjectPtr<FGraphObject>, 2> References{};

	/** Bounds visitor work to edges explicitly configured by the test. */
	std::size_t ReferenceCount{0};

	/** Selects the active collector targeted only by the recursive-advance regression. */
	FGarbageCollector* ReentrantCollector{nullptr};

	/** Exposes the recursive call result without global test state. */
	ERuntimeResult* ReentrantResult{nullptr};
};

/** Provides a complete typed target for cross-store pointer-origin validation. */
class FCrossStoreLeaf final : public UObject
{
public:
	/** Begins one observable leaf lifetime. */
	explicit FCrossStoreLeaf(FGraphLifetimeState& InState) noexcept : State(InState) { ++State.ConstructionCount; }

	/** Records exact leaf destruction independently from holder destruction. */
	~FCrossStoreLeaf() noexcept override { ++State.DestructionCount; }

protected:
	/** Records managed leaf teardown before exact destruction. */
	void BeginDestroy() noexcept override { ++State.BeginDestroyCount; }

private:
	/** Shares observations only with the store-specific test state. */
	FGraphLifetimeState& State;
};

/** Presents one typed reference whose originating store must be validated. */
class FCrossStoreReferenceHolder final : public UObject
{
public:
	/** Begins one observable holder lifetime. */
	explicit FCrossStoreReferenceHolder(FGraphLifetimeState& InState) noexcept : State(InState) { ++State.ConstructionCount; }

	/** Records exact holder destruction. */
	~FCrossStoreReferenceHolder() noexcept override { ++State.DestructionCount; }

	/** Selects the typed reference presented during the next trace. */
	void SetReference(const TObjectPtr<FCrossStoreLeaf> InReference) noexcept { Reference = InReference; }

protected:
	/** Exercises TObjectPtr store-origin validation at the collector boundary. */
	void VisitReferences(FReferenceCollector& Collector) noexcept override { Collector.AddReferencedObject(Reference); }

	/** Records managed holder teardown before exact destruction. */
	void BeginDestroy() noexcept override { ++State.BeginDestroyCount; }

private:
	/** Shares observations only with the holder's store-specific test state. */
	FGraphLifetimeState& State;

	/** Retains the foreign-or-local typed identity without caching its raw address. */
	TObjectPtr<FCrossStoreLeaf> Reference{};
};

/** Owns one isolated fixed object store for collector behavior tests. */
template<std::uint32_t SlotCount, std::uint32_t RootCapacity>
class TGraphStoreFixture final
{
public:
	/** Binds the store to this fixture's complete aligned caller-owned storage. */
	explicit TGraphStoreFixture(const MicroWorld::FClassRegistryView Classes) noexcept : Store(MakeStorage(), Classes) {}

	/** Exposes the public store under test. */
	FObjectStore& GetStore() noexcept { return Store; }

private:
	static constexpr std::size_t SlotSizeBytes = 128;
	static constexpr std::size_t SlotAlignmentBytes = 16;

	/** Describes the fixed storage retained by this fixture. */
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

	/** Provides aligned non-moving placement storage for every graph node. */
	alignas(SlotAlignmentBytes) std::array<std::byte, SlotSizeBytes * SlotCount> SlotBytes{};

	/** Provides one lifecycle record per fixed slot. */
	std::array<FObjectSlotMetadata, SlotCount> Slots{};

	/** Provides independently counted explicit root entries. */
	std::array<FObjectRootEntry, RootCapacity> Roots{};

	/** Owns managed graph lifetimes while all fixture storage is alive. */
	FObjectStore Store;
};

/** Registers the one graph-node class used by a fresh test. */
EObjectResult RegisterGraphDescriptor(TClassRegistry<2>& Registry, const FClassDescriptor*& OutDescriptor) noexcept
{
	const FClassDescriptor Candidate = MakeClassDescriptor<FGraphObject>(1, "GraphObject", nullptr, &TraceManagedObjectReferences);
	const EObjectResult Result = Registry.Register(Candidate);
	OutDescriptor = Registry.Find(Candidate.TypeId);
	return Result;
}

/** Captures equivalent observable outcomes from full and incremental collection. */
struct FCollectionObservation final
{
	/** Reports whether the tested cycle completed successfully. */
	bool bCycleComplete{false};

	/** Counts objects reclaimed by the tested cycle. */
	std::uint32_t ReclaimedObjects{0};

	/** Counts objects remaining immediately after the tested cycle. */
	std::uint32_t OccupiedSlots{0};

	/** Counts exact destructors run by the tested cycle. */
	std::uint32_t DestructionCount{0};
};

/** Runs the same rooted-chain graph using either full or one-operation slices. */
FCollectionObservation ObserveEquivalentCollection(const bool bIncremental) noexcept
{
	FGraphLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	(void)RegisterGraphDescriptor(Registry, Descriptor);
	TGraphStoreFixture<4, 1> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const auto First = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const auto Second = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const auto Third = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const auto Unreachable = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	static_cast<void>(Unreachable);
	First.Object.Get()->SetReference(0, Second.Object);
	Second.Object.Get()->SetReference(0, Third.Object);
	auto Root = Store.MakeStrongObjectPtr(First.Object);
	static_cast<void>(Root);

	std::array<FObjectHandle, 4> Worklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())});
	FGarbageCollectionResult FinalResult{};
	if (!bIncremental)
	{
		FinalResult = Collector.CollectFull();
	}
	else
	{
		(void)Collector.RequestCollection();
		for (std::uint32_t Slice = 0; Slice < 32 && Collector.Phase() != EGarbageCollectionPhase::Idle; ++Slice)
		{
			FinalResult = Collector.Advance(FGarbageCollectionBudget{1, 1, 1});
		}
	}

	const FObjectStoreStats StoreStats = Store.Stats();
	const MicroWorld::FGarbageCollectionStats CollectionStats = Collector.Stats();
	return FCollectionObservation{
		FinalResult.bCycleComplete,
		CollectionStats.ReclaimedObjects,
		StoreStats.OccupiedSlots,
		Lifetime.DestructionCount,
	};
}

/** Proves a root keeps its complete descriptor-visible object graph reachable. */
MW_TEST_CASE(GarbageCollectorPreservesRootedGraph)
{
	FGraphLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterGraphDescriptor(Registry, Descriptor);
	TGraphStoreFixture<3, 1> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const auto First = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const auto Second = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const auto Third = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	First.Object.Get()->SetReference(0, Second.Object);
	Second.Object.Get()->SetReference(0, Third.Object);
	auto Root = Store.MakeStrongObjectPtr(First.Object);
	std::array<FObjectHandle, 3> Worklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())});

	const FGarbageCollectionResult CollectionResult = Collector.CollectFull();

	const EObjectResult ExpectedObjectSuccess = EObjectResult::Success;
	const ERuntimeResult ExpectedCollectionSuccess = ERuntimeResult::Success;
	const std::uint32_t ExpectedOccupiedSlots = 3;
	const std::uint32_t ExpectedReclaimedObjects = 0;
	const bool bFirstResolves = First.Object.Get() != nullptr;
	const bool bSecondResolves = Second.Object.Get() != nullptr;
	const bool bThirdResolves = Third.Object.Get() != nullptr;
	const FObjectStoreStats StoreStats = Store.Stats();
	MW_EXPECT_EQ(Test, ExpectedObjectSuccess, RegistrationResult, "The graph class should register");
	MW_EXPECT_EQ(Test, ExpectedCollectionSuccess, CollectionResult.Result, "A full rooted collection should succeed");
	MW_EXPECT_TRUE(Test, CollectionResult.bCycleComplete, "A full collection should complete its cycle");
	MW_EXPECT_EQ(Test, ExpectedReclaimedObjects, CollectionResult.ObjectsReclaimed, "No reachable graph node should be reclaimed");
	MW_EXPECT_EQ(Test, ExpectedOccupiedSlots, StoreStats.OccupiedSlots, "Every rooted graph node should remain occupied");
	MW_EXPECT_TRUE(Test, bFirstResolves, "The explicit root should remain resolvable");
	MW_EXPECT_TRUE(Test, bSecondResolves, "The first traced child should remain resolvable");
	MW_EXPECT_TRUE(Test, bThirdResolves, "The transitive traced child should remain resolvable");
}

/** Proves an unreachable reference cycle is reclaimed without recursive ownership. */
MW_TEST_CASE(GarbageCollectorReclaimsUnrootedCycle)
{
	FGraphLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterGraphDescriptor(Registry, Descriptor);
	TGraphStoreFixture<2, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const auto First = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const auto Second = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	First.Object.Get()->SetReference(0, Second.Object);
	Second.Object.Get()->SetReference(0, First.Object);
	TWeakObjectPtr<FGraphObject> FirstWeak(First.Object);
	TWeakObjectPtr<FGraphObject> SecondWeak(Second.Object);
	std::array<FObjectHandle, 2> Worklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())});

	const FGarbageCollectionResult CollectionResult = Collector.CollectFull();

	const EObjectResult ExpectedObjectSuccess = EObjectResult::Success;
	const std::uint32_t ExpectedReclaimedObjects = 2;
	const std::uint32_t ExpectedDestructionCount = 2;
	const bool bFirstWeakExpired = FirstWeak.IsExpired();
	const bool bSecondWeakExpired = SecondWeak.IsExpired();
	MW_EXPECT_EQ(Test, ExpectedObjectSuccess, RegistrationResult, "The graph class should register");
	MW_EXPECT_TRUE(Test, CollectionResult.bCycleComplete, "Cycle collection should complete");
	MW_EXPECT_EQ(Test, ExpectedReclaimedObjects, CollectionResult.ObjectsReclaimed, "Both unreachable cycle members should be reclaimed");
	MW_EXPECT_EQ(Test, ExpectedDestructionCount, Lifetime.BeginDestroyCount, "Every cycle member should begin destruction once");
	MW_EXPECT_EQ(Test, ExpectedDestructionCount, Lifetime.DestructionCount, "Every cycle member should be destroyed once");
	MW_EXPECT_TRUE(Test, bFirstWeakExpired, "The first weak observer should expire after cycle collection");
	MW_EXPECT_TRUE(Test, bSecondWeakExpired, "The second weak observer should expire after cycle collection");
}

/** Proves weak observation expires when an otherwise isolated object is collected. */
MW_TEST_CASE(GarbageCollectorExpiresWeakReferenceWithoutRooting)
{
	FGraphLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterGraphDescriptor(Registry, Descriptor);
	TGraphStoreFixture<1, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const auto Creation = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	TWeakObjectPtr<FGraphObject> WeakObject(Creation.Object);
	std::array<FObjectHandle, 1> Worklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())});

	const FGarbageCollectionResult CollectionResult = Collector.CollectFull();

	const EObjectResult ExpectedObjectSuccess = EObjectResult::Success;
	const std::uint32_t ExpectedReclaimedObjects = 1;
	const bool bWeakExpired = WeakObject.IsExpired();
	MW_EXPECT_EQ(Test, ExpectedObjectSuccess, RegistrationResult, "The graph class should register");
	MW_EXPECT_EQ(Test, ExpectedReclaimedObjects, CollectionResult.ObjectsReclaimed, "The unrooted object should be reclaimed");
	MW_EXPECT_TRUE(Test, bWeakExpired, "Weak observation must not keep the object reachable");
}

/** Proves one-operation slices produce the same graph result as full collection. */
MW_TEST_CASE(GarbageCollectorIncrementalAndFullCyclesHaveEquivalentOutcomes)
{
	const FCollectionObservation FullObservation = ObserveEquivalentCollection(false);

	const FCollectionObservation IncrementalObservation = ObserveEquivalentCollection(true);

	MW_EXPECT_TRUE(Test, FullObservation.bCycleComplete, "Full collection should complete the comparison cycle");
	MW_EXPECT_TRUE(Test, IncrementalObservation.bCycleComplete, "Incremental collection should complete the comparison cycle");
	MW_EXPECT_EQ(
		Test, FullObservation.ReclaimedObjects, IncrementalObservation.ReclaimedObjects, "Both collection modes should reclaim the same objects");
	MW_EXPECT_EQ(Test, FullObservation.OccupiedSlots, IncrementalObservation.OccupiedSlots, "Both collection modes should preserve the same graph");
	MW_EXPECT_EQ(
		Test, FullObservation.DestructionCount, IncrementalObservation.DestructionCount, "Both collection modes should run the same destructors");
}

/** Proves zero budgets do no work and one-operation phase budgets are respected. */
MW_TEST_CASE(GarbageCollectorHonorsZeroAndOneOperationBudgets)
{
	FGraphLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterGraphDescriptor(Registry, Descriptor);
	TGraphStoreFixture<2, 1> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const auto Rooted = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const auto Unreachable = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	static_cast<void>(Unreachable);
	auto Root = Store.MakeStrongObjectPtr(Rooted.Object);
	static_cast<void>(Root);
	std::array<FObjectHandle, 2> Worklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())});
	const ERuntimeResult RequestResult = Collector.RequestCollection();

	const FGarbageCollectionResult ZeroBudgetResult = Collector.Advance(FGarbageCollectionBudget{0, 0, 0});
	bool bEverySliceBounded = true;
	FGarbageCollectionResult FinalResult{};
	for (std::uint32_t Slice = 0; Slice < 16 && Collector.Phase() != EGarbageCollectionPhase::Idle; ++Slice)
	{
		const FGarbageCollectionResult SliceResult = Collector.Advance(FGarbageCollectionBudget{1, 1, 1});
		if (SliceResult.RootOperations > 1 || SliceResult.MarkOperations > 1 || SliceResult.SweepOperations > 1
			|| SliceResult.OperationsPerformed > 3)
		{
			bEverySliceBounded = false;
		}
		FinalResult = SliceResult;
	}

	const EObjectResult ExpectedObjectSuccess = EObjectResult::Success;
	const ERuntimeResult ExpectedCollectionSuccess = ERuntimeResult::Success;
	const std::uint32_t ExpectedZeroOperations = 0;
	const std::uint32_t ExpectedReclaimedObjects = 1;
	const MicroWorld::FGarbageCollectionStats CollectionStats = Collector.Stats();
	MW_EXPECT_EQ(Test, ExpectedObjectSuccess, RegistrationResult, "The graph class should register");
	MW_EXPECT_EQ(Test, ExpectedCollectionSuccess, RequestResult, "An adequately provisioned cycle should start");
	MW_EXPECT_EQ(Test, ExpectedZeroOperations, ZeroBudgetResult.OperationsPerformed, "Zero budgets must perform no hidden work");
	MW_EXPECT_EQ(Test, EGarbageCollectionPhase::SeedRoots, ZeroBudgetResult.Phase, "Zero budgets should preserve the waiting phase");
	MW_EXPECT_TRUE(Test, bEverySliceBounded, "No phase should exceed its one-operation slice budget");
	MW_EXPECT_TRUE(Test, FinalResult.bCycleComplete, "Repeated bounded slices should eventually finish");
	MW_EXPECT_EQ(Test, ExpectedReclaimedObjects, CollectionStats.ReclaimedObjects, "The one unreachable object should be reclaimed");
}

/** Proves one visitor may discover multiple references while charging one mark operation. */
MW_TEST_CASE(GarbageCollectorMultiReferenceVisitorCountsOneMarkAndPreservesGraph)
{
	FGraphLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterGraphDescriptor(Registry, Descriptor);
	TGraphStoreFixture<3, 1> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const auto Parent = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const auto FirstChild = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const auto SecondChild = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	Parent.Object.Get()->SetReference(0, FirstChild.Object);
	Parent.Object.Get()->SetReference(1, SecondChild.Object);
	auto Root = Store.MakeStrongObjectPtr(Parent.Object);
	static_cast<void>(Root);
	std::array<FObjectHandle, 3> Worklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())});
	const ERuntimeResult RequestResult = Collector.RequestCollection();

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

	const EObjectResult ExpectedObjectSuccess = EObjectResult::Success;
	const ERuntimeResult ExpectedCollectionSuccess = ERuntimeResult::Success;
	const std::uint32_t ExpectedOccupiedSlots = 3;
	const std::uint32_t ExpectedReclaimedObjects = 0;
	const FObjectStoreStats StoreStats = Store.Stats();
	const MicroWorld::FGarbageCollectionStats CollectionStats = Collector.Stats();
	MW_EXPECT_EQ(Test, ExpectedObjectSuccess, RegistrationResult, "The graph class should register");
	MW_EXPECT_EQ(Test, ExpectedCollectionSuccess, RequestResult, "The bounded traversal should start");
	MW_EXPECT_TRUE(Test, bMarkBudgetRespected, "A multi-reference visitor should consume only one mark operation");
	MW_EXPECT_TRUE(Test, FinalResult.bCycleComplete, "Bounded mark slices should finish the cycle");
	MW_EXPECT_EQ(Test, ExpectedReclaimedObjects, CollectionStats.ReclaimedObjects, "Both discovered children should remain reachable");
	MW_EXPECT_EQ(Test, ExpectedOccupiedSlots, StoreStats.OccupiedSlots, "The complete two-edge graph should survive");
}

/** Proves a deep reachable chain is traversed iteratively without call-stack recursion. */
MW_TEST_CASE(GarbageCollectorTraversesDeepGraphWithoutRecursion)
{
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
		const auto Creation = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
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
	auto Root = Store.MakeStrongObjectPtr(Nodes[0]);
	std::array<FObjectHandle, NodeCount> Worklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())});

	const FGarbageCollectionResult RootedCollection = Collector.CollectFull();
	Root.Pointer.Reset();
	const FGarbageCollectionResult UnrootedCollection = Collector.CollectFull();

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

/** Proves an incremental cycle exclusively owns all reachability-changing store mutation. */
MW_TEST_CASE(GarbageCollectorLocksMutationAndSecondCollectorBetweenSlices)
{
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
	const auto Rooted = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	auto Root = Store.MakeStrongObjectPtr(Rooted.Object);
	std::array<FObjectHandle, 2> FirstWorklist{};
	std::array<FObjectHandle, 2> SecondWorklist{};
	FGarbageCollector FirstCollector(Store, FGarbageCollectorStorage{FirstWorklist.data(), static_cast<std::uint32_t>(FirstWorklist.size())});
	FGarbageCollector SecondCollector(Store, FGarbageCollectorStorage{SecondWorklist.data(), static_cast<std::uint32_t>(SecondWorklist.size())});

	const ERuntimeResult RequestResult = FirstCollector.RequestCollection();
	const FGarbageCollectionResult RootSlice = FirstCollector.Advance(FGarbageCollectionBudget{1, 0, 0});
	const auto RejectedCreation = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const EObjectResult RejectedPending = Store.MarkPendingDestroy(Rooted.Object.Handle());
	auto RejectedRoot = Store.MakeStrongObjectPtr(Rooted.Object);
	const MicroWorld::FObjectMutationResult RejectedBarrier = Store.ApplyPendingDestroy(1);
	const ERuntimeResult RejectedSecondCollector = SecondCollector.RequestCollection();
	Root.Pointer.Reset();
	const FObjectStoreStats StatsAfterRootRemoval = Store.Stats();
	const ERuntimeResult CancelResult = FirstCollector.CancelCollection();
	const auto CreationAfterCancel = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const FGarbageCollectionResult Cleanup = SecondCollector.CollectFull();

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

/** Proves collector destruction clears partial marks before releasing store ownership. */
MW_TEST_CASE(GarbageCollectorDestructorReleasesActiveCycleAndPartialMarks)
{
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
	const auto Creation = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	auto Root = Store.MakeStrongObjectPtr(Creation.Object);
	ERuntimeResult RequestResult = ERuntimeResult::InvalidLifecycle;
	EGarbageCollectionPhase PausedPhase = EGarbageCollectionPhase::Idle;
	{
		std::array<FObjectHandle, 1> AbandonedWorklist{};
		FGarbageCollector AbandonedCollector(
			Store, FGarbageCollectorStorage{AbandonedWorklist.data(), static_cast<std::uint32_t>(AbandonedWorklist.size())});
		RequestResult = AbandonedCollector.RequestCollection();
		PausedPhase = AbandonedCollector.Advance(FGarbageCollectionBudget{1, 0, 0}).Phase;
	}

	Root.Pointer.Reset();
	std::array<FObjectHandle, 1> FinalWorklist{};
	FGarbageCollector FinalCollector(Store, FGarbageCollectorStorage{FinalWorklist.data(), static_cast<std::uint32_t>(FinalWorklist.size())});
	const FGarbageCollectionResult FinalCollection = FinalCollector.CollectFull();

	MW_EXPECT_EQ(Test, EObjectResult::Success, RegistrationResult, "The graph class should register");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, RequestResult, "The abandoned collector should first acquire the store");
	MW_EXPECT_EQ(Test, EGarbageCollectionPhase::Mark, PausedPhase, "The abandoned cycle should retain one partial mark");
	MW_EXPECT_TRUE(Test, FinalCollection.bCycleComplete, "A later collector should acquire the released store");
	MW_EXPECT_EQ(Test, 1U, FinalCollection.ObjectsReclaimed, "Destructor cancellation must clear the abandoned mark");
	MW_EXPECT_EQ(Test, 1U, Lifetime.DestructionCount, "The formerly marked object should be destroyed exactly once");
}

/** Proves a reference visitor cannot recursively advance its active collector. */
MW_TEST_CASE(GarbageCollectorRejectsRecursiveAdvanceFromReferenceVisitor)
{
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
	const auto Creation = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	auto Root = Store.MakeStrongObjectPtr(Creation.Object);
	static_cast<void>(Root);
	std::array<FObjectHandle, 1> Worklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())});
	ERuntimeResult ReentrantResult = ERuntimeResult::InvalidLifecycle;
	Creation.Object.Get()->SetReentrantAdvance(Collector, ReentrantResult);

	const ERuntimeResult RequestResult = Collector.RequestCollection();
	const FGarbageCollectionResult CollectionResult = Collector.Advance(FGarbageCollectionBudget{1, 1, 1});

	MW_EXPECT_EQ(Test, EObjectResult::Success, RegistrationResult, "The graph class should register");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, RequestResult, "The outer collection should start");
	MW_EXPECT_EQ(Test, ERuntimeResult::LifecycleLocked, ReentrantResult, "A managed visitor cannot recursively advance the active collector");
	MW_EXPECT_TRUE(Test, CollectionResult.bCycleComplete, "Rejecting reentry must not prevent the outer cycle from completing");
	MW_EXPECT_TRUE(Test, Creation.Object.Get() != nullptr, "The rooted object must remain live after rejected recursive advance");
}

/** Proves insufficient iterative storage rejects a cycle before changing reachability. */
MW_TEST_CASE(GarbageCollectorRejectsInsufficientWorklistAtomically)
{
	FGraphLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterGraphDescriptor(Registry, Descriptor);
	TGraphStoreFixture<3, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const auto Creation = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	std::array<FObjectHandle, 2> TooSmallWorklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{TooSmallWorklist.data(), static_cast<std::uint32_t>(TooSmallWorklist.size())});

	const ERuntimeResult RequestResult = Collector.RequestCollection();

	const EObjectResult ExpectedObjectSuccess = EObjectResult::Success;
	const ERuntimeResult ExpectedCapacityFailure = ERuntimeResult::CapacityExceeded;
	const std::uint32_t ExpectedRejectedRequests = 1;
	const std::uint32_t ExpectedDestructionCount = 0;
	const bool bObjectStillResolves = Creation.Object.Get() != nullptr;
	const EGarbageCollectionPhase CollectorPhase = Collector.Phase();
	const MicroWorld::FGarbageCollectionStats CollectionStats = Collector.Stats();
	MW_EXPECT_EQ(Test, ExpectedObjectSuccess, RegistrationResult, "The graph class should register");
	MW_EXPECT_EQ(Test, ExpectedCapacityFailure, RequestResult, "A worklist smaller than slot capacity should reject collection");
	MW_EXPECT_EQ(Test, EGarbageCollectionPhase::Idle, CollectorPhase, "Rejected collection should remain idle");
	MW_EXPECT_EQ(Test, ExpectedRejectedRequests, CollectionStats.RejectedRequests, "Rejected storage should be observable");
	MW_EXPECT_EQ(Test, ExpectedDestructionCount, Lifetime.DestructionCount, "Rejected collection must not reclaim an object");
	MW_EXPECT_TRUE(Test, bObjectStillResolves, "The object should remain live after atomic request rejection");
}

/** Proves a same-valued handle from another store cannot retain this store's object. */
MW_TEST_CASE(GarbageCollectorIgnoresCrossStoreSameValuedReference)
{
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

	const auto StoreAHolder = StoreA.NewObject<FCrossStoreReferenceHolder>(*RegisteredHolderDescriptor, StoreALifetime);
	const auto StoreAUnrelated = StoreA.NewObject<FCrossStoreLeaf>(*RegisteredLeafDescriptor, StoreALifetime);
	const auto StoreBDummy = StoreB.NewObject<FCrossStoreReferenceHolder>(*RegisteredHolderDescriptor, StoreBLifetime);
	const auto StoreBReferenced = StoreB.NewObject<FCrossStoreLeaf>(*RegisteredLeafDescriptor, StoreBLifetime);
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
	auto HolderRoot = StoreA.MakeStrongObjectPtr(StoreAHolder.Object);
	static_cast<void>(HolderRoot);
	std::array<FObjectHandle, 2> Worklist{};
	FGarbageCollector Collector(StoreA, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())});
	const bool bHandlesHaveSameValue = StoreAUnrelated.Object.Handle() == StoreBReferenced.Object.Handle();

	const FGarbageCollectionResult CollectionResult = Collector.CollectFull();

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
