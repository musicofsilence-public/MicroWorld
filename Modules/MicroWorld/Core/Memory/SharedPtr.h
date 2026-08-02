#pragma once

#include <MicroWorld/Core/Containers/RawSlot.h>
#include <MicroWorld/Core/Memory/MemoryResource.h>
#include <MicroWorld/Core/Memory/SharedControlBlock.h>
#include <MicroWorld/Core/Memory/SharedPointerMode.h>

#include <cstddef>
#include <limits>
#include <type_traits>

namespace MicroWorld::Core
{

template<typename ValueType, ESharedPointerMode Mode>
class TWeakPtr;

template<typename ValueType, ESharedPointerMode Mode>
struct TSharedPointerResult;

template<typename ValueType, ESharedPointerMode Mode>
struct TWeakPointerResult;

template<typename ValueType, ESharedPointerMode Mode = ESharedPointerMode::SingleThreaded, typename... ConstructorArgumentTypes>
TSharedPointerResult<ValueType, Mode> MakeShared(IMemoryResource&, ConstructorArgumentTypes&&...) noexcept;

/**
 * Motivation: Lets an owner share one non-managed value through explicit single-threaded reference counting.
 * Responsibilities: Hold one strong count, keep move-only so a copy cannot overflow the counter at its boundary,
 *   and destroy the value exactly once when the last strong owner releases it.
 * Example:
 *   auto Result = MakeShared<int>(Arena, 7);
 *   TSharedPtr<int> Owner = std::move(Result.Pointer);
 *   TSharedPointerResult<int> Shared = Owner.TryShare();
 */
template<typename ValueType, ESharedPointerMode Mode = ESharedPointerMode::SingleThreaded>
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

} // namespace MicroWorld::Core
