#include "TestSupport.h"
#include "TimerManagerTestHelpers.h"

#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Core/TimerManager.h>

#include <limits>

namespace
{

using namespace ::MicroWorld::Tests;

// ---------------------------------------------------------------------------
// Category 1: One-shot scheduling
// ---------------------------------------------------------------------------

/**
 * Motivation: Schedule a one-shot timer and advance before its deadline.
 * Responsibilities: The timer does not fire before its deadline and remains occupied.
 */
MW_TEST_CASE(EngineTimerOneShotDoesNotFireBeforeDeadline)
{
	// Arrange
	FFireCounter Counter;
	FTestManager Manager{SaturatedTestInitialNow};
	FTimerHandle Handle{};

	// Act
	const ETimerResult Result = Manager.Schedule(MakeCounterCallback(Counter), StandardTimerPeriod, ETimerMode::OneShot, Handle);

	// Assert
	MW_EXPECT_SUCCESS(Test, Result, "A valid one-shot schedule should succeed");
	MW_EXPECT_TRUE(Test, Handle.IsValid(), "A successful schedule publishes a valid handle");

	// Act
	const ETimerResult AdvanceResult = Manager.Advance(1050);

	// Assert
	MW_EXPECT_SUCCESS(Test, AdvanceResult, "Advance before deadline should succeed");
	MW_EXPECT_EQ(Test, 0u, Counter.Count, "A one-shot timer must not fire before its deadline");
	MW_EXPECT_EQ(Test, 1u, Manager.TimerCount(), "A not-yet-due timer must remain occupied");
}

/**
 * Motivation: Schedule a one-shot timer and advance to its deadline.
 * Responsibilities: The one-shot timer fires exactly once when due and leaves no occupied slot.
 */
MW_TEST_CASE(EngineTimerOneShotFiresOnceWhenDue)
{
	// Arrange
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	// Act
	// Assert
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeCounterCallback(Counter), StandardTimerPeriod, ETimerMode::OneShot, Handle), "Schedule should succeed");
	MW_EXPECT_TRUE(Test, Handle.IsValid(), "A successful schedule publishes a valid handle");

	// Act
	// Assert
	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance to the deadline should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "A one-shot timer should fire exactly once when due");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "A fired one-shot should leave no occupied slot");
}

/**
 * Motivation: Schedule a one-shot timer, fire it, then attempt to cancel its handle.
 * Responsibilities: The fired one-shot timer's handle becomes stale and cancel returns StaleHandle.
 */
MW_TEST_CASE(EngineTimerOneShotHandleBecomesStaleAfterFiring)
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
	MW_EXPECT_EQ(Test, ETimerResult::StaleHandle, CancelResult, "A fired one-shot handle must be stale");
}

// ---------------------------------------------------------------------------
// Category 2: Looping scheduling
// ---------------------------------------------------------------------------

/**
 * Motivation: Schedule a looping timer and advance through three successive deadlines.
 * Responsibilities: The looping timer fires on its repeat cadence at each deadline.
 */
MW_TEST_CASE(EngineTimerLoopingFiresOnCadence)
{
	// Arrange
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	// Act
	// Assert
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeCounterCallback(Counter), StandardTimerPeriod, ETimerMode::Looping, Handle), "Schedule should succeed");

	// Act
	// Assert
	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance to the first deadline should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "The looping timer should fire at its first deadline");

	// Act
	// Assert
	MW_EXPECT_SUCCESS(Test, Manager.Advance(200), "Advance to the second deadline should succeed");
	MW_EXPECT_EQ(Test, 2u, Counter.Count, "The looping timer should fire at its second deadline");

	// Act
	// Assert
	MW_EXPECT_SUCCESS(Test, Manager.Advance(300), "Advance to the third deadline should succeed");
	MW_EXPECT_EQ(Test, 3u, Counter.Count, "The looping timer should fire at its third deadline");
}

/**
 * Motivation: Schedule a looping timer, advance to its first deadline, then advance far past the next deadline.
 * Responsibilities: The looping timer fires at most once per Advance even when its deadline is far overdue, with no
 *   catch-up burst.
 */
MW_TEST_CASE(EngineTimerLoopingFiresAtMostOncePerAdvance)
{
	// Arrange
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	// Act
	// Assert
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeCounterCallback(Counter), StandardTimerPeriod, ETimerMode::Looping, Handle), "Schedule should succeed");

	// Act
	// Assert
	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance to the first deadline should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "The looping timer should fire once at its first deadline");

	// Act
	// Assert
	MW_EXPECT_SUCCESS(Test, Manager.Advance(500), "A delayed Advance should succeed");
	MW_EXPECT_EQ(Test, 2u, Counter.Count, "A delayed Advance must not produce a catch-up burst");
}

/**
 * Motivation: Scenario: Fire a looping timer at an accepted Now later than its previous deadline, then advance to
 *   the previous-deadline time and the actual-Now deadline.
 * Responsibilities: Expected: The next deadline is computed from the actual accepted Now, so the timer does not refire
 *   at the previous-deadline time.
 */
MW_TEST_CASE(EngineTimerLoopingNextDeadlineUsesActualNow)
{
	// Arrange
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	// Act
	// Assert
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeCounterCallback(Counter), StandardTimerPeriod, ETimerMode::Looping, Handle), "Schedule should succeed");

	// Act
	// Assert
	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance to the first deadline should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "The looping timer should fire at its first deadline");

	// Act
	// Assert
	MW_EXPECT_SUCCESS(Test, Manager.Advance(250), "Advance to 250 should succeed");
	MW_EXPECT_EQ(Test, 2u, Counter.Count, "The looping timer should fire at the actual accepted Now=250");

	// After firing at Now=250 with period 100, the Now-based next deadline is 350; a previous-deadline
	// based design would set 300 and refire here.
	// Act
	// Assert
	MW_EXPECT_SUCCESS(Test, Manager.Advance(300), "Advance to 300 should succeed");
	MW_EXPECT_EQ(Test, 2u, Counter.Count, "The looping deadline must advance from actual Now, not the previous deadline");

	// Act
	// Assert
	MW_EXPECT_SUCCESS(Test, Manager.Advance(350), "Advance to the actual-Now-derived deadline should succeed");
	MW_EXPECT_EQ(Test, 3u, Counter.Count, "The looping timer refires at the actual-Now-derived deadline");
}

/**
 * Motivation: Schedule a zero-period looping timer and advance twice at the same timestamp.
 * Responsibilities: The zero-period looping timer fires once per Advance, including at the same timestamp.
 */
MW_TEST_CASE(EngineTimerZeroPeriodLoopingFiresOncePerAdvance)
{
	// Arrange
	FFireCounter Counter;
	FTestManager Manager{SaturatedTestInitialNow};
	FTimerHandle Handle{};

	// Act
	// Assert
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeCounterCallback(Counter), 0, ETimerMode::Looping, Handle), "Schedule should succeed");

	// Act
	// Assert
	MW_EXPECT_SUCCESS(Test, Manager.Advance(SaturatedTestInitialNow), "The first Advance at InitialNow should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "A zero-period looping timer should fire on the first Advance");

	// Act
	// Assert
	MW_EXPECT_SUCCESS(Test, Manager.Advance(SaturatedTestInitialNow), "A second Advance at the same timestamp should succeed");
	MW_EXPECT_EQ(Test, 2u, Counter.Count, "A zero-period looping timer fires once per Advance, including at the same timestamp");
}

/**
 * Motivation: Schedule a nonzero-period looping timer at the saturated maximum timestamp, advance once, then
 *   advance again at the same timestamp.
 * Responsibilities: The timer fires once at the saturated timestamp and does not refire on a repeated advance at the
 *   same timestamp.
 */
MW_TEST_CASE(EngineTimerNonzeroPeriodLoopingDoesNotRepeatAtSaturatedTimestamp)
{
	// Arrange
	FFireCounter Counter;
	const TimePointMilliseconds SaturatedNow = std::numeric_limits<TimePointMilliseconds>::max();
	FTestManager Manager{SaturatedNow};
	FTimerHandle Handle{};

	// Act
	const ETimerResult ScheduleResult = Manager.Schedule(MakeCounterCallback(Counter), StandardTimerPeriod, ETimerMode::Looping, Handle);

	// Assert
	MW_EXPECT_SUCCESS(Test, ScheduleResult, "Scheduling near saturation should succeed");

	// Act
	const ETimerResult FirstAdvance = Manager.Advance(SaturatedNow);

	// Assert
	MW_EXPECT_SUCCESS(Test, FirstAdvance, "Advance at saturation should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "The looping timer should fire once at the saturated timestamp");

	// Act
	const ETimerResult SecondAdvance = Manager.Advance(SaturatedNow);

	// Assert
	MW_EXPECT_SUCCESS(Test, SecondAdvance, "A repeated Advance at the same saturated Now should succeed without refiring");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "A nonzero-period looping timer must not repeat at the same saturated timestamp");
}

} // namespace
