#pragma once

#include <MicroWorld/Core/Containers/RawSlot.h>
#include <MicroWorld/Core/Memory/MemoryBlock.h>
#include <MicroWorld/Core/Memory/MemoryResource.h>
#include <MicroWorld/Core/Memory/MemoryResult.h>
#include <MicroWorld/Core/Memory/UniquePtr.h>

#include <type_traits>
#include <utility>

namespace MicroWorld::Core
{

/**
 * Motivation: Couples an explicit allocation result with the exclusive owner it created.
 * Responsibilities: Carry the result and one exclusive owner that is valid only when Result is Success.
 * Example:
 *   TUniquePointerResult<int> Result = MakeUnique<int>(Arena, 7);
 */
template<typename ValueType>
struct TUniquePointerResult
{
	/** Motivation: Distinguishes successful construction from the resource's exact failure. */
	EMemoryResult Result{EMemoryResult::OutOfMemory};

	/** Motivation: Owns the constructed value only when Result is Success. */
	TUniquePtr<ValueType> Pointer{};
};

/**
 * Motivation: Lets a caller construct one exclusively owned value in caller-selected storage.
 * Responsibilities: Allocate from InResource, construct the value with forwarded arguments, and return the typed
 *   outcome and exclusive owner on success or the exact resource failure otherwise.
 */
template<typename ValueType, typename... ConstructorArgumentTypes>
TUniquePointerResult<ValueType> MakeUnique(IMemoryResource& InResource, ConstructorArgumentTypes&&... Arguments) noexcept
{
	static_assert(!std::is_array<ValueType>::value, "MakeUnique constructs one non-array value.");
	static_assert(std::is_nothrow_constructible<ValueType, ConstructorArgumentTypes...>::value, "MakeUnique requires noexcept construction.");
	static_assert(std::is_nothrow_destructible<ValueType>::value, "MakeUnique requires noexcept destruction.");

	FMemoryBlock Allocation{};
	const EMemoryResult AllocationResult = InResource.TryAllocate(sizeof(ValueType), alignof(ValueType), Allocation);
	if (AllocationResult != EMemoryResult::Success)
	{
		TUniquePointerResult<ValueType> FailedResult{};
		FailedResult.Result = AllocationResult;
		return FailedResult;
	}

	ValueType* const Value = RawStorage::ConstructAt<ValueType>(Allocation.Address, std::forward<ConstructorArgumentTypes>(Arguments)...);

	TUniquePointerResult<ValueType> SuccessfulResult{};
	SuccessfulResult.Result = EMemoryResult::Success;
	SuccessfulResult.Pointer = TUniquePtr<ValueType>(Value, InResource, Allocation);
	return SuccessfulResult;
}

} // namespace MicroWorld::Core
