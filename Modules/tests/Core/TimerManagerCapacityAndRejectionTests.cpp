#include "TestSupport.h"
#include "TimerManagerTestHelpers.h"

#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Core/TimerManager.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace
{

using namespace ::MicroWorld::Tests;

// ---------------------------------------------------------------------------
// Category 5: Capacity and invalid input
// ---------------------------------------------------------------------------

/**
 * Motivation: Schedule against a zero-capacity manager holding a canary output handle.
 * Responsibilities: The zero-capacity manager reports CapacityExceeded and clears the canary handle.
 */
MW_TEST_CASE(EngineTimerZeroCapacityRejectsSchedule)
{
	// Arrange
	TTimerManager<0, TestInlineCallbackBytes> Manager{0};
	FFireCounter Counter;
	FTimerHandle Handle{CanaryHandle};

	// Act
	const ETimerResult Result = Manager.Schedule(MakeCounterCallback(Counter), 1, ETimerMode::OneShot, Handle);

	// Assert
	MW_EXPECT_EQ(Test, ETimerResult::CapacityExceeded, Result, "A zero-capacity manager must reject scheduling");
	MW_EXPECT_TRUE(Test, !Handle.IsValid(), "A failed schedule must clear the canary output handle");
}

/**
 * Motivation: Fill a capacity-two manager, then attempt a third schedule holding a canary output handle.
 * Responsibilities: The full manager returns CapacityExceeded without consuming the supplied callback and clears the
 *   canary handle.
 */
MW_TEST_CASE(EngineTimerFullManagerPreservesCallbackOnFailure)
{
	// Arrange
	TTimerManager<2, TestInlineCallbackBytes> Manager{0};
	FFireCounter OccupantCounter;
	FFireCounter ThirdCounter;
	FTimerHandle FirstHandle{};
	FTimerHandle SecondHandle{};
	FTimerHandle ThirdHandle{CanaryHandle};

	// Act
	MW_EXPECT_SUCCESS(
		Test,
		Manager.Schedule(MakeCounterCallback(OccupantCounter), StandardTimerPeriod, ETimerMode::Looping, FirstHandle),
		"First occupant should schedule");
	MW_EXPECT_SUCCESS(
		Test,
		Manager.Schedule(MakeCounterCallback(OccupantCounter), StandardTimerPeriod, ETimerMode::Looping, SecondHandle),
		"Second occupant should schedule");

	FTestDelegate ThirdCallback = MakeCounterCallback(ThirdCounter);
	const ETimerResult Result = Manager.Schedule(std::move(ThirdCallback), StandardTimerPeriod, ETimerMode::OneShot, ThirdHandle);

	// Assert
	MW_EXPECT_EQ(Test, ETimerResult::CapacityExceeded, Result, "A full manager must report CapacityExceeded");
	MW_EXPECT_TRUE(Test, !ThirdHandle.IsValid(), "The failed schedule must clear the canary output handle");
	MW_EXPECT_TRUE(Test, ThirdCallback.IsBound(), "A rejected schedule must leave its input delegate bound to the caller");
	MW_EXPECT_EQ(Test, 2u, Manager.TimerCount(), "A failed schedule must not change occupancy");
}

/**
 * Motivation: Schedule with an unbound delegate holding a canary output handle.
 * Responsibilities: The unbound callback is rejected as InvalidCallback before any slot is consumed and clears the
 *   canary handle.
 */
MW_TEST_CASE(EngineTimerUnboundCallbackRejected)
{
	// Arrange
	FTestManager Manager{0};
	FTestDelegate Unbound;
	FTimerHandle Handle{CanaryHandle};

	// Act
	const ETimerResult Result = Manager.Schedule(std::move(Unbound), StandardTimerPeriod, ETimerMode::OneShot, Handle);

	// Assert
	MW_EXPECT_EQ(Test, ETimerResult::InvalidCallback, Result, "An unbound delegate must be rejected");
	MW_EXPECT_TRUE(Test, !Handle.IsValid(), "The failed schedule must clear the canary output handle");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "Invalid callback rejection must not change occupancy");
}

/**
 * Motivation: Schedule with the None mode holding a canary output handle.
 * Responsibilities: The None mode is rejected transactionally and clears the canary handle without changing occupancy.
 */
MW_TEST_CASE(EngineTimerInvalidModeRejected)
{
	// Arrange
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{CanaryHandle};

	// Act
	const ETimerResult Result = Manager.Schedule(MakeCounterCallback(Counter), StandardTimerPeriod, ETimerMode::None, Handle);

	// Assert
	MW_EXPECT_EQ(Test, ETimerResult::InvalidMode, Result, "The None mode must be rejected");
	MW_EXPECT_TRUE(Test, !Handle.IsValid(), "The failed schedule must clear the canary output handle");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "Invalid mode rejection must not change occupancy");
}

/**
 * Motivation: Schedule with an out-of-range ETimerMode cast holding a canary output handle.
 * Responsibilities: The out-of-range mode is rejected transactionally and clears the canary handle without changing
 *   occupancy.
 */
MW_TEST_CASE(EngineTimerOutOfRangeModeRejectedTransactionally)
{
	// Arrange
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{CanaryHandle};

	// Act
	const ETimerMode OutOfRangeMode = MakeOutOfRangeTimerMode();
	const ETimerResult Result = Manager.Schedule(MakeCounterCallback(Counter), 100, OutOfRangeMode, Handle);

	// Assert
	MW_EXPECT_EQ(Test, ETimerResult::InvalidMode, Result, "An out-of-range ETimerMode cast must be rejected as InvalidMode");
	MW_EXPECT_TRUE(Test, !Handle.IsValid(), "The failed schedule must clear the canary output handle");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "Out-of-range mode rejection must not change occupancy");
}

// ---------------------------------------------------------------------------
// Category 6: Caller-supplied time
// ---------------------------------------------------------------------------

/**
 * Motivation: Schedule a zero-delay one-shot timer and then advance at InitialNow.
 * Responsibilities: The zero-delay timer does not fire synchronously at schedule time; it becomes due and fires on the
 *   next Advance at InitialNow.
 */
MW_TEST_CASE(EngineTimerZeroDelayBecomesDueOnNextAdvance)
{
	// Arrange
	FFireCounter Counter;
	FTestManager Manager{SaturatedTestInitialNow};
	FTimerHandle Handle{};

	// Act
	// Assert
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeCounterCallback(Counter), 0, ETimerMode::OneShot, Handle), "Schedule should succeed");
	MW_EXPECT_EQ(Test, 0u, Counter.Count, "Schedule must not synchronously invoke a zero-delay timer");
	MW_EXPECT_EQ(Test, 1u, Manager.TimerCount(), "A scheduled zero-delay timer must occupy a slot before the first Advance");

	// Act
	// Assert
	MW_EXPECT_SUCCESS(Test, Manager.Advance(SaturatedTestInitialNow), "The first Advance at InitialNow should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "A zero-delay timer must fire on the first Advance at InitialNow");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "The fired zero-delay one-shot must be removed");
}

/**
 * Motivation: Scenario: Schedule a not-yet-due timer, accept an advance, then issue a backward and an intermediate
 *   backward advance, then reach the original deadline.
 * Responsibilities: Expected: Rolled-back Advances are rejected transactionally without changing occupancy or firing
 *   callbacks, and the original deadline is preserved.
 */
MW_TEST_CASE(EngineTimerRollbackAdvanceRejectedTransactionally)
{
	// Arrange
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	// Deadline 200 isolates the rollback: the timer is not yet due at the accepted times below.
	// Act
	// Assert
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeCounterCallback(Counter), 200, ETimerMode::OneShot, Handle), "Setup schedule should succeed");
	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance to 100 should succeed");

	// Act
	const std::size_t OccupancyBeforeRollback = Manager.TimerCount();
	const ETimerResult RollbackResult = Manager.Advance(50);

	// Assert
	MW_EXPECT_EQ(Test, ETimerResult::NonMonotonicTime, RollbackResult, "Advance to 50 must be rejected as a rollback");
	MW_EXPECT_EQ(Test, OccupancyBeforeRollback, Manager.TimerCount(), "A rejected Advance must not change occupancy");
	MW_EXPECT_EQ(Test, 0u, Counter.Count, "A rejected Advance must not invoke any callback");

	// After accepting 100, an intermediate value (75) is still a rollback and must also be rejected.
	// Act
	const ETimerResult IntermediateRollbackResult = Manager.Advance(75);

	// Assert
	MW_EXPECT_EQ(Test, ETimerResult::NonMonotonicTime, IntermediateRollbackResult, "Advance to 75 must still be rejected after accepting 100");
	MW_EXPECT_EQ(Test, 0u, Counter.Count, "The rejected intermediate Advance must not invoke any callback");

	// The original deadline is unchanged: advancing to 200 fires the timer exactly once.
	// Act
	// Assert
	MW_EXPECT_SUCCESS(Test, Manager.Advance(200), "Advance to the original deadline should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "The original deadline must remain 200 and fire exactly once");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "The fired one-shot must be removed");
}

/**
 * Motivation: Schedule a looping timer with a huge period starting near the maximum timestamp, advance to the
 *   boundary, then to the maximum.
 * Responsibilities: First-deadline arithmetic saturates without overflow; the saturated deadline fires exactly once at
 *   the maximum timestamp.
 */
MW_TEST_CASE(EngineTimerFirstDeadlineSaturatesWithoutOverflow)
{
	// Arrange
	FFireCounter Counter;
	const TimePointMilliseconds NearMaximum = std::numeric_limits<TimePointMilliseconds>::max() - 1u;
	const DurationMilliseconds HugeDuration = std::numeric_limits<DurationMilliseconds>::max();
	FTestManager Manager{NearMaximum};
	FTimerHandle Handle{};

	// Act
	const ETimerResult ScheduleResult = Manager.Schedule(MakeCounterCallback(Counter), HugeDuration, ETimerMode::Looping, Handle);

	// Assert
	MW_EXPECT_SUCCESS(Test, ScheduleResult, "Scheduling near saturation should succeed");

	// Act
	const ETimerResult AdvanceResult = Manager.Advance(NearMaximum);

	// Assert
	MW_EXPECT_SUCCESS(Test, AdvanceResult, "Advance at the saturated boundary should succeed");
	MW_EXPECT_EQ(Test, 0u, Counter.Count, "The saturated deadline must not be reached before the maximum timestamp");

	// Act
	const ETimerResult MaximumAdvanceResult = Manager.Advance(std::numeric_limits<TimePointMilliseconds>::max());

	// Assert
	MW_EXPECT_SUCCESS(Test, MaximumAdvanceResult, "Advance at the maximum timestamp should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "The saturated deadline should fire exactly once at the maximum timestamp");
}

// ---------------------------------------------------------------------------
// Category 8: Mutation rules during dispatch
// ---------------------------------------------------------------------------

/**
 * Motivation: From inside a fired callback, issue a Schedule with a delegate holding a canary output handle.
 * Responsibilities: The callback-issued Schedule returns DispatchLocked, preserves the delegate, clears the canary
 *   handle, and changes no occupancy.
 */
MW_TEST_CASE(EngineTimerCallbackScheduleRejectedAndDelegatePreserved)
{
	// Arrange
	FTestManager Manager{0};
	FFireCounter SecondCounter;
	FTimerHandle FirstHandle{};
	FTimerHandle RejectedHandle{CanaryHandle};

	FTestDelegate RejectedDelegate = MakeCounterCallback(SecondCounter);
	FCapturedMutation CapturedSchedule{};

	FTestDelegate FirstCallback;
	(void)FirstCallback.Bind(
		[&Manager, &RejectedDelegate, &RejectedHandle, &CapturedSchedule]() noexcept
		{
			CapturedSchedule.Result = Manager.Schedule(std::move(RejectedDelegate), StandardTimerPeriod, ETimerMode::OneShot, RejectedHandle);
			CapturedSchedule.bObserved = true;
		});

	// Act
	const ETimerResult ScheduleResult = Manager.Schedule(std::move(FirstCallback), StandardTimerPeriod, ETimerMode::OneShot, FirstHandle);
	MW_EXPECT_SUCCESS(Test, ScheduleResult, "The observing timer should schedule successfully");

	// Act
	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance should succeed and fire the observing callback");

	// Assert
	MW_EXPECT_TRUE(Test, CapturedSchedule.bObserved, "The callback should have executed");
	MW_EXPECT_EQ(Test, ETimerResult::DispatchLocked, CapturedSchedule.Result, "In-callback Schedule must return DispatchLocked");
	MW_EXPECT_TRUE(Test, !RejectedHandle.IsValid(), "The rejected in-callback schedule must clear the canary output handle");
	MW_EXPECT_TRUE(Test, RejectedDelegate.IsBound(), "The rejected in-callback schedule must leave its delegate bound to the caller");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "The rejected in-callback schedule must not change occupancy");
}

/**
 * Motivation: From inside a looping callback, cancel itself and a later one-shot, then let the Advance continue.
 * Responsibilities: In-callback cancellation is rejected as DispatchLocked while the other due timer still fires later
 *   in the same Advance.
 */
MW_TEST_CASE(EngineTimerCallbackCancellationRejectedAndOtherTimerStillFires)
{
	// Arrange
	FTestManager Manager{0};
	FDispatchOrderRecorder Recorder;
	FCapturedMutation SelfCancel{};
	FCapturedMutation OtherCancel{};
	FTimerHandle LoopingHandle{};
	FTimerHandle OneShotHandle{};

	FTestDelegate LoopingCallback;
	(void)LoopingCallback.Bind(
		[&Manager, &LoopingHandle, &OneShotHandle, &SelfCancel, &OtherCancel]() noexcept
		{
			SelfCancel.Result = Manager.Cancel(LoopingHandle);
			OtherCancel.Result = Manager.Cancel(OneShotHandle);
			SelfCancel.bObserved = true;
		});

	// Act
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(std::move(LoopingCallback), StandardTimerPeriod, ETimerMode::Looping, LoopingHandle), "Looping timer should schedule");
	MW_EXPECT_SUCCESS(
		Test,
		Manager.Schedule(MakeOrderCallback(Recorder, 7), StandardTimerPeriod, ETimerMode::OneShot, OneShotHandle),
		"One-shot timer should schedule");

	// Act
	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance at the shared deadline should succeed");

	// Assert
	MW_EXPECT_TRUE(Test, SelfCancel.bObserved, "The looping callback should have executed");
	MW_EXPECT_EQ(Test, ETimerResult::DispatchLocked, SelfCancel.Result, "In-callback self-cancel must return DispatchLocked");
	MW_EXPECT_EQ(Test, ETimerResult::DispatchLocked, OtherCancel.Result, "In-callback other-cancel must return DispatchLocked");
	MW_EXPECT_EQ(Test, std::size_t{1}, Recorder.Count, "The other due timer must still fire after the rejected cancels");
	MW_EXPECT_EQ(Test, 7, Recorder.Identities[0], "The other due timer's identity should be recorded");
	MW_EXPECT_EQ(Test, 1u, Manager.TimerCount(), "The looping timer must remain active after rejected in-callback cancels");
}

/**
 * Motivation: From inside a callback, issue a nested Advance, then issue an intermediate Advance after the outer
 *   dispatch ends.
 * Responsibilities: The nested Advance is rejected without changing accepted time, and a later intermediate time between
 *   the outer and nested values is still.
 */
MW_TEST_CASE(EngineTimerNestedAdvanceRejected)
{
	// Arrange
	FTestManager Manager{0};
	FCapturedMutation NestedAdvance{};
	FTimerHandle Handle{};

	FTestDelegate Callback;
	(void)Callback.Bind(
		[&Manager, &NestedAdvance]() noexcept
		{
			NestedAdvance.Result = Manager.Advance(200);
			NestedAdvance.bObserved = true;
		});

	// Act
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(std::move(Callback), StandardTimerPeriod, ETimerMode::OneShot, Handle), "Setup schedule should succeed");
	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Outer Advance to 100 should succeed and fire the callback");

	// Assert
	MW_EXPECT_TRUE(Test, NestedAdvance.bObserved, "The callback should have executed");
	MW_EXPECT_EQ(Test, ETimerResult::DispatchLocked, NestedAdvance.Result, "A nested Advance must return DispatchLocked");

	// After the outer dispatch ends, the manager must still accept a time between the outer accepted
	// time (100) and the rejected nested value (200). This proves the rejected nested Advance changed
	// nothing about the stored clock or transactional rollback boundary.
	// Act
	const ETimerResult IntermediateAdvanceResult = Manager.Advance(150);

	// Assert
	MW_EXPECT_SUCCESS(Test, IntermediateAdvanceResult, "Advance to 150 must succeed after the rejected nested Advance to 200");
}

// ---------------------------------------------------------------------------
// Category 9: Allocation-free steady-state operation
// ---------------------------------------------------------------------------

/**
 * Motivation: Exercise Schedule, Advance dispatch, Cancel, slot reuse, and looping operation while observing the
 *   allocation counter.
 * Responsibilities: The steady-state operations perform no observable allocation.
 */
MW_TEST_CASE(EngineTimerOperationsPerformNoObservableAllocation)
{
	// Arrange
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle OneShotHandle{};
	FTimerHandle LoopingHandle{};

	const std::uint32_t AllocationsBefore = GlobalAllocationCount;

	// Act
	MW_EXPECT_SUCCESS(
		Test,
		Manager.Schedule(MakeCounterCallback(Counter), StandardTimerPeriod, ETimerMode::OneShot, OneShotHandle),
		"One-shot schedule should succeed");
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeCounterCallback(Counter), 50, ETimerMode::Looping, LoopingHandle), "Looping schedule should succeed");
	MW_EXPECT_EQ(Test, 2u, Manager.TimerCount(), "Two schedules should occupy two slots");

	// Act
	MW_EXPECT_SUCCESS(Test, Manager.Advance(50), "Advance to 50 should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "Only the looping timer should fire at 50");
	MW_EXPECT_EQ(Test, 2u, Manager.TimerCount(), "The looping timer stays active after firing");

	// Act
	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance to 100 should succeed");
	// At Now=100: the looping timer (period 50, refired deadline 100) and the one-shot (deadline 100) both fire.
	MW_EXPECT_EQ(Test, 3u, Counter.Count, "Both the looping refire and the one-shot completion fire at 100");
	MW_EXPECT_EQ(Test, 1u, Manager.TimerCount(), "Only the looping timer remains after the one-shot fires");

	// Act
	MW_EXPECT_SUCCESS(Test, Manager.Cancel(LoopingHandle), "Canceling the looping timer should succeed");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "Cancellation should release the looping slot");

	// Reuse the freed slot and actually dispatch the reused one-shot at its calculated deadline.
	// Act
	FTimerHandle ReusedHandle{};
	MW_EXPECT_SUCCESS(
		Test,
		Manager.Schedule(MakeCounterCallback(Counter), StandardTimerPeriod, ETimerMode::OneShot, ReusedHandle),
		"Reused schedule should succeed");
	MW_EXPECT_EQ(Test, 1u, Manager.TimerCount(), "The reused slot should be occupied");
	// The reused one-shot deadline is LastAcceptedNow (100) + 100 = 200; prove it actually fires there.
	MW_EXPECT_SUCCESS(Test, Manager.Advance(200), "Advance to the reused calculated deadline should succeed");
	MW_EXPECT_EQ(Test, 4u, Counter.Count, "The reused one-shot should fire exactly once at its calculated deadline");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "The reused one-shot must be removed after firing");

	// Act
	const std::uint32_t AllocationsAfter = GlobalAllocationCount;

	// Assert
	MW_EXPECT_EQ(Test, AllocationsBefore, AllocationsAfter, "Timer schedule, dispatch, cancel, reuse, and looping must not allocate");
}

} // namespace
