#pragma once

#include <MicroWorld/Memory/MemoryResource.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
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

template<typename T, ESharedPointerMode Mode = ESharedPointerMode::SingleThreaded>
class TSharedPtr;

template<typename T, ESharedPointerMode Mode = ESharedPointerMode::SingleThreaded>
class TWeakPtr;

template<typename T, ESharedPointerMode Mode = ESharedPointerMode::SingleThreaded>
struct TSharedPointerResult;

template<typename T, ESharedPointerMode Mode = ESharedPointerMode::SingleThreaded>
struct TWeakPointerResult;

/**
 * Constructs one shared value and control block in one resource allocation.
 *
 * @tparam T Complete value type whose construction and destruction cannot throw.
 * @tparam Mode Reference-counting execution contract.
 * @tparam TArguments Constructor argument types forwarded only after allocation.
 * @param Resource Resource that must outlive every resulting shared and weak handle.
 * @param Arguments Arguments forwarded to T's constructor.
 * @return Typed allocation outcome and first strong owner on success.
 */
template<typename T, ESharedPointerMode Mode = ESharedPointerMode::SingleThreaded, typename... TArguments>
TSharedPointerResult<T, Mode> MakeShared(IMemoryResource& Resource, TArguments&&... Arguments) noexcept;

namespace Detail
{

	/** Retains single-threaded lifetime state until both strong and weak counts reach zero. */
	template<typename T>
	struct TSharedControlBlock final
	{
		/** Counter width keeps handle cost bounded while exposing an explicit overflow result. */
		using FReferenceCount = std::uint16_t;

		/** Identifies the resource that must receive Allocation. */
		IMemoryResource* Resource{nullptr};

		/** Preserves the exact combined object/control-block allocation. */
		FMemoryBlock Allocation{};

		/** Identifies the live value and becomes null before weak observers can report expiry. */
		T* Value{nullptr};

		/** Counts live strong handles that keep Value constructed. */
		FReferenceCount StrongReferenceCount{1};

		/** Counts live weak handles that keep this control block allocated. */
		FReferenceCount WeakReferenceCount{0};

		/** Defers final weak deallocation while the value destructor can release members. */
		bool bValueDestructionInProgress{false};
	};

	/** Converts allocation-boundary failures into the shared-pointer result domain. */
	inline ESharedPointerResult ToSharedPointerResult(const EMemoryResult Result) noexcept
	{
		switch (Result)
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
	template<typename T>
	void DestroySharedControlBlock(TSharedControlBlock<T>* const ControlBlock) noexcept
	{
		IMemoryResource* const Resource = ControlBlock->Resource;
		const FMemoryBlock Allocation = ControlBlock->Allocation;
		ControlBlock->~TSharedControlBlock<T>();
		static_cast<void>(Resource->Deallocate(Allocation));
	}

} // namespace Detail

/**
 * Owns one non-managed value through explicit single-threaded reference counting.
 *
 * Handles are move-only because an implicit copy could fail at the documented
 * counter boundary. TryShare performs the fallible strong-owner acquisition.
 */
template<typename T, ESharedPointerMode Mode>
class TSharedPtr final
{
	static_assert(Mode == ESharedPointerMode::SingleThreaded, "Only single-threaded shared pointers are available.");
	static_assert(!std::is_array<T>::value, "TSharedPtr owns one non-array value.");
	static_assert(std::is_nothrow_destructible<T>::value, "TSharedPtr requires noexcept destruction.");

private:
	/** Names the type-specific control block shared with weak observers. */
	using FControlBlock = Detail::TSharedControlBlock<T>;

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
	T* Get() const noexcept { return ControlBlock == nullptr ? nullptr : ControlBlock->Value; }

	/** Reports whether this handle currently keeps a live value constructed. */
	bool IsValid() const noexcept { return Get() != nullptr; }

	/** Acquires another strong owner or reports the exact counter-boundary failure. */
	TSharedPointerResult<T, Mode> TryShare() const noexcept;

	/** Acquires a weak observer or reports the exact counter-boundary failure. */
	TWeakPointerResult<T, Mode> TryAcquireWeak() const noexcept;

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

		T* const Value = ReleasedControlBlock->Value;
		ReleasedControlBlock->Value = nullptr;
		ReleasedControlBlock->bValueDestructionInProgress = true;
		Value->~T();
		ReleasedControlBlock->bValueDestructionInProgress = false;

		if (ReleasedControlBlock->WeakReferenceCount == 0)
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
	/** Adopts one strong count already acquired by a factory or fallible operation. */
	explicit TSharedPtr(FControlBlock* const InControlBlock) noexcept : ControlBlock(InControlBlock) {}

	/** Allows matching weak observers to create an already-counted strong handle. */
	friend class TWeakPtr<T, Mode>;

	/** Lets the factory create the first strong owner without exposing raw adoption. */
	template<typename TObject, ESharedPointerMode PointerMode, typename... TObjectArguments>
	friend TSharedPointerResult<TObject, PointerMode> MakeShared(IMemoryResource&, TObjectArguments&&...) noexcept;

	/** Retains the allocation while this handle contributes one strong count. */
	FControlBlock* ControlBlock{nullptr};
};

/**
 * Observes one shared value without extending its construction lifetime.
 *
 * Handles are move-only because duplicating a weak count can fail explicitly at
 * the counter boundary. Pin and TryAcquireStrong never expose an expired value.
 */
template<typename T, ESharedPointerMode Mode>
class TWeakPtr final
{
	static_assert(Mode == ESharedPointerMode::SingleThreaded, "Only single-threaded weak pointers are available.");

private:
	/** Names the type-specific control block retained by weak observers. */
	using FControlBlock = Detail::TSharedControlBlock<T>;

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
	TWeakPointerResult<T, Mode> TryObserve() const noexcept;

	/** Acquires a strong owner only while the observed value remains live. */
	TSharedPointerResult<T, Mode> TryAcquireStrong() const noexcept;

	/** Provides UE-familiar naming for the same typed, fallible strong acquisition. */
	TSharedPointerResult<T, Mode> Pin() const noexcept;

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
	friend class TSharedPtr<T, Mode>;

	/** Retains an expired control block until this handle releases its weak count. */
	FControlBlock* ControlBlock{nullptr};
};

/** Couples a shared-pointer operation outcome with its acquired strong owner. */
template<typename T, ESharedPointerMode Mode>
struct TSharedPointerResult
{
	/** Distinguishes acquisition success from allocation, expiry, and overflow. */
	ESharedPointerResult Result{ESharedPointerResult::OutOfMemory};

	/** Owns one strong count only when Result is Success. */
	TSharedPtr<T, Mode> Pointer{};
};

/** Couples a weak-pointer operation outcome with its acquired observer. */
template<typename T, ESharedPointerMode Mode>
struct TWeakPointerResult
{
	/** Distinguishes acquisition success from expiry and counter overflow. */
	ESharedPointerResult Result{ESharedPointerResult::Expired};

	/** Owns one weak count only when Result is Success. */
	TWeakPtr<T, Mode> Pointer{};
};

template<typename T, ESharedPointerMode Mode>
TSharedPointerResult<T, Mode> TSharedPtr<T, Mode>::TryShare() const noexcept
{
	TSharedPointerResult<T, Mode> ShareResult{};
	if (ControlBlock == nullptr || ControlBlock->StrongReferenceCount == 0)
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

template<typename T, ESharedPointerMode Mode>
TWeakPointerResult<T, Mode> TSharedPtr<T, Mode>::TryAcquireWeak() const noexcept
{
	TWeakPointerResult<T, Mode> WeakResult{};
	if (ControlBlock == nullptr || ControlBlock->StrongReferenceCount == 0)
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
	WeakResult.Pointer = TWeakPtr<T, Mode>(ControlBlock);
	return WeakResult;
}

template<typename T, ESharedPointerMode Mode>
TWeakPointerResult<T, Mode> TWeakPtr<T, Mode>::TryObserve() const noexcept
{
	TWeakPointerResult<T, Mode> ObserveResult{};
	if (ControlBlock == nullptr || ControlBlock->StrongReferenceCount == 0 || ControlBlock->Value == nullptr)
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

template<typename T, ESharedPointerMode Mode>
TSharedPointerResult<T, Mode> TWeakPtr<T, Mode>::TryAcquireStrong() const noexcept
{
	TSharedPointerResult<T, Mode> StrongResult{};
	if (ControlBlock == nullptr || ControlBlock->StrongReferenceCount == 0 || ControlBlock->Value == nullptr)
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
	StrongResult.Pointer = TSharedPtr<T, Mode>(ControlBlock);
	return StrongResult;
}

template<typename T, ESharedPointerMode Mode>
TSharedPointerResult<T, Mode> TWeakPtr<T, Mode>::Pin() const noexcept
{
	return TryAcquireStrong();
}

template<typename T, ESharedPointerMode Mode, typename... TArguments>
TSharedPointerResult<T, Mode> MakeShared(IMemoryResource& Resource, TArguments&&... Arguments) noexcept
{
	static_assert(Mode == ESharedPointerMode::SingleThreaded, "Only single-threaded shared pointers are available.");
	static_assert(!std::is_array<T>::value, "MakeShared constructs one non-array value.");
	static_assert(std::is_nothrow_constructible<T, TArguments...>::value, "MakeShared requires noexcept construction.");
	static_assert(std::is_nothrow_destructible<T>::value, "MakeShared requires noexcept destruction.");

	using FControlBlock = Detail::TSharedControlBlock<T>;
	constexpr std::size_t CombinedAlignment = alignof(FControlBlock) > alignof(T) ? alignof(FControlBlock) : alignof(T);
	static_assert(sizeof(FControlBlock) <= std::numeric_limits<std::size_t>::max() - (alignof(T) - 1U), "Shared layout padding must fit in size_t.");
	constexpr std::size_t ValueOffset = (sizeof(FControlBlock) + alignof(T) - 1U) & ~(alignof(T) - 1U);
	static_assert(ValueOffset <= std::numeric_limits<std::size_t>::max() - sizeof(T), "Shared allocation size must fit in size_t.");
	constexpr std::size_t CombinedSize = ValueOffset + sizeof(T);

	FMemoryBlock Allocation{};
	const EMemoryResult AllocationResult = Resource.TryAllocate(CombinedSize, CombinedAlignment, Allocation);
	if (AllocationResult != EMemoryResult::Success)
	{
		TSharedPointerResult<T, Mode> FailedResult{};
		FailedResult.Result = Detail::ToSharedPointerResult(AllocationResult);
		return FailedResult;
	}

	std::byte* const AllocationBytes = static_cast<std::byte*>(Allocation.Address);
	FControlBlock* const ControlBlock = ::new (AllocationBytes) FControlBlock{};
	T* const Value = ::new (AllocationBytes + ValueOffset) T(std::forward<TArguments>(Arguments)...);
	ControlBlock->Resource = &Resource;
	ControlBlock->Allocation = Allocation;
	ControlBlock->Value = Value;

	TSharedPointerResult<T, Mode> SuccessfulResult{};
	SuccessfulResult.Result = ESharedPointerResult::Success;
	SuccessfulResult.Pointer = TSharedPtr<T, Mode>(ControlBlock);
	return SuccessfulResult;
}

} // namespace MicroWorld
