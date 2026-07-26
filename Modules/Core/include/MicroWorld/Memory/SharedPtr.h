#pragma once

#include <MicroWorld/Containers/RawSlot.h>
#include <MicroWorld/Memory/MemoryResource.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace MicroWorld
{

/** Identifies the only reference-counting execution contract currently supported. */
enum class ESharedPointerMode : std::uint8_t
{
	/** Requires all ownership operations to execute from one caller-controlled thread. */
	SingleThreaded,
};

/** Reports every fallible shared/weak ownership operation without exceptions. */
enum class ESharedPointerResult : std::uint8_t
{
	/** Confirms that the requested owner or observer was acquired. */
	Success,

	/** Reports that the selected resource could not hold the combined allocation. */
	OutOfMemory,

	/** Reports that the selected resource cannot satisfy the combined alignment. */
	UnsupportedAlignment,

	/** Rejects acquisition after the observed value's last strong owner released it. */
	Expired,

	/** Rejects an increment that would make a reference counter wrap. */
	ReferenceCountOverflow,

	/** Preserves an unexpected resource failure without pretending it was exhaustion. */
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
 * Constructs one shared value and control block in one resource allocation.
 *
 * @tparam ValueType Complete value type whose construction and destruction cannot throw.
 * @tparam Mode Reference-counting execution contract.
 * @tparam ConstructorArgumentTypes Constructor argument types forwarded only after allocation.
 * @param InResource Resource that must outlive every resulting shared and weak handle.
 * @param Arguments Arguments forwarded to ValueType's constructor.
 * @return Typed allocation outcome and first strong owner on success.
 */
template<typename ValueType, ESharedPointerMode Mode = ESharedPointerMode::SingleThreaded, typename... ConstructorArgumentTypes>
TSharedPointerResult<ValueType, Mode> MakeShared(IMemoryResource& InResource, ConstructorArgumentTypes&&... Arguments) noexcept;

namespace Detail
{

	/** Retains single-threaded lifetime state until both strong and weak counts reach zero. */
	template<typename ValueType>
	struct TSharedControlBlock final
	{
		/** Counter width keeps handle cost bounded while exposing an explicit overflow result. */
		using FReferenceCount = std::uint16_t;

		/** Identifies the resource that must receive Allocation. */
		IMemoryResource* Resource{nullptr};

		/** Preserves the exact combined object/control-block allocation. */
		FMemoryBlock Allocation{};

		/** Identifies the live value and becomes null before weak observers can report expiry. */
		ValueType* Value{nullptr};

		/** Counts live strong handles that keep Value constructed. */
		FReferenceCount StrongReferenceCount{1};

		/** Counts live weak handles that keep this control block allocated. */
		FReferenceCount WeakReferenceCount{0};

		/**
		 * True only while the final strong Reset() runs the value destructor. It
		 * makes the weak-side Reset() defer control-block deallocation, so a value
		 * that drops its own last weak handle mid-destruction cannot free the block
		 * out from under the strong side (self-observer teardown; see
		 * MemoryTests.cpp:558).
		 */
		bool bValueDestructionInProgress{false};

		/** Reports whether at least one strong handle still keeps this block alive. */
		bool HasLiveStrongReference() const noexcept { return StrongReferenceCount != 0; }

		/** Reports whether the value is still constructed and observable through a strong handle. */
		bool HasLiveValue() const noexcept { return StrongReferenceCount != 0 && Value != nullptr; }
	};

	/** Converts allocation-boundary failures into the shared-pointer result domain. */
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

	/** Returns an expired control block to its exact resource after its final weak release. */
	template<typename ValueType>
	void DestroySharedControlBlock(TSharedControlBlock<ValueType>* const InControlBlock) noexcept
	{
		IMemoryResource* const Resource = InControlBlock->Resource;
		const FMemoryBlock Allocation = InControlBlock->Allocation;
		DestroyAt(InControlBlock);
		static_cast<void>(Resource->Deallocate(Allocation));
	}

	/**
	 * Places the value immediately after its control block in one shared
	 * allocation, each on its own aligned boundary. ValueOffsetBytes rounds the
	 * control-block size up to the value's alignment so the value starts aligned.
	 */
	template<typename ValueType>
	struct TSharedAllocationLayout final
	{
		using FControlBlock = TSharedControlBlock<ValueType>;
		static constexpr std::size_t CombinedAlignmentBytes = alignof(FControlBlock) > alignof(ValueType) ? alignof(FControlBlock)
																										  : alignof(ValueType);
		static_assert(
			sizeof(FControlBlock) <= std::numeric_limits<std::size_t>::max() - (alignof(ValueType) - 1U),
			"Shared layout padding must fit in size_t.");
		static constexpr std::size_t ValueOffsetBytes = AlignSizeUp(sizeof(FControlBlock), alignof(ValueType));
		static_assert(ValueOffsetBytes <= std::numeric_limits<std::size_t>::max() - sizeof(ValueType), "Shared allocation size must fit in size_t.");
		static constexpr std::size_t CombinedSizeBytes = ValueOffsetBytes + sizeof(ValueType);
	};

	/** Constructs the control block and value in one allocation and links the block to its resource. */
	template<typename ValueType, typename... ConstructorArgumentTypes>
	TSharedControlBlock<ValueType>* ConstructSharedBlock(
		IMemoryResource& InResource,
		const FMemoryBlock InAllocation,
		const std::size_t InValueOffsetBytes,
		ConstructorArgumentTypes&&... Arguments) noexcept
	{
		using FControlBlock = TSharedControlBlock<ValueType>;
		std::byte* const AllocationBytes = static_cast<std::byte*>(InAllocation.Address);
		FControlBlock* const ControlBlock = ConstructAt<FControlBlock>(AllocationBytes);
		ValueType* const Value = ConstructAt<ValueType>(AllocationBytes + InValueOffsetBytes, std::forward<ConstructorArgumentTypes>(Arguments)...);
		ControlBlock->Resource = &InResource;
		ControlBlock->Allocation = InAllocation;
		ControlBlock->Value = Value;
		return ControlBlock;
	}

} // namespace Detail

/**
 * Owns one non-managed value through explicit single-threaded reference counting.
 *
 * Handles are move-only because an implicit copy could fail at the documented
 * counter boundary. TryShare performs the fallible strong-owner acquisition.
 */
template<typename ValueType, ESharedPointerMode Mode>
class TSharedPtr final
{
	static_assert(Mode == ESharedPointerMode::SingleThreaded, "Only single-threaded shared pointers are available.");
	static_assert(!std::is_array<ValueType>::value, "TSharedPtr owns one non-array value.");
	static_assert(std::is_nothrow_destructible<ValueType>::value, "TSharedPtr requires noexcept destruction.");

private:
	/** Names the type-specific control block shared with weak observers. */
	using FControlBlock = Detail::TSharedControlBlock<ValueType>;

	/** Names the bounded counter used by the selected control-block layout. */
	using FReferenceCount = typename FControlBlock::FReferenceCount;

public:
	/** Creates an empty owner without selecting or touching a resource. */
	TSharedPtr() noexcept = default;

	/** Prevents an invisible reference-count overflow during copy construction. */
	TSharedPtr(const TSharedPtr&) = delete;

	/** Prevents an invisible reference-count overflow during copy assignment. */
	TSharedPtr& operator=(const TSharedPtr&) = delete;

	/** Transfers one already-counted strong handle without changing counters. */
	TSharedPtr(TSharedPtr&& Other) noexcept : ControlBlock(Other.ControlBlock) { Other.ControlBlock = nullptr; }

	/** Releases any current owner, then transfers another already-counted handle. */
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

	/** Releases this strong handle and completes destruction when it is the last. */
	~TSharedPtr() noexcept { Reset(); }

	/** Observes the live value without changing either reference counter. */
	ValueType* Get() const noexcept { return ControlBlock == nullptr ? nullptr : ControlBlock->Value; }

	/** Reports whether this handle currently keeps a live value constructed. */
	bool IsValid() const noexcept { return Get() != nullptr; }

	/** Acquires another strong owner or reports the exact counter-boundary failure. */
	TSharedPointerResult<ValueType, Mode> TryShare() const noexcept;

	/** Acquires a weak observer or reports the exact counter-boundary failure. */
	TWeakPointerResult<ValueType, Mode> TryAcquireWeak() const noexcept;

	/** Releases this strong handle and destroys the value at the final strong release. */
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

	/** Reports the current strong count for diagnostics and boundary tests. */
	std::size_t StrongReferenceCount() const noexcept
	{
		return ControlBlock == nullptr ? 0U : static_cast<std::size_t>(ControlBlock->StrongReferenceCount);
	}

	/** Reports the current weak count for diagnostics and boundary tests. */
	std::size_t WeakReferenceCount() const noexcept
	{
		return ControlBlock == nullptr ? 0U : static_cast<std::size_t>(ControlBlock->WeakReferenceCount);
	}

	/** Exposes the exact supported counter boundary without a mutation backdoor. */
	static constexpr std::size_t MaximumReferenceCount() noexcept { return static_cast<std::size_t>(std::numeric_limits<FReferenceCount>::max()); }

private:
	/** Adopts one strong count already acquired by a factory or fallible operation. */
	explicit TSharedPtr(FControlBlock* const InControlBlock) noexcept : ControlBlock(InControlBlock) {}

	/** Allows matching weak observers to create an already-counted strong handle. */
	friend class TWeakPtr<ValueType, Mode>;

	/** Lets the factory create the first strong owner without exposing raw adoption. */
	template<typename FactoryValueType, ESharedPointerMode PointerMode, typename... FactoryConstructorArgumentTypes>
	friend TSharedPointerResult<FactoryValueType, PointerMode> MakeShared(IMemoryResource&, FactoryConstructorArgumentTypes&&...) noexcept;

	/** Destroys the owned value in place while blocking weak-side reclamation mid-teardown. */
	static void DestroyValueInPlace(FControlBlock* const InReleasedControlBlock) noexcept
	{
		ValueType* const Value = InReleasedControlBlock->Value;
		InReleasedControlBlock->Value = nullptr;
		InReleasedControlBlock->bValueDestructionInProgress = true;
		Detail::DestroyAt(Value);
		InReleasedControlBlock->bValueDestructionInProgress = false;
	}

	/** Frees the control block once no strong or weak handle can still observe it. */
	static void ReclaimControlBlockIfUnreferenced(FControlBlock* const InReleasedControlBlock) noexcept
	{
		if (InReleasedControlBlock->WeakReferenceCount == 0)
		{
			Detail::DestroySharedControlBlock(InReleasedControlBlock);
		}
	}

	/** Retains the allocation while this handle contributes one strong count. */
	FControlBlock* ControlBlock{nullptr};
};

/**
 * Observes one shared value without extending its construction lifetime.
 *
 * Handles are move-only because duplicating a weak count can fail explicitly at
 * the counter boundary. Pin and TryAcquireStrong never expose an expired value.
 */
template<typename ValueType, ESharedPointerMode Mode>
class TWeakPtr final
{
	static_assert(Mode == ESharedPointerMode::SingleThreaded, "Only single-threaded weak pointers are available.");

private:
	/** Names the type-specific control block retained by weak observers. */
	using FControlBlock = Detail::TSharedControlBlock<ValueType>;

	/** Names the bounded counter used by the selected control-block layout. */
	using FReferenceCount = typename FControlBlock::FReferenceCount;

public:
	/** Creates an empty observer without selecting or touching a resource. */
	TWeakPtr() noexcept = default;

	/** Prevents an invisible reference-count overflow during copy construction. */
	TWeakPtr(const TWeakPtr&) = delete;

	/** Prevents an invisible reference-count overflow during copy assignment. */
	TWeakPtr& operator=(const TWeakPtr&) = delete;

	/** Transfers one already-counted weak handle without changing counters. */
	TWeakPtr(TWeakPtr&& Other) noexcept : ControlBlock(Other.ControlBlock) { Other.ControlBlock = nullptr; }

	/** Releases any current observer, then transfers another already-counted handle. */
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

	/** Releases this weak handle and returns an expired final block when necessary. */
	~TWeakPtr() noexcept { Reset(); }

	/** Reports expiry without dereferencing the value storage. */
	bool IsExpired() const noexcept { return ControlBlock == nullptr || ControlBlock->StrongReferenceCount == 0; }

	/** Acquires another weak observer or reports the exact counter-boundary failure. */
	TWeakPointerResult<ValueType, Mode> TryObserve() const noexcept;

	/** Acquires a strong owner only while the observed value remains live. */
	TSharedPointerResult<ValueType, Mode> TryAcquireStrong() const noexcept;

	/** Provides UE-familiar naming for the same typed, fallible strong acquisition. */
	TSharedPointerResult<ValueType, Mode> Pin() const noexcept;

	/** Releases this weak count and deallocates the block after expiry when final. */
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
			Detail::DestroySharedControlBlock(ReleasedControlBlock);
		}
	}

	/** Reports the current strong count for diagnostics and boundary tests. */
	std::size_t StrongReferenceCount() const noexcept
	{
		return ControlBlock == nullptr ? 0U : static_cast<std::size_t>(ControlBlock->StrongReferenceCount);
	}

	/** Reports the current weak count for diagnostics and boundary tests. */
	std::size_t WeakReferenceCount() const noexcept
	{
		return ControlBlock == nullptr ? 0U : static_cast<std::size_t>(ControlBlock->WeakReferenceCount);
	}

	/** Exposes the exact supported counter boundary without a mutation backdoor. */
	static constexpr std::size_t MaximumReferenceCount() noexcept { return static_cast<std::size_t>(std::numeric_limits<FReferenceCount>::max()); }

private:
	/** Adopts one weak count already acquired by a fallible operation. */
	explicit TWeakPtr(FControlBlock* const InControlBlock) noexcept : ControlBlock(InControlBlock) {}

	/** Allows strong owners to create an already-counted weak handle. */
	friend class TSharedPtr<ValueType, Mode>;

	/** Retains an expired control block until this handle releases its weak count. */
	FControlBlock* ControlBlock{nullptr};
};

/** Couples a shared-pointer operation outcome with its acquired strong owner. */
template<typename ValueType, ESharedPointerMode Mode>
struct TSharedPointerResult
{
	/** Distinguishes acquisition success from allocation, expiry, and overflow. */
	ESharedPointerResult Result{ESharedPointerResult::OutOfMemory};

	/** Owns one strong count only when Result is Success. */
	TSharedPtr<ValueType, Mode> Pointer{};
};

/** Couples a weak-pointer operation outcome with its acquired observer. */
template<typename ValueType, ESharedPointerMode Mode>
struct TWeakPointerResult
{
	/** Distinguishes acquisition success from expiry and counter overflow. */
	ESharedPointerResult Result{ESharedPointerResult::Expired};

	/** Owns one weak count only when Result is Success. */
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

	using FLayout = Detail::TSharedAllocationLayout<ValueType>;

	FMemoryBlock Allocation{};
	const EMemoryResult AllocationResult = InResource.TryAllocate(FLayout::CombinedSizeBytes, FLayout::CombinedAlignmentBytes, Allocation);
	if (AllocationResult != EMemoryResult::Success)
	{
		TSharedPointerResult<ValueType, Mode> FailedResult{};
		FailedResult.Result = Detail::ToSharedPointerResult(AllocationResult);
		return FailedResult;
	}

	Detail::TSharedControlBlock<ValueType>* const ControlBlock = Detail::ConstructSharedBlock<ValueType>(
		InResource, Allocation, FLayout::ValueOffsetBytes, std::forward<ConstructorArgumentTypes>(Arguments)...);

	TSharedPointerResult<ValueType, Mode> SuccessfulResult{};
	SuccessfulResult.Result = ESharedPointerResult::Success;
	SuccessfulResult.Pointer = TSharedPtr<ValueType, Mode>(ControlBlock);
	return SuccessfulResult;
}

} // namespace MicroWorld
