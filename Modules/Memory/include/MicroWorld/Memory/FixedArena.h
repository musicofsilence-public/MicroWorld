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
 * @tparam Bytes Number of caller-usable bytes retained by the arena.
 * @tparam Alignment Maximum power-of-two alignment guaranteed by the arena.
 */
template<std::size_t Bytes, std::size_t Alignment>
class TFixedArena final : public IMemoryResource
{
	static_assert(Bytes > 0, "A fixed arena must expose at least one byte.");
	static_assert(Alignment > 0 && (Alignment & (Alignment - 1U)) == 0, "A fixed arena alignment must be a non-zero power of two.");
	static_assert(Bytes <= std::numeric_limits<std::size_t>::max() - (Alignment - 1U), "A fixed arena's aligned backing storage must fit in size_t.");

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
	EMemoryResult TryAllocate(const std::size_t SizeBytes, const std::size_t AlignmentBytes, FMemoryBlock& OutBlock) noexcept override
	{
		OutBlock = {};

		if (!IsSupportedAlignment(AlignmentBytes))
		{
			return EMemoryResult::UnsupportedAlignment;
		}
		if (SizeBytes == 0 || SizeBytes > Bytes - UsedSizeBytes)
		{
			return EMemoryResult::OutOfMemory;
		}

		bool bInsideAllocation = false;
		std::size_t FreeRangeStart = 0;
		std::size_t FreeRangeSize = 0;

		for (std::size_t Offset = 0; Offset < Bytes; ++Offset)
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
			else if ((Offset & (AlignmentBytes - 1U)) == 0)
			{
				FreeRangeStart = Offset;
				FreeRangeSize = 1;
			}

			if (FreeRangeSize == SizeBytes)
			{
				const std::size_t AllocationEnd = FreeRangeStart + SizeBytes - 1U;
				WriteMarker(AllocationStartMarkers, FreeRangeStart, true);
				WriteMarker(AllocationEndMarkers, AllocationEnd, true);
				UsedSizeBytes += SizeBytes;

				OutBlock.Address = static_cast<void*>(StorageBegin() + FreeRangeStart);
				OutBlock.SizeBytes = SizeBytes;
				return EMemoryResult::Success;
			}

			if (ReadMarker(AllocationEndMarkers, Offset))
			{
				bInsideAllocation = false;
			}
		}

		return EMemoryResult::OutOfMemory;
	}

	/** Releases only an exact active range belonging to this arena. */
	EMemoryResult Deallocate(const FMemoryBlock Block) noexcept override
	{
		if (Block.Address == nullptr || Block.SizeBytes == 0)
		{
			return EMemoryResult::InvalidBlock;
		}

		const std::uintptr_t StorageAddress = reinterpret_cast<std::uintptr_t>(StorageBegin());
		const std::uintptr_t StorageEndAddress = reinterpret_cast<std::uintptr_t>(StorageBegin() + Bytes);
		const std::uintptr_t BlockAddress = reinterpret_cast<std::uintptr_t>(Block.Address);

		if (BlockAddress < StorageAddress || BlockAddress >= StorageEndAddress)
		{
			return EMemoryResult::InvalidBlock;
		}

		const std::size_t AllocationStart = static_cast<std::size_t>(BlockAddress - StorageAddress);
		if (Block.SizeBytes > Bytes - AllocationStart)
		{
			return EMemoryResult::InvalidBlock;
		}

		const std::size_t AllocationEnd = AllocationStart + Block.SizeBytes - 1U;
		if (!ReadMarker(AllocationStartMarkers, AllocationStart) || !ReadMarker(AllocationEndMarkers, AllocationEnd))
		{
			return EMemoryResult::InvalidBlock;
		}

		for (std::size_t Offset = AllocationStart; Offset <= AllocationEnd; ++Offset)
		{
			const bool bUnexpectedStart = Offset != AllocationStart && ReadMarker(AllocationStartMarkers, Offset);
			const bool bUnexpectedEnd = Offset != AllocationEnd && ReadMarker(AllocationEndMarkers, Offset);
			if (bUnexpectedStart || bUnexpectedEnd)
			{
				return EMemoryResult::InvalidBlock;
			}
		}

		if (UsedSizeBytes < Block.SizeBytes)
		{
			return EMemoryResult::InvalidBlock;
		}

		WriteMarker(AllocationStartMarkers, AllocationStart, false);
		WriteMarker(AllocationEndMarkers, AllocationEnd, false);
		UsedSizeBytes -= Block.SizeBytes;
		return EMemoryResult::Success;
	}

	/** Reports the compile-time caller-usable capacity without marker storage. */
	std::size_t CapacityBytes() const noexcept override { return Bytes; }

	/** Reports the exact payload bytes retained by active allocations. */
	std::size_t UsedBytes() const noexcept override { return UsedSizeBytes; }

private:
	/** Packs one allocation-boundary bit per usable byte into bounded metadata. */
	static constexpr std::size_t MarkerStorageBytes = (Bytes + 7U) / 8U;

	/** Reserves enough local bytes to expose Bytes after aligning the first usable byte. */
	static constexpr std::size_t RawStorageSizeBytes = Bytes + Alignment - 1U;

	/** Finds the stable aligned start without requiring padding around the virtual base. */
	std::byte* StorageBegin() noexcept
	{
		const std::uintptr_t RawAddress = reinterpret_cast<std::uintptr_t>(Storage.data());
		const std::size_t Misalignment = static_cast<std::size_t>(RawAddress & (Alignment - 1U));
		const std::size_t AlignmentAdjustment = Misalignment == 0 ? 0 : Alignment - Misalignment;
		return Storage.data() + AlignmentAdjustment;
	}

	/** Confirms the arena can guarantee the requested power-of-two alignment. */
	static bool IsSupportedAlignment(const std::size_t AlignmentBytes) noexcept
	{
		return AlignmentBytes > 0 && (AlignmentBytes & (AlignmentBytes - 1U)) == 0 && AlignmentBytes <= Alignment;
	}

	/** Reads one boundary marker without exposing bookkeeping to callers. */
	static bool ReadMarker(const std::array<std::uint8_t, MarkerStorageBytes>& Markers, const std::size_t Offset) noexcept
	{
		const std::size_t MarkerByte = Offset / 8U;
		const std::uint8_t MarkerMask = static_cast<std::uint8_t>(1U << (Offset % 8U));
		return (Markers[MarkerByte] & MarkerMask) != 0;
	}

	/** Changes one boundary marker while leaving unrelated allocations intact. */
	static void WriteMarker(std::array<std::uint8_t, MarkerStorageBytes>& Markers, const std::size_t Offset, const bool bValue) noexcept
	{
		const std::size_t MarkerByte = Offset / 8U;
		const std::uint8_t MarkerMask = static_cast<std::uint8_t>(1U << (Offset % 8U));
		if (bValue)
		{
			Markers[MarkerByte] = static_cast<std::uint8_t>(Markers[MarkerByte] | MarkerMask);
			return;
		}
		Markers[MarkerByte] = static_cast<std::uint8_t>(Markers[MarkerByte] & ~MarkerMask);
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
