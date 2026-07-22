#include "TestSupport.h"

#include <MicroWorld/Delegates/Delegate.h>

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace
{

using MicroWorld::EDelegateResult;
using MicroWorld::FDelegateHandle;
using MicroWorld::TDelegate;
using MicroWorld::TMulticastDelegate;

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
	void operator()(const int Value) noexcept
	{
		++State->InvocationCount;
		State->LastValue = Value;
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
	std::array<std::byte, 128> Payload{};

	/** Records any unexpected attempt to construct a stored callable by moving. */
	FOversizedCallable(FOversizedCallable&& Other) noexcept : State(Other.State), Payload(Other.Payload) { ++State->MoveCount; }

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
	void Add(const int Event) noexcept
	{
		if (EventCount < Capacity)
		{
			Events[EventCount] = Event;
			++EventCount;
		}
	}

	/** Starts a fresh broadcast observation phase in the same test. */
	void Clear() noexcept { EventCount = 0; }

	/** Reports how many callbacks were publicly observed. */
	std::size_t Size() const noexcept { return EventCount; }

	/** Exposes one observed callback identity in broadcast order. */
	int At(const std::size_t Index) const noexcept { return Events[Index]; }

private:
	/** Retains only the bounded event sequence needed by the current test. */
	std::array<int, Capacity> Events{};

	/** Separates initialized observations from unused fixed capacity. */
	std::size_t EventCount{0};
};

/** Carries active-broadcast operation results outside the inline callback. */
struct FBroadcastMutationState final
{
	/** Selects the multicast whose active iteration must remain unchanged. */
	TMulticastDelegate<void(), 4, 128>* Multicast{nullptr};

	/** Supplies a binding whose rejected Add must retain ownership. */
	TDelegate<void(), 128>* PendingBinding{nullptr};

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

/** Proves bind, execute, move, reset, and destruction transfer one callable lifetime exactly once. */
MW_TEST_CASE(DelegateBindExecuteMoveAndResetOwnCallableExactlyOnce)
{
	FCallableState State;
	FTrackedCallable Callable(State);
	TDelegate<void(int), 64> SourceDelegate;
	const EDelegateResult BindResult = SourceDelegate.Bind(std::move(Callable));
	const bool bSourceBoundAfterBind = SourceDelegate.IsBound();

	TDelegate<void(int), 64> MovedDelegate(std::move(SourceDelegate));
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

/** Proves unbound delegates reject execution without beginning callable behavior. */
MW_TEST_CASE(UnboundDelegateExecuteReturnsInvalidHandle)
{
	TDelegate<void(), 32> Delegate;

	const EDelegateResult ExecuteResult = Delegate.Execute();

	const bool bDelegateBound = Delegate.IsBound();
	const bool bDelegateUnbound = !bDelegateBound;
	MW_EXPECT_EQ(Test, EDelegateResult::InvalidHandle, ExecuteResult, "Unbound Execute should report invalid handle");
	MW_EXPECT_TRUE(Test, bDelegateUnbound, "Rejected unbound Execute should preserve unbound state");
}

/** Proves oversized and over-aligned callables are rejected before stored construction. */
MW_TEST_CASE(DelegateRejectsUnsupportedCallableLayoutsBeforeConstruction)
{
	FCallableState OversizedState;
	FOversizedCallable OversizedCallable(OversizedState);
	TDelegate<void(), 32> SmallDelegate;
	const EDelegateResult OversizedResult = SmallDelegate.Bind(std::move(OversizedCallable));
	const bool bSmallDelegateBound = SmallDelegate.IsBound();
	const bool bSmallDelegateUnbound = !bSmallDelegateBound;
	const std::size_t OversizedMoveCount = OversizedState.MoveCount;

	FCallableState OverAlignedState;
	FOverAlignedCallable OverAlignedCallable(OverAlignedState);
	TDelegate<void(), 128> AlignedDelegate;
	const EDelegateResult OverAlignedResult = AlignedDelegate.Bind(std::move(OverAlignedCallable));
	const bool bAlignedDelegateBound = AlignedDelegate.IsBound();
	const bool bAlignedDelegateUnbound = !bAlignedDelegateBound;
	const std::size_t OverAlignedMoveCount = OverAlignedState.MoveCount;

	MW_EXPECT_EQ(Test, EDelegateResult::CallableTooLarge, OversizedResult, "Oversized callable should report inline-capacity failure");
	MW_EXPECT_TRUE(Test, bSmallDelegateUnbound, "Oversized rejection should preserve unbound state");
	MW_EXPECT_EQ(Test, std::size_t{0}, OversizedMoveCount, "Oversized rejection should occur before stored callable construction");
	MW_EXPECT_EQ(
		Test, EDelegateResult::CallableAlignmentUnsupported, OverAlignedResult, "Over-aligned callable should report inline-alignment failure");
	MW_EXPECT_TRUE(Test, bAlignedDelegateUnbound, "Over-aligned rejection should preserve unbound state");
	MW_EXPECT_EQ(Test, std::size_t{0}, OverAlignedMoveCount, "Over-aligned rejection should occur before stored callable construction");
}

/** Proves multicast exact capacity preserves insertion order and rejects capacity plus one atomically. */
MW_TEST_CASE(MulticastPreservesInsertionOrderAndRejectsCapacityPlusOne)
{
	using FMulticast = TMulticastDelegate<void(), 2, 64>;
	FMulticast Multicast;
	TIntEventLog<4> Events;
	TDelegate<void(), 64> FirstBinding;
	TDelegate<void(), 64> SecondBinding;
	TDelegate<void(), 64> ExcessBinding;
	const EDelegateResult FirstBindResult = FirstBinding.Bind([&Events]() noexcept { Events.Add(1); });
	const EDelegateResult SecondBindResult = SecondBinding.Bind([&Events]() noexcept { Events.Add(2); });
	const EDelegateResult ExcessBindResult = ExcessBinding.Bind([&Events]() noexcept { Events.Add(3); });
	FDelegateHandle FirstHandle{};
	FDelegateHandle SecondHandle{};
	FDelegateHandle ExcessHandle{};

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

/** Proves slot reuse changes generation so stale removal cannot affect the new binding. */
MW_TEST_CASE(MulticastReusedSlotRejectsStaleHandleAndKeepsNewBinding)
{
	using FMulticast = TMulticastDelegate<void(), 2, 64>;
	FMulticast Multicast;
	TIntEventLog<4> Events;
	TDelegate<void(), 64> FirstBinding;
	TDelegate<void(), 64> SecondBinding;
	TDelegate<void(), 64> ReusedBinding;
	const EDelegateResult FirstBindResult = FirstBinding.Bind([&Events]() noexcept { Events.Add(1); });
	const EDelegateResult SecondBindResult = SecondBinding.Bind([&Events]() noexcept { Events.Add(2); });
	const EDelegateResult ReusedBindResult = ReusedBinding.Bind([&Events]() noexcept { Events.Add(3); });
	FDelegateHandle FirstHandle{};
	FDelegateHandle SecondHandle{};
	FDelegateHandle ReusedHandle{};
	const EDelegateResult FirstAddResult = Multicast.Add(std::move(FirstBinding), FirstHandle);
	const EDelegateResult SecondAddResult = Multicast.Add(std::move(SecondBinding), SecondHandle);

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

/** Proves callback mutation and reentry are locked without changing active order or count. */
MW_TEST_CASE(MulticastRejectsMutationAndNestedBroadcastDuringActiveBroadcast)
{
	using FMulticast = TMulticastDelegate<void(), 4, 128>;
	FMulticast Multicast;
	TIntEventLog<8> Events;
	TDelegate<void(), 128> PendingBinding;
	TDelegate<void(), 128> MutatingBinding;
	TDelegate<void(), 128> MiddleBinding;
	TDelegate<void(), 128> RemovalTargetBinding;
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
	MW_EXPECT_EQ(Test, EDelegateResult::Success, AddAfterBroadcastResult, "Add should succeed after active broadcast ends");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, RemoveAfterBroadcastResult, "Remove should succeed after active broadcast ends");
	MW_EXPECT_EQ(Test, std::size_t{3}, CountAfterUnlockedChanges, "Post-broadcast Add and Remove should preserve expected count");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SecondBroadcastResult, "Multicast should remain usable after unlocked changes");
	MW_EXPECT_EQ(Test, std::size_t{3}, SecondBroadcastEventCount, "Later broadcast should visit the updated binding set");
	MW_EXPECT_EQ(Test, 1, SecondBroadcastFirstEvent, "Existing first binding should retain insertion order");
	MW_EXPECT_EQ(Test, 2, SecondBroadcastSecondEvent, "Existing middle binding should retain insertion order");
	MW_EXPECT_EQ(Test, 4, SecondBroadcastThirdEvent, "Post-broadcast Add should execute at the end");
}

/** Proves each multicast value binding receives an independent copy of the argument. */
MW_TEST_CASE(MulticastCopiesValueArgumentForEveryBinding)
{
	TMulticastDelegate<void(FMutableValue), 2, 64> Multicast;
	int FirstObservedValue = 0;
	int SecondObservedValue = 0;
	TDelegate<void(FMutableValue), 64> FirstBinding;
	TDelegate<void(FMutableValue), 64> SecondBinding;
	const EDelegateResult FirstBindResult = FirstBinding.Bind(
		[&FirstObservedValue](FMutableValue Value) noexcept
		{
			FirstObservedValue = Value.Value;
			Value.Value = 99;
		});
	const EDelegateResult SecondBindResult =
		SecondBinding.Bind([&SecondObservedValue](FMutableValue Value) noexcept { SecondObservedValue = Value.Value; });
	FDelegateHandle FirstHandle{};
	FDelegateHandle SecondHandle{};
	const EDelegateResult FirstAddResult = Multicast.Add(std::move(FirstBinding), FirstHandle);
	const EDelegateResult SecondAddResult = Multicast.Add(std::move(SecondBinding), SecondHandle);
	const FMutableValue OriginalValue{42};

	const EDelegateResult BroadcastResult = Multicast.Broadcast(OriginalValue);

	const int OriginalValueAfterBroadcast = OriginalValue.Value;
	MW_EXPECT_EQ(Test, EDelegateResult::Success, FirstBindResult, "First value callback should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SecondBindResult, "Second value callback should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, FirstAddResult, "First value callback should add");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SecondAddResult, "Second value callback should add");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BroadcastResult, "Value multicast should broadcast successfully");
	MW_EXPECT_EQ(Test, 42, FirstObservedValue, "First binding should receive the caller value");
	MW_EXPECT_EQ(Test, 42, SecondObservedValue, "Second binding should receive an independent unmodified copy");
	MW_EXPECT_EQ(Test, 42, OriginalValueAfterBroadcast, "Broadcast should not mutate the caller's value argument");
}

/** Proves zero-capacity multicast rejects Add while empty Broadcast remains valid. */
MW_TEST_CASE(ZeroCapacityMulticastRejectsAddAndBroadcastsEmptySet)
{
	TMulticastDelegate<void(), 0, 32> Multicast;
	std::size_t InvocationCount = 0;
	TDelegate<void(), 32> Binding;
	const EDelegateResult BindResult = Binding.Bind([&InvocationCount]() noexcept { ++InvocationCount; });
	FDelegateHandle Handle{};

	const EDelegateResult AddResult = Multicast.Add(std::move(Binding), Handle);
	const std::size_t BindingCountAfterAdd = Multicast.BindingCount();
	const bool bHandleInvalid = !Handle.IsValid();
	const bool bBindingRetained = Binding.IsBound();
	const EDelegateResult BroadcastResult = Multicast.Broadcast();
	const std::size_t InvocationCountAfterBroadcast = InvocationCount;
	const EDelegateResult RemoveResult = Multicast.Remove(Handle);

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
