#pragma once

#include <MicroWorld/Core/Delegates/MulticastDelegate.h>

#include <cstddef>
#include <type_traits>

namespace MicroWorld::Tests
{

using MicroWorld::Core::EDelegateResult;
using MicroWorld::Core::FDelegateHandle;
using MicroWorld::Core::TDelegate;
using MicroWorld::Core::TMulticastDelegate;

/** Motivation: Inline storage the small-capacity delegates and bindings share across the layout tests. */
constexpr std::size_t SmallInlineBytes = 32;

/** Motivation: Inline storage the value-argument and large-layout delegates use across the broadcast tests. */
constexpr std::size_t StandardInlineBytes = 64;

/** Motivation: Inline storage large enough that only alignment, not size, can reject the over-aligned probe. */
constexpr std::size_t LargeInlineBytes = 128;

/** Motivation: Byte count of the oversized probe payload, sized to exceed the small delegate's inline capacity. */
constexpr std::size_t OversizedPayloadByteCount = 128;

/** Motivation: Multicast slot count the insertion-order and value-copy tests exercise below capacity. */
constexpr std::size_t SmallMulticastCapacity = 2;

/** Motivation: Multicast slot count the active-broadcast mutation test fills so iteration order is observable. */
constexpr std::size_t LargeMulticastCapacity = 4;

/** Motivation: Multicast slot count the zero-capacity test uses to prove Add rejection and empty broadcast. */
constexpr std::size_t ZeroMulticastCapacity = 0;

/**
 * Motivation: Records callable movement, invocation, and owned-lifetime destruction per test.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FCallableState final
{
	/** Motivation: Proves supported bindings construct only through explicit moves. */
	std::size_t MoveCount{0};

	/** Motivation: Proves Execute and Broadcast invoke the expected number of bindings. */
	std::size_t InvocationCount{0};

	/** Motivation: Proves the stored callable lifetime ends exactly once. */
	std::size_t OwnedDestructionCount{0};

	/** Motivation: Preserves the latest delivered value for direct Execute assertions. */
	int LastValue{0};
};

/**
 * Motivation: Transfers one observable callable lifetime without counting moved-from destruction.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FTrackedCallable final
{
public:
	/**
	 * Motivation: Begins the caller-owned source lifetime without claiming a stored move yet.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	explicit FTrackedCallable(FCallableState& InState) noexcept : State(&InState) {}

	/**
	 * Motivation: Only the final stored callable counts destruction.
	 * Responsibilities: Transfers observation ownership.
	 */
	FTrackedCallable(FTrackedCallable&& Other) noexcept : State(Other.State), bOwnsObservation(Other.bOwnsObservation)
	{
		Other.bOwnsObservation = false;
		if (State != nullptr)
		{
			++State->MoveCount;
		}
	}

	/**
	 * Motivation: Keeps one inline callable lifetime uniquely owned.
	 * Responsibilities: Remain deleted so the documented guarantee cannot be violated by copy or assignment.
	 */
	FTrackedCallable& operator=(FTrackedCallable&&) = delete;

	/**
	 * Motivation: Prevents tests from accidentally duplicating the tracked callable.
	 * Responsibilities: Remain deleted so the documented guarantee cannot be violated by copy or assignment.
	 */
	FTrackedCallable(const FTrackedCallable&) = delete;

	/**
	 * Motivation: Prevents tests from accidentally duplicating observation ownership.
	 * Responsibilities: Remain deleted so the documented guarantee cannot be violated by copy or assignment.
	 */
	FTrackedCallable& operator=(const FTrackedCallable&) = delete;

	/**
	 * Motivation: Counts only destruction of the final observation-owning callable.
	 * Responsibilities: Release the documented observation exactly once and leave no leak behind.
	 */
	~FTrackedCallable() noexcept
	{
		if (bOwnsObservation && State != nullptr)
		{
			++State->OwnedDestructionCount;
		}
	}

	/**
	 * Motivation: Records one delivered value through the public delegate execution path.
	 * Responsibilities: Implement only the documented operation and own no side effects beyond it.
	 */
	void operator()(const int InValue) noexcept
	{
		++State->InvocationCount;
		State->LastValue = InValue;
	}

private:
	/** Motivation: Shares only the fresh per-test observation counters. */
	FCallableState* State{nullptr};

	/** Motivation: Ensures moves do not make source destruction look like stored destruction. */
	bool bOwnsObservation{true};
};

/**
 * Motivation: Makes a callable exceed a small delegate's byte capacity without side effects.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FOversizedCallable final
{
	/** Motivation: Shares counters that prove rejection occurs before a stored move. */
	FCallableState* State{nullptr};

	/** Motivation: Forces the callable object above the tested inline capacity. */
	std::byte Payload[OversizedPayloadByteCount]{};

	/**
	 * Motivation: Records any unexpected attempt to construct a stored callable by moving.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FOversizedCallable(FOversizedCallable&& Other) noexcept : State(Other.State)
	{
		for (std::size_t Index = 0; Index < OversizedPayloadByteCount; ++Index)
		{
			Payload[Index] = Other.Payload[Index];
		}
		++State->MoveCount;
	}

	/**
	 * Motivation: Begins one caller-owned source callable for layout rejection.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	explicit FOversizedCallable(FCallableState& InState) noexcept : State(&InState) {}

	/**
	 * Motivation: Keeps the rejection probe move-only like production inline callables.
	 * Responsibilities: Remain deleted so the documented guarantee cannot be violated by copy or assignment.
	 */
	FOversizedCallable(const FOversizedCallable&) = delete;

	/**
	 * Motivation: Keeps the rejection probe free of unrelated assignment behavior.
	 * Responsibilities: Remain deleted so the documented guarantee cannot be violated by copy or assignment.
	 */
	FOversizedCallable& operator=(const FOversizedCallable&) = delete;

	/**
	 * Motivation: Keeps the rejection probe free of unrelated assignment behavior.
	 * Responsibilities: Remain deleted so the documented guarantee cannot be violated by copy or assignment.
	 */
	FOversizedCallable& operator=(FOversizedCallable&&) = delete;

	/**
	 * Motivation: Supplies the declared signature if the layout were accepted.
	 * Responsibilities: Implement only the documented operation and own no side effects beyond it.
	 */
	void operator()() noexcept { ++State->InvocationCount; }
};

/**
 * Motivation: Makes alignment, rather than size, the unsupported callable property.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct alignas(64) FOverAlignedCallable final
{
	/**
	 * Motivation: Begins one caller-owned source callable for alignment rejection.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	explicit FOverAlignedCallable(FCallableState& InState) noexcept : State(&InState) {}

	/**
	 * Motivation: Records any unexpected attempt to construct a stored callable by moving.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FOverAlignedCallable(FOverAlignedCallable&& Other) noexcept : State(Other.State) { ++State->MoveCount; }

	/**
	 * Motivation: Keeps the rejection probe move-only like production inline callables.
	 * Responsibilities: Remain deleted so the documented guarantee cannot be violated by copy or assignment.
	 */
	FOverAlignedCallable(const FOverAlignedCallable&) = delete;

	/**
	 * Motivation: Keeps the rejection probe free of unrelated assignment behavior.
	 * Responsibilities: Remain deleted so the documented guarantee cannot be violated by copy or assignment.
	 */
	FOverAlignedCallable& operator=(const FOverAlignedCallable&) = delete;

	/**
	 * Motivation: Keeps the rejection probe free of unrelated assignment behavior.
	 * Responsibilities: Remain deleted so the documented guarantee cannot be violated by copy or assignment.
	 */
	FOverAlignedCallable& operator=(FOverAlignedCallable&&) = delete;

	/**
	 * Motivation: Supplies the declared signature if the layout were accepted.
	 * Responsibilities: Implement only the documented operation and own no side effects beyond it.
	 */
	void operator()() noexcept { ++State->InvocationCount; }

	/** Motivation: Shares only fresh counters used to prove early rejection. */
	FCallableState* State{nullptr};
};

/**
 * Motivation: Records bounded callback order without allocating or exposing delegate slots.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
template<std::size_t Capacity>
class TIntEventLog final
{
public:
	/**
	 * Motivation: Appends one event only within the caller-selected observation bound.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void Add(const int InEvent) noexcept
	{
		if (EventCount < Capacity)
		{
			Events[EventCount] = InEvent;
			++EventCount;
		}
	}

	/**
	 * Motivation: Starts a fresh broadcast observation phase in the same test.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void Clear() noexcept { EventCount = 0; }

	/**
	 * Motivation: Reports how many callbacks were publicly observed.
	 * Responsibilities: Return the stored value and touch nothing else.
	 */
	std::size_t Size() const noexcept { return EventCount; }

	/**
	 * Motivation: Exposes one observed callback identity in broadcast order.
	 * Responsibilities: Return the stored value and touch nothing else.
	 */
	int At(const std::size_t InIndex) const noexcept { return Events[InIndex]; }

private:
	/** Motivation: Retains only the bounded event sequence needed by the current test. */
	int Events[Capacity]{};

	/** Motivation: Separates initialized observations from unused fixed capacity. */
	std::size_t EventCount{0};
};

/**
 * Motivation: Carries active-broadcast operation results outside the inline callback.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FBroadcastMutationState final
{
	/** Motivation: Selects the multicast whose active iteration must remain unchanged. */
	TMulticastDelegate<void(), LargeMulticastCapacity, LargeInlineBytes>* Multicast{nullptr};

	/** Motivation: Supplies a binding whose rejected Add must retain ownership. */
	TDelegate<void(), LargeInlineBytes>* PendingBinding{nullptr};

	/** Motivation: Identifies a live callback whose rejected Remove must leave it active. */
	FDelegateHandle HandleToRemove{};

	/** Motivation: Records the attempted Add result from inside a callback. */
	EDelegateResult AddResult{EDelegateResult::InvalidHandle};

	/** Motivation: Records the attempted Remove result from inside a callback. */
	EDelegateResult RemoveResult{EDelegateResult::InvalidHandle};

	/** Motivation: Records the nested Broadcast result from inside a callback. */
	EDelegateResult NestedBroadcastResult{EDelegateResult::InvalidHandle};

	/** Motivation: Captures binding count while all active-broadcast operations are rejected. */
	std::size_t BindingCountDuringCallback{0};

	/** Motivation: Receives the handle only if an unexpected callback-time Add succeeds. */
	FDelegateHandle UnexpectedAddedHandle{};

	/** Motivation: Shares the fresh bounded trace used to prove active iteration order. */
	TIntEventLog<8>* Events{nullptr};
};

/**
 * Motivation: Gives value-argument tests one mutable payload whose copies are distinguishable.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FMutableValue final
{
	/** Motivation: Carries the value each binding should receive independently. */
	int Value{0};
};

/**
 * Motivation: Models a value that cannot satisfy multicast's noexcept repeat-delivery contract.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FPotentiallyThrowingCopyValue final
{
	/**
	 * Motivation: Creates the unused compile-time contract probe.
	 * Responsibilities: Remain defaulted so the documented contract is satisfied without added behaviour.
	 */
	FPotentiallyThrowingCopyValue() noexcept = default;

	/**
	 * Motivation: Makes the copy operation observably incompatible with noexcept broadcast.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FPotentiallyThrowingCopyValue(const FPotentiallyThrowingCopyValue&) noexcept(false) {}
};

static_assert(std::is_nothrow_copy_constructible<FMutableValue>::value, "The multicast value fixture must preserve noexcept repeat delivery.");
static_assert(
	!std::is_nothrow_copy_constructible<FPotentiallyThrowingCopyValue>::value,
	"A potentially throwing copy must remain distinguishable from supported multicast values.");

} // namespace MicroWorld::Tests
