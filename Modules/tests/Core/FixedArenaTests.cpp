#include "TestSupport.h"
#include "MemoryTestHelpers.h"

#include <MicroWorld/Core/Memory/FixedArena.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace
{

using namespace ::MicroWorld::Tests;

/**
 * Motivation: Allocate a small aligned block from a fresh arena, then deallocate it.
 * Responsibilities: The aligned in-capacity allocation returns an exact block with diagnostics, and deallocation
 *   restores zero usage.
 */
MW_TEST_CASE(FixedArenaAcceptsAlignedAllocationAndReportsExactUsage)
{
	// Arrange
	TFixedArena<64, 16> Arena;
	FMemoryBlock Block{};
	const std::size_t ExpectedCapacityBytes = 64;
	const std::size_t ExpectedUsedBytes = 7;

	// Act
	const EMemoryResult AllocationResult = Arena.TryAllocate(ExpectedUsedBytes, 16, Block);

	const bool bAddressReturned = Block.Address != nullptr;
	const std::uintptr_t BlockAddress = reinterpret_cast<std::uintptr_t>(Block.Address);
	const bool bAddressAligned = (BlockAddress % 16U) == 0;
	const std::size_t ActualBlockSizeBytes = Block.SizeBytes;
	const std::size_t ActualCapacityBytes = Arena.CapacityBytes();
	const std::size_t ActualUsedBytes = Arena.UsedBytes();

	// Assert
	MW_EXPECT_EQ(Test, EMemoryResult::Success, AllocationResult, "Aligned in-capacity allocation should succeed");
	MW_EXPECT_TRUE(Test, bAddressReturned, "Successful allocation should return a non-null address");
	MW_EXPECT_TRUE(Test, bAddressAligned, "Returned allocation should satisfy requested alignment");
	MW_EXPECT_EQ(Test, ExpectedUsedBytes, ActualBlockSizeBytes, "Returned block should preserve the exact requested size");
	MW_EXPECT_EQ(Test, ExpectedCapacityBytes, ActualCapacityBytes, "Arena should report exact caller-usable capacity");
	MW_EXPECT_EQ(Test, ExpectedUsedBytes, ActualUsedBytes, "Arena should report exact active payload bytes");

	// Act
	const EMemoryResult DeallocationResult = Arena.Deallocate(Block);
	const std::size_t UsedBytesAfterDeallocation = Arena.UsedBytes();

	// Assert
	MW_EXPECT_EQ(Test, EMemoryResult::Success, DeallocationResult, "Exact active block should deallocate successfully");
	MW_EXPECT_EQ(Test, std::size_t{0}, UsedBytesAfterDeallocation, "Successful deallocation should restore zero usage");
}

/**
 * Motivation: Attempt allocation with zero, non-power-of-two, and excess alignments.
 * Responsibilities: Each invalid alignment is rejected atomically, clears the output block, and leaves usage unchanged.
 */
MW_TEST_CASE(FixedArenaRejectsZeroNonPowerAndExcessAlignmentAtomically)
{
	// Arrange
	TFixedArena<64, 16> Arena;
	FMemoryBlock ZeroAlignmentBlock{reinterpret_cast<void*>(std::uintptr_t{1}), 9};
	FMemoryBlock NonPowerAlignmentBlock{reinterpret_cast<void*>(std::uintptr_t{1}), 9};
	FMemoryBlock ExcessAlignmentBlock{reinterpret_cast<void*>(std::uintptr_t{1}), 9};

	// Act
	const EMemoryResult ZeroAlignmentResult = Arena.TryAllocate(8, 0, ZeroAlignmentBlock);
	const std::size_t UsedAfterZeroAlignment = Arena.UsedBytes();
	const EMemoryResult NonPowerAlignmentResult = Arena.TryAllocate(8, 3, NonPowerAlignmentBlock);
	const std::size_t UsedAfterNonPowerAlignment = Arena.UsedBytes();
	const EMemoryResult ExcessAlignmentResult = Arena.TryAllocate(8, 32, ExcessAlignmentBlock);
	const std::size_t UsedAfterExcessAlignment = Arena.UsedBytes();

	const bool bZeroAlignmentBlockCleared = ZeroAlignmentBlock.Address == nullptr && ZeroAlignmentBlock.SizeBytes == 0;
	const bool bNonPowerAlignmentBlockCleared = NonPowerAlignmentBlock.Address == nullptr && NonPowerAlignmentBlock.SizeBytes == 0;
	const bool bExcessAlignmentBlockCleared = ExcessAlignmentBlock.Address == nullptr && ExcessAlignmentBlock.SizeBytes == 0;

	// Assert
	MW_EXPECT_EQ(Test, EMemoryResult::UnsupportedAlignment, ZeroAlignmentResult, "Zero alignment should be rejected explicitly");
	MW_EXPECT_EQ(Test, std::size_t{0}, UsedAfterZeroAlignment, "Zero-alignment rejection should not change usage");
	MW_EXPECT_TRUE(Test, bZeroAlignmentBlockCleared, "Zero-alignment rejection should clear the output block");
	MW_EXPECT_EQ(Test, EMemoryResult::UnsupportedAlignment, NonPowerAlignmentResult, "Non-power-of-two alignment should be rejected explicitly");
	MW_EXPECT_EQ(Test, std::size_t{0}, UsedAfterNonPowerAlignment, "Non-power alignment rejection should not change usage");
	MW_EXPECT_TRUE(Test, bNonPowerAlignmentBlockCleared, "Non-power alignment rejection should clear the output block");
	MW_EXPECT_EQ(Test, EMemoryResult::UnsupportedAlignment, ExcessAlignmentResult, "Alignment above the arena guarantee should be rejected");
	MW_EXPECT_EQ(Test, std::size_t{0}, UsedAfterExcessAlignment, "Excess-alignment rejection should not change usage");
	MW_EXPECT_TRUE(Test, bExcessAlignmentBlockCleared, "Excess-alignment rejection should clear the output block");
}

/**
 * Motivation: Allocate at exact capacity, then attempt zero, oversized, exhausted, and maximum-size requests.
 * Responsibilities: Each failing request is rejected, clears the output block, and leaves usage unchanged.
 */
MW_TEST_CASE(FixedArenaRejectsZeroOversizeExhaustedAndMaximumRequests)
{
	// Arrange
	TFixedArena<16, 8> Arena;
	FMemoryBlock ZeroBlock{};
	FMemoryBlock OversizeBlock{};
	FMemoryBlock FullBlock{};
	FMemoryBlock ExhaustedBlock{};
	FMemoryBlock MaximumBlock{};

	// Act
	const EMemoryResult ZeroResult = Arena.TryAllocate(0, 1, ZeroBlock);
	const EMemoryResult OversizeResult = Arena.TryAllocate(17, 1, OversizeBlock);
	const EMemoryResult FullResult = Arena.TryAllocate(16, 8, FullBlock);
	const std::size_t UsedAtCapacity = Arena.UsedBytes();
	const EMemoryResult ExhaustedResult = Arena.TryAllocate(1, 1, ExhaustedBlock);
	const EMemoryResult MaximumResult = Arena.TryAllocate(std::numeric_limits<std::size_t>::max(), 1, MaximumBlock);
	const std::size_t UsedAfterFailures = Arena.UsedBytes();

	const bool bZeroBlockCleared = ZeroBlock.Address == nullptr && ZeroBlock.SizeBytes == 0;
	const bool bOversizeBlockCleared = OversizeBlock.Address == nullptr && OversizeBlock.SizeBytes == 0;
	const bool bExhaustedBlockCleared = ExhaustedBlock.Address == nullptr && ExhaustedBlock.SizeBytes == 0;
	const bool bMaximumBlockCleared = MaximumBlock.Address == nullptr && MaximumBlock.SizeBytes == 0;

	// Assert
	MW_EXPECT_EQ(Test, EMemoryResult::OutOfMemory, ZeroResult, "Zero-size allocation should fail as out of memory");
	MW_EXPECT_TRUE(Test, bZeroBlockCleared, "Zero-size failure should clear the output block");
	MW_EXPECT_EQ(Test, EMemoryResult::OutOfMemory, OversizeResult, "Capacity-plus-one allocation should fail");
	MW_EXPECT_TRUE(Test, bOversizeBlockCleared, "Oversized failure should clear the output block");
	MW_EXPECT_EQ(Test, EMemoryResult::Success, FullResult, "Exact-capacity allocation should succeed");
	MW_EXPECT_EQ(Test, std::size_t{16}, UsedAtCapacity, "Exact-capacity allocation should report full usage");
	MW_EXPECT_EQ(Test, EMemoryResult::OutOfMemory, ExhaustedResult, "Allocation after exact capacity should fail");
	MW_EXPECT_TRUE(Test, bExhaustedBlockCleared, "Exhaustion failure should clear the output block");
	MW_EXPECT_EQ(Test, EMemoryResult::OutOfMemory, MaximumResult, "Maximum-size request should fail without arithmetic wrap");
	MW_EXPECT_TRUE(Test, bMaximumBlockCleared, "Maximum-size failure should clear the output block");
	MW_EXPECT_EQ(Test, std::size_t{16}, UsedAfterFailures, "Failed requests should preserve exact full usage");
}

/**
 * Motivation: Deallocation foreign, interior, wrong-size, and double-freed blocks after one valid allocation.
 * Responsibilities: Each malformed deallocation is rejected and preserves usage; only the exact original block releases.
 */
MW_TEST_CASE(FixedArenaRejectsForeignInteriorWrongSizeAndDoubleFreeAtomically)
{
	// Arrange
	TFixedArena<32, 8> Arena;
	TFixedArena<32, 8> ForeignArena;
	FMemoryBlock Block{};
	FMemoryBlock ForeignBlock{};
	const EMemoryResult AllocationResult = Arena.TryAllocate(8, 8, Block);
	const EMemoryResult ForeignAllocationResult = ForeignArena.TryAllocate(8, 8, ForeignBlock);

	// Act
	const EMemoryResult ForeignResult = Arena.Deallocate(ForeignBlock);
	const std::size_t UsedAfterForeign = Arena.UsedBytes();
	FMemoryBlock InteriorBlock{static_cast<std::byte*>(Block.Address) + 1, Block.SizeBytes - 1U};
	const EMemoryResult InteriorResult = Arena.Deallocate(InteriorBlock);
	const std::size_t UsedAfterInterior = Arena.UsedBytes();
	FMemoryBlock SmallerBlock{Block.Address, Block.SizeBytes - 1U};
	const EMemoryResult SmallerResult = Arena.Deallocate(SmallerBlock);
	const std::size_t UsedAfterSmaller = Arena.UsedBytes();
	FMemoryBlock LargerBlock{Block.Address, Block.SizeBytes + 1U};
	const EMemoryResult LargerResult = Arena.Deallocate(LargerBlock);
	const std::size_t UsedAfterLarger = Arena.UsedBytes();
	const EMemoryResult ExactResult = Arena.Deallocate(Block);
	const std::size_t UsedAfterExact = Arena.UsedBytes();
	const EMemoryResult DoubleFreeResult = Arena.Deallocate(Block);
	const std::size_t UsedAfterDoubleFree = Arena.UsedBytes();

	// Assert
	MW_EXPECT_EQ(Test, EMemoryResult::Success, AllocationResult, "Test allocation should succeed before malformed deallocations");
	MW_EXPECT_EQ(Test, EMemoryResult::Success, ForeignAllocationResult, "Foreign arena should produce a valid foreign block");
	MW_EXPECT_EQ(Test, EMemoryResult::InvalidBlock, ForeignResult, "Foreign block should be rejected");
	MW_EXPECT_EQ(Test, std::size_t{8}, UsedAfterForeign, "Foreign-block rejection should preserve usage");
	MW_EXPECT_EQ(Test, EMemoryResult::InvalidBlock, InteriorResult, "Interior pointer should be rejected");
	MW_EXPECT_EQ(Test, std::size_t{8}, UsedAfterInterior, "Interior-pointer rejection should preserve usage");
	MW_EXPECT_EQ(Test, EMemoryResult::InvalidBlock, SmallerResult, "Wrong smaller size should be rejected");
	MW_EXPECT_EQ(Test, std::size_t{8}, UsedAfterSmaller, "Smaller-size rejection should preserve usage");
	MW_EXPECT_EQ(Test, EMemoryResult::InvalidBlock, LargerResult, "Wrong larger size should be rejected");
	MW_EXPECT_EQ(Test, std::size_t{8}, UsedAfterLarger, "Larger-size rejection should preserve usage");
	MW_EXPECT_EQ(Test, EMemoryResult::Success, ExactResult, "Original exact block should remain releasable");
	MW_EXPECT_EQ(Test, std::size_t{0}, UsedAfterExact, "Exact release should restore zero usage");
	MW_EXPECT_EQ(Test, EMemoryResult::InvalidBlock, DoubleFreeResult, "Double free should be rejected");
	MW_EXPECT_EQ(Test, std::size_t{0}, UsedAfterDoubleFree, "Double-free rejection should preserve zero usage");
}

/**
 * Motivation: Allocate three blocks, free the middle and reuse it, then free the remaining blocks in arbitrary
 *   order.
 * Responsibilities: The freed range is reused regardless of release order, and final usage returns to zero.
 */
MW_TEST_CASE(FixedArenaReusesBlocksAfterArbitraryOrderDeallocation)
{
	// Arrange
	TFixedArena<24, 8> Arena;
	FMemoryBlock FirstBlock{};
	FMemoryBlock MiddleBlock{};
	FMemoryBlock LastBlock{};
	FMemoryBlock ReusedBlock{};
	const EMemoryResult FirstResult = Arena.TryAllocate(8, 8, FirstBlock);
	const EMemoryResult MiddleResult = Arena.TryAllocate(8, 8, MiddleBlock);
	const EMemoryResult LastResult = Arena.TryAllocate(8, 8, LastBlock);

	// Act
	const EMemoryResult MiddleFreeResult = Arena.Deallocate(MiddleBlock);
	const EMemoryResult ReuseResult = Arena.TryAllocate(8, 8, ReusedBlock);
	const bool bMiddleAddressReused = ReusedBlock.Address == MiddleBlock.Address;
	const EMemoryResult LastFreeResult = Arena.Deallocate(LastBlock);
	const EMemoryResult FirstFreeResult = Arena.Deallocate(FirstBlock);
	const EMemoryResult ReusedFreeResult = Arena.Deallocate(ReusedBlock);
	const std::size_t FinalUsedBytes = Arena.UsedBytes();

	// Assert
	MW_EXPECT_EQ(Test, EMemoryResult::Success, FirstResult, "First bounded allocation should succeed");
	MW_EXPECT_EQ(Test, EMemoryResult::Success, MiddleResult, "Middle bounded allocation should succeed");
	MW_EXPECT_EQ(Test, EMemoryResult::Success, LastResult, "Last bounded allocation should succeed at capacity");
	MW_EXPECT_EQ(Test, EMemoryResult::Success, MiddleFreeResult, "Middle block should release before its neighbors");
	MW_EXPECT_EQ(Test, EMemoryResult::Success, ReuseResult, "Freed middle range should accept an equal allocation");
	MW_EXPECT_TRUE(Test, bMiddleAddressReused, "Equal allocation should reuse the freed middle address");
	MW_EXPECT_EQ(Test, EMemoryResult::Success, LastFreeResult, "Last block should release independently");
	MW_EXPECT_EQ(Test, EMemoryResult::Success, FirstFreeResult, "First block should release independently");
	MW_EXPECT_EQ(Test, EMemoryResult::Success, ReusedFreeResult, "Reused middle block should release exactly once");
	MW_EXPECT_EQ(Test, std::size_t{0}, FinalUsedBytes, "Arbitrary-order releases should restore zero usage");
}

} // namespace
