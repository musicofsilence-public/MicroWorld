#pragma once

#include <MicroWorld/Core/Containers/RawSlot.h>
#include <MicroWorld/Core/Memory/MemoryBlock.h>
#include <MicroWorld/Core/Memory/MemoryResource.h>
#include <MicroWorld/Core/Memory/MemoryResult.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace MicroWorld::Core
{

/**
 * Motivation: Names the only reference-counting execution contract the implementation currently supports.
 * Responsibilities: Distinguish the single-threaded contract so callers know all ownership operations share one thread.
 * Example:
 *   TSharedPtr<int, ESharedPointerMode::SingleThreaded> Owner;
 */
enum class ESharedPointerMode : std::uint8_t
{
	/** Motivation: Requires all ownership operations to execute from one caller-controlled thread. */
	SingleThreaded,
};

/**
 * Motivation: Gives every fallible shared or weak ownership operation one result vocabulary without exceptions.
 * Responsibilities: Distinguish acquisition success from capacity, alignment, expiry, overflow, and resource failure.
 * Example:
 *   if (Result == ESharedPointerResult::Expired) { Recover(); }
 */
enum class ESharedPointerResult : std::uint8_t
{
	/** Motivation: Confirms that the requested owner or observer was acquired. */
	Success,

	/** Motivation: Reports that the selected resource could not hold the combined allocation. */
	OutOfMemory,

	/** Motivation: Reports that the selected resource cannot satisfy the combined alignment. */
	UnsupportedAlignment,

	/** Motivation: Rejects acquisition after the observed value's last strong owner released it. */
	Expired,

	/** Motivation: Rejects an increment that would make a reference counter wrap. */
	ReferenceCountOverflow,

	/** Motivation: Preserves an unexpected resource failure without pretending it was exhaustion. */
	ResourceFailure,
};

template<typename ValueType, ESharedPointerMode Mode = ESharedPointerMode::SingleThreaded>
class TSharedPtr;

template<typename ValueType, ESharedPointerMode Mode = ESharedPointerMode::SingleThreaded>
class TWeakPtr;

template<typename ValueType, ESharedPointerMode Mode = ESharedPointerMode::SingleThreaded>
struct TSharedPointerResult;

template<typename ValueType, ESharedPointerMode Mode = ESharedPointerMode::SingleThreaded>
struct TWeakPointerResult;

/**
 * Motivation: Lets a caller construct one shared value and its control block in a single allocation.
 * Responsibilities: Allocate combined storage from InResource, construct the value with forwarded arguments, and
 *   return the typed outcome and first strong owner on success or the exact resource failure otherwise.
 */
template<typename ValueType, ESharedPointerMode Mode = ESharedPointerMode::SingleThreaded, typename... ConstructorArgumentTypes>
TSharedPointerResult<ValueType, Mode> MakeShared(IMemoryResource& InResource, ConstructorArgumentTypes&&... Arguments) noexcept;

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
 * Motivation: Lets the shared-pointer internals translate a memory-resource failure into their own result domain.
 * Responsibilities: Map each EMemoryResult to the matching ESharedPointerResult, defaulting unexpected results to ResourceFailure.
 */
inline ESharedPointerResult ToSharedPointerResult(const EMemoryResult InResult) noexcept
{
	switch (InResult)
	{
		case EMemoryResult::Success:
			return ESharedPointerResult::Success;
		case EMemoryResult::OutOfMemory:
			return ESharedPointerResult::OutOfMemory;
		case EMemoryResult::UnsupportedAlignment:
			return ESharedPointerResult::UnsupportedAlignment;
		case EMemoryResult::InvalidBlock:
		default:
			return ESharedPointerResult::ResourceFailure;
	}
}

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

/**
 * Motivation: Lets an owner share one non-managed value through explicit single-threaded reference counting.
 * Responsibilities: Hold one strong count, keep move-only so a copy cannot overflow the counter at its boundary,
 *   and destroy the value exactly once when the last strong owner releases it.
 * Example:
 *   auto Result = MakeShared<int>(Arena, 7);
 *   TSharedPtr<int> Owner = std::move(Result.Pointer);
 *   TSharedPointerResult<int> Shared = Owner.TryShare();
 */
template<typename ValueType, ESharedPointerMode Mode>
class TSharedPtr final
{
	static_assert(Mode == ESharedPointerMode::SingleThreaded, "Only single-threaded shared pointers are available.");
	static_assert(!std::is_array<ValueType>::value, "TSharedPtr owns one non-array value.");
	static_assert(std::is_nothrow_destructible<ValueType>::value, "TSharedPtr requires noexcept destruction.");

private:
	/** Motivation: Names the type-specific control block shared with weak observers. */
	using FControlBlock = TSharedControlBlock<ValueType>;

	/** Motivation: Names the bounded counter used by the selected control-block layout. */
	using FReferenceCount = typename FControlBlock::FReferenceCount;

public:
	/**
	 * Motivation: Lets an owner declare an empty shared pointer before any resource is chosen.
	 * Responsibilities: Produce an owner holding no strong count and touching no resource.
	 */
	TSharedPtr() noexcept = default;

	/**
	 * Motivation: Prevents an invisible reference-count overflow during copy construction.
	 * Responsibilities: Reject copy construction so acquiring a strong owner stays an explicit, fallible step.
	 */
	TSharedPtr(const TSharedPtr&) = delete;

	/**
	 * Motivation: Prevents an invisible reference-count overflow during copy assignment.
	 * Responsibilities: Reject copy assignment so acquiring a strong owner stays an explicit, fallible step.
	 */
	TSharedPtr& operator=(const TSharedPtr&) = delete;

	/**
	 * Motivation: Lets an owner transfer one already-counted strong handle without touching the counter.
	 * Responsibilities: Move the control block pointer and leave the source empty.
	 */
	TSharedPtr(TSharedPtr&& Other) noexcept : ControlBlock(Other.ControlBlock) { Other.ControlBlock = nullptr; }

	/**
	 * Motivation: Lets an owner replace its strong handle with another already-counted one.
	 * Responsibilities: Release any current owner, then adopt the source handle and leave the source empty.
	 */
	TSharedPtr& operator=(TSharedPtr&& Other) noexcept
	{
		if (this == &Other)
		{
			return *this;
		}

		Reset();
		ControlBlock = Other.ControlBlock;
		Other.ControlBlock = nullptr;
		return *this;
	}

	/**
	 * Motivation: Ensures no strong handle outlives the count it contributes.
	 * Responsibilities: Release this strong handle and complete destruction when it is the last.
	 */
	~TSharedPtr() noexcept { Reset(); }

	/**
	 * Motivation: Lets a caller observe the live value without changing the counts.
	 * Responsibilities: Return the value pointer, or null when no live value is held.
	 */
	ValueType* Get() const noexcept { return ControlBlock == nullptr ? nullptr : ControlBlock->Value; }

	/**
	 * Motivation: Lets a caller guard dereference behind one cheap check.
	 * Responsibilities: Report whether this handle currently keeps a live value constructed.
	 */
	bool IsValid() const noexcept { return Get() != nullptr; }

	/**
	 * Motivation: Lets a caller acquire another strong owner at the documented counter boundary.
	 * Responsibilities: Increment the strong count or report the exact expiry or overflow failure.
	 */
	TSharedPointerResult<ValueType, Mode> TryShare() const noexcept;

	/**
	 * Motivation: Lets a caller acquire a weak observer of this value.
	 * Responsibilities: Increment the weak count or report the exact expiry or overflow failure.
	 */
	TWeakPointerResult<ValueType, Mode> TryAcquireWeak() const noexcept;

	/**
	 * Motivation: Lets an owner release its strong handle deterministically.
	 * Responsibilities: Decrement the strong count, destroy the value at the final strong release, and reclaim the block when unreferenced.
	 */
	void Reset() noexcept
	{
		if (ControlBlock == nullptr)
		{
			return;
		}
		FControlBlock* const ReleasedControlBlock = ControlBlock;
		ControlBlock = nullptr;
		--ReleasedControlBlock->StrongReferenceCount;
		if (ReleasedControlBlock->StrongReferenceCount != 0)
		{
			return;
		}
		DestroyValueInPlace(ReleasedControlBlock);
		ReclaimControlBlockIfUnreferenced(ReleasedControlBlock);
	}

	/**
	 * Motivation: Lets diagnostics and boundary tests inspect the strong count without a backdoor.
	 * Responsibilities: Report the current strong count, or zero when empty.
	 */
	std::size_t StrongReferenceCount() const noexcept
	{
		return ControlBlock == nullptr ? 0U : static_cast<std::size_t>(ControlBlock->StrongReferenceCount);
	}

	/**
	 * Motivation: Lets diagnostics and boundary tests inspect the weak count without a backdoor.
	 * Responsibilities: Report the current weak count, or zero when empty.
	 */
	std::size_t WeakReferenceCount() const noexcept
	{
		return ControlBlock == nullptr ? 0U : static_cast<std::size_t>(ControlBlock->WeakReferenceCount);
	}

	/**
	 * Motivation: Lets a caller test against the supported counter boundary without a mutation backdoor.
	 * Responsibilities: Report the maximum count the selected counter width can hold.
	 */
	static constexpr std::size_t MaximumReferenceCount() noexcept { return static_cast<std::size_t>(std::numeric_limits<FReferenceCount>::max()); }

private:
	/**
	 * Motivation: Lets a factory or fallible operation adopt one strong count already acquired.
	 * Responsibilities: Bind the control block pointer without incrementing the count.
	 */
	explicit TSharedPtr(FControlBlock* const InControlBlock) noexcept : ControlBlock(InControlBlock) {}

	/** Motivation: Allows matching weak observers to create an already-counted strong handle. */
	friend class TWeakPtr<ValueType, Mode>;

	/**
	 * Motivation: Lets the factory create the first strong owner without exposing raw adoption.
	 * Responsibilities: Grant the factory access to the private adoption constructor.
	 */
	template<typename FactoryValueType, ESharedPointerMode PointerMode, typename... FactoryConstructorArgumentTypes>
	friend TSharedPointerResult<FactoryValueType, PointerMode> MakeShared(IMemoryResource&, FactoryConstructorArgumentTypes&&...) noexcept;

	/**
	 * Motivation: Destroys the owned value while blocking weak-side reclamation mid-teardown.
	 * Responsibilities: Run the value destructor in place and set the destruction-in-progress flag for the duration.
	 */
	static void DestroyValueInPlace(FControlBlock* const InReleasedControlBlock) noexcept
	{
		ValueType* const Value = InReleasedControlBlock->Value;
		InReleasedControlBlock->Value = nullptr;
		InReleasedControlBlock->bValueDestructionInProgress = true;
		RawStorage::DestroyAt(Value);
		InReleasedControlBlock->bValueDestructionInProgress = false;
	}

	/**
	 * Motivation: Reclaims the control block exactly when no handle can still observe it.
	 * Responsibilities: Destroy and return the block to its resource once both strong and weak counts are zero.
	 */
	static void ReclaimControlBlockIfUnreferenced(FControlBlock* const InReleasedControlBlock) noexcept
	{
		if (InReleasedControlBlock->WeakReferenceCount == 0)
		{
			DestroySharedControlBlock(InReleasedControlBlock);
		}
	}

	/** Motivation: Retains the allocation while this handle contributes one strong count. */
	FControlBlock* ControlBlock{nullptr};
};

/**
 * Motivation: Lets a caller observe one shared value without extending its construction lifetime.
 * Responsibilities: Hold one weak count, stay move-only so duplicating a weak count stays explicit and fallible,
 *   and never expose an expired value through Pin or TryAcquireStrong.
 * Example:
 *   TWeakPointerResult<int> Weak = Owner.TryAcquireWeak();
 *   if (!Weak.Pointer.IsExpired()) { TSharedPointerResult<int> Strong = Weak.Pointer.Pin(); }
 */
template<typename ValueType, ESharedPointerMode Mode>
class TWeakPtr final
{
	static_assert(Mode == ESharedPointerMode::SingleThreaded, "Only single-threaded weak pointers are available.");

private:
	/** Motivation: Names the type-specific control block retained by weak observers. */
	using FControlBlock = TSharedControlBlock<ValueType>;

	/** Motivation: Names the bounded counter used by the selected control-block layout. */
	using FReferenceCount = typename FControlBlock::FReferenceCount;

public:
	/**
	 * Motivation: Lets an owner declare an empty weak pointer before any value is observed.
	 * Responsibilities: Produce an observer holding no weak count and touching no resource.
	 */
	TWeakPtr() noexcept = default;

	/**
	 * Motivation: Prevents an invisible reference-count overflow during copy construction.
	 * Responsibilities: Reject copy construction so acquiring a weak observer stays an explicit, fallible step.
	 */
	TWeakPtr(const TWeakPtr&) = delete;

	/**
	 * Motivation: Prevents an invisible reference-count overflow during copy assignment.
	 * Responsibilities: Reject copy assignment so acquiring a weak observer stays an explicit, fallible step.
	 */
	TWeakPtr& operator=(const TWeakPtr&) = delete;

	/**
	 * Motivation: Lets an owner transfer one already-counted weak handle without touching the counter.
	 * Responsibilities: Move the control block pointer and leave the source empty.
	 */
	TWeakPtr(TWeakPtr&& Other) noexcept : ControlBlock(Other.ControlBlock) { Other.ControlBlock = nullptr; }

	/**
	 * Motivation: Lets an owner replace its weak handle with another already-counted one.
	 * Responsibilities: Release any current observer, then adopt the source handle and leave the source empty.
	 */
	TWeakPtr& operator=(TWeakPtr&& Other) noexcept
	{
		if (this == &Other)
		{
			return *this;
		}

		Reset();
		ControlBlock = Other.ControlBlock;
		Other.ControlBlock = nullptr;
		return *this;
	}

	/**
	 * Motivation: Ensures no weak handle outlives the count it contributes.
	 * Responsibilities: Release this weak handle and return an expired final block to its resource when necessary.
	 */
	~TWeakPtr() noexcept { Reset(); }

	/**
	 * Motivation: Lets a caller decide whether a strong acquisition can still succeed.
	 * Responsibilities: Report expiry without dereferencing the value storage.
	 */
	bool IsExpired() const noexcept { return ControlBlock == nullptr || ControlBlock->StrongReferenceCount == 0; }

	/**
	 * Motivation: Lets a caller acquire another weak observer at the documented counter boundary.
	 * Responsibilities: Increment the weak count or report the exact expiry or overflow failure.
	 */
	TWeakPointerResult<ValueType, Mode> TryObserve() const noexcept;

	/**
	 * Motivation: Lets a caller promote an observer to a strong owner only while the value is live.
	 * Responsibilities: Increment the strong count or report the exact expiry or overflow failure.
	 */
	TSharedPointerResult<ValueType, Mode> TryAcquireStrong() const noexcept;

	/**
	 * Motivation: Gives UE-familiar naming for the same typed, fallible strong acquisition.
	 * Responsibilities: Delegate to TryAcquireStrong without changing semantics.
	 */
	TSharedPointerResult<ValueType, Mode> Pin() const noexcept;

	/**
	 * Motivation: Lets an observer release its weak handle deterministically.
	 * Responsibilities: Decrement the weak count and deallocate the block after expiry when this is the final handle.
	 */
	void Reset() noexcept
	{
		if (ControlBlock == nullptr)
		{
			return;
		}

		FControlBlock* const ReleasedControlBlock = ControlBlock;
		ControlBlock = nullptr;
		--ReleasedControlBlock->WeakReferenceCount;

		if (ReleasedControlBlock->WeakReferenceCount == 0 && ReleasedControlBlock->StrongReferenceCount == 0
			&& !ReleasedControlBlock->bValueDestructionInProgress)
		{
			DestroySharedControlBlock(ReleasedControlBlock);
		}
	}

	/**
	 * Motivation: Lets diagnostics and boundary tests inspect the strong count without a backdoor.
	 * Responsibilities: Report the current strong count, or zero when empty.
	 */
	std::size_t StrongReferenceCount() const noexcept
	{
		return ControlBlock == nullptr ? 0U : static_cast<std::size_t>(ControlBlock->StrongReferenceCount);
	}

	/**
	 * Motivation: Lets diagnostics and boundary tests inspect the weak count without a backdoor.
	 * Responsibilities: Report the current weak count, or zero when empty.
	 */
	std::size_t WeakReferenceCount() const noexcept
	{
		return ControlBlock == nullptr ? 0U : static_cast<std::size_t>(ControlBlock->WeakReferenceCount);
	}

	/**
	 * Motivation: Lets a caller test against the supported counter boundary without a mutation backdoor.
	 * Responsibilities: Report the maximum count the selected counter width can hold.
	 */
	static constexpr std::size_t MaximumReferenceCount() noexcept { return static_cast<std::size_t>(std::numeric_limits<FReferenceCount>::max()); }

private:
	/**
	 * Motivation: Lets a fallible operation adopt one weak count already acquired.
	 * Responsibilities: Bind the control block pointer without incrementing the count.
	 */
	explicit TWeakPtr(FControlBlock* const InControlBlock) noexcept : ControlBlock(InControlBlock) {}

	/** Motivation: Allows strong owners to create an already-counted weak handle. */
	friend class TSharedPtr<ValueType, Mode>;

	/** Motivation: Retains an expired control block until this handle releases its weak count. */
	FControlBlock* ControlBlock{nullptr};
};

/**
 * Motivation: Couples a shared-pointer operation outcome with the strong owner it acquired.
 * Responsibilities: Carry the result and one strong owner that is valid only when Result is Success.
 * Example:
 *   TSharedPointerResult<int> Shared = Owner.TryShare();
 */
template<typename ValueType, ESharedPointerMode Mode>
struct TSharedPointerResult
{
	/** Motivation: Distinguishes acquisition success from allocation, expiry, and overflow. */
	ESharedPointerResult Result{ESharedPointerResult::OutOfMemory};

	/** Motivation: Owns one strong count only when Result is Success. */
	TSharedPtr<ValueType, Mode> Pointer{};
};

/**
 * Motivation: Couples a weak-pointer operation outcome with the observer it acquired.
 * Responsibilities: Carry the result and one weak observer that is valid only when Result is Success.
 * Example:
 *   TWeakPointerResult<int> Weak = Owner.TryAcquireWeak();
 */
template<typename ValueType, ESharedPointerMode Mode>
struct TWeakPointerResult
{
	/** Motivation: Distinguishes acquisition success from expiry and counter overflow. */
	ESharedPointerResult Result{ESharedPointerResult::Expired};

	/** Motivation: Owns one weak count only when Result is Success. */
	TWeakPtr<ValueType, Mode> Pointer{};
};

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
