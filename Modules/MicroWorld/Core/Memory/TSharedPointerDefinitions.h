#pragma once

#include <MicroWorld/Core/Containers/RawSlot.h>
#include <MicroWorld/Core/Memory/ESharedPointerResult.h>
#include <MicroWorld/Core/Memory/MemoryBlock.h>
#include <MicroWorld/Core/Memory/MemoryResource.h>
#include <MicroWorld/Core/Memory/SharedAllocationLayout.h>
#include <MicroWorld/Core/Memory/SharedControlBlock.h>
#include <MicroWorld/Core/Memory/SharedPointerMode.h>
#include <MicroWorld/Core/Memory/SharedPtr.h>
#include <MicroWorld/Core/Memory/TSharedPointerResult.h>
#include <MicroWorld/Core/Memory/WeakPtr.h>
#include <MicroWorld/Core/Memory/TWeakPointerResult.h>

#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>

namespace MicroWorld::Core
{

template<typename ValueType, ESharedPointerMode Mode>
TSharedPointerResult<ValueType, Mode> TSharedPtr<ValueType, Mode>::TryShare() const noexcept
{
	TSharedPointerResult<ValueType, Mode> ShareResult{};
	if (ControlBlock == nullptr || !ControlBlock->HasLiveStrongReference())
	{
		ShareResult.Result = ESharedPointerResult::Expired;
		return ShareResult;
	}

	if (ControlBlock->StrongReferenceCount == std::numeric_limits<FReferenceCount>::max())
	{
		ShareResult.Result = ESharedPointerResult::ReferenceCountOverflow;
		return ShareResult;
	}

	++ControlBlock->StrongReferenceCount;
	ShareResult.Result = ESharedPointerResult::Success;
	ShareResult.Pointer = TSharedPtr(ControlBlock);
	return ShareResult;
}

template<typename ValueType, ESharedPointerMode Mode>
TWeakPointerResult<ValueType, Mode> TSharedPtr<ValueType, Mode>::TryAcquireWeak() const noexcept
{
	TWeakPointerResult<ValueType, Mode> WeakResult{};
	if (ControlBlock == nullptr || !ControlBlock->HasLiveStrongReference())
	{
		WeakResult.Result = ESharedPointerResult::Expired;
		return WeakResult;
	}

	if (ControlBlock->WeakReferenceCount == std::numeric_limits<FReferenceCount>::max())
	{
		WeakResult.Result = ESharedPointerResult::ReferenceCountOverflow;
		return WeakResult;
	}

	++ControlBlock->WeakReferenceCount;
	WeakResult.Result = ESharedPointerResult::Success;
	WeakResult.Pointer = TWeakPtr<ValueType, Mode>(ControlBlock);
	return WeakResult;
}

template<typename ValueType, ESharedPointerMode Mode>
TWeakPointerResult<ValueType, Mode> TWeakPtr<ValueType, Mode>::TryObserve() const noexcept
{
	TWeakPointerResult<ValueType, Mode> ObserveResult{};
	if (ControlBlock == nullptr || !ControlBlock->HasLiveValue())
	{
		ObserveResult.Result = ESharedPointerResult::Expired;
		return ObserveResult;
	}

	if (ControlBlock->WeakReferenceCount == std::numeric_limits<FReferenceCount>::max())
	{
		ObserveResult.Result = ESharedPointerResult::ReferenceCountOverflow;
		return ObserveResult;
	}

	++ControlBlock->WeakReferenceCount;
	ObserveResult.Result = ESharedPointerResult::Success;
	ObserveResult.Pointer = TWeakPtr(ControlBlock);
	return ObserveResult;
}

template<typename ValueType, ESharedPointerMode Mode>
TSharedPointerResult<ValueType, Mode> TWeakPtr<ValueType, Mode>::TryAcquireStrong() const noexcept
{
	TSharedPointerResult<ValueType, Mode> StrongResult{};
	if (ControlBlock == nullptr || !ControlBlock->HasLiveValue())
	{
		StrongResult.Result = ESharedPointerResult::Expired;
		return StrongResult;
	}

	if (ControlBlock->StrongReferenceCount == std::numeric_limits<FReferenceCount>::max())
	{
		StrongResult.Result = ESharedPointerResult::ReferenceCountOverflow;
		return StrongResult;
	}

	++ControlBlock->StrongReferenceCount;
	StrongResult.Result = ESharedPointerResult::Success;
	StrongResult.Pointer = TSharedPtr<ValueType, Mode>(ControlBlock);
	return StrongResult;
}

template<typename ValueType, ESharedPointerMode Mode>
TSharedPointerResult<ValueType, Mode> TWeakPtr<ValueType, Mode>::Pin() const noexcept
{
	return TryAcquireStrong();
}

/**
 * Motivation: Lets a caller construct one shared value and its control block in a single allocation.
 * Responsibilities: Allocate combined storage from InResource, construct the value with forwarded arguments, and
 *   return the typed outcome and first strong owner on success or the exact resource failure otherwise.
 */
template<typename ValueType, ESharedPointerMode Mode, typename... ConstructorArgumentTypes>
TSharedPointerResult<ValueType, Mode> MakeShared(IMemoryResource& InResource, ConstructorArgumentTypes&&... Arguments) noexcept
{
	static_assert(Mode == ESharedPointerMode::SingleThreaded, "Only single-threaded shared pointers are available.");
	static_assert(!std::is_array<ValueType>::value, "MakeShared constructs one non-array value.");
	static_assert(std::is_nothrow_constructible<ValueType, ConstructorArgumentTypes...>::value, "MakeShared requires noexcept construction.");
	static_assert(std::is_nothrow_destructible<ValueType>::value, "MakeShared requires noexcept destruction.");

	using FLayout = TSharedAllocationLayout<ValueType>;

	FMemoryBlock Allocation{};
	const EMemoryResult AllocationResult = InResource.TryAllocate(FLayout::CombinedSizeBytes, FLayout::CombinedAlignmentBytes, Allocation);
	if (AllocationResult != EMemoryResult::Success)
	{
		TSharedPointerResult<ValueType, Mode> FailedResult{};
		FailedResult.Result = ToSharedPointerResult(AllocationResult);
		return FailedResult;
	}

	TSharedControlBlock<ValueType>* const ControlBlock =
		ConstructSharedBlock<ValueType>(InResource, Allocation, FLayout::ValueOffsetBytes, std::forward<ConstructorArgumentTypes>(Arguments)...);

	TSharedPointerResult<ValueType, Mode> SuccessfulResult{};
	SuccessfulResult.Result = ESharedPointerResult::Success;
	SuccessfulResult.Pointer = TSharedPtr<ValueType, Mode>(ControlBlock);
	return SuccessfulResult;
}

} // namespace MicroWorld::Core
