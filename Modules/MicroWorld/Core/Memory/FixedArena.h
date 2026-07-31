#pragma once

#include <MicroWorld/Core/ByteCodecConstants.h>
#include <MicroWorld/Core/Memory/MemoryResource.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace MicroWorld::Core
{

/**
 * Motivation: Gives a fixed-capacity owner one memory resource that supplies reusable aligned
 *   storage from caller-selected bytes, so allocation never falls back to a heap.
 * Responsibilities: Track free and used ranges with packed boundary markers, allocate the first
 *   fitting aligned range, and release only an exact active block, without compaction or fallback.
 * Example:
 *   TFixedArena<256, 8> Arena;
 *   FMemoryBlock Block{};
 *   Arena.TryAllocate(16, 4, Block);
 */
template<std::size_t StorageCapacityBytes, std::size_t GuaranteedAlignmentBytes>
class TFixedArena final : public IMemoryResource
{
	static_assert(StorageCapacityBytes > 0, "A fixed arena must expose at least one byte.");
	static_assert(
		GuaranteedAlignmentBytes > 0 && (GuaranteedAlignmentBytes & (GuaranteedAlignmentBytes - 1U)) == 0,
		"A fixed arena alignment must be a non-zero power of two.");
	static_assert(
		StorageCapacityBytes <= std::numeric_limits<std::size_t>::max() - (GuaranteedAlignmentBytes - 1U),
		"A fixed arena's aligned backing storage must fit in size_t.");

public:
	/**
	 * Motivation: Lets an owner declare an empty arena whose storage and metadata are caller-owned.
	 * Responsibilities: Produce zero active bytes over the reserved fixed storage.
	 */
	TFixedArena() noexcept = default;

	/**
	 * Motivation: Preserves the resource identity every outstanding block depends on.
	 * Responsibilities: Reject copy construction so outstanding block addresses stay valid.
	 */
	TFixedArena(const TFixedArena&) = delete;

	/**
	 * Motivation: Prevents assigning storage identity across resource boundaries.
	 * Responsibilities: Reject copy assignment so outstanding block addresses stay valid.
	 */
	TFixedArena& operator=(const TFixedArena&) = delete;

	/**
	 * Motivation: Preserves addresses returned from this caller-owned arena.
	 * Responsibilities: Reject move construction so outstanding block addresses stay valid.
	 */
	TFixedArena(TFixedArena&&) = delete;

	/**
	 * Motivation: Prevents moving storage behind outstanding block addresses.
	 * Responsibilities: Reject move assignment so outstanding block addresses stay valid.
	 */
	TFixedArena& operator=(TFixedArena&&) = delete;

	/**
	 * Motivation: Lets an owner end the resource without owning the objects constructed in it.
	 * Responsibilities: Destroy resource identity without touching constructed object lifetimes.
	 */
	~TFixedArena() noexcept override = default;

	/**
	 * Motivation: Lets a caller reserve one aligned range when capacity allows.
	 * Responsibilities: Reject unsupported alignment and exhaustion, then commit the first fitting free range into OutBlock.
	 */
	EMemoryResult TryAllocate(const std::size_t InSizeBytes, const std::size_t InAlignmentBytes, FMemoryBlock& OutBlock) noexcept override
	{
		OutBlock = {};
		const EMemoryResult RequestResult = ValidateAllocationRequest(InSizeBytes, InAlignmentBytes);
		if (RequestResult != EMemoryResult::Success)
		{
			return RequestResult;
		}
		std::size_t StartOffset = 0;
		if (!FindAlignedFreeRange(InSizeBytes, InAlignmentBytes, StartOffset))
		{
			return EMemoryResult::OutOfMemory;
		}
		CommitAllocation(StartOffset, InSizeBytes, OutBlock);
		return EMemoryResult::Success;
	}

	/**
	 * Motivation: Lets a caller return one range to the free pool without freeing another's bytes.
	 * Responsibilities: Release only an exact active range that belongs to this arena, rejecting anything else.
	 */
	EMemoryResult Deallocate(const FMemoryBlock InBlock) noexcept override
	{
		std::size_t AllocationStart = 0;
		std::size_t AllocationEnd = 0;
		const EMemoryResult LocateResult = LocateOwnedAllocation(InBlock, AllocationStart, AllocationEnd);
		if (LocateResult != EMemoryResult::Success)
		{
			return LocateResult;
		}
		const EMemoryResult BoundaryResult = ValidateExactBlockBoundaries(AllocationStart, AllocationEnd);
		if (BoundaryResult != EMemoryResult::Success)
		{
			return BoundaryResult;
		}
		if (UsedSizeBytes < InBlock.SizeBytes)
		{
			return EMemoryResult::InvalidBlock;
		}
		ReleaseMarkedRange(AllocationStart, AllocationEnd, InBlock.SizeBytes);
		return EMemoryResult::Success;
	}

	/**
	 * Motivation: Lets a caller test a request against the fixed limit without magic numbers.
	 * Responsibilities: Report the compile-time caller-usable capacity without marker storage.
	 */
	std::size_t CapacityBytes() const noexcept override { return StorageCapacityBytes; }

	/**
	 * Motivation: Keeps exhaustion observable so a caller can react before allocation fails.
	 * Responsibilities: Report the exact payload bytes retained by active allocations.
	 */
	std::size_t UsedBytes() const noexcept override { return UsedSizeBytes; }

private:
	/**
	 * Motivation: Lets TryAllocate reject a bad request before scanning the storage.
	 * Responsibilities: Report unsupported alignment or a size that cannot fit the remaining capacity.
	 */
	EMemoryResult ValidateAllocationRequest(const std::size_t InSizeBytes, const std::size_t InAlignmentBytes) const noexcept
	{
		if (!IsSupportedAlignment(InAlignmentBytes))
		{
			return EMemoryResult::UnsupportedAlignment;
		}
		if (!FitsFreeCapacity(InSizeBytes))
		{
			return EMemoryResult::OutOfMemory;
		}
		return EMemoryResult::Success;
	}

	/**
	 * Motivation: Lets TryAllocate choose where the next range starts without per-allocation linked lists.
	 * Responsibilities: Scan for the first aligned run of InSizeBytes free bytes and report its start offset.
	 */
	bool FindAlignedFreeRange(const std::size_t InSizeBytes, const std::size_t InAlignmentBytes, std::size_t& OutStartOffset) const noexcept
	{
		bool bInsideAllocation = false;
		std::size_t FreeRangeStart = 0;
		std::size_t FreeRangeSize = 0;
		for (std::size_t Offset = 0; Offset < StorageCapacityBytes; ++Offset)
		{
			if (ReadMarker(AllocationStartMarkers, Offset))
			{
				bInsideAllocation = true;
			}
			if (bInsideAllocation)
			{
				FreeRangeSize = 0;
			}
			else if (FreeRangeSize > 0)
			{
				++FreeRangeSize;
			}
			else if ((Offset & (InAlignmentBytes - 1U)) == 0)
			{
				FreeRangeStart = Offset;
				FreeRangeSize = 1;
			}
			if (FreeRangeSize == InSizeBytes)
			{
				OutStartOffset = FreeRangeStart;
				return true;
			}
			if (ReadMarker(AllocationEndMarkers, Offset))
			{
				bInsideAllocation = false;
			}
		}
		return false;
	}

	/**
	 * Motivation: Lets TryAllocate turn a found range into a live block atomically.
	 * Responsibilities: Mark the range boundaries, account for its bytes, and hand its address back to the caller.
	 */
	void CommitAllocation(const std::size_t InStartOffset, const std::size_t InSizeBytes, FMemoryBlock& OutBlock) noexcept
	{
		const std::size_t AllocationEnd = InStartOffset + InSizeBytes - 1U;
		WriteMarker(AllocationStartMarkers, InStartOffset, true);
		WriteMarker(AllocationEndMarkers, AllocationEnd, true);
		UsedSizeBytes += InSizeBytes;
		OutBlock.Address = static_cast<void*>(StorageBegin() + InStartOffset);
		OutBlock.SizeBytes = InSizeBytes;
	}

	/**
	 * Motivation: Lets Deallocate prove a block belongs to this arena before releasing it.
	 * Responsibilities: Map the block to its owned byte range, rejecting anything not exactly allocated here.
	 */
	EMemoryResult LocateOwnedAllocation(const FMemoryBlock InBlock, std::size_t& OutStart, std::size_t& OutEnd) noexcept
	{
		if (InBlock.Address == nullptr || InBlock.SizeBytes == 0)
		{
			return EMemoryResult::InvalidBlock;
		}
		const std::uintptr_t StorageAddress = reinterpret_cast<std::uintptr_t>(StorageBegin());
		const std::uintptr_t StorageEndAddress = reinterpret_cast<std::uintptr_t>(StorageBegin() + StorageCapacityBytes);
		const std::uintptr_t BlockAddress = reinterpret_cast<std::uintptr_t>(InBlock.Address);
		if (!IsBlockInRange(BlockAddress, StorageAddress, StorageEndAddress))
		{
			return EMemoryResult::InvalidBlock;
		}
		const std::size_t AllocationStart = static_cast<std::size_t>(BlockAddress - StorageAddress);
		if (InBlock.SizeBytes > StorageCapacityBytes - AllocationStart)
		{
			return EMemoryResult::InvalidBlock;
		}
		const std::size_t AllocationEnd = AllocationStart + InBlock.SizeBytes - 1U;
		if (!ReadMarker(AllocationStartMarkers, AllocationStart) || !ReadMarker(AllocationEndMarkers, AllocationEnd))
		{
			return EMemoryResult::InvalidBlock;
		}
		OutStart = AllocationStart;
		OutEnd = AllocationEnd;
		return EMemoryResult::Success;
	}

	/**
	 * Motivation: Stops Deallocate from freeing bytes shared with a neighboring allocation.
	 * Responsibilities: Reject when any other allocation boundary falls inside the block's byte range.
	 */
	EMemoryResult ValidateExactBlockBoundaries(const std::size_t InAllocationStart, const std::size_t InAllocationEnd) const noexcept
	{
		for (std::size_t Offset = InAllocationStart; Offset <= InAllocationEnd; ++Offset)
		{
			const bool bUnexpectedStart = Offset != InAllocationStart && ReadMarker(AllocationStartMarkers, Offset);
			const bool bUnexpectedEnd = Offset != InAllocationEnd && ReadMarker(AllocationEndMarkers, Offset);
			if (bUnexpectedStart || bUnexpectedEnd)
			{
				return EMemoryResult::InvalidBlock;
			}
		}
		return EMemoryResult::Success;
	}

	/**
	 * Motivation: Lets Deallocate return one block to the free pool cleanly.
	 * Responsibilities: Clear the block's boundary markers and subtract its bytes from the used count.
	 */
	void ReleaseMarkedRange(const std::size_t InAllocationStart, const std::size_t InAllocationEnd, const std::size_t InSizeBytes) noexcept
	{
		WriteMarker(AllocationStartMarkers, InAllocationStart, false);
		WriteMarker(AllocationEndMarkers, InAllocationEnd, false);
		UsedSizeBytes -= InSizeBytes;
	}

	/** Motivation: Sizes the boundary-marker storage so one bit covers every usable byte. */
	static constexpr std::size_t MarkerStorageBytes = (StorageCapacityBytes + (BitsPerByte - 1)) / BitsPerByte;

	/** Motivation: Reserves enough local bytes to expose StorageCapacityBytes after aligning the first usable byte. */
	static constexpr std::size_t RawStorageSizeBytes = StorageCapacityBytes + GuaranteedAlignmentBytes - 1U;

	/**
	 * Motivation: Gives every allocation a stable aligned address without padding around the base.
	 * Responsibilities: Return the first guaranteed-aligned byte of the backing storage.
	 */
	std::byte* StorageBegin() noexcept
	{
		const std::uintptr_t RawAddress = reinterpret_cast<std::uintptr_t>(Storage.data());
		const std::size_t Misalignment = static_cast<std::size_t>(RawAddress & (GuaranteedAlignmentBytes - 1U));
		const std::size_t AlignmentAdjustment = Misalignment == 0 ? 0 : GuaranteedAlignmentBytes - Misalignment;
		return Storage.data() + AlignmentAdjustment;
	}

	/**
	 * Motivation: Lets validation reject alignments the arena cannot guarantee.
	 * Responsibilities: Confirm the alignment is a positive power of two at most the guaranteed value.
	 */
	static bool IsSupportedAlignment(const std::size_t InAlignmentBytes) noexcept
	{
		const bool bIsPositive = InAlignmentBytes > 0;
		const bool bIsPowerOfTwo = (InAlignmentBytes & (InAlignmentBytes - 1U)) == 0;
		const bool bFitsGuarantee = InAlignmentBytes <= GuaranteedAlignmentBytes;
		return bIsPositive && bIsPowerOfTwo && bFitsGuarantee;
	}

	/**
	 * Motivation: Lets TryAllocate reject a request that cannot possibly fit.
	 * Responsibilities: Report whether a non-zero request still fits the unused capacity.
	 */
	bool FitsFreeCapacity(const std::size_t InSizeBytes) const noexcept
	{
		return InSizeBytes != 0 && InSizeBytes <= StorageCapacityBytes - UsedSizeBytes;
	}

	/**
	 * Motivation: Lets LocateOwnedAllocation reject a foreign block before touching markers.
	 * Responsibilities: Report whether a block address falls within the arena's aligned storage window.
	 */
	static bool IsBlockInRange(
		const std::uintptr_t InBlockAddress, const std::uintptr_t InStorageAddress, const std::uintptr_t InStorageEndAddress) noexcept
	{
		return InBlockAddress >= InStorageAddress && InBlockAddress < InStorageEndAddress;
	}

	/**
	 * Motivation: Lets the scanner inspect one allocation boundary without exposing bookkeeping.
	 * Responsibilities: Return the boundary bit packed at InOffset, BitsPerByte markers per byte.
	 */
	static bool ReadMarker(const std::array<std::uint8_t, MarkerStorageBytes>& InMarkers, const std::size_t InOffset) noexcept
	{
		// Hand-rolled bitset: one bit per usable byte, packed BitsPerByte to a std::uint8_t
		// -- byte index = InOffset / BitsPerByte, bit index = InOffset % BitsPerByte.
		const std::size_t MarkerByte = InOffset / BitsPerByte;
		const std::uint8_t MarkerMask = static_cast<std::uint8_t>(1U << (InOffset % BitsPerByte));
		return (InMarkers[MarkerByte] & MarkerMask) != 0;
	}

	/**
	 * Motivation: Lets Commit and Release change one boundary without disturbing neighbors.
	 * Responsibilities: Set or clear the boundary bit at InOffset while leaving the rest of the markers intact.
	 */
	static void WriteMarker(std::array<std::uint8_t, MarkerStorageBytes>& InMarkers, const std::size_t InOffset, const bool bInValue) noexcept
	{
		const std::size_t MarkerByte = InOffset / BitsPerByte;
		const std::uint8_t MarkerMask = static_cast<std::uint8_t>(1U << (InOffset % BitsPerByte));
		if (bInValue)
		{
			InMarkers[MarkerByte] = static_cast<std::uint8_t>(InMarkers[MarkerByte] | MarkerMask);
			return;
		}
		InMarkers[MarkerByte] = static_cast<std::uint8_t>(InMarkers[MarkerByte] & ~MarkerMask);
	}

	/** Motivation: Retains caller-owned capacity plus bounded space for the aligned usable start. */
	std::array<std::byte, RawStorageSizeBytes> Storage{};

	/** Motivation: Identifies each active block's first byte without consuming payload capacity. */
	std::array<std::uint8_t, MarkerStorageBytes> AllocationStartMarkers{};

	/** Motivation: Identifies each active block's last byte for exact-size validation. */
	std::array<std::uint8_t, MarkerStorageBytes> AllocationEndMarkers{};

	/** Motivation: Makes active payload usage observable without rescanning allocation markers. */
	std::size_t UsedSizeBytes{0};
};

} // namespace MicroWorld::Core
