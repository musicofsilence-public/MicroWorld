#include "TestSupport.h"

#include <MicroWorld/Application.h>
#include <MicroWorld/ApplicationRunner.h>

#include <array>
#include <cstddef>
#include <initializer_list>

namespace MicroWorld::Tests
{

namespace
{

	/** File-static counter for the free noexcept pacing function, since FSleepFunction cannot bind a member. */
	int GPacingCallCount{0};

	/**
	 * Free noexcept pacing function that counts calls without sleeping.
	 *
	 * The signature must match FSleepFunction exactly, so the call counter cannot live on a member
	 * and the runner exercises the same binding a real platform sleep would use.
	 */
	void CountingSleepFunction(MicroWorld::DurationMilliseconds) noexcept
	{
		++GPacingCallCount;
	}

	/** Resets the file-static pacing counter before one test observes its value. */
	void ResetPacingCounter() noexcept
	{
		GPacingCallCount = 0;
	}

	/**
	 * Returns a scripted monotonic clock sequence so tests can reason about which Now value reached each hook.
	 *
	 * Hold one by reference and call Now() repeatedly; the sequence advances by one entry per call so the
	 * runner's begin-then-frames split is deterministic.
	 */
	class FScriptedClock
	{
	public:
		/** Seeds the clock with the monotonic values the test expects BeginPlay and each Advance to see. */
		explicit FScriptedClock(std::initializer_list<MicroWorld::TimePointMilliseconds> Values) noexcept
		{
			// Copying under the capacity check keeps an over-long list from writing past Sequence.
			for (const MicroWorld::TimePointMilliseconds Value : Values)
			{
				if (ValueCount == MaximumScriptedValues)
				{
					break;
				}
				Sequence[ValueCount++] = Value;
			}
		}

		/**
		 * Returns the next scripted value, then holds the last one for every later call.
		 *
		 * Holding rather than extrapolating keeps every reading monotonic without inventing
		 * timestamps a test never scripted; FApplication accepts a repeated timestamp.
		 */
		MicroWorld::TimePointMilliseconds Now() noexcept
		{
			const MicroWorld::TimePointMilliseconds ScriptedValue = Sequence[CallIndex];
			if (CallIndex + 1 < ValueCount)
			{
				++CallIndex;
			}
			return ScriptedValue;
		}

	private:
		/** Caps the scripted sequence so one test drives many frames without unbounded storage. */
		static constexpr std::size_t MaximumScriptedValues = 16;

		/** Holds the scripted monotonic values the test selected. */
		std::array<MicroWorld::TimePointMilliseconds, MaximumScriptedValues> Sequence{};

		/** Counts how many scripted values are populated, so unused tail slots are never read. */
		std::size_t ValueCount{0};

		/** Tracks the next value Now() will return. */
		std::size_t CallIndex{0};
	};

	/**
	 * FApplication double whose Advance stops the runner on a configured frame and records observed timestamps.
	 *
	 * OnAdvance returns non-Success once FrameIndex reaches the configured stop frame, so Run returns instead
	 * of looping forever; the observed-timestamp vector is what RunnerFeedsClockValuesIntoAdvance inspects.
	 */
	class FScriptedApplication final : public MicroWorld::FApplication
	{
	public:
		/** Selects the zero-based Advance index that returns non-Success so Run terminates. */
		void ConfigureStopOnFrame(int FrameIndex) noexcept { StopOnFrameIndex = FrameIndex; }

		/** Selects the result OnBeginPlay returns, so a failed-begin run is reachable from a test. */
		void ConfigureBeginResult(MicroWorld::ERuntimeResult Result) noexcept { ConfiguredBeginResult = Result; }

		/** Records whether the rollback hook fired after a failed begin. */
		int BeginPlayFailedCount{0};

		/** Records whether OnEndPlay ran, since the runner must skip it after a failed begin. */
		int EndPlayCount{0};

		/** Holds the Now timestamps OnAdvance observed, so a test can assert the scripted clock reached frames. */
		std::array<MicroWorld::TimePointMilliseconds, 16> ObservedAdvanceTimestamps{};

		/** Counts how many frames OnAdvance accepted, so a test can size observed timestamps and pacing calls. */
		int AdvanceCount{0};

	protected:
		MicroWorld::ERuntimeResult OnBeginPlay(MicroWorld::TimePointMilliseconds) override { return ConfiguredBeginResult; }

		void OnBeginPlayFailed() noexcept override { ++BeginPlayFailedCount; }

		MicroWorld::ERuntimeResult OnAdvance(MicroWorld::TimePointMilliseconds NowMilliseconds) override
		{
			if (static_cast<std::size_t>(AdvanceCount) < ObservedAdvanceTimestamps.size())
			{
				ObservedAdvanceTimestamps[static_cast<std::size_t>(AdvanceCount)] = NowMilliseconds;
			}
			if (AdvanceCount == StopOnFrameIndex)
			{
				++AdvanceCount;
				return MicroWorld::ERuntimeResult::CapacityExceeded;
			}
			++AdvanceCount;
			return MicroWorld::ERuntimeResult::Success;
		}

		void OnEndPlay() override { ++EndPlayCount; }

	private:
		/** Holds the frame index at which OnAdvance stops the run, so Run always returns in tests. */
		int StopOnFrameIndex{0};

		/** Holds the result OnBeginPlay returns, seeded to Success so the happy-path runs need no setup. */
		MicroWorld::ERuntimeResult ConfiguredBeginResult{MicroWorld::ERuntimeResult::Success};
	};

} // namespace

/** Proves the runner returns the frame's non-Success result as soon as one frame stops the run. */
MW_TEST_CASE(RunnerStopsOnFirstNonSuccessFrameAndReturnsIt)
{
	FScriptedClock Clock{10, 20, 30};
	FScriptedApplication Application;
	Application.ConfigureStopOnFrame(1);
	MicroWorld::TApplicationRunner<FScriptedClock> Runner{Clock, &CountingSleepFunction, 1};

	const MicroWorld::ERuntimeResult StopResult = Runner.Run(Application);

	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::CapacityExceeded, StopResult, "Runner should return the stopping frame's result");
}

/** Proves the runner ends play exactly once after a frame stops the run. */
MW_TEST_CASE(RunnerEndsPlayAfterAStoppingFrame)
{
	FScriptedClock Clock{10, 20, 30};
	FScriptedApplication Application;
	Application.ConfigureStopOnFrame(0);
	MicroWorld::TApplicationRunner<FScriptedClock> Runner{Clock, &CountingSleepFunction, 1};

	(void)Runner.Run(Application);

	MW_EXPECT_EQ(Test, 1, Application.EndPlayCount, "Runner should invoke EndPlay once after a stopping frame");
}

/** Proves a failed begin returns the begin result without ever invoking OnEndPlay. */
MW_TEST_CASE(RunnerDoesNotEndPlayAfterFailedBeginPlay)
{
	FScriptedClock Clock{10, 20, 30};
	FScriptedApplication Application;
	Application.ConfigureBeginResult(MicroWorld::ERuntimeResult::CapacityExceeded);
	MicroWorld::TApplicationRunner<FScriptedClock> Runner{Clock, &CountingSleepFunction, 1};

	const MicroWorld::ERuntimeResult StopResult = Runner.Run(Application);

	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::CapacityExceeded, StopResult, "Runner should return the begin failure result");
	MW_EXPECT_EQ(Test, 0, Application.EndPlayCount, "Runner must not invoke EndPlay after a failed begin");
}

/** Proves pacing fires once per successful frame only, never for the stopping frame. */
MW_TEST_CASE(RunnerSleepsOncePerSuccessfulFrameOnly)
{
	ResetPacingCounter();
	FScriptedClock Clock{10, 20, 30, 40};
	FScriptedApplication Application;
	Application.ConfigureStopOnFrame(2);
	MicroWorld::TApplicationRunner<FScriptedClock> Runner{Clock, &CountingSleepFunction, 1};

	(void)Runner.Run(Application);

	MW_EXPECT_EQ(Test, 2, GPacingCallCount, "Pacing should fire once per successful frame only");
}

/** Proves the runner feeds the scripted clock values into each OnAdvance, with begin consuming the first reading. */
MW_TEST_CASE(RunnerFeedsClockValuesIntoAdvance)
{
	FScriptedClock Clock{10, 20, 30};
	FScriptedApplication Application;
	Application.ConfigureStopOnFrame(1);
	MicroWorld::TApplicationRunner<FScriptedClock> Runner{Clock, &CountingSleepFunction, 1};

	(void)Runner.Run(Application);

	const bool bFirstFrameSawSecondClockValue = Application.ObservedAdvanceTimestamps[0] == MicroWorld::TimePointMilliseconds{20};
	const bool bSecondFrameSawThirdClockValue = Application.ObservedAdvanceTimestamps[1] == MicroWorld::TimePointMilliseconds{30};
	MW_EXPECT_TRUE(Test, bFirstFrameSawSecondClockValue, "First Advance should see the second clock value (begin consumed the first)");
	MW_EXPECT_TRUE(Test, bSecondFrameSawThirdClockValue, "Second Advance should see the third clock value");
}

} // namespace MicroWorld::Tests
