#include "TestSupport.h"

#include <MicroWorld/Core/Delegates/Delegate.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace
{

using MicroWorld::Core::EDelegateResult;
using MicroWorld::Core::FDelegateHandle;
using MicroWorld::Core::TDelegate;
using MicroWorld::Core::TMulticastDelegate;

/** Inline storage the small-capacity delegates and bindings share across the layout tests. */
constexpr std::size_t SmallInlineBytes = 32;

/** Inline storage the value-argument and large-layout delegates use across the broadcast tests. */
constexpr std::size_t StandardInlineBytes = 64;

/** Inline storage large enough that only alignment, not size, can reject the over-aligned probe. */
constexpr std::size_t LargeInlineBytes = 128;

/** Byte count of the oversized probe payload, sized to exceed the small delegate's inline capacity. */
constexpr std::size_t OversizedPayloadByteCount = 128;

/** Multicast slot count the insertion-order and value-copy tests exercise below capacity. */
constexpr std::size_t SmallMulticastCapacity = 2;

/** Multicast slot count the active-broadcast mutation test fills so iteration order is observable. */
constexpr std::size_t LargeMulticastCapacity = 4;

/** Multicast slot count the zero-capacity test uses to prove Add rejection and empty broadcast. */
constexpr std::size_t ZeroMulticastCapacity = 0;

/** Records callable movement, invocation, and owned-lifetime destruction per test. */
struct FCallableState final
{
	/** Proves supported bindings construct only through explicit moves. */
	std::size_t MoveCount{0};

	/** Proves Execute and Broadcast invoke the expected number of bindings. */
	std::size_t InvocationCount{0};

	/** Proves the stored callable lifetime ends exactly once. */
	std::size_t OwnedDestructionCount{0};

	/** Preserves the latest delivered value for direct Execute assertions. */
	int LastValue{0};
};

/** Transfers one observable callable lifetime without counting moved-from destruction. */
class FTrackedCallable final
{
public:
	/** Begins the caller-owned source lifetime without claiming a stored move yet. */
	explicit FTrackedCallable(FCallableState& InState) noexcept : State(&InState) {}

	/** Transfers observation ownership so only the final stored callable counts destruction. */
	FTrackedCallable(FTrackedCallable&& Other) noexcept : State(Other.State), bOwnsObservation(Other.bOwnsObservation)
	{
		Other.bOwnsObservation = false;
		if (State != nullptr)
		{
			++State->MoveCount;
		}
	}

	/** Keeps one inline callable lifetime uniquely owned. */
	FTrackedCallable& operator=(FTrackedCallable&&) = delete;

	/** Prevents tests from accidentally duplicating the tracked callable. */
	FTrackedCallable(const FTrackedCallable&) = delete;

	/** Prevents tests from accidentally duplicating observation ownership. */
	FTrackedCallable& operator=(const FTrackedCallable&) = delete;

	/** Counts only destruction of the final observation-owning callable. */
	~FTrackedCallable() noexcept
	{
		if (bOwnsObservation && State != nullptr)
		{
			++State->OwnedDestructionCount;
		}
	}

	/** Records one delivered value through the public delegate execution path. */
	void operator()(const int InValue) noexcept
	{
		++State->InvocationCount;
		State->LastValue = InValue;
	}

private:
	/** Shares only the fresh per-test observation counters. */
	FCallableState* State{nullptr};

	/** Ensures moves do not make source destruction look like stored destruction. */
	bool bOwnsObservation{true};
};

/** Makes a callable exceed a small delegate's byte capacity without side effects. */
struct FOversizedCallable final
{
	/** Shares counters that prove rejection occurs before a stored move. */
	FCallableState* State{nullptr};

	/** Forces the callable object above the tested inline capacity. */
	std::byte Payload[OversizedPayloadByteCount]{};

	/** Records any unexpected attempt to construct a stored callable by moving. */
	FOversizedCallable(FOversizedCallable&& Other) noexcept : State(Other.State)
	{
		for (std::size_t Index = 0; Index < OversizedPayloadByteCount; ++Index)
		{
			Payload[Index] = Other.Payload[Index];
		}
		++State->MoveCount;
	}

	/** Begins one caller-owned source callable for layout rejection. */
	explicit FOversizedCallable(FCallableState& InState) noexcept : State(&InState) {}

	/** Keeps the rejection probe move-only like production inline callables. */
	FOversizedCallable(const FOversizedCallable&) = delete;

	/** Keeps the rejection probe free of unrelated assignment behavior. */
	FOversizedCallable& operator=(const FOversizedCallable&) = delete;

	/** Keeps the rejection probe free of unrelated assignment behavior. */
	FOversizedCallable& operator=(FOversizedCallable&&) = delete;

	/** Supplies the declared signature if the layout were accepted. */
	void operator()() noexcept { ++State->InvocationCount; }
};

/** Makes alignment, rather than size, the unsupported callable property. */
struct alignas(64) FOverAlignedCallable final
{
	/** Begins one caller-owned source callable for alignment rejection. */
	explicit FOverAlignedCallable(FCallableState& InState) noexcept : State(&InState) {}

	/** Records any unexpected attempt to construct a stored callable by moving. */
	FOverAlignedCallable(FOverAlignedCallable&& Other) noexcept : State(Other.State) { ++State->MoveCount; }

	/** Keeps the rejection probe move-only like production inline callables. */
	FOverAlignedCallable(const FOverAlignedCallable&) = delete;

	/** Keeps the rejection probe free of unrelated assignment behavior. */
	FOverAlignedCallable& operator=(const FOverAlignedCallable&) = delete;

	/** Keeps the rejection probe free of unrelated assignment behavior. */
	FOverAlignedCallable& operator=(FOverAlignedCallable&&) = delete;

	/** Supplies the declared signature if the layout were accepted. */
	void operator()() noexcept { ++State->InvocationCount; }

	/** Shares only fresh counters used to prove early rejection. */
	FCallableState* State{nullptr};
};

/** Records bounded callback order without allocating or exposing delegate slots. */
template<std::size_t Capacity>
class TIntEventLog final
{
public:
	/** Appends one event only within the caller-selected observation bound. */
	void Add(const int InEvent) noexcept
	{
		if (EventCount < Capacity)
		{
			Events[EventCount] = InEvent;
			++EventCount;
		}
	}

	/** Starts a fresh broadcast observation phase in the same test. */
	void Clear() noexcept { EventCount = 0; }

	/** Reports how many callbacks were publicly observed. */
	std::size_t Size() const noexcept { return EventCount; }

	/** Exposes one observed callback identity in broadcast order. */
	int At(const std::size_t InIndex) const noexcept { return Events[InIndex]; }

private:
	/** Retains only the bounded event sequence needed by the current test. */
	int Events[Capacity]{};

	/** Separates initialized observations from unused fixed capacity. */
	std::size_t EventCount{0};
};

/** Carries active-broadcast operation results outside the inline callback. */
struct FBroadcastMutationState final
{
	/** Selects the multicast whose active iteration must remain unchanged. */
	TMulticastDelegate<void(), LargeMulticastCapacity, LargeInlineBytes>* Multicast{nullptr};

	/** Supplies a binding whose rejected Add must retain ownership. */
	TDelegate<void(), LargeInlineBytes>* PendingBinding{nullptr};

	/** Identifies a live callback whose rejected Remove must leave it active. */
	FDelegateHandle HandleToRemove{};

	/** Records the attempted Add result from inside a callback. */
	EDelegateResult AddResult{EDelegateResult::InvalidHandle};

	/** Records the attempted Remove result from inside a callback. */
	EDelegateResult RemoveResult{EDelegateResult::InvalidHandle};

	/** Records the nested Broadcast result from inside a callback. */
	EDelegateResult NestedBroadcastResult{EDelegateResult::InvalidHandle};

	/** Captures binding count while all active-broadcast operations are rejected. */
	std::size_t BindingCountDuringCallback{0};

	/** Receives the handle only if an unexpected callback-time Add succeeds. */
	FDelegateHandle UnexpectedAddedHandle{};

	/** Shares the fresh bounded trace used to prove active iteration order. */
	TIntEventLog<8>* Events{nullptr};
};

/** Gives value-argument tests one mutable payload whose copies are distinguishable. */
struct FMutableValue final
{
	/** Carries the value each binding should receive independently. */
	int Value{0};
};

/** Models a value that cannot satisfy multicast's noexcept repeat-delivery contract. */
struct FPotentiallyThrowingCopyValue final
{
	/** Creates the unused compile-time contract probe. */
	FPotentiallyThrowingCopyValue() noexcept = default;

	/** Makes the copy operation observably incompatible with noexcept broadcast. */
	FPotentiallyThrowingCopyValue(const FPotentiallyThrowingCopyValue&) noexcept(false) {}
};

static_assert(std::is_nothrow_copy_constructible<FMutableValue>::value, "The multicast value fixture must preserve noexcept repeat delivery.");
static_assert(
	!std::is_nothrow_copy_constructible<FPotentiallyThrowingCopyValue>::value,
	"A potentially throwing copy must remain distinguishable from supported multicast values.");

/**
 * Scenario: Bind a tracked callable, move the delegate, execute it, then reset the destination twice.
 * Expected: Bind, move, execute, and reset transfer one callable lifetime exactly once, delivering the caller value once.
 */
MW_TEST_CASE(DelegateBindExecuteMoveAndResetOwnCallableExactlyOnce)
{
	// Arrange
	FCallableState State;
	FTrackedCallable Callable(State);
	TDelegate<void(int), StandardInlineBytes> SourceDelegate;

	// Act
	const EDelegateResult BindResult = SourceDelegate.Bind(std::move(Callable));
	const bool bSourceBoundAfterBind = SourceDelegate.IsBound();

	TDelegate<void(int), StandardInlineBytes> MovedDelegate(std::move(SourceDelegate));
	const bool bSourceBoundAfterMove = SourceDelegate.IsBound();
	const bool bSourceUnboundAfterMove = !bSourceBoundAfterMove;
	const bool bDestinationBoundAfterMove = MovedDelegate.IsBound();
	const EDelegateResult ExecuteResult = MovedDelegate.Execute(27);
	const std::size_t InvocationCount = State.InvocationCount;
	const int LastValue = State.LastValue;
	MovedDelegate.Reset();
	MovedDelegate.Reset();

	const bool bDestinationBoundAfterReset = MovedDelegate.IsBound();
	const bool bDestinationUnboundAfterReset = !bDestinationBoundAfterReset;
	const std::size_t OwnedDestructionCount = State.OwnedDestructionCount;

	// Assert
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindResult, "Supported callable should bind successfully");
	MW_EXPECT_TRUE(Test, bSourceBoundAfterBind, "Successful Bind should make the source delegate bound");
	MW_EXPECT_TRUE(Test, bSourceUnboundAfterMove, "Delegate move should leave the source unbound");
	MW_EXPECT_TRUE(Test, bDestinationBoundAfterMove, "Delegate move should transfer the binding");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, ExecuteResult, "Bound delegate should execute successfully");
	MW_EXPECT_EQ(Test, std::size_t{1}, InvocationCount, "Execute should invoke the bound callable exactly once");
	MW_EXPECT_EQ(Test, 27, LastValue, "Execute should deliver the caller-provided value");
	MW_EXPECT_TRUE(Test, bDestinationUnboundAfterReset, "Reset should restore the unbound state");
	MW_EXPECT_EQ(Test, std::size_t{1}, OwnedDestructionCount, "Repeated reset should destroy the stored callable exactly once");
}

/**
 * Scenario: Execute a freshly constructed unbound delegate.
 * Expected: Execute returns InvalidHandle without beginning callable behavior and leaves the delegate unbound.
 */
MW_TEST_CASE(UnboundDelegateExecuteReturnsInvalidHandle)
{
	// Arrange
	TDelegate<void(), SmallInlineBytes> Delegate;

	// Act
	const EDelegateResult ExecuteResult = Delegate.Execute();
	const bool bDelegateBound = Delegate.IsBound();
	const bool bDelegateUnbound = !bDelegateBound;

	// Assert
	MW_EXPECT_EQ(Test, EDelegateResult::InvalidHandle, ExecuteResult, "Unbound Execute should report invalid handle");
	MW_EXPECT_TRUE(Test, bDelegateUnbound, "Rejected unbound Execute should preserve unbound state");
}

/**
 * Scenario: Bind an oversized callable to a small delegate and an over-aligned callable to a large delegate.
 * Expected: Both unsupported layouts are rejected before stored construction, leaving the delegates unbound and recording no stored move.
 */
MW_TEST_CASE(DelegateRejectsUnsupportedCallableLayoutsBeforeConstruction)
{
	// Arrange
	FCallableState OversizedState;
	FOversizedCallable OversizedCallable(OversizedState);
	TDelegate<void(), SmallInlineBytes> SmallDelegate;

	FCallableState OverAlignedState;
	FOverAlignedCallable OverAlignedCallable(OverAlignedState);
	TDelegate<void(), LargeInlineBytes> AlignedDelegate;

	// Act
	const EDelegateResult OversizedResult = SmallDelegate.Bind(std::move(OversizedCallable));
	const bool bSmallDelegateBound = SmallDelegate.IsBound();
	const bool bSmallDelegateUnbound = !bSmallDelegateBound;
	const std::size_t OversizedMoveCount = OversizedState.MoveCount;

	const EDelegateResult OverAlignedResult = AlignedDelegate.Bind(std::move(OverAlignedCallable));
	const bool bAlignedDelegateBound = AlignedDelegate.IsBound();
	const bool bAlignedDelegateUnbound = !bAlignedDelegateBound;
	const std::size_t OverAlignedMoveCount = OverAlignedState.MoveCount;

	// Assert
	MW_EXPECT_EQ(Test, EDelegateResult::CallableTooLarge, OversizedResult, "Oversized callable should report inline-capacity failure");
	MW_EXPECT_TRUE(Test, bSmallDelegateUnbound, "Oversized rejection should preserve unbound state");
	MW_EXPECT_EQ(Test, std::size_t{0}, OversizedMoveCount, "Oversized rejection should occur before stored callable construction");
	MW_EXPECT_EQ(
		Test, EDelegateResult::CallableAlignmentUnsupported, OverAlignedResult, "Over-aligned callable should report inline-alignment failure");
	MW_EXPECT_TRUE(Test, bAlignedDelegateUnbound, "Over-aligned rejection should preserve unbound state");
	MW_EXPECT_EQ(Test, std::size_t{0}, OverAlignedMoveCount, "Over-aligned rejection should occur before stored callable construction");
}

/**
 * Scenario: Add two bindings to a multicast at capacity, attempt a capacity-plus-one add, then broadcast.
 * Expected: The excess add is rejected atomically, clears its handle and retains caller ownership; broadcast invokes the accepted bindings in
 * insertion order.
 */
MW_TEST_CASE(MulticastPreservesInsertionOrderAndRejectsCapacityPlusOne)
{
	// Arrange
	using FMulticast = TMulticastDelegate<void(), SmallMulticastCapacity, StandardInlineBytes>;
	FMulticast Multicast;
	TIntEventLog<4> Events;
	TDelegate<void(), StandardInlineBytes> FirstBinding;
	TDelegate<void(), StandardInlineBytes> SecondBinding;
	TDelegate<void(), StandardInlineBytes> ExcessBinding;
	const EDelegateResult FirstBindResult = FirstBinding.Bind([&Events]() noexcept { Events.Add(1); });
	const EDelegateResult SecondBindResult = SecondBinding.Bind([&Events]() noexcept { Events.Add(2); });
	const EDelegateResult ExcessBindResult = ExcessBinding.Bind([&Events]() noexcept { Events.Add(3); });
	FDelegateHandle FirstHandle{};
	FDelegateHandle SecondHandle{};
	FDelegateHandle ExcessHandle{};

	// Act
	const EDelegateResult FirstAddResult = Multicast.Add(std::move(FirstBinding), FirstHandle);
	const EDelegateResult SecondAddResult = Multicast.Add(std::move(SecondBinding), SecondHandle);
	const std::size_t CountAtCapacity = Multicast.BindingCount();
	const EDelegateResult ExcessAddResult = Multicast.Add(std::move(ExcessBinding), ExcessHandle);
	const std::size_t CountAfterExcess = Multicast.BindingCount();
	const bool bExcessHandleInvalid = !ExcessHandle.IsValid();
	const bool bExcessBindingRetained = ExcessBinding.IsBound();
	const EDelegateResult BroadcastResult = Multicast.Broadcast();
	const std::size_t EventCount = Events.Size();
	const int FirstEvent = Events.At(0);
	const int SecondEvent = Events.At(1);

	// Assert
	MW_EXPECT_EQ(Test, EDelegateResult::Success, FirstBindResult, "First multicast callable should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SecondBindResult, "Second multicast callable should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, ExcessBindResult, "Excess callable should bind before multicast capacity check");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, FirstAddResult, "First binding should add below capacity");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SecondAddResult, "Binding at exact capacity should add");
	MW_EXPECT_EQ(Test, std::size_t{2}, CountAtCapacity, "Binding count should reach exact capacity");
	MW_EXPECT_EQ(Test, EDelegateResult::CapacityExceeded, ExcessAddResult, "Capacity-plus-one Add should fail");
	MW_EXPECT_EQ(Test, std::size_t{2}, CountAfterExcess, "Rejected excess Add should preserve binding count");
	MW_EXPECT_TRUE(Test, bExcessHandleInvalid, "Rejected excess Add should clear its output handle");
	MW_EXPECT_TRUE(Test, bExcessBindingRetained, "Rejected excess Add should retain caller binding ownership");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BroadcastResult, "Full multicast should broadcast successfully");
	MW_EXPECT_EQ(Test, std::size_t{2}, EventCount, "Broadcast should invoke exactly the accepted bindings");
	MW_EXPECT_EQ(Test, 1, FirstEvent, "Broadcast should invoke first insertion first");
	MW_EXPECT_EQ(Test, 2, SecondEvent, "Broadcast should invoke second insertion second");
}

/**
 * Scenario: Remove a binding, add a replacement that reuses the freed slot, attempt a stale removal of the retired handle, then broadcast.
 * Expected: The reused slot publishes a new generation so the stale handle is rejected; the new binding stays and broadcasts at its later insertion
 * position.
 */
MW_TEST_CASE(MulticastReusedSlotRejectsStaleHandleAndKeepsNewBinding)
{
	// Arrange
	using FMulticast = TMulticastDelegate<void(), SmallMulticastCapacity, StandardInlineBytes>;
	FMulticast Multicast;
	TIntEventLog<4> Events;
	TDelegate<void(), StandardInlineBytes> FirstBinding;
	TDelegate<void(), StandardInlineBytes> SecondBinding;
	TDelegate<void(), StandardInlineBytes> ReusedBinding;
	const EDelegateResult FirstBindResult = FirstBinding.Bind([&Events]() noexcept { Events.Add(1); });
	const EDelegateResult SecondBindResult = SecondBinding.Bind([&Events]() noexcept { Events.Add(2); });
	const EDelegateResult ReusedBindResult = ReusedBinding.Bind([&Events]() noexcept { Events.Add(3); });
	FDelegateHandle FirstHandle{};
	FDelegateHandle SecondHandle{};
	FDelegateHandle ReusedHandle{};
	const EDelegateResult FirstAddResult = Multicast.Add(std::move(FirstBinding), FirstHandle);
	const EDelegateResult SecondAddResult = Multicast.Add(std::move(SecondBinding), SecondHandle);

	// Act
	const EDelegateResult RemoveFirstResult = Multicast.Remove(FirstHandle);
	const std::size_t CountAfterRemove = Multicast.BindingCount();
	const EDelegateResult ReusedAddResult = Multicast.Add(std::move(ReusedBinding), ReusedHandle);
	const bool bSlotIndexReused = ReusedHandle.Index == FirstHandle.Index;
	const bool bGenerationChanged = ReusedHandle.Generation != FirstHandle.Generation;
	const EDelegateResult StaleRemoveResult = Multicast.Remove(FirstHandle);
	const std::size_t CountAfterStaleRemove = Multicast.BindingCount();
	const EDelegateResult BroadcastResult = Multicast.Broadcast();
	const std::size_t EventCount = Events.Size();
	const int FirstEvent = Events.At(0);
	const int SecondEvent = Events.At(1);

	// Assert
	MW_EXPECT_EQ(Test, EDelegateResult::Success, FirstBindResult, "First stale-handle callable should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SecondBindResult, "Second stale-handle callable should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, ReusedBindResult, "Replacement stale-handle callable should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, FirstAddResult, "First stale-handle binding should add");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SecondAddResult, "Second stale-handle binding should add");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, RemoveFirstResult, "Current handle should remove its binding");
	MW_EXPECT_EQ(Test, std::size_t{1}, CountAfterRemove, "Successful remove should reduce binding count");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, ReusedAddResult, "Freed slot should accept a later binding");
	MW_EXPECT_TRUE(Test, bSlotIndexReused, "Later binding should reuse the lowest free slot");
	MW_EXPECT_TRUE(Test, bGenerationChanged, "Reused slot should publish a new generation");
	MW_EXPECT_EQ(Test, EDelegateResult::StaleHandle, StaleRemoveResult, "Old generation should be rejected as stale");
	MW_EXPECT_EQ(Test, std::size_t{2}, CountAfterStaleRemove, "Stale removal should preserve the new binding count");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BroadcastResult, "Bindings should remain broadcastable after stale removal");
	MW_EXPECT_EQ(Test, std::size_t{2}, EventCount, "Broadcast should invoke both remaining bindings");
	MW_EXPECT_EQ(Test, 2, FirstEvent, "Existing binding should keep its earlier insertion order");
	MW_EXPECT_EQ(Test, 3, SecondEvent, "Reused-slot binding should execute at its later insertion position");
}

/**
 * Scenario: During an active broadcast, have a callback attempt Add, Remove, and nested Broadcast, then add, remove, and broadcast again afterward.
 * Expected: Callback mutation and reentry are reported as broadcast-locked and leave the active order and count unchanged; the same operations
 * succeed after the broadcast ends.
 */
MW_TEST_CASE(MulticastRejectsMutationAndNestedBroadcastDuringActiveBroadcast)
{
	// Arrange
	using FMulticast = TMulticastDelegate<void(), LargeMulticastCapacity, LargeInlineBytes>;
	FMulticast Multicast;
	TIntEventLog<8> Events;
	TDelegate<void(), LargeInlineBytes> PendingBinding;
	TDelegate<void(), LargeInlineBytes> MutatingBinding;
	TDelegate<void(), LargeInlineBytes> MiddleBinding;
	TDelegate<void(), LargeInlineBytes> RemovalTargetBinding;
	const EDelegateResult PendingBindResult = PendingBinding.Bind([&Events]() noexcept { Events.Add(4); });
	FBroadcastMutationState MutationState;
	MutationState.Multicast = &Multicast;
	MutationState.PendingBinding = &PendingBinding;
	MutationState.Events = &Events;
	const EDelegateResult MutatingBindResult = MutatingBinding.Bind(
		[&MutationState]() noexcept
		{
			MutationState.Events->Add(1);
			MutationState.AddResult = MutationState.Multicast->Add(std::move(*MutationState.PendingBinding), MutationState.UnexpectedAddedHandle);
			MutationState.RemoveResult = MutationState.Multicast->Remove(MutationState.HandleToRemove);
			MutationState.NestedBroadcastResult = MutationState.Multicast->Broadcast();
			MutationState.BindingCountDuringCallback = MutationState.Multicast->BindingCount();
		});
	const EDelegateResult MiddleBindResult = MiddleBinding.Bind([&Events]() noexcept { Events.Add(2); });
	const EDelegateResult TargetBindResult = RemovalTargetBinding.Bind([&Events]() noexcept { Events.Add(3); });
	FDelegateHandle MutatingHandle{};
	FDelegateHandle MiddleHandle{};
	FDelegateHandle RemovalTargetHandle{};
	const EDelegateResult MutatingAddResult = Multicast.Add(std::move(MutatingBinding), MutatingHandle);
	const EDelegateResult MiddleAddResult = Multicast.Add(std::move(MiddleBinding), MiddleHandle);
	const EDelegateResult TargetAddResult = Multicast.Add(std::move(RemovalTargetBinding), RemovalTargetHandle);
	MutationState.HandleToRemove = RemovalTargetHandle;

	// Act
	const EDelegateResult BroadcastResult = Multicast.Broadcast();
	const std::size_t CountAfterBroadcast = Multicast.BindingCount();
	const bool bPendingBindingRetained = PendingBinding.IsBound();
	const std::size_t FirstBroadcastEventCount = Events.Size();
	const int FirstBroadcastFirstEvent = Events.At(0);
	const int FirstBroadcastSecondEvent = Events.At(1);
	const int FirstBroadcastThirdEvent = Events.At(2);
	const EDelegateResult CallbackAddResult = MutationState.AddResult;
	const EDelegateResult CallbackRemoveResult = MutationState.RemoveResult;
	const EDelegateResult CallbackNestedBroadcastResult = MutationState.NestedBroadcastResult;
	const std::size_t CallbackBindingCount = MutationState.BindingCountDuringCallback;

	// Assert
	MW_EXPECT_EQ(Test, EDelegateResult::Success, PendingBindResult, "Pending callback-time Add binding should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, MutatingBindResult, "Mutation callback should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, MiddleBindResult, "Middle callback should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, TargetBindResult, "Removal target callback should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, MutatingAddResult, "Mutation callback should add");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, MiddleAddResult, "Middle callback should add");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, TargetAddResult, "Removal target callback should add");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BroadcastResult, "Outer broadcast should complete after rejecting callback operations");
	MW_EXPECT_EQ(Test, EDelegateResult::BroadcastLocked, CallbackAddResult, "Callback Add should report broadcast locked");
	MW_EXPECT_EQ(Test, EDelegateResult::BroadcastLocked, CallbackRemoveResult, "Callback Remove should report broadcast locked");
	MW_EXPECT_EQ(Test, EDelegateResult::BroadcastLocked, CallbackNestedBroadcastResult, "Nested callback Broadcast should report broadcast locked");
	MW_EXPECT_EQ(Test, std::size_t{3}, CallbackBindingCount, "Rejected callback operations should preserve active count");
	MW_EXPECT_EQ(Test, std::size_t{3}, CountAfterBroadcast, "Rejected callback operations should preserve post-broadcast count");
	MW_EXPECT_TRUE(Test, bPendingBindingRetained, "Rejected callback Add should retain caller binding ownership");
	MW_EXPECT_EQ(Test, std::size_t{3}, FirstBroadcastEventCount, "Active broadcast should invoke each original binding once");
	MW_EXPECT_EQ(Test, 1, FirstBroadcastFirstEvent, "Active broadcast should begin with the mutating callback");
	MW_EXPECT_EQ(Test, 2, FirstBroadcastSecondEvent, "Rejected mutation should not change middle callback order");
	MW_EXPECT_EQ(Test, 3, FirstBroadcastThirdEvent, "Rejected removal should not skip its active callback");

	// Act
	FDelegateHandle AddedAfterBroadcastHandle{};
	const EDelegateResult AddAfterBroadcastResult = Multicast.Add(std::move(PendingBinding), AddedAfterBroadcastHandle);
	const EDelegateResult RemoveAfterBroadcastResult = Multicast.Remove(RemovalTargetHandle);
	const std::size_t CountAfterUnlockedChanges = Multicast.BindingCount();
	Events.Clear();
	const EDelegateResult SecondBroadcastResult = Multicast.Broadcast();
	const std::size_t SecondBroadcastEventCount = Events.Size();
	const int SecondBroadcastFirstEvent = Events.At(0);
	const int SecondBroadcastSecondEvent = Events.At(1);
	const int SecondBroadcastThirdEvent = Events.At(2);

	// Assert
	MW_EXPECT_EQ(Test, EDelegateResult::Success, AddAfterBroadcastResult, "Add should succeed after active broadcast ends");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, RemoveAfterBroadcastResult, "Remove should succeed after active broadcast ends");
	MW_EXPECT_EQ(Test, std::size_t{3}, CountAfterUnlockedChanges, "Post-broadcast Add and Remove should preserve expected count");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SecondBroadcastResult, "Multicast should remain usable after unlocked changes");
	MW_EXPECT_EQ(Test, std::size_t{3}, SecondBroadcastEventCount, "Later broadcast should visit the updated binding set");
	MW_EXPECT_EQ(Test, 1, SecondBroadcastFirstEvent, "Existing first binding should retain insertion order");
	MW_EXPECT_EQ(Test, 2, SecondBroadcastSecondEvent, "Existing middle binding should retain insertion order");
	MW_EXPECT_EQ(Test, 4, SecondBroadcastThirdEvent, "Post-broadcast Add should execute at the end");
}

/**
 * Scenario: Broadcast a value argument to two bindings where the first mutates its received copy.
 * Expected: Each binding receives an independent copy of the argument and the caller's original value is not mutated.
 */
MW_TEST_CASE(MulticastCopiesValueArgumentForEveryBinding)
{
	// Arrange
	TMulticastDelegate<void(FMutableValue), SmallMulticastCapacity, StandardInlineBytes> Multicast;
	int FirstObservedValue = 0;
	int SecondObservedValue = 0;
	TDelegate<void(FMutableValue), StandardInlineBytes> FirstBinding;
	TDelegate<void(FMutableValue), StandardInlineBytes> SecondBinding;
	const EDelegateResult FirstBindResult = FirstBinding.Bind(
		[&FirstObservedValue](FMutableValue InValue) noexcept
		{
			FirstObservedValue = InValue.Value;
			InValue.Value = 99;
		});
	const EDelegateResult SecondBindResult =
		SecondBinding.Bind([&SecondObservedValue](FMutableValue InValue) noexcept { SecondObservedValue = InValue.Value; });
	FDelegateHandle FirstHandle{};
	FDelegateHandle SecondHandle{};
	const EDelegateResult FirstAddResult = Multicast.Add(std::move(FirstBinding), FirstHandle);
	const EDelegateResult SecondAddResult = Multicast.Add(std::move(SecondBinding), SecondHandle);
	const FMutableValue OriginalValue{42};

	// Act
	const EDelegateResult BroadcastResult = Multicast.Broadcast(OriginalValue);
	const int OriginalValueAfterBroadcast = OriginalValue.Value;

	// Assert
	MW_EXPECT_EQ(Test, EDelegateResult::Success, FirstBindResult, "First value callback should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SecondBindResult, "Second value callback should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, FirstAddResult, "First value callback should add");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SecondAddResult, "Second value callback should add");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BroadcastResult, "Value multicast should broadcast successfully");
	MW_EXPECT_EQ(Test, 42, FirstObservedValue, "First binding should receive the caller value");
	MW_EXPECT_EQ(Test, 42, SecondObservedValue, "Second binding should receive an independent unmodified copy");
	MW_EXPECT_EQ(Test, 42, OriginalValueAfterBroadcast, "Broadcast should not mutate the caller's value argument");
}

/**
 * Scenario: Attempt Add on a zero-capacity multicast with a bound callback, then Broadcast and attempt Remove.
 * Expected: Add is rejected while retaining caller ownership and clearing the handle; empty Broadcast succeeds with no invocation, and Remove returns
 * InvalidHandle.
 */
MW_TEST_CASE(ZeroCapacityMulticastRejectsAddAndBroadcastsEmptySet)
{
	// Arrange
	TMulticastDelegate<void(), ZeroMulticastCapacity, SmallInlineBytes> Multicast;
	std::size_t InvocationCount = 0;
	TDelegate<void(), SmallInlineBytes> Binding;
	const EDelegateResult BindResult = Binding.Bind([&InvocationCount]() noexcept { ++InvocationCount; });
	FDelegateHandle Handle{};

	// Act
	const EDelegateResult AddResult = Multicast.Add(std::move(Binding), Handle);
	const std::size_t BindingCountAfterAdd = Multicast.BindingCount();
	const bool bHandleInvalid = !Handle.IsValid();
	const bool bBindingRetained = Binding.IsBound();
	const EDelegateResult BroadcastResult = Multicast.Broadcast();
	const std::size_t InvocationCountAfterBroadcast = InvocationCount;
	const EDelegateResult RemoveResult = Multicast.Remove(Handle);

	// Assert
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindResult, "Zero-capacity source callable should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::CapacityExceeded, AddResult, "Zero-capacity multicast should reject its first Add");
	MW_EXPECT_EQ(Test, std::size_t{0}, BindingCountAfterAdd, "Rejected zero-capacity Add should preserve count zero");
	MW_EXPECT_TRUE(Test, bHandleInvalid, "Rejected zero-capacity Add should return an invalid handle");
	MW_EXPECT_TRUE(Test, bBindingRetained, "Rejected zero-capacity Add should retain caller binding ownership");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BroadcastResult, "Empty zero-capacity multicast should broadcast successfully");
	MW_EXPECT_EQ(Test, std::size_t{0}, InvocationCountAfterBroadcast, "Empty broadcast should invoke no callback");
	MW_EXPECT_EQ(Test, EDelegateResult::InvalidHandle, RemoveResult, "Invalid zero-capacity handle should be rejected");
}

} // namespace
