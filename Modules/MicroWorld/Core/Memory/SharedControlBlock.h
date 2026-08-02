#pragma once

#include <MicroWorld/Core/Containers/RawSlot.h>
#include <MicroWorld/Core/Memory/MemoryBlock.h>
#include <MicroWorld/Core/Memory/MemoryResource.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace MicroWorld::Core
{

/**
 * Motivation: Holds the single-threaded lifetime state shared by strong and weak handles until both counts reach zero.
 * Responsibilities: Track the strong and weak counts, the live value, and the resource that must reclaim the allocation.
 * Example:
 *   auto Result = MakeShared<int>(Arena, 42);
 *   TSharedPtr<int> Owner = std::move(Result.Pointer);
 */
template<typename ValueType>
struct TSharedControlBlock final
{
	/** Motivation: Keeps handle cost bounded while exposing an explicit counter overflow result. */
	using FReferenceCount = std::uint16_t;

	/** Motivation: Identifies the resource that must receive Allocation. */
	IMemoryResource* Resource{nullptr};

	/** Motivation: Preserves the exact combined object/control-block allocation. */
	FMemoryBlock Allocation{};

	/** Motivation: Identifies the live value and becomes null before weak observers can report expiry. */
	ValueType* Value{nullptr};

	/** Motivation: Counts live strong handles that keep Value constructed. */
	FReferenceCount StrongReferenceCount{1};

	/** Motivation: Counts live weak handles that keep this control block allocated. */
	FReferenceCount WeakReferenceCount{0};

	/** Motivation: Defers weak-side reclamation while the final strong Reset() runs the value destructor. */
	bool bValueDestructionInProgress{false};

	/**
	 * Motivation: Lets a strong handle prove at least one strong owner keeps this block alive.
	 * Responsibilities: Report whether the strong count is non-zero.
	 */
	bool HasLiveStrongReference() const noexcept { return StrongReferenceCount != 0; }

	/**
	 * Motivation: Lets a weak handle prove the value is still constructed and observable.
	 * Responsibilities: Report whether the strong count is non-zero and the value pointer is set.
	 */
	bool HasLiveValue() const noexcept { return StrongReferenceCount != 0 && Value != nullptr; }
};

/**
 * Motivation: Lets the last weak release return an expired control block to its exact resource.
 * Responsibilities: Destroy the control block and return its allocation to the resource that produced it.
 */
template<typename ValueType>
void DestroySharedControlBlock(TSharedControlBlock<ValueType>* const InControlBlock) noexcept
{
	IMemoryResource* const Resource = InControlBlock->Resource;
	const FMemoryBlock Allocation = InControlBlock->Allocation;
	RawStorage::DestroyAt(InControlBlock);
	static_cast<void>(Resource->Deallocate(Allocation));
}

/**
 * Motivation: Lets MakeShared fill one combined allocation with its control block and value.
 * Responsibilities: Construct the control block and value at their aligned offsets and link the block to its resource.
 */
template<typename ValueType, typename... ConstructorArgumentTypes>
TSharedControlBlock<ValueType>* ConstructSharedBlock(
	IMemoryResource& InResource,
	const FMemoryBlock InAllocation,
	const std::size_t InValueOffsetBytes,
	ConstructorArgumentTypes&&... Arguments) noexcept
{
	using FControlBlock = TSharedControlBlock<ValueType>;
	std::byte* const AllocationBytes = static_cast<std::byte*>(InAllocation.Address);
	FControlBlock* const ControlBlock = RawStorage::ConstructAt<FControlBlock>(AllocationBytes);
	ValueType* const Value =
		RawStorage::ConstructAt<ValueType>(AllocationBytes + InValueOffsetBytes, std::forward<ConstructorArgumentTypes>(Arguments)...);
	ControlBlock->Resource = &InResource;
	ControlBlock->Allocation = InAllocation;
	ControlBlock->Value = Value;
	return ControlBlock;
}

} // namespace MicroWorld::Core
