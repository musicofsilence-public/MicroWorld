#include "TestSupport.h"
#include "ObjectStoreTestHelpers.h"

#include <MicroWorld/Engine/ClassRegistryView.h>
#include <MicroWorld/Engine/ObjectCreationResult.h>
#include <MicroWorld/Engine/ObjectMutationResult.h>
#include <MicroWorld/Engine/ObjectSlotMetadata.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/WeakObjectPtr.h>

#include <cstdint>
#include <limits>

namespace
{

using namespace ::MicroWorld::Tests;

using MicroWorld::Engine::CanAdvanceObjectGeneration;
using MicroWorld::Engine::ObjectGeneration;

/**
 * Motivation: A weak reference outside Engine watches nothing but this counter, so the counter must change when the
 *   object dies rather than when some later object happens to reuse the slot.
 * Responsibilities: The generation behind a captured address differs from the captured value once the object is
 *   destroyed, with no reuse in between.
 */
MW_TEST_CASE(ObjectStoreSlotGenerationAdvancesOnDestroyBeforeAnyReuse)
{
	// Arrange
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterTrackedDescriptor(Registry, Descriptor);
	TObjectStoreFixture<128, 16, 1, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FTrackedObject> Creation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);
	const FObjectHandle Handle = Creation.Object.Handle();
	const ObjectGeneration* const GenerationAddress = Store.GetSlotGenerationAddress(Handle);
	const ObjectGeneration GenerationWhileLive = GenerationAddress != nullptr ? *GenerationAddress : 0;
	const EObjectResult PendingResult = Store.MarkPendingDestroy(Handle);

	// Act
	const MicroWorld::Engine::FObjectMutationResult BarrierResult = Store.ApplyPendingDestroy(1);

	// Assert
	const EObjectResult ExpectedSuccess = EObjectResult::Success;
	const std::uint32_t ExpectedOne = 1;
	const bool bGenerationChangedAtDestroy = GenerationAddress != nullptr && *GenerationAddress != GenerationWhileLive;
	MW_EXPECT_EQ(Test, ExpectedSuccess, RegistrationResult, "The tracked class should register");
	MW_EXPECT_EQ(Test, ExpectedSuccess, Creation.Result, "The watched object should be created");
	MW_EXPECT_TRUE(Test, GenerationAddress != nullptr, "A live handle should expose its slot generation address");
	MW_EXPECT_EQ(Test, Handle.Generation, GenerationWhileLive, "The live slot should carry the handle's own generation");
	MW_EXPECT_EQ(Test, ExpectedSuccess, PendingResult, "The watched object should enter pending destruction");
	MW_EXPECT_EQ(Test, ExpectedOne, BarrierResult.ObjectsDestroyed, "The watched object should be destroyed");
	MW_EXPECT_TRUE(Test, bGenerationChangedAtDestroy, "Destruction alone must advance the generation, with no reuse involved");
}

/**
 * Motivation: A caller must not be handed an address to watch when the handle it asked about is already dead.
 * Responsibilities: A stale handle yields no generation address.
 */
MW_TEST_CASE(ObjectStoreSlotGenerationAddressIsNullForAStaleHandle)
{
	// Arrange
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterTrackedDescriptor(Registry, Descriptor);
	TObjectStoreFixture<128, 16, 1, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FTrackedObject> Creation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);
	const FObjectHandle Handle = Creation.Object.Handle();
	const EObjectResult PendingResult = Store.MarkPendingDestroy(Handle);
	const MicroWorld::Engine::FObjectMutationResult BarrierResult = Store.ApplyPendingDestroy(1);

	// Act
	const ObjectGeneration* const StaleGenerationAddress = Store.GetSlotGenerationAddress(Handle);

	// Assert
	const EObjectResult ExpectedSuccess = EObjectResult::Success;
	const std::uint32_t ExpectedOne = 1;
	MW_EXPECT_EQ(Test, ExpectedSuccess, RegistrationResult, "The tracked class should register");
	MW_EXPECT_EQ(Test, ExpectedSuccess, Creation.Result, "The stale-handle object should be created");
	MW_EXPECT_EQ(Test, ExpectedSuccess, PendingResult, "The stale-handle object should enter pending destruction");
	MW_EXPECT_EQ(Test, ExpectedOne, BarrierResult.ObjectsDestroyed, "The stale-handle object should be destroyed");
	MW_EXPECT_TRUE(Test, StaleGenerationAddress == nullptr, "A stale handle should expose no generation address");
}

/**
 * Motivation: Destroy one generation, then construct a second object in the same one-slot store, and attempt to
 *   use the old handle.
 * Responsibilities: The reclaimed slot publishes a new generation; the old handle cannot resolve or release a current
 *   root.
 */
MW_TEST_CASE(ObjectStoreSlotReuseInvalidatesEveryOldHandle)
{
	// Arrange
	FObjectLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	const EObjectResult RegistrationResult = RegisterTrackedDescriptor(Registry, Descriptor);
	TObjectStoreFixture<128, 16, 1, 0> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FTrackedObject> FirstCreation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);
	const FObjectHandle FirstHandle = FirstCreation.Object.Handle();
	const EObjectResult PendingResult = Store.MarkPendingDestroy(FirstHandle);
	const MicroWorld::Engine::FObjectMutationResult BarrierResult = Store.ApplyPendingDestroy(1);

	// Act
	const TObjectCreationResult<FTrackedObject> SecondCreation = Store.NewObject<FTrackedObject>(*Descriptor, Lifetime);

	// Assert
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

/**
 * Motivation: Query generation advancement against the last reusable generation and the fully exhausted
 *   generation.
 * Responsibilities: The final distinct generation may advance once; an exhausted slot must retire before any wrap could
 *   revive an old identity.
 */
MW_TEST_CASE(ObjectHandleGenerationBoundaryRequiresRetirementBeforeWrap)
{
	// Arrange
	const ObjectGeneration LastReusableGeneration = std::numeric_limits<ObjectGeneration>::max() - 1U;
	const ObjectGeneration ExhaustedGeneration = std::numeric_limits<ObjectGeneration>::max();

	// Act
	const bool bLastGenerationCanAdvance = CanAdvanceObjectGeneration(LastReusableGeneration);
	const bool bExhaustedGenerationCanAdvance = CanAdvanceObjectGeneration(ExhaustedGeneration);

	// Assert
	MW_EXPECT_TRUE(Test, bLastGenerationCanAdvance, "The final distinct generation may be published once");
	MW_EXPECT_TRUE(Test, !bExhaustedGenerationCanAdvance, "An exhausted slot must retire before generation wrap");
}

} // namespace
