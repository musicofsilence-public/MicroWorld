#pragma once

#include <MicroWorld/Core/Memory/SharedControlBlock.h>
#include <MicroWorld/Core/Memory/SharedPointerMode.h>

#include <cstddef>
#include <limits>

namespace MicroWorld::Core
{

template<typename ValueType, ESharedPointerMode Mode>
class TSharedPtr;

template<typename ValueType, ESharedPointerMode Mode>
struct TSharedPointerResult;

template<typename ValueType, ESharedPointerMode Mode>
struct TWeakPointerResult;

/**
 * Motivation: Lets a caller observe one shared value without extending its construction lifetime.
 * Responsibilities: Hold one weak count, stay move-only so duplicating a weak count stays explicit and fallible,
 *   and never expose an expired value through Pin or TryAcquireStrong.
 * Example:
 *   TWeakPointerResult<int> Weak = Owner.TryAcquireWeak();
 *   if (!Weak.Pointer.IsExpired()) { TSharedPointerResult<int> Strong = Weak.Pointer.Pin(); }
 */
template<typename ValueType, ESharedPointerMode Mode = ESharedPointerMode::SingleThreaded>
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

} // namespace MicroWorld::Core
