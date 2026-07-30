#include "TestSupport.h"

#include <MicroWorld/Core/TickFunction.h>

#include <limits>

namespace MicroWorld::Tests
{

/** Fast cadence shared by the multi-test tick interval scenarios. */
constexpr DurationMilliseconds FastTickInterval{10};

/** Slow cadence shared by the late-arrival and sibling-tickable scenarios. */
constexpr DurationMilliseconds SlowTickInterval{25};

/** Cadence the live interval-change test switches to so the new deadline is observable. */
constexpr DurationMilliseconds ChangedTickInterval{20};

/**
 * Scenario: Begin an enabled tick function and advance at the same timestamp for the first time.
 * Expected: The first advance succeeds, ticks once, reports the dispatcher time, and uses a zero delta.
 */
MW_TEST_CASE(Tick_FirstAdvanceTicksWithZeroDelta)
{
	// Arrange
	const FTickConfiguration Configuration = FTickConfiguration::EnabledEvery(SlowTickInterval);
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(100);

	// Act
	const FTickDecision Decision = Tick.Advance(100);

	const ERuntimeResult ActualResult = Decision.Result;
	const bool bShouldTick = Decision.bShouldTick;
	const TimePointMilliseconds ActualNow = Decision.Context.NowMilliseconds;
	const DurationMilliseconds ActualDelta = Decision.Context.DeltaMilliseconds;

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ActualResult, "First enabled advance should succeed");
	MW_EXPECT_TRUE(Test, bShouldTick, "First enabled advance should execute a tick");
	MW_EXPECT_EQ(Test, TimePointMilliseconds{100}, ActualNow, "First tick should report dispatcher time");
	MW_EXPECT_EQ(Test, DurationMilliseconds{0}, ActualDelta, "First tick should report zero elapsed time");
}

/**
 * Scenario: Enable a tick-capable function that was constructed disabled and read its interval.
 * Expected: Enabling succeeds and leaves the configured interval unchanged.
 */
MW_TEST_CASE(Tick_EnablingDisabledTickPreservesInterval)
{
	// Arrange
	const FTickConfiguration Configuration{true, false, SlowTickInterval};
	FTickFunction Tick(Configuration);

	// Act
	const ERuntimeResult EnableResult = Tick.SetEnabled(true);

	const bool bEnabled = Tick.IsEnabled();
	const DurationMilliseconds ActualInterval = Tick.GetInterval();

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, EnableResult, "Enabling a tick-capable function should succeed");
	MW_EXPECT_TRUE(Test, bEnabled, "Tick-capable function should become enabled");
	MW_EXPECT_EQ(Test, SlowTickInterval, ActualInterval, "Enabling should preserve the configured interval");
}

/**
 * Scenario: Begin and advance an enabled tick, disable and advance it, then re-enable and advance again.
 * Expected: The disabled advance does not tick; the first re-enabled advance ticks with a zero delta and the original interval is preserved.
 */
MW_TEST_CASE(Tick_ReenabledTickUsesFreshZeroDeltaSchedule)
{
	// Arrange
	const FTickConfiguration Configuration = FTickConfiguration::EnabledEvery(FastTickInterval);
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(100);
	const FTickDecision FirstDecision = Tick.Advance(100);
	const ERuntimeResult DisableResult = Tick.SetEnabled(false);
	const FTickDecision DisabledDecision = Tick.Advance(150);
	const ERuntimeResult EnableResult = Tick.SetEnabled(true);

	// Act
	const FTickDecision ReenabledDecision = Tick.Advance(150);

	const bool bFirstTicked = FirstDecision.bShouldTick;
	const bool bDisabledTicked = DisabledDecision.bShouldTick;
	const ERuntimeResult DisabledAdvanceResult = DisabledDecision.Result;
	const bool bReenabledTicked = ReenabledDecision.bShouldTick;
	const DurationMilliseconds ReenabledDelta = ReenabledDecision.Context.DeltaMilliseconds;
	const DurationMilliseconds ActualInterval = Tick.GetInterval();

	// Assert
	MW_EXPECT_TRUE(Test, bFirstTicked, "Initial enabled advance should execute a tick");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, DisableResult, "Disabling an enabled tick should succeed");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, DisabledAdvanceResult, "Advancing a disabled tick should remain valid");
	MW_EXPECT_EQ(Test, false, bDisabledTicked, "Disabled tick should not execute");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, EnableResult, "Re-enabling a tick-capable function should succeed");
	MW_EXPECT_TRUE(Test, bReenabledTicked, "First advance after re-enable should execute");
	MW_EXPECT_EQ(Test, DurationMilliseconds{0}, ReenabledDelta, "First tick after re-enable should have zero delta");
	MW_EXPECT_EQ(Test, FastTickInterval, ActualInterval, "Disable and re-enable should preserve interval");
}

/**
 * Scenario: Begin a zero-interval tick function and advance it three consecutive times.
 * Expected: Each advance ticks once, with the first using a zero delta and later advances using the elapsed time.
 */
MW_TEST_CASE(Tick_ZeroIntervalTicksOnEveryAdvance)
{
	// Arrange
	const FTickConfiguration Configuration = FTickConfiguration::EnabledEvery(0);
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(20);

	// Act
	const FTickDecision FirstDecision = Tick.Advance(20);
	const FTickDecision SecondDecision = Tick.Advance(21);
	const FTickDecision ThirdDecision = Tick.Advance(22);

	const bool bFirstTicked = FirstDecision.bShouldTick;
	const bool bSecondTicked = SecondDecision.bShouldTick;
	const bool bThirdTicked = ThirdDecision.bShouldTick;
	const DurationMilliseconds FirstDelta = FirstDecision.Context.DeltaMilliseconds;
	const DurationMilliseconds SecondDelta = SecondDecision.Context.DeltaMilliseconds;
	const DurationMilliseconds ThirdDelta = ThirdDecision.Context.DeltaMilliseconds;

	// Assert
	MW_EXPECT_TRUE(Test, bFirstTicked, "Zero interval should tick on first advance");
	MW_EXPECT_TRUE(Test, bSecondTicked, "Zero interval should tick on second advance");
	MW_EXPECT_TRUE(Test, bThirdTicked, "Zero interval should tick on third advance");
	MW_EXPECT_EQ(Test, DurationMilliseconds{0}, FirstDelta, "First zero-interval tick should have zero delta");
	MW_EXPECT_EQ(Test, DurationMilliseconds{1}, SecondDelta, "Second zero-interval tick should use elapsed time");
	MW_EXPECT_EQ(Test, DurationMilliseconds{1}, ThirdDelta, "Third zero-interval tick should use elapsed time");
}

/**
 * Scenario: Advance an interval tick far past its deadline, then at the same timestamp, before the next due point, and at it.
 * Expected: The late advance ticks once with full elapsed time; it does not leave catch-up work, and the next tick waits from the actual execution
 * time.
 */
MW_TEST_CASE(Tick_LateIntervalTickDoesNotCatchUp)
{
	// Arrange
	const FTickConfiguration Configuration = FTickConfiguration::EnabledEvery(FastTickInterval);
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(0);
	const FTickDecision FirstDecision = Tick.Advance(0);

	// Act
	const FTickDecision LateDecision = Tick.Advance(35);
	const FTickDecision SameTimeDecision = Tick.Advance(35);
	const FTickDecision BeforeNextDueDecision = Tick.Advance(44);
	const FTickDecision NextDueDecision = Tick.Advance(45);

	const bool bFirstTicked = FirstDecision.bShouldTick;
	const bool bLateTicked = LateDecision.bShouldTick;
	const DurationMilliseconds LateDelta = LateDecision.Context.DeltaMilliseconds;
	const bool bSameTimeTicked = SameTimeDecision.bShouldTick;
	const bool bBeforeNextDueTicked = BeforeNextDueDecision.bShouldTick;
	const bool bNextDueTicked = NextDueDecision.bShouldTick;
	const DurationMilliseconds NextDueDelta = NextDueDecision.Context.DeltaMilliseconds;

	// Assert
	MW_EXPECT_TRUE(Test, bFirstTicked, "Initial interval advance should execute");
	MW_EXPECT_TRUE(Test, bLateTicked, "Late interval advance should execute once");
	MW_EXPECT_EQ(Test, DurationMilliseconds{35}, LateDelta, "Late tick should report full elapsed time");
	MW_EXPECT_EQ(Test, false, bSameTimeTicked, "Late tick should not leave catch-up work due");
	MW_EXPECT_EQ(Test, false, bBeforeNextDueTicked, "Next tick should wait from actual execution time");
	MW_EXPECT_TRUE(Test, bNextDueTicked, "Tick should execute at rescheduled deadline");
	MW_EXPECT_EQ(Test, FastTickInterval, NextDueDelta, "Rescheduled tick should use its own elapsed time");
}

/**
 * Scenario: Run a fast and a slow sibling tick function from a shared clock, advancing each on its own cadence including a late observation.
 * Expected: Each sibling computes its delta from its own execution history; the late fast delta ignores the slow tick history.
 */
MW_TEST_CASE(Tick_DeltaBelongsToIndividualTickFunction)
{
	// Arrange
	const FTickConfiguration FastConfiguration = FTickConfiguration::EnabledEvery(FastTickInterval);
	const FTickConfiguration SlowConfiguration = FTickConfiguration::EnabledEvery(SlowTickInterval);
	FTickFunction FastTick(FastConfiguration);
	FTickFunction SlowTick(SlowConfiguration);
	FastTick.BeginPlay(0);
	SlowTick.BeginPlay(0);
	const FTickDecision FastFirstDecision = FastTick.Advance(0);
	const FTickDecision SlowFirstDecision = SlowTick.Advance(0);
	const FTickDecision FastSecondDecision = FastTick.Advance(10);
	const FTickDecision SlowEarlyDecision = SlowTick.Advance(10);

	// Act
	const FTickDecision SlowSecondDecision = SlowTick.Advance(25);
	const FTickDecision FastLateDecision = FastTick.Advance(25);

	const bool bFastFirstTicked = FastFirstDecision.bShouldTick;
	const bool bSlowFirstTicked = SlowFirstDecision.bShouldTick;
	const bool bFastSecondTicked = FastSecondDecision.bShouldTick;
	const DurationMilliseconds FastSecondDelta = FastSecondDecision.Context.DeltaMilliseconds;
	const bool bSlowEarlyTicked = SlowEarlyDecision.bShouldTick;
	const bool bSlowSecondTicked = SlowSecondDecision.bShouldTick;
	const DurationMilliseconds SlowSecondDelta = SlowSecondDecision.Context.DeltaMilliseconds;
	const bool bFastLateTicked = FastLateDecision.bShouldTick;
	const DurationMilliseconds FastLateDelta = FastLateDecision.Context.DeltaMilliseconds;

	// Assert
	MW_EXPECT_TRUE(Test, bFastFirstTicked, "Fast tick should establish its own schedule");
	MW_EXPECT_TRUE(Test, bSlowFirstTicked, "Slow tick should establish its own schedule");
	MW_EXPECT_TRUE(Test, bFastSecondTicked, "Fast tick should execute at its interval");
	MW_EXPECT_EQ(Test, FastTickInterval, FastSecondDelta, "Fast tick delta should use fast history");
	MW_EXPECT_EQ(Test, false, bSlowEarlyTicked, "Slow tick should remain pending before interval");
	MW_EXPECT_TRUE(Test, bSlowSecondTicked, "Slow tick should execute at its interval");
	MW_EXPECT_EQ(Test, SlowTickInterval, SlowSecondDelta, "Slow tick delta should use slow history");
	MW_EXPECT_TRUE(Test, bFastLateTicked, "Fast tick should execute once when observed late");
	MW_EXPECT_EQ(Test, DurationMilliseconds{15}, FastLateDelta, "Fast late delta should ignore slow tick history");
}

/**
 * Scenario: Change the interval of a tick-capable function constructed disabled, then advance it.
 * Expected: The interval change succeeds and stores the new value without enabling the tick, so the advance does not tick.
 */
MW_TEST_CASE(Tick_IntervalChangeDoesNotEnableTick)
{
	// Arrange
	const FTickConfiguration Configuration{true, false, FastTickInterval};
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(0);

	// Act
	const ERuntimeResult SetIntervalResult = Tick.SetInterval(SlowTickInterval);
	const FTickDecision Decision = Tick.Advance(25);

	const bool bEnabled = Tick.IsEnabled();
	const DurationMilliseconds ActualInterval = Tick.GetInterval();
	const ERuntimeResult AdvanceResult = Decision.Result;
	const bool bShouldTick = Decision.bShouldTick;

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, SetIntervalResult, "Changing tick interval should succeed");
	MW_EXPECT_EQ(Test, false, bEnabled, "Interval change should preserve disabled state");
	MW_EXPECT_EQ(Test, SlowTickInterval, ActualInterval, "Interval change should store requested value");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, AdvanceResult, "Disabled tick advance should remain valid");
	MW_EXPECT_EQ(Test, false, bShouldTick, "Interval change should not cause disabled work");
}

/**
 * Scenario: After an early non-ticking advance on an enabled tick, change its interval, then advance at the reset time, before, and at the new
 * deadline. Expected: The change succeeds and the next advance starts a fresh zero-delta schedule that ticks at the new deadline with the new
 * interval.
 */
MW_TEST_CASE(Tick_EnabledIntervalChangeResetsNextAdvance)
{
	// Arrange
	const FTickConfiguration Configuration = FTickConfiguration::EnabledEvery(FastTickInterval);
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(0);
	const FTickDecision FirstDecision = Tick.Advance(0);
	const FTickDecision EarlyDecision = Tick.Advance(5);
	const ERuntimeResult SetIntervalResult = Tick.SetInterval(ChangedTickInterval);

	// Act
	const FTickDecision ResetDecision = Tick.Advance(6);
	const FTickDecision BeforeNewDueDecision = Tick.Advance(25);
	const FTickDecision NewDueDecision = Tick.Advance(26);

	const bool bFirstTicked = FirstDecision.bShouldTick;
	const bool bEarlyTicked = EarlyDecision.bShouldTick;
	const bool bResetTicked = ResetDecision.bShouldTick;
	const DurationMilliseconds ResetDelta = ResetDecision.Context.DeltaMilliseconds;
	const bool bBeforeNewDueTicked = BeforeNewDueDecision.bShouldTick;
	const bool bNewDueTicked = NewDueDecision.bShouldTick;
	const DurationMilliseconds NewDueDelta = NewDueDecision.Context.DeltaMilliseconds;

	// Assert
	MW_EXPECT_TRUE(Test, bFirstTicked, "Initial enabled advance should establish schedule");
	MW_EXPECT_EQ(Test, false, bEarlyTicked, "Tick should wait for original interval");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, SetIntervalResult, "Enabled interval change should succeed");
	MW_EXPECT_TRUE(Test, bResetTicked, "Next advance should establish changed interval");
	MW_EXPECT_EQ(Test, DurationMilliseconds{0}, ResetDelta, "Changed interval schedule should start at zero delta");
	MW_EXPECT_EQ(Test, false, bBeforeNewDueTicked, "Changed interval should wait from reset time");
	MW_EXPECT_TRUE(Test, bNewDueTicked, "Changed interval should tick at new deadline");
	MW_EXPECT_EQ(Test, ChangedTickInterval, NewDueDelta, "Changed interval tick should report new interval");
}

/**
 * Scenario: Attempt to enable a cannot-ever-tick function, then advance it.
 * Expected: The enable is rejected, the function stays disabled, and the advance remains valid without ticking.
 */
MW_TEST_CASE(Tick_CannotEverTickRejectsEnable)
{
	// Arrange
	const FTickConfiguration Configuration{false, true, FastTickInterval};
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(0);

	// Act
	const ERuntimeResult EnableResult = Tick.SetEnabled(true);
	const FTickDecision Decision = Tick.Advance(10);

	const bool bEnabled = Tick.IsEnabled();
	const ERuntimeResult AdvanceResult = Decision.Result;
	const bool bShouldTick = Decision.bShouldTick;

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::CannotEverTick, EnableResult, "Cannot-ever tick should reject runtime enable");
	MW_EXPECT_EQ(Test, false, bEnabled, "Rejected enable should leave tick disabled");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, AdvanceResult, "Disabled cannot-ever tick should advance safely");
	MW_EXPECT_EQ(Test, false, bShouldTick, "Cannot-ever tick should never execute");
}

/**
 * Scenario: Advance backward after an established schedule, then forward to the original deadline.
 * Expected: The backward advance is rejected and does not tick; the later valid deadline still ticks with the original delta preserved.
 */
MW_TEST_CASE(Tick_BackwardTimePreservesSchedule)
{
	// Arrange
	const FTickConfiguration Configuration = FTickConfiguration::EnabledEvery(FastTickInterval);
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(100);
	const FTickDecision FirstDecision = Tick.Advance(100);
	const FTickDecision EarlyDecision = Tick.Advance(105);

	// Act
	const FTickDecision BackwardDecision = Tick.Advance(104);
	const FTickDecision DueDecision = Tick.Advance(110);

	const bool bFirstTicked = FirstDecision.bShouldTick;
	const bool bEarlyTicked = EarlyDecision.bShouldTick;
	const ERuntimeResult BackwardResult = BackwardDecision.Result;
	const bool bBackwardTicked = BackwardDecision.bShouldTick;
	const bool bDueTicked = DueDecision.bShouldTick;
	const DurationMilliseconds DueDelta = DueDecision.Context.DeltaMilliseconds;

	// Assert
	MW_EXPECT_TRUE(Test, bFirstTicked, "Initial tick should establish schedule");
	MW_EXPECT_EQ(Test, false, bEarlyTicked, "Early monotonic update should not tick");
	MW_EXPECT_EQ(Test, ERuntimeResult::NonMonotonicTime, BackwardResult, "Backward time should return explicit error");
	MW_EXPECT_EQ(Test, false, bBackwardTicked, "Backward time should never execute a tick");
	MW_EXPECT_TRUE(Test, bDueTicked, "Original deadline should survive backward time");
	MW_EXPECT_EQ(Test, FastTickInterval, DueDelta, "Preserved schedule should retain original delta");
}

/**
 * Scenario: Advance a zero-interval tick from an established history to a time whose delta exceeds the duration representation.
 * Expected: The advance remains valid, ticks, and the unrepresentable delta saturates at the maximum duration.
 */
MW_TEST_CASE(Tick_UnrepresentableDeltaSaturatesAtMaximum)
{
	// Arrange
	const FTickConfiguration Configuration = FTickConfiguration::EnabledEvery(0);
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(0);
	const FTickDecision FirstDecision = Tick.Advance(0);
	const TimePointMilliseconds LargeTime =
		static_cast<TimePointMilliseconds>(std::numeric_limits<DurationMilliseconds>::max()) + TimePointMilliseconds{1};

	// Act
	const FTickDecision SaturatedDecision = Tick.Advance(LargeTime);

	const bool bFirstTicked = FirstDecision.bShouldTick;
	const ERuntimeResult SaturatedResult = SaturatedDecision.Result;
	const bool bSaturatedTicked = SaturatedDecision.bShouldTick;
	const DurationMilliseconds SaturatedDelta = SaturatedDecision.Context.DeltaMilliseconds;
	const DurationMilliseconds MaximumDuration = std::numeric_limits<DurationMilliseconds>::max();

	// Assert
	MW_EXPECT_TRUE(Test, bFirstTicked, "Initial tick should establish delta history");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, SaturatedResult, "Large monotonic delta should remain valid");
	MW_EXPECT_TRUE(Test, bSaturatedTicked, "Zero interval should tick at large time");
	MW_EXPECT_EQ(Test, MaximumDuration, SaturatedDelta, "Unrepresentable delta should saturate at maximum");
}

/**
 * Scenario: Begin a tick near the maximum time point and advance just before, then at, the maximum.
 * Expected: The saturated deadline does not wrap early; the maximum-time advance remains valid, ticks, and reports the elapsed duration.
 */
MW_TEST_CASE(Tick_NextDueSaturatesAtMaximumTime)
{
	// Arrange
	const DurationMilliseconds Interval{10};
	const TimePointMilliseconds MaximumTime = std::numeric_limits<TimePointMilliseconds>::max();
	const TimePointMilliseconds StartTime = MaximumTime - 5;
	const FTickConfiguration Configuration = FTickConfiguration::EnabledEvery(Interval);
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(StartTime);
	const FTickDecision FirstDecision = Tick.Advance(StartTime);

	// Act
	const FTickDecision BeforeMaximumDecision = Tick.Advance(MaximumTime - 1);
	const FTickDecision MaximumDecision = Tick.Advance(MaximumTime);

	const bool bFirstTicked = FirstDecision.bShouldTick;
	const bool bBeforeMaximumTicked = BeforeMaximumDecision.bShouldTick;
	const ERuntimeResult MaximumResult = MaximumDecision.Result;
	const bool bMaximumTicked = MaximumDecision.bShouldTick;
	const DurationMilliseconds MaximumDelta = MaximumDecision.Context.DeltaMilliseconds;

	// Assert
	MW_EXPECT_TRUE(Test, bFirstTicked, "Initial near-maximum advance should tick");
	MW_EXPECT_EQ(Test, false, bBeforeMaximumTicked, "Saturated deadline should not wrap before maximum");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, MaximumResult, "Maximum time point should remain valid");
	MW_EXPECT_TRUE(Test, bMaximumTicked, "Saturated deadline should tick at maximum time");
	MW_EXPECT_EQ(Test, DurationMilliseconds{5}, MaximumDelta, "Maximum-time tick should report elapsed duration");
}

/**
 * Scenario: Bring a positive-interval tick to its saturated deadline, then advance again at the same maximum timestamp.
 * Expected: The saturated deadline ticks once; the repeated same-timestamp advance remains valid but does not tick again.
 */
MW_TEST_CASE(Tick_SaturatedDeadlineDoesNotRepeatWithoutElapsedTime)
{
	// Arrange
	const DurationMilliseconds Interval{10};
	const TimePointMilliseconds MaximumTime = std::numeric_limits<TimePointMilliseconds>::max();
	const TimePointMilliseconds StartTime = MaximumTime - 5;
	const FTickConfiguration Configuration = FTickConfiguration::EnabledEvery(Interval);
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(StartTime);
	const FTickDecision FirstDecision = Tick.Advance(StartTime);
	const FTickDecision MaximumDecision = Tick.Advance(MaximumTime);

	// Act
	const FTickDecision RepeatedMaximumDecision = Tick.Advance(MaximumTime);

	// Assert
	MW_EXPECT_TRUE(Test, FirstDecision.bShouldTick, "Initial near-maximum advance should establish the schedule");
	MW_EXPECT_TRUE(Test, MaximumDecision.bShouldTick, "Saturated deadline should execute once at maximum time");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, RepeatedMaximumDecision.Result, "Repeated maximum time remains a valid monotonic update");
	MW_EXPECT_EQ(Test, false, RepeatedMaximumDecision.bShouldTick, "Positive interval should not repeat without elapsed time");
}

/**
 * Scenario: Advance the tick before BeginPlay, then BeginPlay, EndPlay, and advance again.
 * Expected: Advances outside the play lifecycle are rejected and never tick.
 */
MW_TEST_CASE(Tick_AdvanceOutsidePlayIsRejected)
{
	// Arrange
	const FTickConfiguration Configuration = FTickConfiguration::EnabledEvery(0);
	FTickFunction Tick(Configuration);

	// Act
	const FTickDecision BeforeBeginDecision = Tick.Advance(0);
	Tick.BeginPlay(0);
	Tick.EndPlay();
	const FTickDecision AfterEndDecision = Tick.Advance(1);

	const ERuntimeResult BeforeBeginResult = BeforeBeginDecision.Result;
	const bool bBeforeBeginTicked = BeforeBeginDecision.bShouldTick;
	const ERuntimeResult AfterEndResult = AfterEndDecision.Result;
	const bool bAfterEndTicked = AfterEndDecision.bShouldTick;

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::InvalidLifecycle, BeforeBeginResult, "Advance before BeginPlay should be rejected");
	MW_EXPECT_EQ(Test, false, bBeforeBeginTicked, "Advance before BeginPlay should not tick");
	MW_EXPECT_EQ(Test, ERuntimeResult::InvalidLifecycle, AfterEndResult, "Advance after EndPlay should be rejected");
	MW_EXPECT_EQ(Test, false, bAfterEndTicked, "Advance after EndPlay should not tick");
}

} // namespace MicroWorld::Tests
