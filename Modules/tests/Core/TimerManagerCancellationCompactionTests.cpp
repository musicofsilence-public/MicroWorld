#include "TestSupport.h"
#include "TimerManagerTestHelpers.h"

#include <MicroWorld/Core/TimerManager.h>

#include <cstddef>

namespace
{

using namespace ::MicroWorld::Tests;

// ---------------------------------------------------------------------------
// Category 3: Cancellation
// ---------------------------------------------------------------------------

/**
 * Motivation: Schedule a one-shot timer, cancel it before its deadline, then advance well past the deadline.
 * Responsibilities: Cancellation before the deadline releases the slot and prevents any invocation.
 */
MW_TEST_CASE(EngineTimerCancellationBeforeDuePreventsInvocation)
{
	// Arrange
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	// Act
	// Assert
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeCounterCallback(Counter), StandardTimerPeriod, ETimerMode::OneShot, Handle), "Schedule should succeed");
	MW_EXPECT_EQ(Test, 1u, Manager.TimerCount(), "One schedule should occupy one slot");

	// Act
	const ETimerResult CancelResult = Manager.Cancel(Handle);

	// Assert
	MW_EXPECT_SUCCESS(Test, CancelResult, "Cancellation of an active timer should succeed");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "Cancellation should release the slot");

	// Act
	// Assert
	MW_EXPECT_SUCCESS(Test, Manager.Advance(200), "Advance well past the original deadline should succeed");
	MW_EXPECT_EQ(Test, 0u, Counter.Count, "A canceled timer must not fire");
}

/**
 * Motivation: Schedule a one-shot timer and then cancel it.
 * Responsibilities: Successful cancellation reduces observable occupancy to zero.
 */
MW_TEST_CASE(EngineTimerCancellationReducesOccupancy)
{
	// Arrange
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	// Act
	// Assert
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeCounterCallback(Counter), StandardTimerPeriod, ETimerMode::OneShot, Handle), "Schedule should succeed");
	MW_EXPECT_EQ(Test, 1u, Manager.TimerCount(), "One schedule should occupy one slot");

	// Act
	// Assert
	MW_EXPECT_SUCCESS(Test, Manager.Cancel(Handle), "Cancellation should succeed");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "Cancellation should release the slot");
}

/**
 * Motivation: Schedule a one-shot timer, cancel it once, then cancel the same handle again.
 * Responsibilities: Repeated cancellation of the same handle returns StaleHandle.
 */
MW_TEST_CASE(EngineTimerRepeatedCancellationReturnsStaleHandle)
{
	// Arrange
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	// Act
	// Assert
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeCounterCallback(Counter), StandardTimerPeriod, ETimerMode::OneShot, Handle), "Schedule should succeed");

	// Act
	const ETimerResult FirstCancel = Manager.Cancel(Handle);

	// Assert
	MW_EXPECT_SUCCESS(Test, FirstCancel, "The first cancellation should succeed");

	// Act
	const ETimerResult SecondCancel = Manager.Cancel(Handle);

	// Assert
	MW_EXPECT_EQ(Test, ETimerResult::StaleHandle, SecondCancel, "A repeated cancellation must return StaleHandle");
}

/**
 * Motivation: Schedule a one-shot timer, fire it to completion, then attempt to cancel its handle.
 * Responsibilities: Cancellation after one-shot completion returns StaleHandle.
 */
MW_TEST_CASE(EngineTimerCancellationAfterCompletionReturnsStaleHandle)
{
	// Arrange
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	// Act
	// Assert
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeCounterCallback(Counter), StandardTimerPeriod, ETimerMode::OneShot, Handle), "Schedule should succeed");
	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance to the deadline should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "The one-shot should have fired exactly once");

	// Act
	const ETimerResult CancelResult = Manager.Cancel(Handle);

	// Assert
	MW_EXPECT_EQ(Test, ETimerResult::StaleHandle, CancelResult, "Canceling a completed one-shot must return StaleHandle");
}

// ---------------------------------------------------------------------------
// Category 7: Deterministic ordering
// ---------------------------------------------------------------------------

/**
 * Motivation: Schedule three one-shot timers sharing one deadline and advance at that deadline.
 * Responsibilities: Simultaneously due timers fire in insertion order rather than slot order and are removed after
 *   firing.
 */
MW_TEST_CASE(EngineTimerSimultaneouslyDueTimersFireInInsertionOrder)
{
	// Arrange
	FDispatchOrderRecorder Recorder;
	FTestManager Manager{0};
	FTimerHandle HandleA{};
	FTimerHandle HandleB{};
	FTimerHandle HandleC{};

	// Act
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 1), StandardTimerPeriod, ETimerMode::OneShot, HandleA), "A should schedule");
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 2), StandardTimerPeriod, ETimerMode::OneShot, HandleB), "B should schedule");
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 3), StandardTimerPeriod, ETimerMode::OneShot, HandleC), "C should schedule");

	// Act
	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance at the shared deadline should succeed");

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{3}, Recorder.Count, "All three due timers should fire");
	MW_EXPECT_EQ(Test, std::size_t{3}, Recorder.Count, "All three due timers should fire");
	MW_EXPECT_EQ(Test, 1, Recorder.Identities[0], "The first-inserted timer should fire first");
	MW_EXPECT_EQ(Test, 2, Recorder.Identities[1], "The second-inserted timer should fire second");
	MW_EXPECT_EQ(Test, 3, Recorder.Identities[2], "The third-inserted timer should fire third");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "All three one-shots must be removed after firing");
}

/**
 * Motivation: Schedule three timers, cancel the middle one, schedule a replacement into the freed slot, then
 *   advance at the shared deadline.
 * Responsibilities: The reused slot's replacement dispatches at the insertion tail rather than in the canceled slot's
 *   position.
 */
MW_TEST_CASE(EngineTimerCancelReuseAppendsReplacementAtInsertionTail)
{
	// Arrange
	FDispatchOrderRecorder Recorder;
	FTestManager Manager{0};
	FTimerHandle HandleA{};
	FTimerHandle HandleB{};
	FTimerHandle HandleC{};
	FTimerHandle HandleD{};

	// Act
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 1), StandardTimerPeriod, ETimerMode::OneShot, HandleA), "A should schedule");
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 2), StandardTimerPeriod, ETimerMode::OneShot, HandleB), "B should schedule");
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 3), StandardTimerPeriod, ETimerMode::OneShot, HandleC), "C should schedule");
	MW_EXPECT_SUCCESS(Test, Manager.Cancel(HandleB), "Canceling B should succeed");

	// The replacement reuses the freed slot but must dispatch after C, not between A and C.
	MW_EXPECT_SUCCESS(
		Test,
		Manager.Schedule(MakeOrderCallback(Recorder, 4), StandardTimerPeriod, ETimerMode::OneShot, HandleD),
		"D should schedule as replacement");

	// Act
	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance at the shared deadline should succeed");

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{3}, Recorder.Count, "Three timers should fire after the cancel/reuse");
	MW_EXPECT_EQ(Test, 1, Recorder.Identities[0], "A remains first in insertion order");
	MW_EXPECT_EQ(Test, 3, Recorder.Identities[1], "C retains its insertion position ahead of the reused slot");
	MW_EXPECT_EQ(Test, 4, Recorder.Identities[2], "The reused slot's replacement dispatches at the insertion tail");
}

/**
 * Motivation: Schedule four one-shots to fill capacity at a shared deadline, advance to fire them, then schedule
 *   and fire a reused-slot replacement.
 * Responsibilities: The full-capacity set dispatches in stable insertion order, removes completely, and the reused slot
 *   fires once at its calculated.
 */
MW_TEST_CASE(EngineTimerFullCapacitySameDeadlineStableOrderAndReuse)
{
	// Arrange
	FDispatchOrderRecorder Recorder;
	FTestManager Manager{0};
	FTimerHandle HandleA{};
	FTimerHandle HandleB{};
	FTimerHandle HandleC{};
	FTimerHandle HandleD{};

	// Act
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 1), StandardTimerPeriod, ETimerMode::OneShot, HandleA), "A should schedule");
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 2), StandardTimerPeriod, ETimerMode::OneShot, HandleB), "B should schedule");
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 3), StandardTimerPeriod, ETimerMode::OneShot, HandleC), "C should schedule");
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 4), StandardTimerPeriod, ETimerMode::OneShot, HandleD), "D should schedule");
	MW_EXPECT_EQ(Test, TestTimerCapacity, Manager.TimerCount(), "Four timers should occupy every slot");

	// Act
	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance at the shared deadline should succeed");

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{4}, Recorder.Count, "All four due one-shots should fire");
	MW_EXPECT_EQ(Test, 1, Recorder.Identities[0], "A fires first in insertion order");
	MW_EXPECT_EQ(Test, 2, Recorder.Identities[1], "B fires second in insertion order");
	MW_EXPECT_EQ(Test, 3, Recorder.Identities[2], "C fires third in insertion order");
	MW_EXPECT_EQ(Test, 4, Recorder.Identities[3], "D fires fourth in insertion order");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "All four one-shots must be removed after firing");

	// Every slot was freed by the single post-dispatch compaction pass; a fresh schedule must reuse one.
	// Act
	FTimerHandle ReusedHandle{};
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeOrderCallback(Recorder, 9), StandardTimerPeriod, ETimerMode::OneShot, ReusedHandle), "Slot reuse should succeed");
	MW_EXPECT_EQ(Test, 1u, Manager.TimerCount(), "The reused slot should be occupied");

	// Act
	MW_EXPECT_SUCCESS(Test, Manager.Advance(200), "Advance past the reused deadline should succeed");

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{5}, Recorder.Count, "The reused one-shot should fire exactly once");
	MW_EXPECT_EQ(Test, 9, Recorder.Identities[4], "The reused timer's identity should be recorded last");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "The reused one-shot must be removed after firing");
}

/**
 * Motivation: Scenario: Schedule Looping A, OneShot B, and Looping C at a shared deadline, fire them, then
 *   schedule replacement D into B's freed slot and advance again.
 * Responsibilities: Expected: Compaction preserves the looping survivors while dropping B, and the reused-slot
 *   replacement dispatches at the logical insertion tail, not its physical slot.
 */
MW_TEST_CASE(EngineTimerMixedStableCompactionPreservesSurvivorsAndTailReuse)
{
	// Arrange
	FDispatchOrderRecorder Recorder;
	FTestManager Manager{0};
	FTimerHandle HandleA{};
	FTimerHandle HandleB{};
	FTimerHandle HandleC{};

	// Act
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeOrderCallback(Recorder, 1), StandardTimerPeriod, ETimerMode::Looping, HandleA), "Looping A should schedule");
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeOrderCallback(Recorder, 2), StandardTimerPeriod, ETimerMode::OneShot, HandleB), "OneShot B should schedule");
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeOrderCallback(Recorder, 3), StandardTimerPeriod, ETimerMode::Looping, HandleC), "Looping C should schedule");
	MW_EXPECT_EQ(Test, 3u, Manager.TimerCount(), "A, B, and C should occupy three slots");

	const std::uint16_t SlotIndexOfB = HandleB.Index;

	// All three share the deadline 100; the single Advance fires them in insertion order A, B, C.
	// Act
	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "First Advance at the shared deadline should succeed");

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{3}, Recorder.Count, "All three due timers should fire on the first Advance");
	MW_EXPECT_EQ(Test, 1, Recorder.Identities[0], "Looping A should fire first");
	MW_EXPECT_EQ(Test, 2, Recorder.Identities[1], "OneShot B should fire second");
	MW_EXPECT_EQ(Test, 3, Recorder.Identities[2], "Looping C should fire third");

	// The one-shot B was cleared in place and dropped by post-dispatch compaction; the two loopers survive.
	MW_EXPECT_EQ(Test, 2u, Manager.TimerCount(), "Only the two looping survivors A and C should remain after B completes");

	// B's published handle is now stale: its slot generation advanced when it was cleared.
	MW_EXPECT_EQ(Test, ETimerResult::StaleHandle, Manager.Cancel(HandleB), "The completed one-shot B handle must be stale");

	// Schedule replacement D. It must reuse B's freed physical slot (lowest free index) but append
	// at the logical insertion tail, not restore B's original position between A and C.
	// Act
	FTimerHandle HandleD{};
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeOrderCallback(Recorder, 4), StandardTimerPeriod, ETimerMode::OneShot, HandleD), "Replacement D should schedule");
	MW_EXPECT_EQ(Test, SlotIndexOfB, HandleD.Index, "D must reuse B's freed physical slot");
	MW_EXPECT_TRUE(Test, HandleD.Generation != HandleB.Generation, "D must publish a fresh generation distinct from B's retired handle");
	MW_EXPECT_EQ(Test, 3u, Manager.TimerCount(), "A, C, and D should occupy three slots after reuse");

	// At Now=200 the loopers A and C refire (their Now-derived deadline is 100+100=200), and D's
	// one-shot deadline is 100+100=200 as well. Stable order must be A, C, D, proving that compaction
	// preserved A and C and that D dispatches at the tail rather than in B's old slot position.
	// Act
	MW_EXPECT_SUCCESS(Test, Manager.Advance(200), "Second Advance at the shared deadline should succeed");

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{6}, Recorder.Count, "A, C, and D should all fire on the second Advance");
	MW_EXPECT_EQ(Test, 1, Recorder.Identities[3], "Looping A should fire first again after compaction");
	MW_EXPECT_EQ(Test, 3, Recorder.Identities[4], "Looping C should retain its position ahead of the reused slot");
	MW_EXPECT_EQ(Test, 4, Recorder.Identities[5], "The reused-slot replacement D should dispatch at the insertion tail");

	// Only the two loopers remain after the second Advance; D completed and was removed.
	MW_EXPECT_EQ(Test, 2u, Manager.TimerCount(), "Only the two looping survivors A and C should remain after D completes");

	// D's handle is now stale for the same reason B's was.
	MW_EXPECT_EQ(Test, ETimerResult::StaleHandle, Manager.Cancel(HandleD), "The completed replacement D handle must be stale");
}

} // namespace
