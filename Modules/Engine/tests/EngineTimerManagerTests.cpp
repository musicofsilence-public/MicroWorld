#include "EngineAllocationCounters.h"
#include "TestSupport.h"

#include <MicroWorld/Delegates/Delegate.h>
#include <MicroWorld/Engine/Timer.h>
#include <MicroWorld/Time.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace
{
using MicroWorld::CanAdvanceTimerGeneration;
using MicroWorld::DurationMilliseconds;
using MicroWorld::ETimerMode;
using MicroWorld::ETimerResult;
using MicroWorld::FTimerHandle;
using MicroWorld::TDelegate;
using MicroWorld::TimePointMilliseconds;
using MicroWorld::TTimerManager;
using MicroWorld::Tests::GlobalAllocationCount;

/** Asserts a timer operation returned Success without discarding the result. */
#define MW_EXPECT_SUCCESS(TestContext, Result, Message) MW_EXPECT_EQ(TestContext, ETimerResult::Success, Result, Message)

/** Inline callback storage shared by every timer test so capturing lambdas fit one fixed size. */
constexpr std::size_t TestInlineCallbackBytes = 64;

/** Capacity large enough for ordering and mutation tests without masking capacity behavior. */
constexpr std::size_t TestTimerCapacity = 4;

using FTestManager = TTimerManager<TestTimerCapacity, TestInlineCallbackBytes>;
using FTestDelegate = TDelegate<void(), TestInlineCallbackBytes>;

/** A valid-looking handle value used to prove failed Schedule calls clear their output. */
constexpr FTimerHandle CanaryHandle{0u, 1u};

/** Counts callback invocations for one timer without allocating. */
struct FFireCounter final
{
	/** Records one observed callback invocation. */
	std::uint32_t Count{0};
};

/** Binds a nothrow inline callback that increments the supplied counter when invoked. */
FTestDelegate MakeCounterCallback(FFireCounter& Counter) noexcept
{
	FTestDelegate Delegate;
	(void)Delegate.Bind([&Counter]() noexcept { ++Counter.Count; });
	return Delegate;
}

/**
 * Produces an out-of-range ETimerMode value at runtime.
 *
 * Routed through a function rather than a `const` initializer so the cast is
 * not a constant expression: Clang's default `-Wenum-constexpr-conversion`
 * rejects `static_cast<ETimerMode>(non-enumerator)` only in constant contexts,
 * and this regression test deliberately targets the runtime rejection path.
 */
ETimerMode MakeOutOfRangeTimerMode() noexcept
{
	return static_cast<ETimerMode>(3);
}

// ---------------------------------------------------------------------------
// Category 1: One-shot scheduling
// ---------------------------------------------------------------------------

/** Proves a one-shot timer does not fire before its deadline. */
MW_TEST_CASE(EngineTimerOneShotDoesNotFireBeforeDeadline)
{
	FFireCounter Counter;
	FTestManager Manager{1000};
	FTimerHandle Handle{};

	const ETimerResult Result = Manager.Schedule(MakeCounterCallback(Counter), 100, ETimerMode::OneShot, Handle);
	MW_EXPECT_SUCCESS(Test, Result, "A valid one-shot schedule should succeed");
	MW_EXPECT_TRUE(Test, Handle.IsValid(), "A successful schedule publishes a valid handle");

	const ETimerResult AdvanceResult = Manager.Advance(1050);
	MW_EXPECT_SUCCESS(Test, AdvanceResult, "Advance before deadline should succeed");
	MW_EXPECT_EQ(Test, 0u, Counter.Count, "A one-shot timer must not fire before its deadline");
	MW_EXPECT_EQ(Test, 1u, Manager.TimerCount(), "A not-yet-due timer must remain occupied");
}

/** Proves a one-shot timer fires exactly once when its deadline arrives. */
MW_TEST_CASE(EngineTimerOneShotFiresOnceWhenDue)
{
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeCounterCallback(Counter), 100, ETimerMode::OneShot, Handle), "Schedule should succeed");
	MW_EXPECT_TRUE(Test, Handle.IsValid(), "A successful schedule publishes a valid handle");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance to the deadline should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "A one-shot timer should fire exactly once when due");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "A fired one-shot should leave no occupied slot");
}

/** Proves a fired one-shot timer is removed and no longer occupies a slot. */
MW_TEST_CASE(EngineTimerOneShotIsRemovedAfterFiring)
{
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeCounterCallback(Counter), 100, ETimerMode::OneShot, Handle), "Schedule should succeed");
	MW_EXPECT_EQ(Test, 1u, Manager.TimerCount(), "One schedule should occupy one slot");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance to the deadline should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "The one-shot should have fired exactly once");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "A fired one-shot timer should leave no occupied slot");
}

/** Proves a fired one-shot timer's handle becomes stale. */
MW_TEST_CASE(EngineTimerOneShotHandleBecomesStaleAfterFiring)
{
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeCounterCallback(Counter), 100, ETimerMode::OneShot, Handle), "Schedule should succeed");
	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance to the deadline should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "The one-shot should have fired exactly once");

	const ETimerResult CancelResult = Manager.Cancel(Handle);
	MW_EXPECT_EQ(Test, ETimerResult::StaleHandle, CancelResult, "A fired one-shot handle must be stale");
}

// ---------------------------------------------------------------------------
// Category 2: Looping scheduling
// ---------------------------------------------------------------------------

/** Proves a looping timer fires on its repeat cadence. */
MW_TEST_CASE(EngineTimerLoopingFiresOnCadence)
{
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeCounterCallback(Counter), 100, ETimerMode::Looping, Handle), "Schedule should succeed");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance to the first deadline should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "The looping timer should fire at its first deadline");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(200), "Advance to the second deadline should succeed");
	MW_EXPECT_EQ(Test, 2u, Counter.Count, "The looping timer should fire at its second deadline");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(300), "Advance to the third deadline should succeed");
	MW_EXPECT_EQ(Test, 3u, Counter.Count, "The looping timer should fire at its third deadline");
}

/** Proves a looping timer fires at most once per Advance even when its deadline is far overdue. */
MW_TEST_CASE(EngineTimerLoopingFiresAtMostOncePerAdvance)
{
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeCounterCallback(Counter), 100, ETimerMode::Looping, Handle), "Schedule should succeed");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance to the first deadline should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "The looping timer should fire once at its first deadline");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(500), "A delayed Advance should succeed");
	MW_EXPECT_EQ(Test, 2u, Counter.Count, "A delayed Advance must not produce a catch-up burst");
}

/** Proves a looping timer's next deadline is computed from the actual accepted Now, not its previous deadline. */
MW_TEST_CASE(EngineTimerLoopingNextDeadlineUsesActualNow)
{
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeCounterCallback(Counter), 100, ETimerMode::Looping, Handle), "Schedule should succeed");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance to the first deadline should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "The looping timer should fire at its first deadline");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(250), "Advance to 250 should succeed");
	MW_EXPECT_EQ(Test, 2u, Counter.Count, "The looping timer should fire at the actual accepted Now=250");

	// After firing at Now=250 with period 100, the Now-based next deadline is 350; a previous-deadline
	// based design would set 300 and refire here.
	MW_EXPECT_SUCCESS(Test, Manager.Advance(300), "Advance to 300 should succeed");
	MW_EXPECT_EQ(Test, 2u, Counter.Count, "The looping deadline must advance from actual Now, not the previous deadline");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(350), "Advance to the actual-Now-derived deadline should succeed");
	MW_EXPECT_EQ(Test, 3u, Counter.Count, "The looping timer refires at the actual-Now-derived deadline");
}

/** Proves a zero-period looping timer fires once per Advance at the same timestamp. */
MW_TEST_CASE(EngineTimerZeroPeriodLoopingFiresOncePerAdvance)
{
	FFireCounter Counter;
	FTestManager Manager{1000};
	FTimerHandle Handle{};

	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeCounterCallback(Counter), 0, ETimerMode::Looping, Handle), "Schedule should succeed");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(1000), "The first Advance at InitialNow should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "A zero-period looping timer should fire on the first Advance");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(1000), "A second Advance at the same timestamp should succeed");
	MW_EXPECT_EQ(Test, 2u, Counter.Count, "A zero-period looping timer fires once per Advance, including at the same timestamp");
}

/** Proves a nonzero-period looping timer does not refire at the same saturated timestamp. */
MW_TEST_CASE(EngineTimerNonzeroPeriodLoopingDoesNotRepeatAtSaturatedTimestamp)
{
	FFireCounter Counter;
	const TimePointMilliseconds SaturatedNow = std::numeric_limits<TimePointMilliseconds>::max();
	FTestManager Manager{SaturatedNow};
	FTimerHandle Handle{};

	const ETimerResult ScheduleResult = Manager.Schedule(MakeCounterCallback(Counter), 100, ETimerMode::Looping, Handle);
	MW_EXPECT_SUCCESS(Test, ScheduleResult, "Scheduling near saturation should succeed");

	const ETimerResult FirstAdvance = Manager.Advance(SaturatedNow);
	MW_EXPECT_SUCCESS(Test, FirstAdvance, "Advance at saturation should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "The looping timer should fire once at the saturated timestamp");

	const ETimerResult SecondAdvance = Manager.Advance(SaturatedNow);
	MW_EXPECT_SUCCESS(Test, SecondAdvance, "A repeated Advance at the same saturated Now should succeed without refiring");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "A nonzero-period looping timer must not repeat at the same saturated timestamp");
}

// ---------------------------------------------------------------------------
// Category 3: Cancellation
// ---------------------------------------------------------------------------

/** Proves cancellation before the deadline prevents any invocation. */
MW_TEST_CASE(EngineTimerCancellationBeforeDuePreventsInvocation)
{
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeCounterCallback(Counter), 100, ETimerMode::OneShot, Handle), "Schedule should succeed");
	MW_EXPECT_EQ(Test, 1u, Manager.TimerCount(), "One schedule should occupy one slot");

	const ETimerResult CancelResult = Manager.Cancel(Handle);
	MW_EXPECT_SUCCESS(Test, CancelResult, "Cancellation of an active timer should succeed");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "Cancellation should release the slot");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(200), "Advance well past the original deadline should succeed");
	MW_EXPECT_EQ(Test, 0u, Counter.Count, "A canceled timer must not fire");
}

/** Proves successful cancellation reduces observable occupancy. */
MW_TEST_CASE(EngineTimerCancellationReducesOccupancy)
{
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeCounterCallback(Counter), 100, ETimerMode::OneShot, Handle), "Schedule should succeed");
	MW_EXPECT_EQ(Test, 1u, Manager.TimerCount(), "One schedule should occupy one slot");

	MW_EXPECT_SUCCESS(Test, Manager.Cancel(Handle), "Cancellation should succeed");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "Cancellation should release the slot");
}

/** Proves repeated cancellation of the same handle returns StaleHandle. */
MW_TEST_CASE(EngineTimerRepeatedCancellationReturnsStaleHandle)
{
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeCounterCallback(Counter), 100, ETimerMode::OneShot, Handle), "Schedule should succeed");

	const ETimerResult FirstCancel = Manager.Cancel(Handle);
	MW_EXPECT_SUCCESS(Test, FirstCancel, "The first cancellation should succeed");

	const ETimerResult SecondCancel = Manager.Cancel(Handle);
	MW_EXPECT_EQ(Test, ETimerResult::StaleHandle, SecondCancel, "A repeated cancellation must return StaleHandle");
}

/** Proves cancellation after one-shot completion returns StaleHandle. */
MW_TEST_CASE(EngineTimerCancellationAfterCompletionReturnsStaleHandle)
{
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeCounterCallback(Counter), 100, ETimerMode::OneShot, Handle), "Schedule should succeed");
	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance to the deadline should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "The one-shot should have fired exactly once");

	const ETimerResult CancelResult = Manager.Cancel(Handle);
	MW_EXPECT_EQ(Test, ETimerResult::StaleHandle, CancelResult, "Canceling a completed one-shot must return StaleHandle");
}

// ---------------------------------------------------------------------------
// Category 4: Handles and generation safety
// ---------------------------------------------------------------------------

/** Proves default, sentinel, capacity-boundary, and zero-generation handles return InvalidHandle. */
MW_TEST_CASE(EngineTimerInvalidHandleIndicesRejected)
{
	FTestManager Manager{0};

	const FTimerHandle DefaultHandle{};
	const FTimerHandle SentinelHandle{FTimerHandle::InvalidIndex, 1u};
	const FTimerHandle IndexAtCapacity{static_cast<std::uint16_t>(TestTimerCapacity), 1u};
	const FTimerHandle ZeroGeneration{0u, 0u};

	MW_EXPECT_EQ(Test, ETimerResult::InvalidHandle, Manager.Cancel(DefaultHandle), "A default handle must be rejected as InvalidHandle");
	MW_EXPECT_EQ(Test, ETimerResult::InvalidHandle, Manager.Cancel(SentinelHandle), "A sentinel-index handle must be rejected as InvalidHandle");
	MW_EXPECT_EQ(
		Test, ETimerResult::InvalidHandle, Manager.Cancel(IndexAtCapacity), "A handle at Index == Capacity must be rejected as InvalidHandle");
	MW_EXPECT_EQ(
		Test, ETimerResult::InvalidHandle, Manager.Cancel(ZeroGeneration), "A handle with Generation == 0 must be rejected as InvalidHandle");
}

/** Proves a canceled handle and a generation-mismatched handle return StaleHandle. */
MW_TEST_CASE(EngineTimerStaleAndGenerationMismatchedHandlesRejected)
{
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeCounterCallback(Counter), 100, ETimerMode::Looping, Handle), "Schedule should succeed");
	const std::uint16_t SlotIndex = Handle.Index;
	const std::uint32_t PublishedGeneration = Handle.Generation;

	MW_EXPECT_SUCCESS(Test, Manager.Cancel(Handle), "The first cancellation should succeed");
	const FTimerHandle CanceledHandle{SlotIndex, PublishedGeneration};
	const FTimerHandle MismatchedHandle{SlotIndex, PublishedGeneration + 1u};

	MW_EXPECT_EQ(Test, ETimerResult::StaleHandle, Manager.Cancel(CanceledHandle), "A canceled handle must return StaleHandle");
	MW_EXPECT_EQ(Test, ETimerResult::StaleHandle, Manager.Cancel(MismatchedHandle), "A generation-mismatched handle must return StaleHandle");
}

/** Proves slot reuse publishes a different generation than the retired handle. */
MW_TEST_CASE(EngineTimerSlotReusePublishesDifferentGeneration)
{
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle FirstHandle{};

	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeCounterCallback(Counter), 100, ETimerMode::OneShot, FirstHandle), "First schedule should succeed");
	MW_EXPECT_SUCCESS(Test, Manager.Cancel(FirstHandle), "Canceling the first timer should succeed");

	FTimerHandle SecondHandle{};
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeCounterCallback(Counter), 100, ETimerMode::OneShot, SecondHandle), "Second schedule should succeed");

	MW_EXPECT_TRUE(Test, FirstHandle.Index == SecondHandle.Index, "A freed slot should be reused first");
	MW_EXPECT_TRUE(Test, FirstHandle.Generation != SecondHandle.Generation, "Reused slot must publish a different generation");
}

/** Proves a stale handle cannot cancel a slot's replacement timer. */
MW_TEST_CASE(EngineTimerStaleHandleCannotAffectReplacement)
{
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle FirstHandle{};

	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeCounterCallback(Counter), 100, ETimerMode::Looping, FirstHandle), "First schedule should succeed");
	MW_EXPECT_SUCCESS(Test, Manager.Cancel(FirstHandle), "Canceling the first timer should succeed");

	FTimerHandle SecondHandle{};
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeCounterCallback(Counter), 100, ETimerMode::Looping, SecondHandle), "Replacement schedule should succeed");
	MW_EXPECT_EQ(Test, 1u, Manager.TimerCount(), "The replacement should occupy one slot");

	const ETimerResult StaleCancel = Manager.Cancel(FirstHandle);
	MW_EXPECT_EQ(Test, ETimerResult::StaleHandle, StaleCancel, "The retired handle must not cancel the replacement");
	MW_EXPECT_EQ(Test, 1u, Manager.TimerCount(), "A stale cancel must not change occupancy");

	const ETimerResult LiveCancel = Manager.Cancel(SecondHandle);
	MW_EXPECT_SUCCESS(Test, LiveCancel, "The replacement handle should still cancel successfully");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "The live cancel should release the slot");
}

/** Proves the generation helper refuses wrap at the type maximum and accepts every earlier value. */
MW_TEST_CASE(EngineTimerGenerationHelperRefusesWrap)
{
	constexpr bool AtZero = CanAdvanceTimerGeneration(0u);
	constexpr bool AtFirst = CanAdvanceTimerGeneration(1u);
	constexpr bool BeforeWrap = CanAdvanceTimerGeneration(std::numeric_limits<std::uint32_t>::max() - 1u);
	constexpr bool AtWrap = CanAdvanceTimerGeneration(std::numeric_limits<std::uint32_t>::max());

	MW_EXPECT_TRUE(Test, AtZero, "Generation zero must be advanceable");
	MW_EXPECT_TRUE(Test, AtFirst, "Generation one must be advanceable");
	MW_EXPECT_TRUE(Test, BeforeWrap, "The last finite generation must be advanceable");
	MW_EXPECT_TRUE(Test, !AtWrap, "The maximum generation must refuse to advance and trigger retirement");
}

// ---------------------------------------------------------------------------
// Category 5: Capacity and invalid input
// ---------------------------------------------------------------------------

/** Proves a zero-capacity manager reports CapacityExceeded on every schedule attempt and clears the canary handle. */
MW_TEST_CASE(EngineTimerZeroCapacityRejectsSchedule)
{
	TTimerManager<0, TestInlineCallbackBytes> Manager{0};
	FFireCounter Counter;
	FTimerHandle Handle{CanaryHandle};

	const ETimerResult Result = Manager.Schedule(MakeCounterCallback(Counter), 1, ETimerMode::OneShot, Handle);

	MW_EXPECT_EQ(Test, ETimerResult::CapacityExceeded, Result, "A zero-capacity manager must reject scheduling");
	MW_EXPECT_TRUE(Test, !Handle.IsValid(), "A failed schedule must clear the canary output handle");
}

/** Proves a full manager returns CapacityExceeded without consuming the supplied callback and clears the canary handle. */
MW_TEST_CASE(EngineTimerFullManagerPreservesCallbackOnFailure)
{
	TTimerManager<2, TestInlineCallbackBytes> Manager{0};
	FFireCounter OccupantCounter;
	FFireCounter ThirdCounter;
	FTimerHandle FirstHandle{};
	FTimerHandle SecondHandle{};
	FTimerHandle ThirdHandle{CanaryHandle};

	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeCounterCallback(OccupantCounter), 100, ETimerMode::Looping, FirstHandle), "First occupant should schedule");
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeCounterCallback(OccupantCounter), 100, ETimerMode::Looping, SecondHandle), "Second occupant should schedule");

	FTestDelegate ThirdCallback = MakeCounterCallback(ThirdCounter);
	const ETimerResult Result = Manager.Schedule(std::move(ThirdCallback), 100, ETimerMode::OneShot, ThirdHandle);

	MW_EXPECT_EQ(Test, ETimerResult::CapacityExceeded, Result, "A full manager must report CapacityExceeded");
	MW_EXPECT_TRUE(Test, !ThirdHandle.IsValid(), "The failed schedule must clear the canary output handle");
	MW_EXPECT_TRUE(Test, ThirdCallback.IsBound(), "A rejected schedule must leave its input delegate bound to the caller");
	MW_EXPECT_EQ(Test, 2u, Manager.TimerCount(), "A failed schedule must not change occupancy");
}

/** Proves an unbound callback is rejected as InvalidCallback before any slot is consumed and clears the canary handle. */
MW_TEST_CASE(EngineTimerUnboundCallbackRejected)
{
	FTestManager Manager{0};
	FTestDelegate Unbound;
	FTimerHandle Handle{CanaryHandle};

	const ETimerResult Result = Manager.Schedule(std::move(Unbound), 100, ETimerMode::OneShot, Handle);

	MW_EXPECT_EQ(Test, ETimerResult::InvalidCallback, Result, "An unbound delegate must be rejected");
	MW_EXPECT_TRUE(Test, !Handle.IsValid(), "The failed schedule must clear the canary output handle");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "Invalid callback rejection must not change occupancy");
}

/** Proves the None mode is rejected transactionally and clears the canary handle. */
MW_TEST_CASE(EngineTimerInvalidModeRejected)
{
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{CanaryHandle};

	const ETimerResult Result = Manager.Schedule(MakeCounterCallback(Counter), 100, ETimerMode::None, Handle);

	MW_EXPECT_EQ(Test, ETimerResult::InvalidMode, Result, "The None mode must be rejected");
	MW_EXPECT_TRUE(Test, !Handle.IsValid(), "The failed schedule must clear the canary output handle");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "Invalid mode rejection must not change occupancy");
}

/** Proves an out-of-range ETimerMode cast is rejected transactionally and clears the canary handle. */
MW_TEST_CASE(EngineTimerOutOfRangeModeRejectedTransactionally)
{
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{CanaryHandle};

	const ETimerMode OutOfRangeMode = MakeOutOfRangeTimerMode();
	const ETimerResult Result = Manager.Schedule(MakeCounterCallback(Counter), 100, OutOfRangeMode, Handle);

	MW_EXPECT_EQ(Test, ETimerResult::InvalidMode, Result, "An out-of-range ETimerMode cast must be rejected as InvalidMode");
	MW_EXPECT_TRUE(Test, !Handle.IsValid(), "The failed schedule must clear the canary output handle");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "Out-of-range mode rejection must not change occupancy");
}

// ---------------------------------------------------------------------------
// Category 6: Caller-supplied time
// ---------------------------------------------------------------------------

/** Proves a zero-delay timer becomes due on the next Advance at InitialNow rather than firing synchronously. */
MW_TEST_CASE(EngineTimerZeroDelayBecomesDueOnNextAdvance)
{
	FFireCounter Counter;
	FTestManager Manager{1000};
	FTimerHandle Handle{};

	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeCounterCallback(Counter), 0, ETimerMode::OneShot, Handle), "Schedule should succeed");
	MW_EXPECT_EQ(Test, 0u, Counter.Count, "Schedule must not synchronously invoke a zero-delay timer");
	MW_EXPECT_EQ(Test, 1u, Manager.TimerCount(), "A scheduled zero-delay timer must occupy a slot before the first Advance");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(1000), "The first Advance at InitialNow should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "A zero-delay timer must fire on the first Advance at InitialNow");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "The fired zero-delay one-shot must be removed");
}

/** Proves rolled-back Advances are rejected transactionally and the original deadline is preserved. */
MW_TEST_CASE(EngineTimerRollbackAdvanceRejectedTransactionally)
{
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	// Deadline 200 isolates the rollback: the timer is not yet due at the accepted times below.
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeCounterCallback(Counter), 200, ETimerMode::OneShot, Handle), "Setup schedule should succeed");
	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance to 100 should succeed");

	const std::size_t OccupancyBeforeRollback = Manager.TimerCount();
	const ETimerResult RollbackResult = Manager.Advance(50);
	MW_EXPECT_EQ(Test, ETimerResult::NonMonotonicTime, RollbackResult, "Advance to 50 must be rejected as a rollback");
	MW_EXPECT_EQ(Test, OccupancyBeforeRollback, Manager.TimerCount(), "A rejected Advance must not change occupancy");
	MW_EXPECT_EQ(Test, 0u, Counter.Count, "A rejected Advance must not invoke any callback");

	// After accepting 100, an intermediate value (75) is still a rollback and must also be rejected.
	const ETimerResult IntermediateRollbackResult = Manager.Advance(75);
	MW_EXPECT_EQ(Test, ETimerResult::NonMonotonicTime, IntermediateRollbackResult, "Advance to 75 must still be rejected after accepting 100");
	MW_EXPECT_EQ(Test, 0u, Counter.Count, "The rejected intermediate Advance must not invoke any callback");

	// The original deadline is unchanged: advancing to 200 fires the timer exactly once.
	MW_EXPECT_SUCCESS(Test, Manager.Advance(200), "Advance to the original deadline should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "The original deadline must remain 200 and fire exactly once");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "The fired one-shot must be removed");
}

/** Proves first-deadline arithmetic saturates at the TimePointMilliseconds maximum. */
MW_TEST_CASE(EngineTimerFirstDeadlineSaturatesWithoutOverflow)
{
	FFireCounter Counter;
	const TimePointMilliseconds NearMaximum = std::numeric_limits<TimePointMilliseconds>::max() - 1u;
	const DurationMilliseconds HugeDuration = std::numeric_limits<DurationMilliseconds>::max();
	FTestManager Manager{NearMaximum};
	FTimerHandle Handle{};

	const ETimerResult ScheduleResult = Manager.Schedule(MakeCounterCallback(Counter), HugeDuration, ETimerMode::Looping, Handle);
	MW_EXPECT_SUCCESS(Test, ScheduleResult, "Scheduling near saturation should succeed");

	const ETimerResult AdvanceResult = Manager.Advance(NearMaximum);
	MW_EXPECT_SUCCESS(Test, AdvanceResult, "Advance at the saturated boundary should succeed");
	MW_EXPECT_EQ(Test, 0u, Counter.Count, "The saturated deadline must not be reached before the maximum timestamp");

	const ETimerResult MaximumAdvanceResult = Manager.Advance(std::numeric_limits<TimePointMilliseconds>::max());
	MW_EXPECT_SUCCESS(Test, MaximumAdvanceResult, "Advance at the maximum timestamp should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "The saturated deadline should fire exactly once at the maximum timestamp");
}

// ---------------------------------------------------------------------------
// Category 7: Deterministic ordering
// ---------------------------------------------------------------------------

/** Records the identity of each callback in stable dispatch order without allocating. */
struct FDispatchOrderRecorder final
{
	/** Bounds the recorded sequence so the test fixtures stay allocation-free and fixed-size. */
	static constexpr std::size_t MaximumEntries = 8;

	/** Tracks the next write position so later reads observe insertion-order dispatch. */
	std::size_t Count{0};

	/** Stores the caller-supplied identity of each fired callback. */
	int Identities[MaximumEntries]{0};

	/** Appends one observed identity when space remains. */
	void Record(const int Identity) noexcept
	{
		if (Count < MaximumEntries)
		{
			Identities[Count] = Identity;
			++Count;
		}
	}
};

/** Binds a callback that records its identity in the shared recorder. */
FTestDelegate MakeOrderCallback(FDispatchOrderRecorder& Recorder, const int Identity) noexcept
{
	FTestDelegate Delegate;
	(void)Delegate.Bind([&Recorder, Identity]() noexcept { Recorder.Record(Identity); });
	return Delegate;
}

/** Proves simultaneously due timers fire in insertion order rather than slot order. */
MW_TEST_CASE(EngineTimerSimultaneouslyDueTimersFireInInsertionOrder)
{
	FDispatchOrderRecorder Recorder;
	FTestManager Manager{0};
	FTimerHandle HandleA{};
	FTimerHandle HandleB{};
	FTimerHandle HandleC{};

	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 1), 100, ETimerMode::OneShot, HandleA), "A should schedule");
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 2), 100, ETimerMode::OneShot, HandleB), "B should schedule");
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 3), 100, ETimerMode::OneShot, HandleC), "C should schedule");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance at the shared deadline should succeed");
	MW_EXPECT_EQ(Test, std::size_t{3}, Recorder.Count, "All three due timers should fire");
	MW_EXPECT_EQ(Test, 1, Recorder.Identities[0], "The first-inserted timer should fire first");
	MW_EXPECT_EQ(Test, 2, Recorder.Identities[1], "The second-inserted timer should fire second");
	MW_EXPECT_EQ(Test, 3, Recorder.Identities[2], "The third-inserted timer should fire third");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "All three one-shots must be removed after firing");
}

/** Proves canceling and reusing a slot appends the replacement at the tail of insertion order. */
MW_TEST_CASE(EngineTimerCancelReuseAppendsReplacementAtInsertionTail)
{
	FDispatchOrderRecorder Recorder;
	FTestManager Manager{0};
	FTimerHandle HandleA{};
	FTimerHandle HandleB{};
	FTimerHandle HandleC{};
	FTimerHandle HandleD{};

	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 1), 100, ETimerMode::OneShot, HandleA), "A should schedule");
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 2), 100, ETimerMode::OneShot, HandleB), "B should schedule");
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 3), 100, ETimerMode::OneShot, HandleC), "C should schedule");
	MW_EXPECT_SUCCESS(Test, Manager.Cancel(HandleB), "Canceling B should succeed");

	// The replacement reuses the freed slot but must dispatch after C, not between A and C.
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 4), 100, ETimerMode::OneShot, HandleD), "D should schedule as replacement");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance at the shared deadline should succeed");
	MW_EXPECT_EQ(Test, std::size_t{3}, Recorder.Count, "Three timers should fire after the cancel/reuse");
	MW_EXPECT_EQ(Test, 1, Recorder.Identities[0], "A remains first in insertion order");
	MW_EXPECT_EQ(Test, 3, Recorder.Identities[1], "C retains its insertion position ahead of the reused slot");
	MW_EXPECT_EQ(Test, 4, Recorder.Identities[2], "The reused slot's replacement dispatches at the insertion tail");
}

/** Proves a full-capacity set of same-deadline one-shots dispatches in stable order, removes completely, and reuses slots. */
MW_TEST_CASE(EngineTimerFullCapacitySameDeadlineStableOrderAndReuse)
{
	FDispatchOrderRecorder Recorder;
	FTestManager Manager{0};
	FTimerHandle HandleA{};
	FTimerHandle HandleB{};
	FTimerHandle HandleC{};
	FTimerHandle HandleD{};

	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 1), 100, ETimerMode::OneShot, HandleA), "A should schedule");
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 2), 100, ETimerMode::OneShot, HandleB), "B should schedule");
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 3), 100, ETimerMode::OneShot, HandleC), "C should schedule");
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 4), 100, ETimerMode::OneShot, HandleD), "D should schedule");
	MW_EXPECT_EQ(Test, TestTimerCapacity, Manager.TimerCount(), "Four timers should occupy every slot");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance at the shared deadline should succeed");
	MW_EXPECT_EQ(Test, std::size_t{4}, Recorder.Count, "All four due one-shots should fire");
	MW_EXPECT_EQ(Test, 1, Recorder.Identities[0], "A fires first in insertion order");
	MW_EXPECT_EQ(Test, 2, Recorder.Identities[1], "B fires second in insertion order");
	MW_EXPECT_EQ(Test, 3, Recorder.Identities[2], "C fires third in insertion order");
	MW_EXPECT_EQ(Test, 4, Recorder.Identities[3], "D fires fourth in insertion order");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "All four one-shots must be removed after firing");

	// Every slot was freed by the single post-dispatch compaction pass; a fresh schedule must reuse one.
	FTimerHandle ReusedHandle{};
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 9), 100, ETimerMode::OneShot, ReusedHandle), "Slot reuse should succeed");
	MW_EXPECT_EQ(Test, 1u, Manager.TimerCount(), "The reused slot should be occupied");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(200), "Advance past the reused deadline should succeed");
	MW_EXPECT_EQ(Test, std::size_t{5}, Recorder.Count, "The reused one-shot should fire exactly once");
	MW_EXPECT_EQ(Test, 9, Recorder.Identities[4], "The reused timer's identity should be recorded last");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "The reused one-shot must be removed after firing");
}

/**
 * Proves post-dispatch compaction preserves multiple looping survivors around a removed one-shot,
 * and that slot reuse does not restore physical slot order.
 *
 * Schedule order is Looping A, OneShot B, Looping C. The single post-dispatch compaction must
 * keep A and C in their relative order while dropping B, and a replacement D scheduled into B's
 * freed physical slot must dispatch at the logical insertion tail (A, C, D), not between A and C.
 */
MW_TEST_CASE(EngineTimerMixedStableCompactionPreservesSurvivorsAndTailReuse)
{
	FDispatchOrderRecorder Recorder;
	FTestManager Manager{0};
	FTimerHandle HandleA{};
	FTimerHandle HandleB{};
	FTimerHandle HandleC{};

	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 1), 100, ETimerMode::Looping, HandleA), "Looping A should schedule");
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 2), 100, ETimerMode::OneShot, HandleB), "OneShot B should schedule");
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 3), 100, ETimerMode::Looping, HandleC), "Looping C should schedule");
	MW_EXPECT_EQ(Test, 3u, Manager.TimerCount(), "A, B, and C should occupy three slots");

	const std::uint16_t SlotIndexOfB = HandleB.Index;

	// All three share the deadline 100; the single Advance fires them in insertion order A, B, C.
	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "First Advance at the shared deadline should succeed");
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
	FTimerHandle HandleD{};
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeOrderCallback(Recorder, 4), 100, ETimerMode::OneShot, HandleD), "Replacement D should schedule");
	MW_EXPECT_EQ(Test, SlotIndexOfB, HandleD.Index, "D must reuse B's freed physical slot");
	MW_EXPECT_TRUE(Test, HandleD.Generation != HandleB.Generation, "D must publish a fresh generation distinct from B's retired handle");
	MW_EXPECT_EQ(Test, 3u, Manager.TimerCount(), "A, C, and D should occupy three slots after reuse");

	// At Now=200 the loopers A and C refire (their Now-derived deadline is 100+100=200), and D's
	// one-shot deadline is 100+100=200 as well. Stable order must be A, C, D, proving that compaction
	// preserved A and C and that D dispatches at the tail rather than in B's old slot position.
	MW_EXPECT_SUCCESS(Test, Manager.Advance(200), "Second Advance at the shared deadline should succeed");
	MW_EXPECT_EQ(Test, std::size_t{6}, Recorder.Count, "A, C, and D should all fire on the second Advance");
	MW_EXPECT_EQ(Test, 1, Recorder.Identities[3], "Looping A should fire first again after compaction");
	MW_EXPECT_EQ(Test, 3, Recorder.Identities[4], "Looping C should retain its position ahead of the reused slot");
	MW_EXPECT_EQ(Test, 4, Recorder.Identities[5], "The reused-slot replacement D should dispatch at the insertion tail");

	// Only the two loopers remain after the second Advance; D completed and was removed.
	MW_EXPECT_EQ(Test, 2u, Manager.TimerCount(), "Only the two looping survivors A and C should remain after D completes");

	// D's handle is now stale for the same reason B's was.
	MW_EXPECT_EQ(Test, ETimerResult::StaleHandle, Manager.Cancel(HandleD), "The completed replacement D handle must be stale");
}

// ---------------------------------------------------------------------------
// Category 8: Mutation rules during dispatch
// ---------------------------------------------------------------------------

/** Records the result of one attempted mutation performed from inside a callback. */
struct FCapturedMutation final
{
	/** Holds the result of the attempted in-callback operation. */
	ETimerResult Result{ETimerResult::Success};

	/** Remembers whether the captured operation has executed. */
	bool bObserved{false};
};

/** Proves a callback-issued Schedule returns DispatchLocked, preserves the delegate, and clears the canary output handle. */
MW_TEST_CASE(EngineTimerCallbackScheduleRejectedAndDelegatePreserved)
{
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
			CapturedSchedule.Result = Manager.Schedule(std::move(RejectedDelegate), 100, ETimerMode::OneShot, RejectedHandle);
			CapturedSchedule.bObserved = true;
		});

	const ETimerResult ScheduleResult = Manager.Schedule(std::move(FirstCallback), 100, ETimerMode::OneShot, FirstHandle);
	MW_EXPECT_SUCCESS(Test, ScheduleResult, "The observing timer should schedule successfully");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance should succeed and fire the observing callback");

	MW_EXPECT_TRUE(Test, CapturedSchedule.bObserved, "The callback should have executed");
	MW_EXPECT_EQ(Test, ETimerResult::DispatchLocked, CapturedSchedule.Result, "In-callback Schedule must return DispatchLocked");
	MW_EXPECT_TRUE(Test, !RejectedHandle.IsValid(), "The rejected in-callback schedule must clear the canary output handle");
	MW_EXPECT_TRUE(Test, RejectedDelegate.IsBound(), "The rejected in-callback schedule must leave its delegate bound to the caller");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "The rejected in-callback schedule must not change occupancy");
}

/** Proves in-callback cancellation is rejected while another due timer still fires later in the same Advance. */
MW_TEST_CASE(EngineTimerCallbackCancellationRejectedAndOtherTimerStillFires)
{
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

	MW_EXPECT_SUCCESS(Test, Manager.Schedule(std::move(LoopingCallback), 100, ETimerMode::Looping, LoopingHandle), "Looping timer should schedule");
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeOrderCallback(Recorder, 7), 100, ETimerMode::OneShot, OneShotHandle), "One-shot timer should schedule");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance at the shared deadline should succeed");
	MW_EXPECT_TRUE(Test, SelfCancel.bObserved, "The looping callback should have executed");
	MW_EXPECT_EQ(Test, ETimerResult::DispatchLocked, SelfCancel.Result, "In-callback self-cancel must return DispatchLocked");
	MW_EXPECT_EQ(Test, ETimerResult::DispatchLocked, OtherCancel.Result, "In-callback other-cancel must return DispatchLocked");
	MW_EXPECT_EQ(Test, std::size_t{1}, Recorder.Count, "The other due timer must still fire after the rejected cancels");
	MW_EXPECT_EQ(Test, 7, Recorder.Identities[0], "The other due timer's identity should be recorded");
	MW_EXPECT_EQ(Test, 1u, Manager.TimerCount(), "The looping timer must remain active after rejected in-callback cancels");
}

/** Proves a nested Advance is rejected without changing accepted time, and a later intermediate time is still accepted. */
MW_TEST_CASE(EngineTimerNestedAdvanceRejected)
{
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

	MW_EXPECT_SUCCESS(Test, Manager.Schedule(std::move(Callback), 100, ETimerMode::OneShot, Handle), "Setup schedule should succeed");
	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Outer Advance to 100 should succeed and fire the callback");

	MW_EXPECT_TRUE(Test, NestedAdvance.bObserved, "The callback should have executed");
	MW_EXPECT_EQ(Test, ETimerResult::DispatchLocked, NestedAdvance.Result, "A nested Advance must return DispatchLocked");

	// After the outer dispatch ends, the manager must still accept a time between the outer accepted
	// time (100) and the rejected nested value (200). This proves the rejected nested Advance changed
	// nothing about the stored clock or transactional rollback boundary.
	const ETimerResult IntermediateAdvanceResult = Manager.Advance(150);
	MW_EXPECT_SUCCESS(Test, IntermediateAdvanceResult, "Advance to 150 must succeed after the rejected nested Advance to 200");
}

// ---------------------------------------------------------------------------
// Category 9: Allocation-free steady-state operation
// ---------------------------------------------------------------------------

/** Proves Schedule, Advance dispatch, Cancel, slot reuse, and looping operation perform no observable allocation. */
MW_TEST_CASE(EngineTimerOperationsPerformNoObservableAllocation)
{
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle OneShotHandle{};
	FTimerHandle LoopingHandle{};

	const std::uint32_t AllocationsBefore = GlobalAllocationCount;

	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeCounterCallback(Counter), 100, ETimerMode::OneShot, OneShotHandle), "One-shot schedule should succeed");
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeCounterCallback(Counter), 50, ETimerMode::Looping, LoopingHandle), "Looping schedule should succeed");
	MW_EXPECT_EQ(Test, 2u, Manager.TimerCount(), "Two schedules should occupy two slots");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(50), "Advance to 50 should succeed");
	MW_EXPECT_EQ(Test, 1u, Counter.Count, "Only the looping timer should fire at 50");
	MW_EXPECT_EQ(Test, 2u, Manager.TimerCount(), "The looping timer stays active after firing");

	MW_EXPECT_SUCCESS(Test, Manager.Advance(100), "Advance to 100 should succeed");
	// At Now=100: the looping timer (period 50, refired deadline 100) and the one-shot (deadline 100) both fire.
	MW_EXPECT_EQ(Test, 3u, Counter.Count, "Both the looping refire and the one-shot completion fire at 100");
	MW_EXPECT_EQ(Test, 1u, Manager.TimerCount(), "Only the looping timer remains after the one-shot fires");

	MW_EXPECT_SUCCESS(Test, Manager.Cancel(LoopingHandle), "Canceling the looping timer should succeed");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "Cancellation should release the looping slot");

	// Reuse the freed slot and actually dispatch the reused one-shot at its calculated deadline.
	FTimerHandle ReusedHandle{};
	MW_EXPECT_SUCCESS(Test, Manager.Schedule(MakeCounterCallback(Counter), 100, ETimerMode::OneShot, ReusedHandle), "Reused schedule should succeed");
	MW_EXPECT_EQ(Test, 1u, Manager.TimerCount(), "The reused slot should be occupied");
	// The reused one-shot deadline is LastAcceptedNow (100) + 100 = 200; prove it actually fires there.
	MW_EXPECT_SUCCESS(Test, Manager.Advance(200), "Advance to the reused calculated deadline should succeed");
	MW_EXPECT_EQ(Test, 4u, Counter.Count, "The reused one-shot should fire exactly once at its calculated deadline");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "The reused one-shot must be removed after firing");

	const std::uint32_t AllocationsAfter = GlobalAllocationCount;
	MW_EXPECT_EQ(Test, AllocationsBefore, AllocationsAfter, "Timer schedule, dispatch, cancel, reuse, and looping must not allocate");
}

} // namespace
