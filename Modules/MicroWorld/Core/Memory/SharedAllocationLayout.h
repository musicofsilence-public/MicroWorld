#pragma once

#include <MicroWorld/Core/Memory/MemoryBlock.h>
#include <MicroWorld/Core/Memory/SharedControlBlock.h>

#include <cstddef>
#include <limits>

namespace MicroWorld::Core
{

/**
 * Motivation: Places the value immediately after its control block in one shared allocation.
 * Responsibilities: Compute the combined alignment, the value offset that keeps it aligned, and the total size.
 * Example:
 *   using FLayout = TSharedAllocationLayout<int>;
 *   static constexpr std::size_t Size = FLayout::CombinedSizeBytes;
 */
template<typename ValueType>
struct TSharedAllocationLayout final
{
	using FControlBlock = TSharedControlBlock<ValueType>;
	static constexpr std::size_t CombinedAlignmentBytes = alignof(FControlBlock) > alignof(ValueType) ? alignof(FControlBlock) : alignof(ValueType);
	static_assert(
		sizeof(FControlBlock) <= std::numeric_limits<std::size_t>::max() - (alignof(ValueType) - 1U), "Shared layout padding must fit in size_t.");
	static constexpr std::size_t ValueOffsetBytes = AlignSizeUp(sizeof(FControlBlock), alignof(ValueType));
	static_assert(ValueOffsetBytes <= std::numeric_limits<std::size_t>::max() - sizeof(ValueType), "Shared allocation size must fit in size_t.");
	static constexpr std::size_t CombinedSizeBytes = ValueOffsetBytes + sizeof(ValueType);
};

} // namespace MicroWorld::Core
