#include "TestSupport.h"
#include "GarbageCollectorTestGraph.h"

#include <MicroWorld/Engine/GarbageCollectionBudget.h>
#include <MicroWorld/Engine/GarbageCollectionPhase.h>
#include <MicroWorld/Engine/GarbageCollectionResult.h>
#include <MicroWorld/Engine/GarbageCollectionStats.h>
#include <MicroWorld/Engine/GarbageCollector.h>

#include <array>
#include <cstdint>

namespace
{

using namespace ::MicroWorld::Tests;

/**
 * Motivation: Run the same rooted-chain graph once with a full collection and once with one-operation slices.
 * Responsibilities: Both modes complete the cycle, reclaim the same objects, preserve the same graph, and run the same
 *   destructors.
 */
MW_TEST_CASE(GarbageCollectorIncrementalAndFullCyclesHaveEquivalentOutcomes)
{
	// Act
	const FCollectionObservation FullObservation = ObserveEquivalentCollection(false);

	const FCollectionObservation IncrementalObservation = ObserveEquivalentCollection(true);

	// Assert
	MW_EXPECT_TRUE(Test, FullObservation.bCycleComplete, "Full collection should complete the comparison cycle");
	MW_EXPECT_TRUE(Test, IncrementalObservation.bCycleComplete, "Incremental collection should complete the comparison cycle");
	MW_EXPECT_EQ(
		Test, FullObservation.ReclaimedObjects, IncrementalObservation.ReclaimedObjects, "Both collection modes should reclaim the same objects");
	MW_EXPECT_EQ(Test, FullObservation.OccupiedSlots, IncrementalObservation.OccupiedSlots, "Both collection modes should preserve the same graph");
	MW_EXPECT_EQ(
		Test, FullObservation.DestructionCount, IncrementalObservation.DestructionCount, "Both collection modes should run the same destructors");
}

/**
 * Motivation: Start a cycle, advance once with a zero budget, then drive repeated one-operation slices to
 *   completion.
 * Responsibilities: The zero budget performs no work and keeps the waiting phase; every bounded slice respects its
 *   one-operation phase budget and the cycle.
 */
MW_TEST_CASE(GarbageCollectorHonorsZeroAndOneOperationBudgets)
{
	// Arrange
	FGraphLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterGraphDescriptor(Registry, Descriptor);
	TGraphStoreFixture<2, 1> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FGraphObject> Rooted = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const TObjectCreationResult<FGraphObject> Unreachable = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	static_cast<void>(Unreachable);
	TStrongObjectPointerResult<FGraphObject> Root = Store.MakeStrongObjectPtr(Rooted.Object);
	static_cast<void>(Root);
	std::array<FObjectHandle, 2> Worklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())});
	const ERuntimeResult RequestResult = Collector.RequestCollection();

	// Act
	const FGarbageCollectionResult ZeroBudgetResult = Collector.Advance(ZeroSliceBudget);
	bool bEverySliceBounded = true;
	FGarbageCollectionResult FinalResult{};
	for (std::uint32_t Slice = 0; Slice < BoundedSliceIterationCap && Collector.Phase() != EGarbageCollectionPhase::Idle; ++Slice)
	{
		const FGarbageCollectionResult SliceResult = Collector.Advance(UnitSliceBudget);
		const bool bRootWithinBudget = SliceResult.RootOperations <= UnitSliceBudget.MaxRootOperations;
		const bool bMarkWithinBudget = SliceResult.MarkOperations <= UnitSliceBudget.MaxMarkOperations;
		const bool bSweepWithinBudget = SliceResult.SweepOperations <= UnitSliceBudget.MaxSweepOperations;
		const bool bTotalWithinBudget = SliceResult.OperationsPerformed <= MaxOperationsPerUnitSlice;
		if (!bRootWithinBudget || !bMarkWithinBudget || !bSweepWithinBudget || !bTotalWithinBudget)
		{
			bEverySliceBounded = false;
		}
		FinalResult = SliceResult;
	}

	// Assert
	const EObjectResult ExpectedObjectSuccess = EObjectResult::Success;
	const ERuntimeResult ExpectedCollectionSuccess = ERuntimeResult::Success;
	const std::uint32_t ExpectedZeroOperations = 0;
	const std::uint32_t ExpectedReclaimedObjects = 1;
	const MicroWorld::Engine::FGarbageCollectionStats CollectionStats = Collector.Stats();
	MW_EXPECT_EQ(Test, ExpectedObjectSuccess, RegistrationResult, "The graph class should register");
	MW_EXPECT_EQ(Test, ExpectedCollectionSuccess, RequestResult, "An adequately provisioned cycle should start");
	MW_EXPECT_EQ(Test, ExpectedZeroOperations, ZeroBudgetResult.OperationsPerformed, "Zero budgets must perform no hidden work");
	MW_EXPECT_EQ(Test, EGarbageCollectionPhase::SeedRoots, ZeroBudgetResult.Phase, "Zero budgets should preserve the waiting phase");
	MW_EXPECT_TRUE(Test, bEverySliceBounded, "No phase should exceed its one-operation slice budget");
	MW_EXPECT_TRUE(Test, FinalResult.bCycleComplete, "Repeated bounded slices should eventually finish");
	MW_EXPECT_EQ(Test, ExpectedReclaimedObjects, CollectionStats.ReclaimedObjects, "The one unreachable object should be reclaimed");
}

} // namespace
