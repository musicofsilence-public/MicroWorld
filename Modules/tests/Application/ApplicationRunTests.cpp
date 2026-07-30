#include "TestSupport.h"

#include <MicroWorld/Application/Application.h>
#include <MicroWorld/Engine/ObjectStore.h>

#include <cstddef>
#include <initializer_list>
#include <utility>

namespace MicroWorld::Tests
{

namespace
{

	/** File-static counter for the free noexcept pacing function, since FSleepFunction cannot bind a member. */
	int GPacingCallCount{0};

	/** Maximum scripted clock/timestamp entries one test drives, bounding both fixtures without dynamic storage. */
	constexpr std::size_t MaximumScriptedEntries = 16;

	/** Capacity of the recorded observed-tick array, sized to the most frames any runner test drives. */
	constexpr std::size_t MaximumObservedTickTimestamps = 16;

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
		explicit FScriptedClock(std::initializer_list<MicroWorld::TimePointMilliseconds> InValues) noexcept
		{
			// Copying under the capacity check keeps an over-long list from writing past Sequence.
			for (const MicroWorld::TimePointMilliseconds Value : InValues)
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
		static constexpr std::size_t MaximumScriptedValues = MaximumScriptedEntries;

		/** Holds the scripted monotonic values the test selected. */
		MicroWorld::TimePointMilliseconds Sequence[MaximumScriptedValues]{};

		/** Counts how many scripted values are populated, so unused tail slots are never read. */
		std::size_t ValueCount{0};

		/** Tracks the next value Now() will return. */
		std::size_t CallIndex{0};
	};

	/**
	 * IEngine double whose Tick stops the runner on a configured frame and records observed timestamps.
	 *
	 * Tick returns non-Success once TickCount reaches the configured stop frame, so Run returns instead
	 * of looping forever; the observed-timestamp vector is what RunnerFeedsClockValuesIntoTick inspects.
	 * BeginPlay can also be configured to fail so a failed-begin run is reachable from a test.
	 */
	class FScriptedEngine final : public MicroWorld::IEngine
	{
	public:
		/** Selects the zero-based Tick index that returns non-Success so Run terminates. */
		void ConfigureStopOnFrame(int InFrameIndex) noexcept { StopOnFrameIndex = InFrameIndex; }

		/** Selects the result BeginPlay returns, so a failed-begin run is reachable from a test. */
		void ConfigureBeginPlayResult(MicroWorld::ERuntimeResult InResult) noexcept { ConfiguredBeginPlayResult = InResult; }

		/** Holds the Now timestamps Tick observed, so a test can assert the scripted clock reached frames. */
		MicroWorld::TimePointMilliseconds ObservedTickTimestamps[MaximumObservedTickTimestamps]{};

		/** Counts how many frames Tick accepted, so a test can size observed timestamps and pacing calls. */
		int TickCount{0};

		/** Counts BeginPlay calls so a test can confirm the runner begins the engine exactly once. */
		int BeginPlayCount{0};

		/** Counts EndPlay calls so a test can confirm the runner ends exactly once after a stopping frame. */
		int EndPlayCount{0};

		MicroWorld::ERuntimeResult BeginPlay(MicroWorld::TimePointMilliseconds) noexcept override
		{
			++BeginPlayCount;
			return ConfiguredBeginPlayResult;
		}

		MicroWorld::ERuntimeResult Tick(MicroWorld::TimePointMilliseconds InNowMilliseconds) noexcept override
		{
			if (static_cast<std::size_t>(TickCount) < MaximumObservedTickTimestamps)
			{
				ObservedTickTimestamps[static_cast<std::size_t>(TickCount)] = InNowMilliseconds;
			}
			if (TickCount == StopOnFrameIndex)
			{
				++TickCount;
				return MicroWorld::ERuntimeResult::CapacityExceeded;
			}
			++TickCount;
			return MicroWorld::ERuntimeResult::Success;
		}

		MicroWorld::ERuntimeResult EndPlay() noexcept override
		{
			++EndPlayCount;
			return MicroWorld::ERuntimeResult::Success;
		}

		MicroWorld::UWorld& GetWorld() noexcept override { return *reinterpret_cast<MicroWorld::UWorld*>(&WorldStorage); }
		MicroWorld::FObjectStore& GetObjectStore() noexcept override { return *reinterpret_cast<MicroWorld::FObjectStore*>(&StoreStorage); }

	private:
		/** Holds the frame index at which Tick stops the run, so Run always returns in tests. */
		int StopOnFrameIndex{0};

		/** Holds the result BeginPlay returns, seeded to Success so the happy-path runs need no setup. */
		MicroWorld::ERuntimeResult ConfiguredBeginPlayResult{MicroWorld::ERuntimeResult::Success};

		/** Raw storage for the world/store pointers the contract requires but these tests never use. */
		std::uint64_t WorldStorage{0};
		std::uint64_t StoreStorage{0};
	};

	/**
	 * Minimal FApplication subclass bound to the scripted engine; OnConfigure always succeeds so the
	 * runner's begin-then-frames loop is driven entirely by the engine's BeginPlay/Tick/EndPlay.
	 */
	class FRunnerApplication final : public MicroWorld::FApplication
	{
	public:
		explicit FRunnerApplication(MicroWorld::IEngine& InEngine) noexcept : MicroWorld::FApplication(InEngine) {}

		int BeginPlayFailedCount{0};

	protected:
		MicroWorld::ERuntimeResult OnConfigure(MicroWorld::IEngine& InEngine, MicroWorld::TimePointMilliseconds) noexcept override
		{
			(void)InEngine;
			return MicroWorld::ERuntimeResult::Success;
		}

		void OnBeginPlayFailed() noexcept override { ++BeginPlayFailedCount; }
	};

} // namespace

/**
 * Scenario: Run the application against a scripted engine whose Tick returns non-Success on the first frame.
 * Expected: Run returns that frame's non-Success result as soon as the run stops.
 */
MW_TEST_CASE(RunnerStopsOnFirstNonSuccessFrameAndReturnsIt)
{
	// Arrange
	FScriptedClock Clock{10, 20, 30};
	FScriptedEngine Engine;
	Engine.ConfigureStopOnFrame(1);
	FRunnerApplication Application{Engine};

	// Act
	const MicroWorld::ERuntimeResult StopResult = Application.Run(Clock, &CountingSleepFunction, 1);

	// Assert
	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::CapacityExceeded, StopResult, "Runner should return the stopping frame's result");
}

/**
 * Scenario: Run the application against a scripted engine that stops on the first frame.
 * Expected: The engine EndPlay is invoked exactly once after the stopping frame.
 */
MW_TEST_CASE(RunnerEndsPlayAfterAStoppingFrame)
{
	// Arrange
	FScriptedClock Clock{10, 20, 30};
	FScriptedEngine Engine;
	Engine.ConfigureStopOnFrame(0);
	FRunnerApplication Application{Engine};

	// Act
	(void)Application.Run(Clock, &CountingSleepFunction, 1);

	// Assert
	MW_EXPECT_EQ(Test, 1, Engine.EndPlayCount, "Runner should invoke EndPlay once after a stopping frame");
}

/**
 * Scenario: Run the application against a scripted engine whose BeginPlay fails.
 * Expected: Run returns the begin failure result without ever invoking EndPlay.
 */
MW_TEST_CASE(RunnerDoesNotEndPlayAfterFailedBeginPlay)
{
	// Arrange
	FScriptedClock Clock{10, 20, 30};
	FScriptedEngine Engine;
	Engine.ConfigureBeginPlayResult(MicroWorld::ERuntimeResult::CapacityExceeded);
	FRunnerApplication Application{Engine};

	// Act
	const MicroWorld::ERuntimeResult StopResult = Application.Run(Clock, &CountingSleepFunction, 1);

	// Assert
	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::CapacityExceeded, StopResult, "Runner should return the begin failure result");
	MW_EXPECT_EQ(Test, 0, Engine.EndPlayCount, "Runner must not invoke EndPlay after a failed begin");
}

/**
 * Scenario: Run the application across several successful frames until a configured frame stops the run.
 * Expected: Pacing fires exactly once per successful frame and never for the stopping frame.
 */
MW_TEST_CASE(RunnerSleepsOncePerSuccessfulFrameOnly)
{
	// Arrange
	ResetPacingCounter();
	FScriptedClock Clock{10, 20, 30, 40};
	FScriptedEngine Engine;
	Engine.ConfigureStopOnFrame(2);
	FRunnerApplication Application{Engine};

	// Act
	(void)Application.Run(Clock, &CountingSleepFunction, 1);

	// Assert
	MW_EXPECT_EQ(Test, 2, GPacingCallCount, "Pacing should fire once per successful frame only");
}

/**
 * Scenario: Run the application with a scripted clock and a scripted engine that stops on the first frame.
 * Expected: Each engine Tick sees the next scripted clock value; begin consumes the first reading before the first Tick.
 */
MW_TEST_CASE(RunnerFeedsClockValuesIntoTick)
{
	// Arrange
	FScriptedClock Clock{10, 20, 30};
	FScriptedEngine Engine;
	Engine.ConfigureStopOnFrame(1);
	FRunnerApplication Application{Engine};

	// Act
	(void)Application.Run(Clock, &CountingSleepFunction, 1);

	// Assert
	const bool bFirstFrameSawSecondClockValue = Engine.ObservedTickTimestamps[0] == MicroWorld::TimePointMilliseconds{20};
	const bool bSecondFrameSawThirdClockValue = Engine.ObservedTickTimestamps[1] == MicroWorld::TimePointMilliseconds{30};
	MW_EXPECT_TRUE(Test, bFirstFrameSawSecondClockValue, "First Tick should see the second clock value (begin consumed the first)");
	MW_EXPECT_TRUE(Test, bSecondFrameSawThirdClockValue, "Second Tick should see the third clock value");
}

} // namespace MicroWorld::Tests
