#include "TestSupport.h"

#include <MicroWorld/Application.h>

namespace MicroWorld::Tests
{

namespace
{

	/**
	 * Records every lifecycle hook invocation so FApplication's contract is observed behaviourally.
	 *
	 * Carries one configurable begin result so a test can drive the failed-begin path without
	 * duplicating the application base's own state machine.
	 */
	class FCountingApplication final : public MicroWorld::FApplication
	{
	public:
		/** Drives the next OnBeginPlay result so the failed-begin path is reachable from a test. */
		void ConfigureBeginResult(MicroWorld::ERuntimeResult Result) noexcept { ConfiguredBeginResult = Result; }

		/** Observes how many times OnBeginPlay fired, since double-begin must not re-invoke it. */
		int BeginPlayCount{0};

		/** Observes whether the rollback hook fired exactly once after a failed begin. */
		int BeginPlayFailedCount{0};

		/** Observes whether OnAdvance was bypassed for rejected lifecycle or backward-time calls. */
		int AdvanceCount{0};

		/** Observes whether OnEndPlay fired exactly once across repeated EndPlay calls. */
		int EndPlayCount{0};

	protected:
		MicroWorld::ERuntimeResult OnBeginPlay(MicroWorld::TimePointMilliseconds) override
		{
			++BeginPlayCount;
			return ConfiguredBeginResult;
		}

		void OnBeginPlayFailed() noexcept override { ++BeginPlayFailedCount; }

		MicroWorld::ERuntimeResult OnAdvance(MicroWorld::TimePointMilliseconds) override
		{
			++AdvanceCount;
			return MicroWorld::ERuntimeResult::Success;
		}

		void OnEndPlay() override { ++EndPlayCount; }

	private:
		/** Holds the result OnBeginPlay will return, seeded to Success so the happy path needs no setup. */
		MicroWorld::ERuntimeResult ConfiguredBeginResult{MicroWorld::ERuntimeResult::Success};
	};

} // namespace

/** Proves a first begin runs the consumer hook exactly once and reports success. */
MW_TEST_CASE(ApplicationBeginPlayInvokesConsumerHookOnce)
{
	FCountingApplication Application;

	const MicroWorld::ERuntimeResult BeginResult = Application.BeginPlay(100);

	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::Success, BeginResult, "First BeginPlay should succeed");
	MW_EXPECT_EQ(Test, 1, Application.BeginPlayCount, "First BeginPlay should invoke the consumer hook once");
}

/** Proves a failed begin fires the rollback hook and leaves the lifecycle terminal for both later phases. */
MW_TEST_CASE(ApplicationFailedBeginInvokesFailureHookAndLatchesTerminal)
{
	FCountingApplication Application;
	Application.ConfigureBeginResult(MicroWorld::ERuntimeResult::CapacityExceeded);

	const MicroWorld::ERuntimeResult BeginResult = Application.BeginPlay(100);
	const MicroWorld::ERuntimeResult AdvanceAfterFailedBeginResult = Application.Advance(100);
	const MicroWorld::ERuntimeResult EndAfterFailedBeginResult = Application.EndPlay();

	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::CapacityExceeded, BeginResult, "Failed begin should surface the consumer result");
	MW_EXPECT_EQ(Test, 1, Application.BeginPlayFailedCount, "Failed begin should invoke the rollback hook once");
	MW_EXPECT_EQ(
		Test, MicroWorld::ERuntimeResult::InvalidLifecycle, AdvanceAfterFailedBeginResult, "Advance after a failed begin should be rejected");
	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::InvalidLifecycle, EndAfterFailedBeginResult, "EndPlay after a failed begin should be rejected");
}

/** Proves a second begin is rejected by the lifecycle guard without re-invoking the consumer hook. */
MW_TEST_CASE(ApplicationSecondBeginPlayIsRejected)
{
	FCountingApplication Application;
	Application.BeginPlay(100);

	const MicroWorld::ERuntimeResult SecondBeginResult = Application.BeginPlay(100);

	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::InvalidLifecycle, SecondBeginResult, "Second BeginPlay should be rejected");
	MW_EXPECT_EQ(Test, 1, Application.BeginPlayCount, "Second BeginPlay should not re-invoke the consumer hook");
}

/** Proves advancing before any begin is rejected and never reaches the consumer frame hook. */
MW_TEST_CASE(ApplicationAdvanceBeforeBeginPlayIsRejected)
{
	FCountingApplication Application;

	const MicroWorld::ERuntimeResult AdvanceResult = Application.Advance(100);

	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::InvalidLifecycle, AdvanceResult, "Advance before BeginPlay should be rejected");
	MW_EXPECT_EQ(Test, 0, Application.AdvanceCount, "Advance before BeginPlay should not invoke the consumer hook");
}

/** Proves a backward timestamp is rejected and the consumer hook is not invoked for that call. */
MW_TEST_CASE(ApplicationAdvanceRejectsBackwardTime)
{
	FCountingApplication Application;
	Application.BeginPlay(100);
	Application.Advance(100);

	const MicroWorld::ERuntimeResult BackwardResult = Application.Advance(99);

	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::NonMonotonicTime, BackwardResult, "Backward time should be rejected");
	MW_EXPECT_EQ(Test, 1, Application.AdvanceCount, "Backward Advance should not invoke the consumer hook");
}

/** Proves an unchanged timestamp is monotonic-equivalent and still dispatches the consumer frame. */
MW_TEST_CASE(ApplicationAdvanceAcceptsRepeatedSameTimestamp)
{
	FCountingApplication Application;
	Application.BeginPlay(100);
	Application.Advance(100);

	const MicroWorld::ERuntimeResult RepeatedTimeResult = Application.Advance(100);

	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::Success, RepeatedTimeResult, "Repeated timestamp should be accepted as monotonic");
	MW_EXPECT_EQ(Test, 2, Application.AdvanceCount, "Repeated-timestamp Advance should still invoke the consumer hook");
}

/** Proves EndPlay succeeds once and a second call is a no-op that does not re-run the consumer hook. */
MW_TEST_CASE(ApplicationEndPlayIsIdempotent)
{
	FCountingApplication Application;
	Application.BeginPlay(100);

	const MicroWorld::ERuntimeResult FirstEndResult = Application.EndPlay();
	const MicroWorld::ERuntimeResult SecondEndResult = Application.EndPlay();

	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::Success, FirstEndResult, "First EndPlay should succeed");
	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::Success, SecondEndResult, "Second EndPlay should remain successful");
	MW_EXPECT_EQ(Test, 1, Application.EndPlayCount, "Idempotent EndPlay should invoke the consumer hook once");
}

/** Proves advancing after end is rejected and never reaches the consumer frame hook. */
MW_TEST_CASE(ApplicationAdvanceAfterEndPlayIsRejected)
{
	FCountingApplication Application;
	Application.BeginPlay(100);
	Application.EndPlay();

	const MicroWorld::ERuntimeResult AdvanceResult = Application.Advance(100);

	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::InvalidLifecycle, AdvanceResult, "Advance after EndPlay should be rejected");
	MW_EXPECT_EQ(Test, 0, Application.AdvanceCount, "Advance after EndPlay should not invoke the consumer hook");
}

} // namespace MicroWorld::Tests
