#include "TestSupport.h"
#include "MemoryTestHelpers.h"

#include <array>
#include <cstddef>
#include <utility>

namespace
{

using namespace ::MicroWorld::Tests;

/**
 * Motivation: Construct a shared value and acquire explicit strong, weak, and pinned owners, then release all
 *   strong owners and finally the weak owner.
 * Responsibilities: One combined allocation backs all owners; the value lives until the final strong release and the
 *   allocation returns only after the final.
 */
MW_TEST_CASE(SharedAndWeakOwnersPreserveValueUntilFinalStrongAndWeakRelease)
{
	// Arrange
	TTrackingMemoryResource<256, 64> Resource;
	FLifetimeState Lifetime;
	TSharedPointerResult<FTrackedValue, ESharedPointerMode::SingleThreaded> SharedFactoryResult = MakeShared<FTrackedValue>(Resource, Lifetime, 41);
	TSharedPtr<FTrackedValue> Owner = std::move(SharedFactoryResult.Pointer);

	// Act
	TSharedPointerResult<FTrackedValue, ESharedPointerMode::SingleThreaded> ShareResult = Owner.TryShare();
	TSharedPtr<FTrackedValue> SecondOwner = std::move(ShareResult.Pointer);
	TWeakPointerResult<FTrackedValue, ESharedPointerMode::SingleThreaded> WeakResult = Owner.TryAcquireWeak();
	TWeakPtr<FTrackedValue> Observer = std::move(WeakResult.Pointer);
	TSharedPointerResult<FTrackedValue, ESharedPointerMode::SingleThreaded> PinResult = Observer.Pin();
	TSharedPtr<FTrackedValue> PinnedOwner = std::move(PinResult.Pointer);

	const ESharedPointerResult FactoryResult = SharedFactoryResult.Result;
	const ESharedPointerResult ShareOperationResult = ShareResult.Result;
	const ESharedPointerResult WeakOperationResult = WeakResult.Result;
	const ESharedPointerResult PinOperationResult = PinResult.Result;
	const std::size_t AllocationRequestCount = Resource.AllocationRequestCount;
	const std::size_t SuccessfulAllocationCount = Resource.SuccessfulAllocationCount;
	const std::size_t StrongCountAfterAcquisitions = Owner.StrongReferenceCount();
	const std::size_t WeakCountAfterAcquisitions = Owner.WeakReferenceCount();
	FTrackedValue* const PinnedValue = PinnedOwner.Get();
	const int PinnedValueNumber = PinnedValue == nullptr ? -1 : PinnedValue->Value;

	// Assert
	MW_EXPECT_EQ(Test, ESharedPointerResult::Success, FactoryResult, "Shared factory should create the first strong owner");
	MW_EXPECT_EQ(Test, ESharedPointerResult::Success, ShareOperationResult, "TryShare should acquire an explicit strong owner");
	MW_EXPECT_EQ(Test, ESharedPointerResult::Success, WeakOperationResult, "TryAcquireWeak should acquire an explicit observer");
	MW_EXPECT_EQ(Test, ESharedPointerResult::Success, PinOperationResult, "Pin should acquire a strong owner while value is live");
	MW_EXPECT_EQ(Test, std::size_t{1}, AllocationRequestCount, "Factory should request one combined resource allocation");
	MW_EXPECT_EQ(Test, std::size_t{1}, SuccessfulAllocationCount, "Factory should use one successful combined allocation");
	MW_EXPECT_EQ(Test, std::size_t{3}, StrongCountAfterAcquisitions, "Explicit acquisitions should report three strong owners");
	MW_EXPECT_EQ(Test, std::size_t{1}, WeakCountAfterAcquisitions, "Explicit observer acquisition should report one weak owner");
	MW_EXPECT_EQ(Test, 41, PinnedValueNumber, "Pinned owner should resolve the live shared value");

	// Act
	Owner.Reset();
	SecondOwner.Reset();
	const std::size_t DestructionBeforeFinalStrong = Lifetime.DestructionCount;
	PinnedOwner.Reset();
	const std::size_t DestructionAfterFinalStrong = Lifetime.DestructionCount;
	const bool bObserverExpired = Observer.IsExpired();
	const std::size_t DeallocationBeforeFinalWeak = Resource.DeallocationRequestCount;
	const TSharedPointerResult<FTrackedValue, ESharedPointerMode::SingleThreaded> ExpiredPinResult = Observer.Pin();
	const ESharedPointerResult ExpiredPinOperationResult = ExpiredPinResult.Result;
	const bool bExpiredPinInvalid = !ExpiredPinResult.Pointer.IsValid();

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{0}, DestructionBeforeFinalStrong, "Value should remain live while one strong owner remains");
	MW_EXPECT_EQ(Test, std::size_t{1}, DestructionAfterFinalStrong, "Final strong release should destroy the value exactly once");
	MW_EXPECT_TRUE(Test, bObserverExpired, "Weak observer should report expiry after final strong release");
	MW_EXPECT_EQ(Test, std::size_t{0}, DeallocationBeforeFinalWeak, "Expired weak observer should retain the combined allocation");
	MW_EXPECT_EQ(Test, ESharedPointerResult::Expired, ExpiredPinOperationResult, "Expired observer should reject Pin");
	MW_EXPECT_TRUE(Test, bExpiredPinInvalid, "Expired Pin should return an empty strong owner");

	// Act
	Observer.Reset();
	const std::size_t DeallocationAfterFinalWeak = Resource.DeallocationRequestCount;
	const bool bSameAddressReturned = Resource.LastDeallocatedBlock.Address == Resource.LastAllocatedBlock.Address;
	const bool bSameSizeReturned = Resource.LastDeallocatedBlock.SizeBytes == Resource.LastAllocatedBlock.SizeBytes;
	const std::size_t UsedBytes = Resource.UsedBytes();

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{1}, DeallocationAfterFinalWeak, "Final weak release should deallocate the combined block once");
	MW_EXPECT_TRUE(Test, bSameAddressReturned, "Shared ownership should return the original combined address");
	MW_EXPECT_TRUE(Test, bSameSizeReturned, "Shared ownership should return the original combined size");
	MW_EXPECT_EQ(Test, std::size_t{0}, UsedBytes, "Final weak release should restore resource usage");
}

/**
 * Motivation: Attempt a shared factory construction against an under-capacity resource.
 * Responsibilities: The factory reports combined-allocation exhaustion, returns an empty owner, constructs no value, and
 *   requires no deallocation.
 */
MW_TEST_CASE(SharedFactoryOutOfMemoryNeverConstructsValue)
{
	// Arrange
	TTrackingMemoryResource<1, 64> Resource;
	FLifetimeState Lifetime;

	// Act
	const TSharedPointerResult<FTrackedValue, ESharedPointerMode::SingleThreaded> SharedResult = MakeShared<FTrackedValue>(Resource, Lifetime, 5);

	const ESharedPointerResult FactoryResult = SharedResult.Result;
	const bool bPointerInvalid = !SharedResult.Pointer.IsValid();
	const std::size_t ConstructionCount = Lifetime.ConstructionCount;
	const std::size_t DeallocationCount = Resource.DeallocationRequestCount;
	const std::size_t UsedBytes = Resource.UsedBytes();

	// Assert
	MW_EXPECT_EQ(Test, ESharedPointerResult::OutOfMemory, FactoryResult, "Shared factory should report combined-allocation exhaustion");
	MW_EXPECT_TRUE(Test, bPointerInvalid, "OOM shared factory should return an empty owner");
	MW_EXPECT_EQ(Test, std::size_t{0}, ConstructionCount, "Shared OOM should occur before value construction");
	MW_EXPECT_EQ(Test, std::size_t{0}, DeallocationCount, "Failed shared allocation should not require deallocation");
	MW_EXPECT_EQ(Test, std::size_t{0}, UsedBytes, "Failed shared allocation should preserve zero usage");
}

/**
 * Motivation: Attempt a shared factory construction for an over-aligned type against a lower-alignment resource.
 * Responsibilities: The unsupported alignment is rejected before construction, returns an empty owner, allocates no
 *   block, and requires no deallocation.
 */
MW_TEST_CASE(SharedFactoryRejectsUnsupportedAlignmentBeforeConstruction)
{
	// Arrange
	TTrackingMemoryResource<256, 16> Resource;
	FLifetimeState Lifetime;

	// Act
	const TSharedPointerResult<FOverAlignedTrackedValue, ESharedPointerMode::SingleThreaded> SharedResult =
		MakeShared<FOverAlignedTrackedValue>(Resource, Lifetime);

	const ESharedPointerResult FactoryResult = SharedResult.Result;
	const bool bPointerInvalid = !SharedResult.Pointer.IsValid();
	const std::size_t ConstructionCount = Lifetime.ConstructionCount;
	const std::size_t SuccessfulAllocationCount = Resource.SuccessfulAllocationCount;
	const std::size_t DeallocationCount = Resource.DeallocationRequestCount;

	// Assert
	MW_EXPECT_EQ(Test, ESharedPointerResult::UnsupportedAlignment, FactoryResult, "Shared factory should preserve unsupported-alignment failure");
	MW_EXPECT_TRUE(Test, bPointerInvalid, "Alignment failure should return an empty shared owner");
	MW_EXPECT_EQ(Test, std::size_t{0}, ConstructionCount, "Alignment failure should occur before value construction");
	MW_EXPECT_EQ(Test, std::size_t{0}, SuccessfulAllocationCount, "Unsupported alignment should not create a resource block");
	MW_EXPECT_EQ(Test, std::size_t{0}, DeallocationCount, "Unsupported alignment should not require deallocation");
}

/**
 * Motivation: Construct a self-observing shared value that adopts its own weak observer, then release the only
 *   strong owner during value destruction.
 * Responsibilities: The self-owned final weak deallocation is deferred until value destruction completes, deallocating
 *   once and restoring usage.
 */
MW_TEST_CASE(SharedPtrDefersSelfOwnedFinalWeakDeallocationUntilValueDestructionCompletes)
{
	// Arrange
	TTrackingMemoryResource<256, 64> Resource;
	FLifetimeState Lifetime;
	TSharedPointerResult<FSelfObservingValue, ESharedPointerMode::SingleThreaded> SharedResult = MakeShared<FSelfObservingValue>(Resource, Lifetime);
	TSharedPtr<FSelfObservingValue> Owner = std::move(SharedResult.Pointer);
	TWeakPointerResult<FSelfObservingValue, ESharedPointerMode::SingleThreaded> WeakResult = Owner.TryAcquireWeak();
	TWeakPtr<FSelfObservingValue> SelfObserver = std::move(WeakResult.Pointer);
	FSelfObservingValue* const Value = Owner.Get();
	if (Value != nullptr)
	{
		Value->AdoptSelfObserver(std::move(SelfObserver));
	}
	const ESharedPointerResult FactoryResult = SharedResult.Result;
	const ESharedPointerResult WeakOperationResult = WeakResult.Result;
	const std::size_t WeakCountBeforeRelease = Owner.WeakReferenceCount();

	// Act
	Owner.Reset();

	const std::size_t ConstructionCount = Lifetime.ConstructionCount;
	const std::size_t DestructionCount = Lifetime.DestructionCount;
	const std::size_t DeallocationCount = Resource.DeallocationRequestCount;
	const std::size_t UsedBytes = Resource.UsedBytes();

	// Assert
	MW_EXPECT_EQ(Test, ESharedPointerResult::Success, FactoryResult, "Self-observing shared value should construct successfully");
	MW_EXPECT_EQ(Test, ESharedPointerResult::Success, WeakOperationResult, "Self observer should be acquired explicitly");
	MW_EXPECT_EQ(Test, std::size_t{1}, WeakCountBeforeRelease, "Value should own the only weak observer before final release");
	MW_EXPECT_EQ(Test, std::size_t{1}, ConstructionCount, "Self-observing value should construct exactly once");
	MW_EXPECT_EQ(Test, std::size_t{1}, DestructionCount, "Final strong release should finish value destruction exactly once");
	MW_EXPECT_EQ(Test, std::size_t{1}, DeallocationCount, "Self-owned final weak release should deallocate only after destruction");
	MW_EXPECT_EQ(Test, std::size_t{0}, UsedBytes, "Self-observer teardown should restore resource usage");
}

/**
 * Motivation: Acquire strong owners up to the maximum reference count, then attempt one more TryShare.
 * Responsibilities: The maximum count succeeds; one more acquisition fails as overflow without wrapping or returning
 *   another owner.
 */
MW_TEST_CASE(SharedPtrRejectsStrongReferenceCountOverflowWithoutWrap)
{
	// Arrange
	using FSharedPointer = TSharedPtr<FTrackedValue>;
	constexpr std::size_t MaximumCount = FSharedPointer::MaximumReferenceCount();
	TTrackingMemoryResource<256, 64> Resource;
	FLifetimeState Lifetime;
	TSharedPointerResult<FTrackedValue, ESharedPointerMode::SingleThreaded> SharedResult = MakeShared<FTrackedValue>(Resource, Lifetime, 8);
	FSharedPointer Owner = std::move(SharedResult.Pointer);
	const ESharedPointerResult FactoryResult = SharedResult.Result;
	std::array<FSharedPointer, MaximumCount - 1U> AdditionalOwners{};
	bool bAllBoundaryAcquisitionsSucceeded = true;

	// Act
	for (std::size_t OwnerIndex = 0; OwnerIndex < AdditionalOwners.size(); ++OwnerIndex)
	{
		TSharedPointerResult<FTrackedValue, ESharedPointerMode::SingleThreaded> ShareResult = Owner.TryShare();
		if (ShareResult.Result != ESharedPointerResult::Success)
		{
			bAllBoundaryAcquisitionsSucceeded = false;
			break;
		}
		AdditionalOwners[OwnerIndex] = std::move(ShareResult.Pointer);
	}

	const std::size_t CountAtBoundary = Owner.StrongReferenceCount();
	const TSharedPointerResult<FTrackedValue, ESharedPointerMode::SingleThreaded> OverflowResult = Owner.TryShare();
	const ESharedPointerResult OverflowOperationResult = OverflowResult.Result;
	const std::size_t CountAfterOverflow = Owner.StrongReferenceCount();
	const bool bOverflowPointerInvalid = !OverflowResult.Pointer.IsValid();

	// Assert
	MW_EXPECT_EQ(Test, ESharedPointerResult::Success, FactoryResult, "Strong boundary value should construct successfully");
	MW_EXPECT_TRUE(Test, bAllBoundaryAcquisitionsSucceeded, "Every strong acquisition through the maximum should succeed");
	MW_EXPECT_EQ(Test, MaximumCount, CountAtBoundary, "Strong diagnostics should reach the exact 65,535 boundary");
	MW_EXPECT_EQ(
		Test, ESharedPointerResult::ReferenceCountOverflow, OverflowOperationResult, "Strong acquisition above 65,535 should report overflow");
	MW_EXPECT_TRUE(Test, bOverflowPointerInvalid, "Strong overflow should not return another owner");
	MW_EXPECT_EQ(Test, MaximumCount, CountAfterOverflow, "Strong overflow should leave the boundary count unchanged");

	// Act
	for (FSharedPointer& AdditionalOwner : AdditionalOwners)
	{
		AdditionalOwner.Reset();
	}
	const std::size_t CountAfterAdditionalRelease = Owner.StrongReferenceCount();
	Owner.Reset();
	const std::size_t DestructionCount = Lifetime.DestructionCount;
	const std::size_t DeallocationCount = Resource.DeallocationRequestCount;

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{1}, CountAfterAdditionalRelease, "Releasing acquired owners should leave the original owner");
	MW_EXPECT_EQ(Test, std::size_t{1}, DestructionCount, "Final boundary-test strong release should destroy exactly once");
	MW_EXPECT_EQ(Test, std::size_t{1}, DeallocationCount, "Boundary-test allocation should deallocate exactly once");
}

/**
 * Motivation: Acquire weak observers up to the maximum reference count, then attempt one more acquisition.
 * Responsibilities: The maximum count succeeds; one more acquisition fails as overflow without wrapping or returning
 *   another observer.
 */
MW_TEST_CASE(SharedPtrRejectsWeakReferenceCountOverflowWithoutWrap)
{
	// Arrange
	using FSharedPointer = TSharedPtr<FTrackedValue>;
	using FWeakPointer = TWeakPtr<FTrackedValue>;
	constexpr std::size_t MaximumCount = FWeakPointer::MaximumReferenceCount();
	TTrackingMemoryResource<256, 64> Resource;
	FLifetimeState Lifetime;
	TSharedPointerResult<FTrackedValue, ESharedPointerMode::SingleThreaded> SharedResult = MakeShared<FTrackedValue>(Resource, Lifetime, 13);
	FSharedPointer Owner = std::move(SharedResult.Pointer);
	const ESharedPointerResult FactoryResult = SharedResult.Result;
	std::array<FWeakPointer, MaximumCount> Observers{};
	bool bAllBoundaryAcquisitionsSucceeded = true;

	// Act
	for (std::size_t ObserverIndex = 0; ObserverIndex < Observers.size(); ++ObserverIndex)
	{
		TWeakPointerResult<FTrackedValue, ESharedPointerMode::SingleThreaded> WeakResult = Owner.TryAcquireWeak();
		if (WeakResult.Result != ESharedPointerResult::Success)
		{
			bAllBoundaryAcquisitionsSucceeded = false;
			break;
		}
		Observers[ObserverIndex] = std::move(WeakResult.Pointer);
	}

	const std::size_t CountAtBoundary = Owner.WeakReferenceCount();
	const TWeakPointerResult<FTrackedValue, ESharedPointerMode::SingleThreaded> OverflowResult = Owner.TryAcquireWeak();
	const ESharedPointerResult OverflowOperationResult = OverflowResult.Result;
	const std::size_t CountAfterOverflow = Owner.WeakReferenceCount();
	const bool bOverflowPointerExpired = OverflowResult.Pointer.IsExpired();

	// Assert
	MW_EXPECT_EQ(Test, ESharedPointerResult::Success, FactoryResult, "Weak boundary value should construct successfully");
	MW_EXPECT_TRUE(Test, bAllBoundaryAcquisitionsSucceeded, "Every weak acquisition through the maximum should succeed");
	MW_EXPECT_EQ(Test, MaximumCount, CountAtBoundary, "Weak diagnostics should reach the exact 65,535 boundary");
	MW_EXPECT_EQ(Test, ESharedPointerResult::ReferenceCountOverflow, OverflowOperationResult, "Weak acquisition above 65,535 should report overflow");
	MW_EXPECT_TRUE(Test, bOverflowPointerExpired, "Weak overflow should not return another observer");
	MW_EXPECT_EQ(Test, MaximumCount, CountAfterOverflow, "Weak overflow should leave the boundary count unchanged");

	// Act
	for (FWeakPointer& Observer : Observers)
	{
		Observer.Reset();
	}
	const std::size_t WeakCountAfterRelease = Owner.WeakReferenceCount();
	Owner.Reset();
	const std::size_t DestructionCount = Lifetime.DestructionCount;
	const std::size_t DeallocationCount = Resource.DeallocationRequestCount;

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{0}, WeakCountAfterRelease, "Releasing all observers should restore zero weak count");
	MW_EXPECT_EQ(Test, std::size_t{1}, DestructionCount, "Final strong release should destroy the boundary-test value once");
	MW_EXPECT_EQ(Test, std::size_t{1}, DeallocationCount, "Boundary-test combined allocation should deallocate once");
}

} // namespace
