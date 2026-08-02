#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Containers/StaticVector.h>
#include <MicroWorld/Core/Memory/FixedArena.h>
#include <MicroWorld/Core/Memory/TSharedPointerDefinitions.h>
#include <MicroWorld/Core/Memory/UniquePointerResult.h>

#include <array>
#include <cstddef>
#include <type_traits>

namespace MicroWorld::Tests
{

using MicroWorld::Core::EMemoryResult;
using MicroWorld::Core::ESharedPointerMode;
using MicroWorld::Core::ESharedPointerResult;
using MicroWorld::Core::FMemoryBlock;
using MicroWorld::Core::IMemoryResource;
using MicroWorld::Core::MakeShared;
using MicroWorld::Core::MakeUnique;
using MicroWorld::Core::TFixedArena;
using MicroWorld::Core::TSharedPointerResult;
using MicroWorld::Core::TSharedPtr;
using MicroWorld::Core::TSpan;
using MicroWorld::Core::TStaticVector;
using MicroWorld::Core::TUniquePointerResult;
using MicroWorld::Core::TUniquePtr;
using MicroWorld::Core::TWeakPointerResult;
using MicroWorld::Core::TWeakPtr;

/**
 * Motivation: Records value construction and destruction without sharing state between tests.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FLifetimeState final
{
	/** Motivation: Proves failed factories never begin a value lifetime. */
	std::size_t ConstructionCount{0};

	/** Motivation: Proves each successful ownership path ends its value lifetime once. */
	std::size_t DestructionCount{0};
};

/**
 * Motivation: Exposes value lifetime through caller-owned counters while remaining nothrow.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FTrackedValue final
{
public:
	/**
	 * Motivation: Begins one observable value lifetime only after its resource allocation succeeds.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	explicit FTrackedValue(FLifetimeState& InState, const int InValue = 0) noexcept : Value(InValue), State(InState) { ++State.ConstructionCount; }

	/**
	 * Motivation: Ownership tests can reject leaks and double destruction.
	 * Responsibilities: Ends one observable lifetime.
	 */
	~FTrackedValue() noexcept { ++State.DestructionCount; }

	/** Motivation: Carries one public value used to prove acquired owners resolve the same live object. */
	int Value{0};

private:
	/** Motivation: Shares only the fresh per-test observation state selected by the caller. */
	FLifetimeState& State;
};

/**
 * Motivation: Forces a layout beyond the small arena's alignment guarantee before construction can begin.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class alignas(64) FOverAlignedTrackedValue final
{
public:
	/**
	 * Motivation: Would expose an invalid construction if alignment rejection occurred too late.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	explicit FOverAlignedTrackedValue(FLifetimeState& InState) noexcept : State(InState) { ++State.ConstructionCount; }

	/**
	 * Motivation: Balances construction only when the resource accepted the layout.
	 * Responsibilities: Release the documented observation exactly once and leave no leak behind.
	 */
	~FOverAlignedTrackedValue() noexcept { ++State.DestructionCount; }

private:
	/** Motivation: Shares only the fresh observation state for the alignment test. */
	FLifetimeState& State;
};

/**
 * Motivation: Records public resource traffic while delegating storage policy to a fixed arena. The wrapper proves
 *   allocation count, exact block identity, and resource reuse without inspecting ownership-control
 *   internals.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
template<std::size_t Bytes, std::size_t Alignment>
class TTrackingMemoryResource final : public IMemoryResource
{
public:
	/**
	 * Motivation: Records one public allocation request and the exact successful block.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	EMemoryResult TryAllocate(const std::size_t InSizeBytes, const std::size_t InAlignmentBytes, FMemoryBlock& OutBlock) noexcept override
	{
		++AllocationRequestCount;

		const EMemoryResult Result = Arena.TryAllocate(InSizeBytes, InAlignmentBytes, OutBlock);
		if (Result == EMemoryResult::Success)
		{
			++SuccessfulAllocationCount;
			LastAllocatedBlock = OutBlock;
		}
		return Result;
	}

	/**
	 * Motivation: Records the exact block returned through the public deallocation boundary.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	EMemoryResult Deallocate(const FMemoryBlock InBlock) noexcept override
	{
		++DeallocationRequestCount;
		LastDeallocatedBlock = InBlock;
		return Arena.Deallocate(InBlock);
	}

	/**
	 * Motivation: Preserves the wrapped arena's public caller-usable capacity.
	 * Responsibilities: Return the stored value and touch nothing else.
	 */
	std::size_t CapacityBytes() const noexcept override { return Arena.CapacityBytes(); }

	/**
	 * Motivation: Preserves the wrapped arena's public active-byte diagnostic.
	 * Responsibilities: Return the stored value and touch nothing else.
	 */
	std::size_t UsedBytes() const noexcept override { return Arena.UsedBytes(); }

	/** Motivation: Counts all factory allocation attempts made through this exact resource. */
	std::size_t AllocationRequestCount{0};

	/** Motivation: Counts only allocations that returned a live block. */
	std::size_t SuccessfulAllocationCount{0};

	/** Motivation: Counts all ownership deallocation attempts made through this exact resource. */
	std::size_t DeallocationRequestCount{0};

	/** Motivation: Preserves the exact latest successful allocation identity. */
	FMemoryBlock LastAllocatedBlock{};

	/** Motivation: Preserves the exact block later returned by an owner. */
	FMemoryBlock LastDeallocatedBlock{};

private:
	/** Motivation: Supplies bounded caller-owned storage while the wrapper observes only public calls. */
	TFixedArena<Bytes, Alignment> Arena;
};

/**
 * Motivation: Owns a weak observer to itself so final-weak release occurs during value destruction.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FSelfObservingValue final
{
public:
	/**
	 * Motivation: Exposes construction and destruction around the self-observer regression.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	explicit FSelfObservingValue(FLifetimeState& InState) noexcept : State(InState) { ++State.ConstructionCount; }

	/**
	 * Motivation: Marks the destructor body before the member weak observer releases its final count.
	 * Responsibilities: Release the documented observation exactly once and leave no leak behind.
	 */
	~FSelfObservingValue() noexcept { ++State.DestructionCount; }

	/**
	 * Motivation: Transfers one already-counted self observer into the value's destruction path.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void AdoptSelfObserver(TWeakPtr<FSelfObservingValue>&& InObserver) noexcept { SelfObserver = std::move(InObserver); }

private:
	/** Motivation: Shares only the fresh counters selected for this regression test. */
	FLifetimeState& State;

	/** Motivation: Exercises final weak release while the containing value destructor is active. */
	TWeakPtr<FSelfObservingValue> SelfObserver;
};

/**
 * Motivation: Records fixed-vector lifetime and reverse destruction order in caller-owned state.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FVectorLifetimeState final
{
	/** Motivation: Proves capacity rejection occurs before element construction. */
	std::size_t ConstructionCount{0};

	/** Motivation: Proves clear and scope exit destroy only live elements. */
	std::size_t DestructionCount{0};

	/** Motivation: Preserves destruction order without dynamic storage. */
	std::array<int, 4> DestructionOrder{};
};

/**
 * Motivation: Gives vector tests one nothrow element with externally observable lifetime.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FVectorTrackedValue final
{
public:
	/**
	 * Motivation: Starts one element lifetime carrying an insertion identity.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FVectorTrackedValue(FVectorLifetimeState& InState, const int InIdentity) noexcept : State(InState), Identity(InIdentity)
	{
		++State.ConstructionCount;
	}

	/**
	 * Motivation: Appends this element's identity to the fresh bounded destruction trace.
	 * Responsibilities: Release the documented observation exactly once and leave no leak behind.
	 */
	~FVectorTrackedValue() noexcept
	{
		State.DestructionOrder[State.DestructionCount] = Identity;
		++State.DestructionCount;
	}

	/**
	 * Motivation: Exposes insertion identity for deterministic iteration assertions.
	 * Responsibilities: Return the stored value and touch nothing else.
	 */
	int GetIdentity() const noexcept { return Identity; }

private:
	/** Motivation: Shares only the observation state owned by the current test. */
	FVectorLifetimeState& State;

	/** Motivation: Distinguishes elements without relying on their storage addresses. */
	int Identity{0};
};

static_assert(!std::is_copy_constructible<TSharedPtr<FTrackedValue>>::value);
static_assert(!std::is_copy_assignable<TSharedPtr<FTrackedValue>>::value);
static_assert(std::is_move_constructible<TSharedPtr<FTrackedValue>>::value);
static_assert(std::is_move_assignable<TSharedPtr<FTrackedValue>>::value);
static_assert(!std::is_copy_constructible<TWeakPtr<FTrackedValue>>::value);
static_assert(!std::is_copy_assignable<TWeakPtr<FTrackedValue>>::value);
static_assert(std::is_move_constructible<TWeakPtr<FTrackedValue>>::value);
static_assert(std::is_move_assignable<TWeakPtr<FTrackedValue>>::value);
static_assert(std::is_constructible<TSpan<const int>, TSpan<int>>::value);
static_assert(!std::is_constructible<TSpan<int>, TSpan<const int>>::value);

} // namespace MicroWorld::Tests
