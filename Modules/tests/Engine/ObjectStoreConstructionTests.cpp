#include "TestSupport.h"
#include "ObjectStoreTestHelpers.h"

#include <MicroWorld/Engine/ClassRegistryView.h>
#include <MicroWorld/Engine/ObjectCreationResult.h>
#include <MicroWorld/Engine/ObjectMutationResult.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/ObjectStoreStats.h>
#include <MicroWorld/Engine/ObjectStoreStorage.h>

#include <cstddef>
#include <cstdint>

namespace
{

using namespace ::MicroWorld::Tests;

/**
 * Motivation: Register a valid tracked descriptor, bind the store to zeroed-out caller storage, and attempt one
 *   construction.
 * Responsibilities: The store reports an unsupported layout before construction; no object lifetime begins.
 */
MW_TEST_CASE(ObjectStoreRejectsInvalidStorageBeforeConstruction)
{
	// Arrange
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterTrackedDescriptor(Registry, Descriptor);
	FObjectStoreStorage InvalidStorage{};
	FObjectStore Store(InvalidStorage, MakeClassRegistryView(Registry));

	// Act
	const TObjectCreationResult<FTrackedObject> Creation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);

	// Assert
	const EObjectResult ExpectedRegistrationResult = EObjectResult::Success;
	const EObjectResult ExpectedConfigurationResult = EObjectResult::UnsupportedObjectLayout;
	const std::uint32_t ExpectedConstructionCount = 0;
	const EObjectResult ConfigurationResult = Store.ConfigurationResult();
	MW_EXPECT_EQ(Test, ExpectedRegistrationResult, RegistrationResult, "The valid class should register for the storage test");
	MW_EXPECT_EQ(Test, ExpectedConfigurationResult, ConfigurationResult, "Malformed store storage should be rejected explicitly");
	MW_EXPECT_EQ(Test, ExpectedConfigurationResult, Creation.Result, "Construction should return the store configuration failure");
	MW_EXPECT_EQ(Test, ExpectedConstructionCount, Lifetime.ConstructionCount, "Invalid storage must reject before placement construction");
}

/**
 * Motivation: Register a valid tracked descriptor into a structurally valid store whose single slot is too small,
 *   then attempt construction.
 * Responsibilities: Construction is rejected atomically with an unsupported-layout result; no lifetime starts and no
 *   slot is consumed.
 */
MW_TEST_CASE(ObjectStoreRejectsUnsupportedObjectLayoutBeforeConstruction)
{
	// Arrange
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterTrackedDescriptor(Registry, Descriptor);
	TObjectStoreFixture<8, 8, 1, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();

	// Act
	const TObjectCreationResult<FTrackedObject> Creation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);

	// Assert
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

/**
 * Motivation: Build a store with one registered descriptor and attempt construction through a descriptor that was
 *   never registered.
 * Responsibilities: The store rejects the unknown class; construction does not start and slot capacity is preserved.
 */
MW_TEST_CASE(ObjectStoreRejectsUnknownClassWithoutConsumingCapacity)
{
	// Arrange
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* RegisteredDescriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterTrackedDescriptor(Registry, RegisteredDescriptor);
	FClassDescriptor UnknownDescriptor = MakeClassDescriptor<FTrackedObject>(2, "Unknown");
	TObjectStoreFixture<128, 16, 1, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();

	// Act
	const TObjectCreationResult<FTrackedObject> Creation = Store.NewObject<FTrackedObject>(UnknownDescriptor, Lifetime);

	// Assert
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

/**
 * Motivation: Register a layout-equivalent descriptor carrying the wrong exact destructor and attempt typed
 *   construction through it.
 * Responsibilities: Construction is rejected by the destructor-token check; no lifetime starts and no slot is consumed.
 */
MW_TEST_CASE(ObjectStoreRejectsSameLayoutDescriptorWithWrongExactDestructor)
{
	// Arrange
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	FClassDescriptor WrongDescriptor = MakeClassDescriptor<FWrongDestructorObject>(1, "WrongDestructor");
	const EObjectResult RegistrationResult = Registry.Register(WrongDescriptor);
	const FClassDescriptor* const RegisteredWrongDescriptor = Registry.Find(WrongDescriptor.TypeId);
	TObjectStoreFixture<128, 16, 1, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();

	// Act
	const TObjectCreationResult<FTrackedObject> Creation = Store.NewObject<FTrackedObject>(*RegisteredWrongDescriptor, Lifetime);

	// Assert
	const EObjectResult ExpectedSuccess = EObjectResult::Success;
	const EObjectResult ExpectedFailure = EObjectResult::UnsupportedObjectLayout;
	const std::uint32_t ExpectedConstructionCount = 0;
	const FObjectStoreStats StoreStats = Store.Stats();
	MW_EXPECT_EQ(Test, ExpectedSuccess, RegistrationResult, "A structurally valid descriptor should register");
	MW_EXPECT_EQ(Test, ExpectedFailure, Creation.Result, "The wrong exact destructor should reject typed construction");
	MW_EXPECT_EQ(Test, ExpectedConstructionCount, Lifetime.ConstructionCount, "Destructor mismatch must reject before construction");
	MW_EXPECT_EQ(Test, ExpectedConstructionCount, StoreStats.OccupiedSlots, "Destructor mismatch must not consume capacity");
}

/**
 * Motivation: Scenario: Register a descriptor, mutate its source copy's destructor, token, and size, then
 *   construct through both the registry-owned copy and the mutated source.
 * Responsibilities: Expected: The owned copy constructs and destroys exactly once; the mutated source is rejected as
 *   unknown.
 */
MW_TEST_CASE(ObjectStoreUsesImmutableRegistryOwnedDescriptorCopy)
{
	// Arrange
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

	// Act
	const TObjectCreationResult<FTrackedObject> OwnedCreation = Store.NewObject<FTrackedObject>(*RegisteredDescriptor, Lifetime);
	const TObjectCreationResult<FTrackedObject> RejectedSourceCreation = Store.NewObject<FTrackedObject>(SourceDescriptor, Lifetime);
	const EObjectResult PendingResult = Store.MarkPendingDestroy(OwnedCreation.Object.Handle());
	const MicroWorld::Engine::FObjectMutationResult Barrier = Store.ApplyPendingDestroy(1);

	// Assert
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

} // namespace
