#include "TestSupport.h"

#include <MicroWorld/TickFunction.h>

#include <limits>

namespace MicroWorld::Tests
{

/** Proves startup establishes a fresh schedule instead of inventing elapsed time. */
MW_TEST_CASE(Tick_FirstAdvanceTicksWithZeroDelta)
{
	const FTickConfiguration Configuration{true, true, 25};
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(100);

	const FTickDecision Decision = Tick.Advance(100);

	const ERuntimeResult ActualResult = Decision.Result;
	const bool bShouldTick = Decision.bShouldTick;
	const TimePointMilliseconds ActualNow = Decision.Context.NowMilliseconds;
	const DurationMilliseconds ActualDelta = Decision.Context.DeltaMilliseconds;
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ActualResult, "First enabled advance should succeed");
	MW_EXPECT_TRUE(Test, bShouldTick, "First enabled advance should execute a tick");
	MW_EXPECT_EQ(Test, TimePointMilliseconds{100}, ActualNow, "First tick should report dispatcher time");
	MW_EXPECT_EQ(Test, DurationMilliseconds{0}, ActualDelta, "First tick should report zero elapsed time");
}

/** Proves runtime enablement cannot silently replace the configured cadence. */
MW_TEST_CASE(Tick_EnablingDisabledTickPreservesInterval)
{
	const FTickConfiguration Configuration{true, false, 25};
	FTickFunction Tick(Configuration);

	const ERuntimeResult EnableResult = Tick.SetEnabled(true);

	const bool bEnabled = Tick.IsEnabled();
	const DurationMilliseconds ActualInterval = Tick.GetInterval();
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, EnableResult, "Enabling a tick-capable function should succeed");
	MW_EXPECT_TRUE(Test, bEnabled, "Tick-capable function should become enabled");
	MW_EXPECT_EQ(Test, DurationMilliseconds{25}, ActualInterval, "Enabling should preserve the configured interval");
}

/** Proves disabled time is not charged to a newly re-enabled schedule. */
MW_TEST_CASE(Tick_ReenabledTickUsesFreshZeroDeltaSchedule)
{
	const FTickConfiguration Configuration{true, true, 10};
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(100);
	const FTickDecision FirstDecision = Tick.Advance(100);
	const ERuntimeResult DisableResult = Tick.SetEnabled(false);
	const FTickDecision DisabledDecision = Tick.Advance(150);
	const ERuntimeResult EnableResult = Tick.SetEnabled(true);

	const FTickDecision ReenabledDecision = Tick.Advance(150);

	const bool bFirstTicked = FirstDecision.bShouldTick;
	const bool bDisabledTicked = DisabledDecision.bShouldTick;
	const ERuntimeResult DisabledAdvanceResult = DisabledDecision.Result;
	const bool bReenabledTicked = ReenabledDecision.bShouldTick;
	const DurationMilliseconds ReenabledDelta = ReenabledDecision.Context.DeltaMilliseconds;
	const DurationMilliseconds ActualInterval = Tick.GetInterval();
	MW_EXPECT_TRUE(Test, bFirstTicked, "Initial enabled advance should execute a tick");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, DisableResult, "Disabling an enabled tick should succeed");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, DisabledAdvanceResult, "Advancing a disabled tick should remain valid");
	MW_EXPECT_EQ(Test, false, bDisabledTicked, "Disabled tick should not execute");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, EnableResult, "Re-enabling a tick-capable function should succeed");
	MW_EXPECT_TRUE(Test, bReenabledTicked, "First advance after re-enable should execute");
	MW_EXPECT_EQ(Test, DurationMilliseconds{0}, ReenabledDelta, "First tick after re-enable should have zero delta");
	MW_EXPECT_EQ(Test, DurationMilliseconds{10}, ActualInterval, "Disable and re-enable should preserve interval");
}

/** Proves interval zero means once per caller update rather than an unbounded loop. */
MW_TEST_CASE(Tick_ZeroIntervalTicksOnEveryAdvance)
{
	const FTickConfiguration Configuration{true, true, 0};
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(20);

	const FTickDecision FirstDecision = Tick.Advance(20);
	const FTickDecision SecondDecision = Tick.Advance(21);
	const FTickDecision ThirdDecision = Tick.Advance(22);

	const bool bFirstTicked = FirstDecision.bShouldTick;
	const bool bSecondTicked = SecondDecision.bShouldTick;
	const bool bThirdTicked = ThirdDecision.bShouldTick;
	const DurationMilliseconds FirstDelta = FirstDecision.Context.DeltaMilliseconds;
	const DurationMilliseconds SecondDelta = SecondDecision.Context.DeltaMilliseconds;
	const DurationMilliseconds ThirdDelta = ThirdDecision.Context.DeltaMilliseconds;
	MW_EXPECT_TRUE(Test, bFirstTicked, "Zero interval should tick on first advance");
	MW_EXPECT_TRUE(Test, bSecondTicked, "Zero interval should tick on second advance");
	MW_EXPECT_TRUE(Test, bThirdTicked, "Zero interval should tick on third advance");
	MW_EXPECT_EQ(Test, DurationMilliseconds{0}, FirstDelta, "First zero-interval tick should have zero delta");
	MW_EXPECT_EQ(Test, DurationMilliseconds{1}, SecondDelta, "Second zero-interval tick should use elapsed time");
	MW_EXPECT_EQ(Test, DurationMilliseconds{1}, ThirdDelta, "Third zero-interval tick should use elapsed time");
}

/** Proves a late caller produces one tick and schedules from actual execution time. */
MW_TEST_CASE(Tick_LateIntervalTickDoesNotCatchUp)
{
	const FTickConfiguration Configuration{true, true, 10};
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(0);
	const FTickDecision FirstDecision = Tick.Advance(0);

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
	MW_EXPECT_TRUE(Test, bFirstTicked, "Initial interval advance should execute");
	MW_EXPECT_TRUE(Test, bLateTicked, "Late interval advance should execute once");
	MW_EXPECT_EQ(Test, DurationMilliseconds{35}, LateDelta, "Late tick should report full elapsed time");
	MW_EXPECT_EQ(Test, false, bSameTimeTicked, "Late tick should not leave catch-up work due");
	MW_EXPECT_EQ(Test, false, bBeforeNextDueTicked, "Next tick should wait from actual execution time");
	MW_EXPECT_TRUE(Test, bNextDueTicked, "Tick should execute at rescheduled deadline");
	MW_EXPECT_EQ(Test, DurationMilliseconds{10}, NextDueDelta, "Rescheduled tick should use its own elapsed time");
}

/** Proves sibling tickables calculate elapsed time from their own execution history. */
MW_TEST_CASE(Tick_DeltaBelongsToIndividualTickFunction)
{
	const FTickConfiguration FastConfiguration{true, true, 10};
	const FTickConfiguration SlowConfiguration{true, true, 25};
	FTickFunction FastTick(FastConfiguration);
	FTickFunction SlowTick(SlowConfiguration);
	FastTick.BeginPlay(0);
	SlowTick.BeginPlay(0);
	const FTickDecision FastFirstDecision = FastTick.Advance(0);
	const FTickDecision SlowFirstDecision = SlowTick.Advance(0);
	const FTickDecision FastSecondDecision = FastTick.Advance(10);
	const FTickDecision SlowEarlyDecision = SlowTick.Advance(10);

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
	MW_EXPECT_TRUE(Test, bFastFirstTicked, "Fast tick should establish its own schedule");
	MW_EXPECT_TRUE(Test, bSlowFirstTicked, "Slow tick should establish its own schedule");
	MW_EXPECT_TRUE(Test, bFastSecondTicked, "Fast tick should execute at its interval");
	MW_EXPECT_EQ(Test, DurationMilliseconds{10}, FastSecondDelta, "Fast tick delta should use fast history");
	MW_EXPECT_EQ(Test, false, bSlowEarlyTicked, "Slow tick should remain pending before interval");
	MW_EXPECT_TRUE(Test, bSlowSecondTicked, "Slow tick should execute at its interval");
	MW_EXPECT_EQ(Test, DurationMilliseconds{25}, SlowSecondDelta, "Slow tick delta should use slow history");
	MW_EXPECT_TRUE(Test, bFastLateTicked, "Fast tick should execute once when observed late");
	MW_EXPECT_EQ(Test, DurationMilliseconds{15}, FastLateDelta, "Fast late delta should ignore slow tick history");
}

/** Proves cadence configuration cannot override independent enablement intent. */
MW_TEST_CASE(Tick_IntervalChangeDoesNotEnableTick)
{
	const FTickConfiguration Configuration{true, false, 10};
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(0);

	const ERuntimeResult SetIntervalResult = Tick.SetInterval(25);
	const FTickDecision Decision = Tick.Advance(25);

	const bool bEnabled = Tick.IsEnabled();
	const DurationMilliseconds ActualInterval = Tick.GetInterval();
	const ERuntimeResult AdvanceResult = Decision.Result;
	const bool bShouldTick = Decision.bShouldTick;
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, SetIntervalResult, "Changing tick interval should succeed");
	MW_EXPECT_EQ(Test, false, bEnabled, "Interval change should preserve disabled state");
	MW_EXPECT_EQ(Test, DurationMilliseconds{25}, ActualInterval, "Interval change should store requested value");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, AdvanceResult, "Disabled tick advance should remain valid");
	MW_EXPECT_EQ(Test, false, bShouldTick, "Interval change should not cause disabled work");
}

/** Proves a live cadence change starts a fresh schedule without stale delta. */
MW_TEST_CASE(Tick_EnabledIntervalChangeResetsNextAdvance)
{
	const FTickConfiguration Configuration{true, true, 10};
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(0);
	const FTickDecision FirstDecision = Tick.Advance(0);
	const FTickDecision EarlyDecision = Tick.Advance(5);
	const ERuntimeResult SetIntervalResult = Tick.SetInterval(20);

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
	MW_EXPECT_TRUE(Test, bFirstTicked, "Initial enabled advance should establish schedule");
	MW_EXPECT_EQ(Test, false, bEarlyTicked, "Tick should wait for original interval");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, SetIntervalResult, "Enabled interval change should succeed");
	MW_EXPECT_TRUE(Test, bResetTicked, "Next advance should establish changed interval");
	MW_EXPECT_EQ(Test, DurationMilliseconds{0}, ResetDelta, "Changed interval schedule should start at zero delta");
	MW_EXPECT_EQ(Test, false, bBeforeNewDueTicked, "Changed interval should wait from reset time");
	MW_EXPECT_TRUE(Test, bNewDueTicked, "Changed interval should tick at new deadline");
	MW_EXPECT_EQ(Test, DurationMilliseconds{20}, NewDueDelta, "Changed interval tick should report new interval");
}

/** Proves construction-time tick capability remains a permanent invariant. */
MW_TEST_CASE(Tick_CannotEverTickRejectsEnable)
{
	const FTickConfiguration Configuration{false, true, 10};
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(0);

	const ERuntimeResult EnableResult = Tick.SetEnabled(true);
	const FTickDecision Decision = Tick.Advance(10);

	const bool bEnabled = Tick.IsEnabled();
	const ERuntimeResult AdvanceResult = Decision.Result;
	const bool bShouldTick = Decision.bShouldTick;
	MW_EXPECT_EQ(Test, ERuntimeResult::CannotEverTick, EnableResult, "Cannot-ever tick should reject runtime enable");
	MW_EXPECT_EQ(Test, false, bEnabled, "Rejected enable should leave tick disabled");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, AdvanceResult, "Disabled cannot-ever tick should advance safely");
	MW_EXPECT_EQ(Test, false, bShouldTick, "Cannot-ever tick should never execute");
}

/** Proves rejected time rollback cannot corrupt a later valid deadline or delta. */
MW_TEST_CASE(Tick_BackwardTimePreservesSchedule)
{
	const FTickConfiguration Configuration{true, true, 10};
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(100);
	const FTickDecision FirstDecision = Tick.Advance(100);
	const FTickDecision EarlyDecision = Tick.Advance(105);

	const FTickDecision BackwardDecision = Tick.Advance(104);
	const FTickDecision DueDecision = Tick.Advance(110);

	const bool bFirstTicked = FirstDecision.bShouldTick;
	const bool bEarlyTicked = EarlyDecision.bShouldTick;
	const ERuntimeResult BackwardResult = BackwardDecision.Result;
	const bool bBackwardTicked = BackwardDecision.bShouldTick;
	const bool bDueTicked = DueDecision.bShouldTick;
	const DurationMilliseconds DueDelta = DueDecision.Context.DeltaMilliseconds;
	MW_EXPECT_TRUE(Test, bFirstTicked, "Initial tick should establish schedule");
	MW_EXPECT_EQ(Test, false, bEarlyTicked, "Early monotonic update should not tick");
	MW_EXPECT_EQ(Test, ERuntimeResult::NonMonotonicTime, BackwardResult, "Backward time should return explicit error");
	MW_EXPECT_EQ(Test, false, bBackwardTicked, "Backward time should never execute a tick");
	MW_EXPECT_TRUE(Test, bDueTicked, "Original deadline should survive backward time");
	MW_EXPECT_EQ(Test, DurationMilliseconds{10}, DueDelta, "Preserved schedule should retain original delta");
}

/** Proves wide clock gaps remain bounded by the public duration representation. */
MW_TEST_CASE(Tick_UnrepresentableDeltaSaturatesAtMaximum)
{
	const FTickConfiguration Configuration{true, true, 0};
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(0);
	const FTickDecision FirstDecision = Tick.Advance(0);
	const TimePointMilliseconds LargeTime =
		static_cast<TimePointMilliseconds>(std::numeric_limits<DurationMilliseconds>::max()) + TimePointMilliseconds{1};

	const FTickDecision SaturatedDecision = Tick.Advance(LargeTime);

	const bool bFirstTicked = FirstDecision.bShouldTick;
	const ERuntimeResult SaturatedResult = SaturatedDecision.Result;
	const bool bSaturatedTicked = SaturatedDecision.bShouldTick;
	const DurationMilliseconds SaturatedDelta = SaturatedDecision.Context.DeltaMilliseconds;
	const DurationMilliseconds MaximumDuration = std::numeric_limits<DurationMilliseconds>::max();
	MW_EXPECT_TRUE(Test, bFirstTicked, "Initial tick should establish delta history");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, SaturatedResult, "Large monotonic delta should remain valid");
	MW_EXPECT_TRUE(Test, bSaturatedTicked, "Zero interval should tick at large time");
	MW_EXPECT_EQ(Test, MaximumDuration, SaturatedDelta, "Unrepresentable delta should saturate at maximum");
}

/** Proves deadline arithmetic cannot wrap and create an early execution. */
MW_TEST_CASE(Tick_NextDueSaturatesAtMaximumTime)
{
	const DurationMilliseconds Interval{10};
	const TimePointMilliseconds MaximumTime = std::numeric_limits<TimePointMilliseconds>::max();
	const TimePointMilliseconds StartTime = MaximumTime - 5;
	const FTickConfiguration Configuration{true, true, Interval};
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(StartTime);
	const FTickDecision FirstDecision = Tick.Advance(StartTime);

	const FTickDecision BeforeMaximumDecision = Tick.Advance(MaximumTime - 1);
	const FTickDecision MaximumDecision = Tick.Advance(MaximumTime);

	const bool bFirstTicked = FirstDecision.bShouldTick;
	const bool bBeforeMaximumTicked = BeforeMaximumDecision.bShouldTick;
	const ERuntimeResult MaximumResult = MaximumDecision.Result;
	const bool bMaximumTicked = MaximumDecision.bShouldTick;
	const DurationMilliseconds MaximumDelta = MaximumDecision.Context.DeltaMilliseconds;
	MW_EXPECT_TRUE(Test, bFirstTicked, "Initial near-maximum advance should tick");
	MW_EXPECT_EQ(Test, false, bBeforeMaximumTicked, "Saturated deadline should not wrap before maximum");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, MaximumResult, "Maximum time point should remain valid");
	MW_EXPECT_TRUE(Test, bMaximumTicked, "Saturated deadline should tick at maximum time");
	MW_EXPECT_EQ(Test, DurationMilliseconds{5}, MaximumDelta, "Maximum-time tick should report elapsed duration");
}

/** Proves a saturated deadline cannot tick repeatedly at one unchanged timestamp. */
MW_TEST_CASE(Tick_SaturatedDeadlineDoesNotRepeatWithoutElapsedTime)
{
	const DurationMilliseconds Interval{10};
	const TimePointMilliseconds MaximumTime = std::numeric_limits<TimePointMilliseconds>::max();
	const TimePointMilliseconds StartTime = MaximumTime - 5;
	const FTickConfiguration Configuration{true, true, Interval};
	FTickFunction Tick(Configuration);
	Tick.BeginPlay(StartTime);
	const FTickDecision FirstDecision = Tick.Advance(StartTime);
	const FTickDecision MaximumDecision = Tick.Advance(MaximumTime);

	const FTickDecision RepeatedMaximumDecision = Tick.Advance(MaximumTime);

	MW_EXPECT_TRUE(Test, FirstDecision.bShouldTick, "Initial near-maximum advance should establish the schedule");
	MW_EXPECT_TRUE(Test, MaximumDecision.bShouldTick, "Saturated deadline should execute once at maximum time");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, RepeatedMaximumDecision.Result, "Repeated maximum time remains a valid monotonic update");
	MW_EXPECT_EQ(Test, false, RepeatedMaximumDecision.bShouldTick, "Positive interval should not repeat without elapsed time");
}

/** Proves scheduling cannot bypass the lifecycle of its owning runtime object. */
MW_TEST_CASE(Tick_AdvanceOutsidePlayIsRejected)
{
	const FTickConfiguration Configuration{true, true, 0};
	FTickFunction Tick(Configuration);

	const FTickDecision BeforeBeginDecision = Tick.Advance(0);
	Tick.BeginPlay(0);
	Tick.EndPlay();
	const FTickDecision AfterEndDecision = Tick.Advance(1);

	const ERuntimeResult BeforeBeginResult = BeforeBeginDecision.Result;
	const bool bBeforeBeginTicked = BeforeBeginDecision.bShouldTick;
	const ERuntimeResult AfterEndResult = AfterEndDecision.Result;
	const bool bAfterEndTicked = AfterEndDecision.bShouldTick;
	MW_EXPECT_EQ(Test, ERuntimeResult::InvalidLifecycle, BeforeBeginResult, "Advance before BeginPlay should be rejected");
	MW_EXPECT_EQ(Test, false, bBeforeBeginTicked, "Advance before BeginPlay should not tick");
	MW_EXPECT_EQ(Test, ERuntimeResult::InvalidLifecycle, AfterEndResult, "Advance after EndPlay should be rejected");
	MW_EXPECT_EQ(Test, false, bAfterEndTicked, "Advance after EndPlay should not tick");
}

} // namespace MicroWorld::Tests
