#pragma once

#include <MicroWorld/Memory/MemoryResource.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace MicroWorld
{

/**
 * Supplies reusable aligned storage with fixed caller-selected capacity.
 *
 * @tparam StorageCapacityBytes Number of caller-usable bytes retained by the arena.
 * @tparam GuaranteedAlignmentBytes Maximum power-of-two alignment guaranteed by the arena.
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
	/** Creates an empty resource whose storage and metadata are caller-owned. */
	TFixedArena() noexcept = default;

	/** Preserves resource identity used by every outstanding block. */
	TFixedArena(const TFixedArena&) = delete;

	/** Prevents assigning storage identity across resource boundaries. */
	TFixedArena& operator=(const TFixedArena&) = delete;

	/** Preserves addresses returned from this caller-owned arena. */
	TFixedArena(TFixedArena&&) = delete;

	/** Prevents moving storage behind outstanding block addresses. */
	TFixedArena& operator=(TFixedArena&&) = delete;

	/** Ends the resource lifetime without assuming ownership of constructed objects. */
	~TFixedArena() noexcept override = default;

	/** Allocates the first fitting aligned free range without fallback or compaction. */
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

	/** Releases only an exact active range belonging to this arena. */
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

	/** Reports the compile-time caller-usable capacity without marker storage. */
	std::size_t CapacityBytes() const noexcept override { return StorageCapacityBytes; }

	/** Reports the exact payload bytes retained by active allocations. */
	std::size_t UsedBytes() const noexcept override { return UsedSizeBytes; }

private:
	/** Rejects an unsupported alignment or a size that cannot fit the remaining capacity. */
	EMemoryResult ValidateAllocationRequest(const std::size_t InSizeBytes, const std::size_t InAlignmentBytes) const noexcept
	{
		if (!IsSupportedAlignment(InAlignmentBytes))
		{
			return EMemoryResult::UnsupportedAlignment;
		}
		if (InSizeBytes == 0 || InSizeBytes > StorageCapacityBytes - UsedSizeBytes)
		{
			return EMemoryResult::OutOfMemory;
		}
		return EMemoryResult::Success;
	}

	/** Scans for the first aligned run of SizeBytes free bytes and reports its start offset. */
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

	/** Marks the found range as one allocation and hands its address back to the caller. */
	void CommitAllocation(const std::size_t InStartOffset, const std::size_t InSizeBytes, FMemoryBlock& OutBlock) noexcept
	{
		const std::size_t AllocationEnd = InStartOffset + InSizeBytes - 1U;
		WriteMarker(AllocationStartMarkers, InStartOffset, true);
		WriteMarker(AllocationEndMarkers, AllocationEnd, true);
		UsedSizeBytes += InSizeBytes;
		OutBlock.Address = static_cast<void*>(StorageBegin() + InStartOffset);
		OutBlock.SizeBytes = InSizeBytes;
	}

	/** Maps a block back to its owned byte range, rejecting anything not exactly allocated here. */
	EMemoryResult LocateOwnedAllocation(const FMemoryBlock InBlock, std::size_t& OutStart, std::size_t& OutEnd) noexcept
	{
		if (InBlock.Address == nullptr || InBlock.SizeBytes == 0)
		{
			return EMemoryResult::InvalidBlock;
		}
		const std::uintptr_t StorageAddress = reinterpret_cast<std::uintptr_t>(StorageBegin());
		const std::uintptr_t StorageEndAddress = reinterpret_cast<std::uintptr_t>(StorageBegin() + StorageCapacityBytes);
		const std::uintptr_t BlockAddress = reinterpret_cast<std::uintptr_t>(InBlock.Address);
		if (BlockAddress < StorageAddress || BlockAddress >= StorageEndAddress)
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

	/** Confirms no other allocation boundary falls inside the block's byte range. */
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

	/** Clears the block's boundary markers and returns its bytes to the free pool. */
	void ReleaseMarkedRange(const std::size_t InAllocationStart, const std::size_t InAllocationEnd, const std::size_t InSizeBytes) noexcept
	{
		WriteMarker(AllocationStartMarkers, InAllocationStart, false);
		WriteMarker(AllocationEndMarkers, InAllocationEnd, false);
		UsedSizeBytes -= InSizeBytes;
	}

	/** Packs one allocation-boundary bit per usable byte into bounded metadata. */
	static constexpr std::size_t MarkerStorageBytes = (StorageCapacityBytes + 7U) / 8U;

	/** Reserves enough local bytes to expose StorageCapacityBytes after aligning the first usable byte. */
	static constexpr std::size_t RawStorageSizeBytes = StorageCapacityBytes + GuaranteedAlignmentBytes - 1U;

	/** Finds the stable aligned start without requiring padding around the virtual base. */
	std::byte* StorageBegin() noexcept
	{
		const std::uintptr_t RawAddress = reinterpret_cast<std::uintptr_t>(Storage.data());
		const std::size_t Misalignment = static_cast<std::size_t>(RawAddress & (GuaranteedAlignmentBytes - 1U));
		const std::size_t AlignmentAdjustment = Misalignment == 0 ? 0 : GuaranteedAlignmentBytes - Misalignment;
		return Storage.data() + AlignmentAdjustment;
	}

	/** Confirms the arena can guarantee the requested power-of-two alignment. */
	static bool IsSupportedAlignment(const std::size_t InAlignmentBytes) noexcept
	{
		return InAlignmentBytes > 0 && (InAlignmentBytes & (InAlignmentBytes - 1U)) == 0 && InAlignmentBytes <= GuaranteedAlignmentBytes;
	}

	/** Reads one boundary marker without exposing bookkeeping to callers. */
	static bool ReadMarker(const std::array<std::uint8_t, MarkerStorageBytes>& InMarkers, const std::size_t InOffset) noexcept
	{
		// Hand-rolled bitset: one bit per usable byte, packed 8 to a std::uint8_t
		// -- byte index = InOffset / 8, bit index = InOffset % 8.
		const std::size_t MarkerByte = InOffset / 8U;
		const std::uint8_t MarkerMask = static_cast<std::uint8_t>(1U << (InOffset % 8U));
		return (InMarkers[MarkerByte] & MarkerMask) != 0;
	}

	/** Changes one boundary marker while leaving unrelated allocations intact. */
	static void WriteMarker(std::array<std::uint8_t, MarkerStorageBytes>& InMarkers, const std::size_t InOffset, const bool bInValue) noexcept
	{
		const std::size_t MarkerByte = InOffset / 8U;
		const std::uint8_t MarkerMask = static_cast<std::uint8_t>(1U << (InOffset % 8U));
		if (bInValue)
		{
			InMarkers[MarkerByte] = static_cast<std::uint8_t>(InMarkers[MarkerByte] | MarkerMask);
			return;
		}
		InMarkers[MarkerByte] = static_cast<std::uint8_t>(InMarkers[MarkerByte] & ~MarkerMask);
	}

	/** Retains caller-owned capacity plus bounded space for the aligned usable start. */
	std::array<std::byte, RawStorageSizeBytes> Storage{};

	/** Identifies each active block's first byte without consuming payload capacity. */
	std::array<std::uint8_t, MarkerStorageBytes> AllocationStartMarkers{};

	/** Identifies each active block's last byte for exact-size validation. */
	std::array<std::uint8_t, MarkerStorageBytes> AllocationEndMarkers{};

	/** Makes active payload usage observable without rescanning allocation markers. */
	std::size_t UsedSizeBytes{0};
};

} // namespace MicroWorld
