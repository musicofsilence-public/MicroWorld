#include "TestSupport.h"
#include "DelegateTestHelpers.h"

#include <MicroWorld/Core/Delegates/MulticastDelegate.h>

#include <cstddef>
#include <utility>

namespace
{

using namespace ::MicroWorld::Tests;

/**
 * Motivation: Add two bindings to a multicast at capacity, attempt a capacity-plus-one add, then broadcast.
 * Responsibilities: The excess add is rejected atomically, clears its handle and retains caller ownership; broadcast
 *   invokes the accepted bindings in.
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
 * Motivation: Remove a binding, add a replacement that reuses the freed slot, attempt a stale removal of the
 *   retired handle, then broadcast.
 * Responsibilities: The reused slot publishes a new generation so the stale handle is rejected; the new binding stays
 *   and broadcasts at its later insertion.
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
 * Motivation: During an active broadcast, have a callback attempt Add, Remove, and nested Broadcast, then add,
 *   remove, and broadcast again afterward.
 * Responsibilities: Callback mutation and reentry are reported as broadcast-locked and leave the active order and count
 *   unchanged; the same operations.
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
 * Motivation: Broadcast a value argument to two bindings where the first mutates its received copy.
 * Responsibilities: Each binding receives an independent copy of the argument and the caller's original value is not
 *   mutated.
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
 * Motivation: Attempt Add on a zero-capacity multicast with a bound callback, then Broadcast and attempt Remove.
 * Responsibilities: Add is rejected while retaining caller ownership and clearing the handle; empty Broadcast succeeds
 *   with no invocation, and Remove returns.
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
