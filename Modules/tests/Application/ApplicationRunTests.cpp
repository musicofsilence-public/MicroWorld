#include "TestSupport.h"

#include <MicroWorld/Application/Application.h>

#include <cstddef>
#include <initializer_list>
#include <utility>

namespace MicroWorld::Tests
{

namespace
{

	/** Motivation: File-static counter for the free noexcept pacing function, since FSleepFunction cannot bind a member. */
	int GPacingCallCount{0};

	/** Motivation: Maximum scripted clock/timestamp entries one test drives, bounding both fixtures without dynamic storage. */
	constexpr std::size_t MaximumScriptedEntries = 16;

	/** Motivation: Capacity of the recorded observed-tick array, sized to the most frames any runner test drives. */
	constexpr std::size_t MaximumObservedTickTimestamps = 16;

	/**
	 * Motivation: Free noexcept pacing function that counts calls without sleeping.
	 * Responsibilities: The signature must match FSleepFunction exactly, so the call counter cannot live on a member and the
	 *   runner exercises the same binding a real platform sleep would use.
	 */
	void CountingSleepFunction(MicroWorld::Core::DurationMilliseconds) noexcept
	{
		++GPacingCallCount;
	}

	/**
	 * Motivation: Resets the file-static pacing counter before one test observes its value.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void ResetPacingCounter() noexcept
	{
		GPacingCallCount = 0;
	}

	/**
	 * Motivation: Returns a scripted monotonic clock sequence so tests can reason about which Now value reached each
	 *   hook. Hold one by reference and call Now() repeatedly; the sequence advances by one entry per call
	 *   so the runner's begin-then-frames split is deterministic.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 * Example:
	 *   // Construct and exercise the type in one behavior test.
	 */
	class FScriptedClock
	{
	public:
		/**
		 * Motivation: Seeds the clock with the monotonic values the test expects BeginPlay and each Advance to see.
		 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
		 */
		explicit FScriptedClock(std::initializer_list<MicroWorld::Core::TimePointMilliseconds> InValues) noexcept
		{
			// Copying under the capacity check keeps an over-long list from writing past Sequence.
			for (const MicroWorld::Core::TimePointMilliseconds Value : InValues)
			{
				if (ValueCount == MaximumScriptedValues)
				{
					break;
				}
				Sequence[ValueCount++] = Value;
			}
		}

		/**
		 * Motivation: Returns the next scripted value, then holds the last one for every later call.
		 * Responsibilities: Holding rather than extrapolating keeps every reading monotonic without inventing timestamps a test
		 *   never scripted; FApplication accepts a repeated timestamp.
		 */
		MicroWorld::Core::TimePointMilliseconds Now() noexcept
		{
			const MicroWorld::Core::TimePointMilliseconds ScriptedValue = Sequence[CallIndex];
			if (CallIndex + 1 < ValueCount)
			{
				++CallIndex;
			}
			return ScriptedValue;
		}

	private:
		/** Motivation: Caps the scripted sequence so one test drives many frames without unbounded storage. */
		static constexpr std::size_t MaximumScriptedValues = MaximumScriptedEntries;

		/** Motivation: Holds the scripted monotonic values the test selected. */
		MicroWorld::Core::TimePointMilliseconds Sequence[MaximumScriptedValues]{};

		/** Motivation: Counts how many scripted values are populated, so unused tail slots are never read. */
		std::size_t ValueCount{0};

		/** Motivation: Tracks the next value Now() will return. */
		std::size_t CallIndex{0};
	};

	/**
	 * Motivation: Runtime double whose Tick stops the runner on a configured frame and records observed timestamps.
	 *   Tick returns non-Success
	 * once TickCount reaches the configured stop frame, so Run returns instead of looping forever; the observed-timestamp vector is what
	 * RunnerFeedsClockValuesIntoTick inspects. BeginPlay can also be configured to fail so a failed-begin run is reachable from a test.
	 *
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 *
	 * Example: FScriptedEngineRuntime Runtime;
	 */
	class FScriptedEngineRuntime final : public MicroWorld::Engine::IEngineRuntime
	{
	public:
		/**
		 * Motivation: Run terminates.
		 * Responsibilities: Selects the zero-based Tick index that returns non-Success.
		 */
		void ConfigureStopOnFrame(int InFrameIndex) noexcept { StopOnFrameIndex = InFrameIndex; }

		/**
		 * Motivation: A failed-begin run is reachable from a test.
		 * Responsibilities: Selects the result BeginPlay returns.
		 */
		void ConfigureBeginPlayResult(MicroWorld::Core::ERuntimeResult InResult) noexcept { ConfiguredBeginPlayResult = InResult; }

		/** Motivation: Holds the Now timestamps Tick observed, so a test can assert the scripted clock reached frames. */
		MicroWorld::Core::TimePointMilliseconds ObservedTickTimestamps[MaximumObservedTickTimestamps]{};

		/** Motivation: Counts how many frames Tick accepted, so a test can size observed timestamps and pacing calls. */
		int TickCount{0};

		/** Motivation: Counts BeginPlay calls so a test can confirm the runner begins the engine exactly once. */
		int BeginPlayCount{0};

		/** Motivation: Counts EndPlay calls so a test can confirm the runner ends exactly once after a stopping frame. */
		int EndPlayCount{0};

		MicroWorld::Core::ERuntimeResult BeginPlay(MicroWorld::Core::TimePointMilliseconds) noexcept override
		{
			++BeginPlayCount;
			return ConfiguredBeginPlayResult;
		}

		MicroWorld::Core::ERuntimeResult Tick(MicroWorld::Core::TimePointMilliseconds InNowMilliseconds) noexcept override
		{
			if (static_cast<std::size_t>(TickCount) < MaximumObservedTickTimestamps)
			{
				ObservedTickTimestamps[static_cast<std::size_t>(TickCount)] = InNowMilliseconds;
			}
			if (TickCount == StopOnFrameIndex)
			{
				++TickCount;
				return MicroWorld::Core::ERuntimeResult::CapacityExceeded;
			}
			++TickCount;
			return MicroWorld::Core::ERuntimeResult::Success;
		}

		MicroWorld::Core::ERuntimeResult EndPlay() noexcept override
		{
			++EndPlayCount;
			return MicroWorld::Core::ERuntimeResult::Success;
		}

	private:
		/** Motivation: Holds the frame index at which Tick stops the run, so Run always returns in tests. */
		int StopOnFrameIndex{0};

		/** Motivation: Holds the result BeginPlay returns, seeded to Success so the happy-path runs need no setup. */
		MicroWorld::Core::ERuntimeResult ConfiguredBeginPlayResult{MicroWorld::Core::ERuntimeResult::Success};
	};

	/**
	 * Motivation: Minimal FApplication subclass bound to the scripted engine; OnConfigure always succeeds so the
	 *   runner's begin-then-frames loop is driven entirely by the engine's BeginPlay/Tick/EndPlay.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 * Example:
	 *   // Construct and exercise the type in one behavior test.
	 */
	class FRunnerApplication final : public MicroWorld::Application::FApplication
	{
	public:
		explicit FRunnerApplication(MicroWorld::Engine::IEngineRuntime& InEngineRuntime) noexcept
			: MicroWorld::Application::FApplication(InEngineRuntime)
		{
		}

		int BeginPlayFailedCount{0};

	protected:
		MicroWorld::Core::ERuntimeResult OnConfigure(MicroWorld::Core::TimePointMilliseconds) noexcept override
		{
			return MicroWorld::Core::ERuntimeResult::Success;
		}

		void OnBeginPlayFailed() noexcept override { ++BeginPlayFailedCount; }
	};

} // namespace

/**
 * Motivation: Run the application against a scripted engine whose Tick returns non-Success on the first frame.
 * Responsibilities: Run returns that frame's non-Success result as soon as the run stops.
 */
MW_TEST_CASE(RunnerStopsOnFirstNonSuccessFrameAndReturnsIt)
{
	// Arrange
	FScriptedClock Clock{10, 20, 30};
	FScriptedEngineRuntime EngineRuntime;
	EngineRuntime.ConfigureStopOnFrame(1);
	FRunnerApplication Application{EngineRuntime};

	// Act
	const MicroWorld::Core::ERuntimeResult StopResult = Application.Run(Clock, &CountingSleepFunction, 1);

	// Assert
	MW_EXPECT_EQ(Test, MicroWorld::Core::ERuntimeResult::CapacityExceeded, StopResult, "Runner should return the stopping frame's result");
}

/**
 * Motivation: Run the application against a scripted engine that stops on the first frame.
 * Responsibilities: The engine EndPlay is invoked exactly once after the stopping frame.
 */
MW_TEST_CASE(RunnerEndsPlayAfterAStoppingFrame)
{
	// Arrange
	FScriptedClock Clock{10, 20, 30};
	FScriptedEngineRuntime EngineRuntime;
	EngineRuntime.ConfigureStopOnFrame(0);
	FRunnerApplication Application{EngineRuntime};

	// Act
	(void)Application.Run(Clock, &CountingSleepFunction, 1);

	// Assert
	MW_EXPECT_EQ(Test, 1, EngineRuntime.EndPlayCount, "Runner should invoke EndPlay once after a stopping frame");
}

/**
 * Motivation: Run the application against a scripted engine whose BeginPlay fails.
 * Responsibilities: Run returns the begin failure result without ever invoking EndPlay.
 */
MW_TEST_CASE(RunnerDoesNotEndPlayAfterFailedBeginPlay)
{
	// Arrange
	FScriptedClock Clock{10, 20, 30};
	FScriptedEngineRuntime EngineRuntime;
	EngineRuntime.ConfigureBeginPlayResult(MicroWorld::Core::ERuntimeResult::CapacityExceeded);
	FRunnerApplication Application{EngineRuntime};

	// Act
	const MicroWorld::Core::ERuntimeResult StopResult = Application.Run(Clock, &CountingSleepFunction, 1);

	// Assert
	MW_EXPECT_EQ(Test, MicroWorld::Core::ERuntimeResult::CapacityExceeded, StopResult, "Runner should return the begin failure result");
	MW_EXPECT_EQ(Test, 0, EngineRuntime.EndPlayCount, "Runner must not invoke EndPlay after a failed begin");
}

/**
 * Motivation: Run the application across several successful frames until a configured frame stops the run.
 * Responsibilities: Pacing fires exactly once per successful frame and never for the stopping frame.
 */
MW_TEST_CASE(RunnerSleepsOncePerSuccessfulFrameOnly)
{
	// Arrange
	ResetPacingCounter();
	FScriptedClock Clock{10, 20, 30, 40};
	FScriptedEngineRuntime EngineRuntime;
	EngineRuntime.ConfigureStopOnFrame(2);
	FRunnerApplication Application{EngineRuntime};

	// Act
	(void)Application.Run(Clock, &CountingSleepFunction, 1);

	// Assert
	MW_EXPECT_EQ(Test, 2, GPacingCallCount, "Pacing should fire once per successful frame only");
}

/**
 * Motivation: Run the application with a scripted clock and a scripted engine that stops on the first frame.
 * Responsibilities: Each engine Tick sees the next scripted clock value; begin consumes the first reading before the
 *   first Tick.
 */
MW_TEST_CASE(RunnerFeedsClockValuesIntoTick)
{
	// Arrange
	FScriptedClock Clock{10, 20, 30};
	FScriptedEngineRuntime EngineRuntime;
	EngineRuntime.ConfigureStopOnFrame(1);
	FRunnerApplication Application{EngineRuntime};

	// Act
	(void)Application.Run(Clock, &CountingSleepFunction, 1);

	// Assert
	const bool bFirstFrameSawSecondClockValue = EngineRuntime.ObservedTickTimestamps[0] == MicroWorld::Core::TimePointMilliseconds{20};
	const bool bSecondFrameSawThirdClockValue = EngineRuntime.ObservedTickTimestamps[1] == MicroWorld::Core::TimePointMilliseconds{30};
	MW_EXPECT_TRUE(Test, bFirstFrameSawSecondClockValue, "First Tick should see the second clock value (begin consumed the first)");
	MW_EXPECT_TRUE(Test, bSecondFrameSawThirdClockValue, "Second Tick should see the third clock value");
}

} // namespace MicroWorld::Tests
