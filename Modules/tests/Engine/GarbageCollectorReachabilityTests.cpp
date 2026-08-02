#include "TestSupport.h"
#include "GarbageCollectorTestGraph.h"

#include <MicroWorld/Engine/GarbageCollector.h>

#include <array>
#include <cstdint>

namespace
{

using namespace ::MicroWorld::Tests;

/**
 * Motivation: Build a three-node chain rooted at one strong pointer and run a full collection.
 * Responsibilities: The cycle completes and reclaims nothing; every rooted and transitively-traced node remains
 *   resolvable and occupied.
 */
MW_TEST_CASE(GarbageCollectorPreservesRootedGraph)
{
	// Arrange
	FGraphLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterGraphDescriptor(Registry, Descriptor);
	TGraphStoreFixture<3, 1> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FGraphObject> First = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const TObjectCreationResult<FGraphObject> Second = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const TObjectCreationResult<FGraphObject> Third = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	First.Object.Get()->SetReference(0, Second.Object);
	Second.Object.Get()->SetReference(0, Third.Object);
	TStrongObjectPointerResult<FGraphObject> Root = Store.MakeStrongObjectPtr(First.Object);
	std::array<FObjectHandle, 3> Worklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())});

	// Act
	const FGarbageCollectionResult CollectionResult = Collector.CollectFull();

	// Assert
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

/**
 * Motivation: Build two objects that reference each other with no root and run a full collection.
 * Responsibilities: The unreachable cycle is reclaimed; both members begin destruction and are destroyed exactly once
 *   and both weak observers expire.
 */
MW_TEST_CASE(GarbageCollectorReclaimsUnrootedCycle)
{
	// Arrange
	FGraphLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterGraphDescriptor(Registry, Descriptor);
	TGraphStoreFixture<2, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FGraphObject> First = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const TObjectCreationResult<FGraphObject> Second = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	First.Object.Get()->SetReference(0, Second.Object);
	Second.Object.Get()->SetReference(0, First.Object);
	TWeakObjectPtr<FGraphObject> FirstWeak(First.Object);
	TWeakObjectPtr<FGraphObject> SecondWeak(Second.Object);
	std::array<FObjectHandle, 2> Worklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())});

	// Act
	const FGarbageCollectionResult CollectionResult = Collector.CollectFull();

	// Assert
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

/**
 * Motivation: Create one unrooted object with a weak observer and run a full collection.
 * Responsibilities: The isolated object is reclaimed; weak observation alone does not keep the object reachable and
 *   expires after collection.
 */
MW_TEST_CASE(GarbageCollectorExpiresWeakReferenceWithoutRooting)
{
	// Arrange
	FGraphLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterGraphDescriptor(Registry, Descriptor);
	TGraphStoreFixture<1, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FGraphObject> Creation = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	TWeakObjectPtr<FGraphObject> WeakObject(Creation.Object);
	std::array<FObjectHandle, 1> Worklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())});

	// Act
	const FGarbageCollectionResult CollectionResult = Collector.CollectFull();

	// Assert
	const EObjectResult ExpectedObjectSuccess = EObjectResult::Success;
	const std::uint32_t ExpectedReclaimedObjects = 1;
	const bool bWeakExpired = WeakObject.IsExpired();
	MW_EXPECT_EQ(Test, ExpectedObjectSuccess, RegistrationResult, "The graph class should register");
	MW_EXPECT_EQ(Test, ExpectedReclaimedObjects, CollectionResult.ObjectsReclaimed, "The unrooted object should be reclaimed");
	MW_EXPECT_TRUE(Test, bWeakExpired, "Weak observation must not keep the object reachable");
}

} // namespace
