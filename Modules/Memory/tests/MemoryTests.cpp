#include "TestSupport.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Containers/StaticVector.h>
#include <MicroWorld/Memory/FixedArena.h>
#include <MicroWorld/Memory/SharedPtr.h>
#include <MicroWorld/Memory/UniquePtr.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{

using MicroWorld::EMemoryResult;
using MicroWorld::ERuntimeResult;
using MicroWorld::ESharedPointerResult;
using MicroWorld::FMemoryBlock;
using MicroWorld::IMemoryResource;
using MicroWorld::MakeShared;
using MicroWorld::MakeUnique;
using MicroWorld::TFixedArena;
using MicroWorld::TSharedPtr;
using MicroWorld::TSpan;
using MicroWorld::TStaticVector;
using MicroWorld::TUniquePtr;
using MicroWorld::TWeakPtr;

/** Records value construction and destruction without sharing state between tests. */
struct FLifetimeState final
{
	/** Proves failed factories never begin a value lifetime. */
	std::size_t ConstructionCount{0};

	/** Proves each successful ownership path ends its value lifetime once. */
	std::size_t DestructionCount{0};
};

/** Exposes value lifetime through caller-owned counters while remaining nothrow. */
class FTrackedValue final
{
public:
	/** Begins one observable value lifetime only after its resource allocation succeeds. */
	explicit FTrackedValue(FLifetimeState& InState, const int InValue = 0) noexcept : Value(InValue), State(InState) { ++State.ConstructionCount; }

	/** Ends one observable lifetime so ownership tests can reject leaks and double destruction. */
	~FTrackedValue() noexcept { ++State.DestructionCount; }

	/** Carries one public value used to prove acquired owners resolve the same live object. */
	int Value{0};

private:
	/** Shares only the fresh per-test observation state selected by the caller. */
	FLifetimeState& State;
};

/** Forces a layout beyond the small arena's alignment guarantee before construction can begin. */
class alignas(64) FOverAlignedTrackedValue final
{
public:
	/** Would expose an invalid construction if alignment rejection occurred too late. */
	explicit FOverAlignedTrackedValue(FLifetimeState& InState) noexcept : State(InState) { ++State.ConstructionCount; }

	/** Balances construction only when the resource accepted the layout. */
	~FOverAlignedTrackedValue() noexcept { ++State.DestructionCount; }

private:
	/** Shares only the fresh observation state for the alignment test. */
	FLifetimeState& State;
};

/**
 * Records public resource traffic while delegating storage policy to a fixed arena.
 *
 * The wrapper proves allocation count, exact block identity, and resource reuse
 * without inspecting ownership-control internals.
 */
template<std::size_t Bytes, std::size_t Alignment>
class TTrackingMemoryResource final : public IMemoryResource
{
public:
	/** Records one public allocation request and the exact successful block. */
	EMemoryResult TryAllocate(const std::size_t SizeBytes, const std::size_t AlignmentBytes, FMemoryBlock& OutBlock) noexcept override
	{
		++AllocationRequestCount;

		const EMemoryResult Result = Arena.TryAllocate(SizeBytes, AlignmentBytes, OutBlock);
		if (Result == EMemoryResult::Success)
		{
			++SuccessfulAllocationCount;
			LastAllocatedBlock = OutBlock;
		}
		return Result;
	}

	/** Records the exact block returned through the public deallocation boundary. */
	EMemoryResult Deallocate(const FMemoryBlock Block) noexcept override
	{
		++DeallocationRequestCount;
		LastDeallocatedBlock = Block;
		return Arena.Deallocate(Block);
	}

	/** Preserves the wrapped arena's public caller-usable capacity. */
	std::size_t CapacityBytes() const noexcept override { return Arena.CapacityBytes(); }

	/** Preserves the wrapped arena's public active-byte diagnostic. */
	std::size_t UsedBytes() const noexcept override { return Arena.UsedBytes(); }

	/** Counts all factory allocation attempts made through this exact resource. */
	std::size_t AllocationRequestCount{0};

	/** Counts only allocations that returned a live block. */
	std::size_t SuccessfulAllocationCount{0};

	/** Counts all ownership deallocation attempts made through this exact resource. */
	std::size_t DeallocationRequestCount{0};

	/** Preserves the exact latest successful allocation identity. */
	FMemoryBlock LastAllocatedBlock{};

	/** Preserves the exact block later returned by an owner. */
	FMemoryBlock LastDeallocatedBlock{};

private:
	/** Supplies bounded caller-owned storage while the wrapper observes only public calls. */
	TFixedArena<Bytes, Alignment> Arena;
};

/** Owns a weak observer to itself so final-weak release occurs during value destruction. */
class FSelfObservingValue final
{
public:
	/** Exposes construction and destruction around the self-observer regression. */
	explicit FSelfObservingValue(FLifetimeState& InState) noexcept : State(InState) { ++State.ConstructionCount; }

	/** Marks the destructor body before the member weak observer releases its final count. */
	~FSelfObservingValue() noexcept { ++State.DestructionCount; }

	/** Transfers one already-counted self observer into the value's destruction path. */
	void AdoptSelfObserver(TWeakPtr<FSelfObservingValue>&& InObserver) noexcept { SelfObserver = std::move(InObserver); }

private:
	/** Shares only the fresh counters selected for this regression test. */
	FLifetimeState& State;

	/** Exercises final weak release while the containing value destructor is active. */
	TWeakPtr<FSelfObservingValue> SelfObserver;
};

/** Records fixed-vector lifetime and reverse destruction order in caller-owned state. */
struct FVectorLifetimeState final
{
	/** Proves capacity rejection occurs before element construction. */
	std::size_t ConstructionCount{0};

	/** Proves clear and scope exit destroy only live elements. */
	std::size_t DestructionCount{0};

	/** Preserves destruction order without dynamic storage. */
	std::array<int, 4> DestructionOrder{};
};

/** Gives vector tests one nothrow element with externally observable lifetime. */
class FVectorTrackedValue final
{
public:
	/** Starts one element lifetime carrying an insertion identity. */
	FVectorTrackedValue(FVectorLifetimeState& InState, const int InIdentity) noexcept : State(InState), Identity(InIdentity)
	{
		++State.ConstructionCount;
	}

	/** Appends this element's identity to the fresh bounded destruction trace. */
	~FVectorTrackedValue() noexcept
	{
		State.DestructionOrder[State.DestructionCount] = Identity;
		++State.DestructionCount;
	}

	/** Exposes insertion identity for deterministic iteration assertions. */
	int GetIdentity() const noexcept { return Identity; }

private:
	/** Shares only the observation state owned by the current test. */
	FVectorLifetimeState& State;

	/** Distinguishes elements without relying on their storage addresses. */
	int Identity{0};
};

static_assert(!std::is_copy_constructible<TSharedPtr<FTrackedValue>>::value);
static_assert(!std::is_copy_assignable<TSharedPtr<FTrackedValue>>::value);
static_assert(std::is_move_constructible<TSharedPtr<FTrackedValue>>::value);
static_assert(std::is_move_assignable<TSharedPtr<FTrackedValue>>::value);
static_assert(!std::is_copy_constructible<TWeakPtr<FTrackedValue>>::value);
static_assert(!std::is_copy_assignable<TWeakPtr<FTrackedValue>>::value);
static_assert(std::is_move_constructible<TWeakPtr<FTrackedValue>>::value);
static_assert(std::is_move_assignable<TWeakPtr<FTrackedValue>>::value);
static_assert(std::is_constructible<TSpan<const int>, TSpan<int>>::value);
static_assert(!std::is_constructible<TSpan<int>, TSpan<const int>>::value);

/** Proves aligned in-capacity allocation returns exact usage and capacity diagnostics. */
MW_TEST_CASE(FixedArenaAcceptsAlignedAllocationAndReportsExactUsage)
{
	TFixedArena<64, 16> Arena;
	FMemoryBlock Block{};
	const std::size_t ExpectedCapacityBytes = 64;
	const std::size_t ExpectedUsedBytes = 7;

	const EMemoryResult AllocationResult = Arena.TryAllocate(ExpectedUsedBytes, 16, Block);

	const bool bAddressReturned = Block.Address != nullptr;
	const std::uintptr_t BlockAddress = reinterpret_cast<std::uintptr_t>(Block.Address);
	const bool bAddressAligned = (BlockAddress % 16U) == 0;
	const std::size_t ActualBlockSizeBytes = Block.SizeBytes;
	const std::size_t ActualCapacityBytes = Arena.CapacityBytes();
	const std::size_t ActualUsedBytes = Arena.UsedBytes();
	MW_EXPECT_EQ(Test, EMemoryResult::Success, AllocationResult, "Aligned in-capacity allocation should succeed");
	MW_EXPECT_TRUE(Test, bAddressReturned, "Successful allocation should return a non-null address");
	MW_EXPECT_TRUE(Test, bAddressAligned, "Returned allocation should satisfy requested alignment");
	MW_EXPECT_EQ(Test, ExpectedUsedBytes, ActualBlockSizeBytes, "Returned block should preserve the exact requested size");
	MW_EXPECT_EQ(Test, ExpectedCapacityBytes, ActualCapacityBytes, "Arena should report exact caller-usable capacity");
	MW_EXPECT_EQ(Test, ExpectedUsedBytes, ActualUsedBytes, "Arena should report exact active payload bytes");

	const EMemoryResult DeallocationResult = Arena.Deallocate(Block);
	const std::size_t UsedBytesAfterDeallocation = Arena.UsedBytes();
	MW_EXPECT_EQ(Test, EMemoryResult::Success, DeallocationResult, "Exact active block should deallocate successfully");
	MW_EXPECT_EQ(Test, std::size_t{0}, UsedBytesAfterDeallocation, "Successful deallocation should restore zero usage");
}

/** Proves every invalid alignment clears output and leaves resource usage unchanged. */
MW_TEST_CASE(FixedArenaRejectsZeroNonPowerAndExcessAlignmentAtomically)
{
	TFixedArena<64, 16> Arena;
	FMemoryBlock ZeroAlignmentBlock{reinterpret_cast<void*>(std::uintptr_t{1}), 9};
	FMemoryBlock NonPowerAlignmentBlock{reinterpret_cast<void*>(std::uintptr_t{1}), 9};
	FMemoryBlock ExcessAlignmentBlock{reinterpret_cast<void*>(std::uintptr_t{1}), 9};

	const EMemoryResult ZeroAlignmentResult = Arena.TryAllocate(8, 0, ZeroAlignmentBlock);
	const std::size_t UsedAfterZeroAlignment = Arena.UsedBytes();
	const EMemoryResult NonPowerAlignmentResult = Arena.TryAllocate(8, 3, NonPowerAlignmentBlock);
	const std::size_t UsedAfterNonPowerAlignment = Arena.UsedBytes();
	const EMemoryResult ExcessAlignmentResult = Arena.TryAllocate(8, 32, ExcessAlignmentBlock);
	const std::size_t UsedAfterExcessAlignment = Arena.UsedBytes();

	const bool bZeroAlignmentBlockCleared = ZeroAlignmentBlock.Address == nullptr && ZeroAlignmentBlock.SizeBytes == 0;
	const bool bNonPowerAlignmentBlockCleared = NonPowerAlignmentBlock.Address == nullptr && NonPowerAlignmentBlock.SizeBytes == 0;
	const bool bExcessAlignmentBlockCleared = ExcessAlignmentBlock.Address == nullptr && ExcessAlignmentBlock.SizeBytes == 0;
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

/** Proves zero, oversized, exhausted, and maximum-size requests fail without corrupting usage. */
MW_TEST_CASE(FixedArenaRejectsZeroOversizeExhaustedAndMaximumRequests)
{
	TFixedArena<16, 8> Arena;
	FMemoryBlock ZeroBlock{};
	FMemoryBlock OversizeBlock{};
	FMemoryBlock FullBlock{};
	FMemoryBlock ExhaustedBlock{};
	FMemoryBlock MaximumBlock{};

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

/** Proves malformed and repeated deallocations are rejected before usage changes. */
MW_TEST_CASE(FixedArenaRejectsForeignInteriorWrongSizeAndDoubleFreeAtomically)
{
	TFixedArena<32, 8> Arena;
	TFixedArena<32, 8> ForeignArena;
	FMemoryBlock Block{};
	FMemoryBlock ForeignBlock{};
	const EMemoryResult AllocationResult = Arena.TryAllocate(8, 8, Block);
	const EMemoryResult ForeignAllocationResult = ForeignArena.TryAllocate(8, 8, ForeignBlock);

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

/** Proves freed ranges can be reused regardless of the order neighboring blocks release. */
MW_TEST_CASE(FixedArenaReusesBlocksAfterArbitraryOrderDeallocation)
{
	TFixedArena<24, 8> Arena;
	FMemoryBlock FirstBlock{};
	FMemoryBlock MiddleBlock{};
	FMemoryBlock LastBlock{};
	FMemoryBlock ReusedBlock{};
	const EMemoryResult FirstResult = Arena.TryAllocate(8, 8, FirstBlock);
	const EMemoryResult MiddleResult = Arena.TryAllocate(8, 8, MiddleBlock);
	const EMemoryResult LastResult = Arena.TryAllocate(8, 8, LastBlock);

	const EMemoryResult MiddleFreeResult = Arena.Deallocate(MiddleBlock);
	const EMemoryResult ReuseResult = Arena.TryAllocate(8, 8, ReusedBlock);
	const bool bMiddleAddressReused = ReusedBlock.Address == MiddleBlock.Address;
	const EMemoryResult LastFreeResult = Arena.Deallocate(LastBlock);
	const EMemoryResult FirstFreeResult = Arena.Deallocate(FirstBlock);
	const EMemoryResult ReusedFreeResult = Arena.Deallocate(ReusedBlock);
	const std::size_t FinalUsedBytes = Arena.UsedBytes();

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

/** Proves unique factory exhaustion reports OOM without constructing a value. */
MW_TEST_CASE(UniqueFactoryOutOfMemoryNeverConstructsValue)
{
	TFixedArena<64, 16> Arena;
	FMemoryBlock CapacityBlock{};
	FLifetimeState Lifetime;
	const EMemoryResult FillResult = Arena.TryAllocate(Arena.CapacityBytes(), 1, CapacityBlock);

	auto UniqueResult = MakeUnique<FTrackedValue>(Arena, Lifetime, 7);

	const EMemoryResult FactoryResult = UniqueResult.Result;
	const bool bPointerInvalid = !UniqueResult.Pointer.IsValid();
	const std::size_t ConstructionCount = Lifetime.ConstructionCount;
	const std::size_t DestructionCount = Lifetime.DestructionCount;
	const std::size_t UsedBytes = Arena.UsedBytes();
	MW_EXPECT_EQ(Test, EMemoryResult::Success, FillResult, "Arena should be full before unique factory attempt");
	MW_EXPECT_EQ(Test, EMemoryResult::OutOfMemory, FactoryResult, "Unique factory should report exact exhaustion");
	MW_EXPECT_TRUE(Test, bPointerInvalid, "Failed unique factory should return an empty owner");
	MW_EXPECT_EQ(Test, std::size_t{0}, ConstructionCount, "OOM should be reported before value construction");
	MW_EXPECT_EQ(Test, std::size_t{0}, DestructionCount, "No value should require destruction after OOM");
	MW_EXPECT_EQ(Test, std::size_t{64}, UsedBytes, "Failed unique factory should not change existing usage");
}

/** Proves moving and resetting a unique owner destroys and returns its original block once. */
MW_TEST_CASE(UniquePtrMoveAndResetReturnExactOriginalBlockOnce)
{
	TTrackingMemoryResource<128, 16> Resource;
	FLifetimeState Lifetime;
	auto UniqueResult = MakeUnique<FTrackedValue>(Resource, Lifetime, 19);
	const FMemoryBlock OriginalBlock = Resource.LastAllocatedBlock;

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

/** Proves unique scope exit destroys a live value when no explicit reset occurs. */
MW_TEST_CASE(UniquePtrScopeExitDestroysAndDeallocatesExactlyOnce)
{
	TTrackingMemoryResource<128, 16> Resource;
	FLifetimeState Lifetime;

	{
		auto UniqueResult = MakeUnique<FTrackedValue>(Resource, Lifetime, 3);
		const EMemoryResult FactoryResult = UniqueResult.Result;
		const bool bPointerValid = UniqueResult.Pointer.IsValid();
		MW_EXPECT_EQ(Test, EMemoryResult::Success, FactoryResult, "Unique factory should succeed before scope-exit test");
		MW_EXPECT_TRUE(Test, bPointerValid, "Successful unique factory should return a live owner");
	}

	const std::size_t DestructionCount = Lifetime.DestructionCount;
	const std::size_t DeallocationCount = Resource.DeallocationRequestCount;
	const std::size_t UsedBytes = Resource.UsedBytes();
	MW_EXPECT_EQ(Test, std::size_t{1}, DestructionCount, "Unique owner scope exit should destroy the value exactly once");
	MW_EXPECT_EQ(Test, std::size_t{1}, DeallocationCount, "Unique owner scope exit should deallocate exactly once");
	MW_EXPECT_EQ(Test, std::size_t{0}, UsedBytes, "Unique owner scope exit should restore resource usage");
}

/** Proves explicit strong and weak acquisition preserve value lifetime and one combined allocation. */
MW_TEST_CASE(SharedAndWeakOwnersPreserveValueUntilFinalStrongAndWeakRelease)
{
	TTrackingMemoryResource<256, 64> Resource;
	FLifetimeState Lifetime;
	auto SharedFactoryResult = MakeShared<FTrackedValue>(Resource, Lifetime, 41);
	TSharedPtr<FTrackedValue> Owner = std::move(SharedFactoryResult.Pointer);

	auto ShareResult = Owner.TryShare();
	TSharedPtr<FTrackedValue> SecondOwner = std::move(ShareResult.Pointer);
	auto WeakResult = Owner.TryAcquireWeak();
	TWeakPtr<FTrackedValue> Observer = std::move(WeakResult.Pointer);
	auto PinResult = Observer.Pin();
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
	MW_EXPECT_EQ(Test, ESharedPointerResult::Success, FactoryResult, "Shared factory should create the first strong owner");
	MW_EXPECT_EQ(Test, ESharedPointerResult::Success, ShareOperationResult, "TryShare should acquire an explicit strong owner");
	MW_EXPECT_EQ(Test, ESharedPointerResult::Success, WeakOperationResult, "TryAcquireWeak should acquire an explicit observer");
	MW_EXPECT_EQ(Test, ESharedPointerResult::Success, PinOperationResult, "Pin should acquire a strong owner while value is live");
	MW_EXPECT_EQ(Test, std::size_t{1}, AllocationRequestCount, "Factory should request one combined resource allocation");
	MW_EXPECT_EQ(Test, std::size_t{1}, SuccessfulAllocationCount, "Factory should use one successful combined allocation");
	MW_EXPECT_EQ(Test, std::size_t{3}, StrongCountAfterAcquisitions, "Explicit acquisitions should report three strong owners");
	MW_EXPECT_EQ(Test, std::size_t{1}, WeakCountAfterAcquisitions, "Explicit observer acquisition should report one weak owner");
	MW_EXPECT_EQ(Test, 41, PinnedValueNumber, "Pinned owner should resolve the live shared value");

	Owner.Reset();
	SecondOwner.Reset();
	const std::size_t DestructionBeforeFinalStrong = Lifetime.DestructionCount;
	PinnedOwner.Reset();
	const std::size_t DestructionAfterFinalStrong = Lifetime.DestructionCount;
	const bool bObserverExpired = Observer.IsExpired();
	const std::size_t DeallocationBeforeFinalWeak = Resource.DeallocationRequestCount;
	auto ExpiredPinResult = Observer.Pin();
	const ESharedPointerResult ExpiredPinOperationResult = ExpiredPinResult.Result;
	const bool bExpiredPinInvalid = !ExpiredPinResult.Pointer.IsValid();
	MW_EXPECT_EQ(Test, std::size_t{0}, DestructionBeforeFinalStrong, "Value should remain live while one strong owner remains");
	MW_EXPECT_EQ(Test, std::size_t{1}, DestructionAfterFinalStrong, "Final strong release should destroy the value exactly once");
	MW_EXPECT_TRUE(Test, bObserverExpired, "Weak observer should report expiry after final strong release");
	MW_EXPECT_EQ(Test, std::size_t{0}, DeallocationBeforeFinalWeak, "Expired weak observer should retain the combined allocation");
	MW_EXPECT_EQ(Test, ESharedPointerResult::Expired, ExpiredPinOperationResult, "Expired observer should reject Pin");
	MW_EXPECT_TRUE(Test, bExpiredPinInvalid, "Expired Pin should return an empty strong owner");

	Observer.Reset();
	const std::size_t DeallocationAfterFinalWeak = Resource.DeallocationRequestCount;
	const bool bSameAddressReturned = Resource.LastDeallocatedBlock.Address == Resource.LastAllocatedBlock.Address;
	const bool bSameSizeReturned = Resource.LastDeallocatedBlock.SizeBytes == Resource.LastAllocatedBlock.SizeBytes;
	const std::size_t UsedBytes = Resource.UsedBytes();
	MW_EXPECT_EQ(Test, std::size_t{1}, DeallocationAfterFinalWeak, "Final weak release should deallocate the combined block once");
	MW_EXPECT_TRUE(Test, bSameAddressReturned, "Shared ownership should return the original combined address");
	MW_EXPECT_TRUE(Test, bSameSizeReturned, "Shared ownership should return the original combined size");
	MW_EXPECT_EQ(Test, std::size_t{0}, UsedBytes, "Final weak release should restore resource usage");
}

/** Proves shared factory OOM is reported before construction or deallocation is needed. */
MW_TEST_CASE(SharedFactoryOutOfMemoryNeverConstructsValue)
{
	TTrackingMemoryResource<1, 64> Resource;
	FLifetimeState Lifetime;

	auto SharedResult = MakeShared<FTrackedValue>(Resource, Lifetime, 5);

	const ESharedPointerResult FactoryResult = SharedResult.Result;
	const bool bPointerInvalid = !SharedResult.Pointer.IsValid();
	const std::size_t ConstructionCount = Lifetime.ConstructionCount;
	const std::size_t DeallocationCount = Resource.DeallocationRequestCount;
	const std::size_t UsedBytes = Resource.UsedBytes();
	MW_EXPECT_EQ(Test, ESharedPointerResult::OutOfMemory, FactoryResult, "Shared factory should report combined-allocation exhaustion");
	MW_EXPECT_TRUE(Test, bPointerInvalid, "OOM shared factory should return an empty owner");
	MW_EXPECT_EQ(Test, std::size_t{0}, ConstructionCount, "Shared OOM should occur before value construction");
	MW_EXPECT_EQ(Test, std::size_t{0}, DeallocationCount, "Failed shared allocation should not require deallocation");
	MW_EXPECT_EQ(Test, std::size_t{0}, UsedBytes, "Failed shared allocation should preserve zero usage");
}

/** Proves unsupported shared layout alignment is rejected before value construction. */
MW_TEST_CASE(SharedFactoryRejectsUnsupportedAlignmentBeforeConstruction)
{
	TTrackingMemoryResource<256, 16> Resource;
	FLifetimeState Lifetime;

	auto SharedResult = MakeShared<FOverAlignedTrackedValue>(Resource, Lifetime);

	const ESharedPointerResult FactoryResult = SharedResult.Result;
	const bool bPointerInvalid = !SharedResult.Pointer.IsValid();
	const std::size_t ConstructionCount = Lifetime.ConstructionCount;
	const std::size_t SuccessfulAllocationCount = Resource.SuccessfulAllocationCount;
	const std::size_t DeallocationCount = Resource.DeallocationRequestCount;
	MW_EXPECT_EQ(Test, ESharedPointerResult::UnsupportedAlignment, FactoryResult, "Shared factory should preserve unsupported-alignment failure");
	MW_EXPECT_TRUE(Test, bPointerInvalid, "Alignment failure should return an empty shared owner");
	MW_EXPECT_EQ(Test, std::size_t{0}, ConstructionCount, "Alignment failure should occur before value construction");
	MW_EXPECT_EQ(Test, std::size_t{0}, SuccessfulAllocationCount, "Unsupported alignment should not create a resource block");
	MW_EXPECT_EQ(Test, std::size_t{0}, DeallocationCount, "Unsupported alignment should not require deallocation");
}

/** Proves a self-owned final weak observer cannot deallocate during containing value destruction. */
MW_TEST_CASE(SharedPtrDefersSelfOwnedFinalWeakDeallocationUntilValueDestructionCompletes)
{
	TTrackingMemoryResource<256, 64> Resource;
	FLifetimeState Lifetime;
	auto SharedResult = MakeShared<FSelfObservingValue>(Resource, Lifetime);
	TSharedPtr<FSelfObservingValue> Owner = std::move(SharedResult.Pointer);
	auto WeakResult = Owner.TryAcquireWeak();
	TWeakPtr<FSelfObservingValue> SelfObserver = std::move(WeakResult.Pointer);
	FSelfObservingValue* const Value = Owner.Get();
	if (Value != nullptr)
	{
		Value->AdoptSelfObserver(std::move(SelfObserver));
	}
	const ESharedPointerResult FactoryResult = SharedResult.Result;
	const ESharedPointerResult WeakOperationResult = WeakResult.Result;
	const std::size_t WeakCountBeforeRelease = Owner.WeakReferenceCount();

	Owner.Reset();

	const std::size_t ConstructionCount = Lifetime.ConstructionCount;
	const std::size_t DestructionCount = Lifetime.DestructionCount;
	const std::size_t DeallocationCount = Resource.DeallocationRequestCount;
	const std::size_t UsedBytes = Resource.UsedBytes();
	MW_EXPECT_EQ(Test, ESharedPointerResult::Success, FactoryResult, "Self-observing shared value should construct successfully");
	MW_EXPECT_EQ(Test, ESharedPointerResult::Success, WeakOperationResult, "Self observer should be acquired explicitly");
	MW_EXPECT_EQ(Test, std::size_t{1}, WeakCountBeforeRelease, "Value should own the only weak observer before final release");
	MW_EXPECT_EQ(Test, std::size_t{1}, ConstructionCount, "Self-observing value should construct exactly once");
	MW_EXPECT_EQ(Test, std::size_t{1}, DestructionCount, "Final strong release should finish value destruction exactly once");
	MW_EXPECT_EQ(Test, std::size_t{1}, DeallocationCount, "Self-owned final weak release should deallocate only after destruction");
	MW_EXPECT_EQ(Test, std::size_t{0}, UsedBytes, "Self-observer teardown should restore resource usage");
}

/** Proves the maximum strong count succeeds and one more TryShare fails without wrapping. */
MW_TEST_CASE(SharedPtrRejectsStrongReferenceCountOverflowWithoutWrap)
{
	using FSharedPointer = TSharedPtr<FTrackedValue>;
	constexpr std::size_t MaximumCount = FSharedPointer::MaximumReferenceCount();
	TTrackingMemoryResource<256, 64> Resource;
	FLifetimeState Lifetime;
	auto SharedResult = MakeShared<FTrackedValue>(Resource, Lifetime, 8);
	FSharedPointer Owner = std::move(SharedResult.Pointer);
	const ESharedPointerResult FactoryResult = SharedResult.Result;
	std::array<FSharedPointer, MaximumCount - 1U> AdditionalOwners{};
	bool bAllBoundaryAcquisitionsSucceeded = true;

	for (std::size_t OwnerIndex = 0; OwnerIndex < AdditionalOwners.size(); ++OwnerIndex)
	{
		auto ShareResult = Owner.TryShare();
		if (ShareResult.Result != ESharedPointerResult::Success)
		{
			bAllBoundaryAcquisitionsSucceeded = false;
			break;
		}
		AdditionalOwners[OwnerIndex] = std::move(ShareResult.Pointer);
	}

	const std::size_t CountAtBoundary = Owner.StrongReferenceCount();
	auto OverflowResult = Owner.TryShare();
	const ESharedPointerResult OverflowOperationResult = OverflowResult.Result;
	const std::size_t CountAfterOverflow = Owner.StrongReferenceCount();
	const bool bOverflowPointerInvalid = !OverflowResult.Pointer.IsValid();
	MW_EXPECT_EQ(Test, ESharedPointerResult::Success, FactoryResult, "Strong boundary value should construct successfully");
	MW_EXPECT_TRUE(Test, bAllBoundaryAcquisitionsSucceeded, "Every strong acquisition through the maximum should succeed");
	MW_EXPECT_EQ(Test, MaximumCount, CountAtBoundary, "Strong diagnostics should reach the exact 65,535 boundary");
	MW_EXPECT_EQ(
		Test, ESharedPointerResult::ReferenceCountOverflow, OverflowOperationResult, "Strong acquisition above 65,535 should report overflow");
	MW_EXPECT_TRUE(Test, bOverflowPointerInvalid, "Strong overflow should not return another owner");
	MW_EXPECT_EQ(Test, MaximumCount, CountAfterOverflow, "Strong overflow should leave the boundary count unchanged");

	for (FSharedPointer& AdditionalOwner : AdditionalOwners)
	{
		AdditionalOwner.Reset();
	}
	const std::size_t CountAfterAdditionalRelease = Owner.StrongReferenceCount();
	Owner.Reset();
	const std::size_t DestructionCount = Lifetime.DestructionCount;
	const std::size_t DeallocationCount = Resource.DeallocationRequestCount;
	MW_EXPECT_EQ(Test, std::size_t{1}, CountAfterAdditionalRelease, "Releasing acquired owners should leave the original owner");
	MW_EXPECT_EQ(Test, std::size_t{1}, DestructionCount, "Final boundary-test strong release should destroy exactly once");
	MW_EXPECT_EQ(Test, std::size_t{1}, DeallocationCount, "Boundary-test allocation should deallocate exactly once");
}

/** Proves the maximum weak count succeeds and one more acquisition fails without wrapping. */
MW_TEST_CASE(SharedPtrRejectsWeakReferenceCountOverflowWithoutWrap)
{
	using FSharedPointer = TSharedPtr<FTrackedValue>;
	using FWeakPointer = TWeakPtr<FTrackedValue>;
	constexpr std::size_t MaximumCount = FWeakPointer::MaximumReferenceCount();
	TTrackingMemoryResource<256, 64> Resource;
	FLifetimeState Lifetime;
	auto SharedResult = MakeShared<FTrackedValue>(Resource, Lifetime, 13);
	FSharedPointer Owner = std::move(SharedResult.Pointer);
	const ESharedPointerResult FactoryResult = SharedResult.Result;
	std::array<FWeakPointer, MaximumCount> Observers{};
	bool bAllBoundaryAcquisitionsSucceeded = true;

	for (std::size_t ObserverIndex = 0; ObserverIndex < Observers.size(); ++ObserverIndex)
	{
		auto WeakResult = Owner.TryAcquireWeak();
		if (WeakResult.Result != ESharedPointerResult::Success)
		{
			bAllBoundaryAcquisitionsSucceeded = false;
			break;
		}
		Observers[ObserverIndex] = std::move(WeakResult.Pointer);
	}

	const std::size_t CountAtBoundary = Owner.WeakReferenceCount();
	auto OverflowResult = Owner.TryAcquireWeak();
	const ESharedPointerResult OverflowOperationResult = OverflowResult.Result;
	const std::size_t CountAfterOverflow = Owner.WeakReferenceCount();
	const bool bOverflowPointerExpired = OverflowResult.Pointer.IsExpired();
	MW_EXPECT_EQ(Test, ESharedPointerResult::Success, FactoryResult, "Weak boundary value should construct successfully");
	MW_EXPECT_TRUE(Test, bAllBoundaryAcquisitionsSucceeded, "Every weak acquisition through the maximum should succeed");
	MW_EXPECT_EQ(Test, MaximumCount, CountAtBoundary, "Weak diagnostics should reach the exact 65,535 boundary");
	MW_EXPECT_EQ(Test, ESharedPointerResult::ReferenceCountOverflow, OverflowOperationResult, "Weak acquisition above 65,535 should report overflow");
	MW_EXPECT_TRUE(Test, bOverflowPointerExpired, "Weak overflow should not return another observer");
	MW_EXPECT_EQ(Test, MaximumCount, CountAfterOverflow, "Weak overflow should leave the boundary count unchanged");

	for (FWeakPointer& Observer : Observers)
	{
		Observer.Reset();
	}
	const std::size_t WeakCountAfterRelease = Owner.WeakReferenceCount();
	Owner.Reset();
	const std::size_t DestructionCount = Lifetime.DestructionCount;
	const std::size_t DeallocationCount = Resource.DeallocationRequestCount;
	MW_EXPECT_EQ(Test, std::size_t{0}, WeakCountAfterRelease, "Releasing all observers should restore zero weak count");
	MW_EXPECT_EQ(Test, std::size_t{1}, DestructionCount, "Final strong release should destroy the boundary-test value once");
	MW_EXPECT_EQ(Test, std::size_t{1}, DeallocationCount, "Boundary-test combined allocation should deallocate once");
}

/** Proves empty and zero-capacity vectors expose safe boundary state and atomic rejection. */
MW_TEST_CASE(StaticVectorHandlesEmptyAndZeroCapacityBoundaries)
{
	TStaticVector<int, 3> EmptyVector;
	TStaticVector<int, 0> ZeroCapacityVector;
	const bool bEmptyVectorIsEmpty = EmptyVector.IsEmpty();
	const bool bEmptyVectorIsFull = EmptyVector.IsFull();
	const bool bEmptyVectorHasSpace = !bEmptyVectorIsFull;
	int* const EmptyData = EmptyVector.Data();
	int* const EmptyBegin = EmptyVector.begin();
	int* const EmptyEnd = EmptyVector.end();
	const std::size_t EmptyCapacity = EmptyVector.Capacity();

	const ERuntimeResult AddResult = ZeroCapacityVector.Emplace(7);
	const bool bZeroVectorIsEmpty = ZeroCapacityVector.IsEmpty();
	const bool bZeroVectorIsFull = ZeroCapacityVector.IsFull();
	const std::size_t ZeroSize = ZeroCapacityVector.Size();
	const std::size_t ZeroCapacity = ZeroCapacityVector.Capacity();
	int* const ZeroData = ZeroCapacityVector.Data();
	int* const NullIntPointer = nullptr;

	MW_EXPECT_TRUE(Test, bEmptyVectorIsEmpty, "Fresh positive-capacity vector should be empty");
	MW_EXPECT_TRUE(Test, bEmptyVectorHasSpace, "Fresh positive-capacity vector should not be full");
	MW_EXPECT_EQ(Test, NullIntPointer, EmptyData, "Empty vector should expose null data");
	MW_EXPECT_EQ(Test, NullIntPointer, EmptyBegin, "Empty vector iteration should begin at null");
	MW_EXPECT_EQ(Test, NullIntPointer, EmptyEnd, "Empty vector iteration should end at null");
	MW_EXPECT_EQ(Test, std::size_t{3}, EmptyCapacity, "Vector should expose its exact compile-time capacity");
	MW_EXPECT_EQ(Test, ERuntimeResult::CapacityExceeded, AddResult, "Zero-capacity vector should reject its first element");
	MW_EXPECT_TRUE(Test, bZeroVectorIsEmpty, "Rejected zero-capacity add should preserve empty state");
	MW_EXPECT_TRUE(Test, bZeroVectorIsFull, "Zero-capacity vector should report its capacity is full");
	MW_EXPECT_EQ(Test, std::size_t{0}, ZeroSize, "Rejected zero-capacity add should preserve size zero");
	MW_EXPECT_EQ(Test, std::size_t{0}, ZeroCapacity, "Zero-capacity vector should report capacity zero");
	MW_EXPECT_EQ(Test, NullIntPointer, ZeroData, "Zero-capacity vector should expose null data");
}

/** Proves capacity-plus-one rejection preserves insertion order and reverse clear destruction. */
MW_TEST_CASE(StaticVectorPreservesIterationAndRejectsCapacityPlusOneBeforeConstruction)
{
	FVectorLifetimeState Lifetime;
	TStaticVector<FVectorTrackedValue, 3> Vector;
	const ERuntimeResult FirstResult = Vector.Emplace(Lifetime, 1);
	const ERuntimeResult SecondResult = Vector.Emplace(Lifetime, 2);
	const ERuntimeResult ThirdResult = Vector.Emplace(Lifetime, 3);
	const ERuntimeResult ExcessResult = Vector.Emplace(Lifetime, 4);

	std::array<int, 3> IterationOrder{};
	std::size_t IterationCount = 0;
	for (const FVectorTrackedValue& Value : Vector)
	{
		IterationOrder[IterationCount] = Value.GetIdentity();
		++IterationCount;
	}
	const std::size_t SizeAtCapacity = Vector.Size();
	const bool bFullAtCapacity = Vector.IsFull();
	const std::size_t ConstructionCountAtCapacity = Lifetime.ConstructionCount;
	const int FirstIterationIdentity = IterationOrder[0];
	const int SecondIterationIdentity = IterationOrder[1];
	const int ThirdIterationIdentity = IterationOrder[2];
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, FirstResult, "First vector element should construct");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, SecondResult, "Second vector element should construct");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ThirdResult, "Element at exact capacity should construct");
	MW_EXPECT_EQ(Test, ERuntimeResult::CapacityExceeded, ExcessResult, "Capacity-plus-one element should be rejected");
	MW_EXPECT_EQ(Test, std::size_t{3}, SizeAtCapacity, "Capacity rejection should preserve exact vector size");
	MW_EXPECT_TRUE(Test, bFullAtCapacity, "Vector should report full at exact capacity");
	MW_EXPECT_EQ(Test, std::size_t{3}, ConstructionCountAtCapacity, "Rejected excess element should never construct");
	MW_EXPECT_EQ(Test, std::size_t{3}, IterationCount, "Iteration should visit every live element once");
	MW_EXPECT_EQ(Test, 1, FirstIterationIdentity, "Iteration should visit the first inserted element first");
	MW_EXPECT_EQ(Test, 2, SecondIterationIdentity, "Iteration should preserve the second insertion position");
	MW_EXPECT_EQ(Test, 3, ThirdIterationIdentity, "Iteration should visit the capacity element last");

	Vector.Clear();
	const std::size_t SizeAfterClear = Vector.Size();
	const std::size_t DestructionCount = Lifetime.DestructionCount;
	const int FirstDestroyedIdentity = Lifetime.DestructionOrder[0];
	const int SecondDestroyedIdentity = Lifetime.DestructionOrder[1];
	const int ThirdDestroyedIdentity = Lifetime.DestructionOrder[2];
	MW_EXPECT_EQ(Test, std::size_t{0}, SizeAfterClear, "Clear should restore vector size zero");
	MW_EXPECT_EQ(Test, std::size_t{3}, DestructionCount, "Clear should destroy every live element exactly once");
	MW_EXPECT_EQ(Test, 3, FirstDestroyedIdentity, "Clear should destroy the latest element first");
	MW_EXPECT_EQ(Test, 2, SecondDestroyedIdentity, "Clear should continue in reverse insertion order");
	MW_EXPECT_EQ(Test, 1, ThirdDestroyedIdentity, "Clear should destroy the earliest element last");
}

/** Proves vector scope exit destroys exactly the elements whose construction succeeded. */
MW_TEST_CASE(StaticVectorScopeExitDestroysOnlyLiveElements)
{
	FVectorLifetimeState Lifetime;
	{
		TStaticVector<FVectorTrackedValue, 3> Vector;
		const ERuntimeResult FirstResult = Vector.Emplace(Lifetime, 5);
		const ERuntimeResult SecondResult = Vector.Emplace(Lifetime, 6);
		MW_EXPECT_EQ(Test, ERuntimeResult::Success, FirstResult, "First scoped vector element should construct");
		MW_EXPECT_EQ(Test, ERuntimeResult::Success, SecondResult, "Second scoped vector element should construct");
	}

	const std::size_t ConstructionCount = Lifetime.ConstructionCount;
	const std::size_t DestructionCount = Lifetime.DestructionCount;
	const int FirstDestroyedIdentity = Lifetime.DestructionOrder[0];
	const int SecondDestroyedIdentity = Lifetime.DestructionOrder[1];
	MW_EXPECT_EQ(Test, std::size_t{2}, ConstructionCount, "Scoped vector should construct only added elements");
	MW_EXPECT_EQ(Test, std::size_t{2}, DestructionCount, "Scoped vector should destroy every live element exactly once");
	MW_EXPECT_EQ(Test, 6, FirstDestroyedIdentity, "Scope exit should destroy latest live element first");
	MW_EXPECT_EQ(Test, 5, SecondDestroyedIdentity, "Scope exit should destroy earliest live element last");
}

/** Proves null spans are valid only at the empty boundary. */
MW_TEST_CASE(SpanDistinguishesValidEmptyNullFromInvalidNonEmptyNull)
{
	TSpan<int> DefaultSpan;
	TSpan<int> ExplicitEmptySpan(nullptr, 0);
	TSpan<int> InvalidSpan(nullptr, 1);

	const bool bDefaultValid = DefaultSpan.IsValid();
	const bool bDefaultEmpty = DefaultSpan.IsEmpty();
	int* const DefaultData = DefaultSpan.Data();
	const bool bExplicitEmptyValid = ExplicitEmptySpan.IsValid();
	const bool bExplicitEmpty = ExplicitEmptySpan.IsEmpty();
	int* const ExplicitEmptyBegin = ExplicitEmptySpan.begin();
	int* const ExplicitEmptyEnd = ExplicitEmptySpan.end();
	const bool bInvalidSpanValid = InvalidSpan.IsValid();
	const bool bInvalidSpanEmpty = InvalidSpan.IsEmpty();
	const bool bInvalidSpanRejected = !bInvalidSpanValid;
	const bool bInvalidSpanNonEmpty = !bInvalidSpanEmpty;
	const std::size_t InvalidSpanSize = InvalidSpan.Size();
	int* const NullIntPointer = nullptr;

	MW_EXPECT_TRUE(Test, bDefaultValid, "Default null span should be a valid empty view");
	MW_EXPECT_TRUE(Test, bDefaultEmpty, "Default span should report empty");
	MW_EXPECT_EQ(Test, NullIntPointer, DefaultData, "Default span should expose null data");
	MW_EXPECT_TRUE(Test, bExplicitEmptyValid, "Explicit null zero-count span should be valid");
	MW_EXPECT_TRUE(Test, bExplicitEmpty, "Explicit null zero-count span should report empty");
	MW_EXPECT_EQ(Test, NullIntPointer, ExplicitEmptyBegin, "Empty null span should begin at null");
	MW_EXPECT_EQ(Test, NullIntPointer, ExplicitEmptyEnd, "Empty null span should end at null");
	MW_EXPECT_TRUE(Test, bInvalidSpanRejected, "Non-empty null span should report invalid");
	MW_EXPECT_TRUE(Test, bInvalidSpanNonEmpty, "Non-empty null span should preserve its non-empty count");
	MW_EXPECT_EQ(Test, std::size_t{1}, InvalidSpanSize, "Invalid span should retain the caller-provided count");
}

/** Proves mutable and const array spans preserve views, mutation, and iteration order. */
MW_TEST_CASE(SpanProvidesMutableAndConstArrayViewsInOrder)
{
	int MutableElements[]{2, 4, 6};
	const int ConstElements[]{1, 3, 5};
	TSpan<int> MutableSpan(MutableElements);
	TSpan<const int> ConstFromMutable(MutableSpan);
	TSpan<const int> ConstSpan(ConstElements);

	MutableSpan[1] = 8;
	int MutableSum = 0;
	for (int& Element : MutableSpan)
	{
		MutableSum += Element;
	}
	int ConstFromMutableSum = 0;
	for (const int Element : ConstFromMutable)
	{
		ConstFromMutableSum += Element;
	}
	int ConstSum = 0;
	for (const int Element : ConstSpan)
	{
		ConstSum += Element;
	}

	const bool bMutableValid = MutableSpan.IsValid();
	const bool bConstFromMutableValid = ConstFromMutable.IsValid();
	const bool bConstValid = ConstSpan.IsValid();
	const std::size_t MutableSize = MutableSpan.Size();
	const std::size_t ConstSize = ConstSpan.Size();
	const int MutatedArrayValue = MutableElements[1];
	const bool bConstConversionSharesData = ConstFromMutable.Data() == MutableSpan.Data();
	MW_EXPECT_TRUE(Test, bMutableValid, "Mutable array span should be valid");
	MW_EXPECT_TRUE(Test, bConstFromMutableValid, "Const view converted from mutable span should be valid");
	MW_EXPECT_TRUE(Test, bConstValid, "Const array span should be valid");
	MW_EXPECT_EQ(Test, std::size_t{3}, MutableSize, "Mutable array span should preserve array extent");
	MW_EXPECT_EQ(Test, std::size_t{3}, ConstSize, "Const array span should preserve array extent");
	MW_EXPECT_EQ(Test, 8, MutatedArrayValue, "Mutable span write should update caller-owned array");
	MW_EXPECT_TRUE(Test, bConstConversionSharesData, "Const conversion should observe the same caller-owned storage");
	MW_EXPECT_EQ(Test, 16, MutableSum, "Mutable span iteration should visit each element in order");
	MW_EXPECT_EQ(Test, 16, ConstFromMutableSum, "Converted const span should observe the mutable update");
	MW_EXPECT_EQ(Test, 9, ConstSum, "Const span iteration should visit each const element in order");
}

} // namespace
