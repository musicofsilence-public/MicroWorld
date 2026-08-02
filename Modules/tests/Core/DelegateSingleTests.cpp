#include "TestSupport.h"
#include "DelegateTestHelpers.h"

#include <MicroWorld/Core/Delegates/MulticastDelegate.h>

#include <cstddef>
#include <utility>

namespace
{

using namespace ::MicroWorld::Tests;

/**
 * Motivation: Bind a tracked callable, move the delegate, execute it, then reset the destination twice.
 * Responsibilities: Bind, move, execute, and reset transfer one callable lifetime exactly once, delivering the caller
 *   value once.
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
 * Motivation: Execute a freshly constructed unbound delegate.
 * Responsibilities: Execute returns InvalidHandle without beginning callable behavior and leaves the delegate unbound.
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
 * Motivation: Bind an oversized callable to a small delegate and an over-aligned callable to a large delegate.
 * Responsibilities: Both unsupported layouts are rejected before stored construction, leaving the delegates unbound and
 *   recording no stored move.
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

} // namespace
