#include "TestSupport.h"
#include "MemoryTestHelpers.h"

#include <MicroWorld/Core/Memory/UniquePointerResult.h>

#include <cstddef>
#include <cstdint>

namespace
{

using namespace ::MicroWorld::Tests;

/**
 * Motivation: Fill the arena to capacity, then attempt a unique factory construction.
 * Responsibilities: The factory reports out of memory, returns an empty owner, constructs no value, and leaves usage
 *   unchanged.
 */
MW_TEST_CASE(UniqueFactoryOutOfMemoryNeverConstructsValue)
{
	// Arrange
	TFixedArena<64, 16> Arena;
	FMemoryBlock CapacityBlock{};
	FLifetimeState Lifetime;
	const EMemoryResult FillResult = Arena.TryAllocate(Arena.CapacityBytes(), 1, CapacityBlock);

	// Act
	constexpr std::uint32_t ConstructedValue = 7;
	const TUniquePointerResult<FTrackedValue> UniqueResult = MakeUnique<FTrackedValue>(Arena, Lifetime, ConstructedValue);

	const EMemoryResult FactoryResult = UniqueResult.Result;
	const bool bPointerInvalid = !UniqueResult.Pointer.IsValid();
	const std::size_t ConstructionCount = Lifetime.ConstructionCount;
	const std::size_t DestructionCount = Lifetime.DestructionCount;
	const std::size_t UsedBytes = Arena.UsedBytes();

	// Assert
	MW_EXPECT_EQ(Test, EMemoryResult::Success, FillResult, "Arena should be full before unique factory attempt");
	MW_EXPECT_EQ(Test, EMemoryResult::OutOfMemory, FactoryResult, "Unique factory should report exact exhaustion");
	MW_EXPECT_TRUE(Test, bPointerInvalid, "Failed unique factory should return an empty owner");
	MW_EXPECT_EQ(Test, std::size_t{0}, ConstructionCount, "OOM should be reported before value construction");
	MW_EXPECT_EQ(Test, std::size_t{0}, DestructionCount, "No value should require destruction after OOM");
	MW_EXPECT_EQ(Test, std::size_t{64}, UsedBytes, "Failed unique factory should not change existing usage");
}

/**
 * Motivation: Construct a unique owner, move it, then reset the destination twice.
 * Responsibilities: The move empties the source and transfers the live value; repeated reset destroys the value once and
 *   returns the original block once.
 */
MW_TEST_CASE(UniquePtrMoveAndResetReturnExactOriginalBlockOnce)
{
	// Arrange
	TTrackingMemoryResource<128, 16> Resource;
	FLifetimeState Lifetime;
	TUniquePointerResult<FTrackedValue> UniqueResult = MakeUnique<FTrackedValue>(Resource, Lifetime, 19);
	const FMemoryBlock OriginalBlock = Resource.LastAllocatedBlock;

	// Act
	TUniquePtr<FTrackedValue> MovedPointer(std::move(UniqueResult.Pointer));
	const EMemoryResult FactoryResult = UniqueResult.Result;
	const bool bSourceInvalidAfterMove = !UniqueResult.Pointer.IsValid();
	const bool bDestinationValidAfterMove = MovedPointer.IsValid();
	FTrackedValue* const MovedValue = MovedPointer.Get();
	const int ActualValue = MovedValue == nullptr ? -1 : MovedValue->Value;
	MovedPointer.Reset();
	MovedPointer.Reset();

	const std::size_t ConstructionCount = Lifetime.ConstructionCount;
	const std::size_t DestructionCount = Lifetime.DestructionCount;
	const std::size_t DeallocationCount = Resource.DeallocationRequestCount;
	const bool bSameAddressReturned = Resource.LastDeallocatedBlock.Address == OriginalBlock.Address;
	const bool bSameSizeReturned = Resource.LastDeallocatedBlock.SizeBytes == OriginalBlock.SizeBytes;
	const std::size_t UsedBytes = Resource.UsedBytes();

	// Assert
	MW_EXPECT_EQ(Test, EMemoryResult::Success, FactoryResult, "Unique factory should construct in available storage");
	MW_EXPECT_TRUE(Test, bSourceInvalidAfterMove, "Move should leave the source unique owner empty");
	MW_EXPECT_TRUE(Test, bDestinationValidAfterMove, "Move should transfer the live unique value");
	MW_EXPECT_EQ(Test, 19, ActualValue, "Moved unique owner should resolve the original value");
	MW_EXPECT_EQ(Test, std::size_t{1}, ConstructionCount, "Successful unique factory should construct exactly once");
	MW_EXPECT_EQ(Test, std::size_t{1}, DestructionCount, "Repeated reset should destroy the value exactly once");
	MW_EXPECT_EQ(Test, std::size_t{1}, DeallocationCount, "Repeated reset should return the allocation exactly once");
	MW_EXPECT_TRUE(Test, bSameAddressReturned, "Unique owner should return the original resource address");
	MW_EXPECT_TRUE(Test, bSameSizeReturned, "Unique owner should return the original resource size");
	MW_EXPECT_EQ(Test, std::size_t{0}, UsedBytes, "Unique reset should restore resource usage");
}

/**
 * Motivation: Construct a unique owner inside a scope and let it exit scope with no explicit reset.
 * Responsibilities: Scope exit destroys the value exactly once and deallocates once, restoring resource usage.
 */
MW_TEST_CASE(UniquePtrScopeExitDestroysAndDeallocatesExactlyOnce)
{
	// Arrange
	TTrackingMemoryResource<128, 16> Resource;
	FLifetimeState Lifetime;

	// Act
	{
		const TUniquePointerResult<FTrackedValue> UniqueResult = MakeUnique<FTrackedValue>(Resource, Lifetime, 3);
		const EMemoryResult FactoryResult = UniqueResult.Result;
		const bool bPointerValid = UniqueResult.Pointer.IsValid();

		// Assert
		MW_EXPECT_EQ(Test, EMemoryResult::Success, FactoryResult, "Unique factory should succeed before scope-exit test");
		MW_EXPECT_TRUE(Test, bPointerValid, "Successful unique factory should return a live owner");
	}

	// Act
	const std::size_t DestructionCount = Lifetime.DestructionCount;
	const std::size_t DeallocationCount = Resource.DeallocationRequestCount;
	const std::size_t UsedBytes = Resource.UsedBytes();

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{1}, DestructionCount, "Unique owner scope exit should destroy the value exactly once");
	MW_EXPECT_EQ(Test, std::size_t{1}, DeallocationCount, "Unique owner scope exit should deallocate exactly once");
	MW_EXPECT_EQ(Test, std::size_t{0}, UsedBytes, "Unique owner scope exit should restore resource usage");
}

} // namespace
