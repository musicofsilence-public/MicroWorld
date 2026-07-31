#include "TestSupport.h"

#include <MicroWorld/Application/Application.h>
#include <MicroWorld/Engine/ObjectStore.h>

#include <utility>

namespace MicroWorld::Tests
{

namespace
{

	/** Dispatcher timestamp every lifecycle test passes to BeginPlay and Advance. */
	constexpr MicroWorld::TimePointMilliseconds DispatcherStartTime{100};

	/**
	 * Records every IEngine call so FApplication's sealed forwarding is observed behaviourally.
	 *
	 * Carries configurable BeginPlay/OnConfigure results so a test can drive the failed-begin path
	 * without duplicating the application base's own state machine. GetWorld/GetObjectStore return
	 * references to backing storage so the IEngine contract is satisfied even though these tests
	 * never exercise the world or store.
	 */
	class FRecordingEngine final : public MicroWorld::IEngine
	{
	public:
		/** Drives the next BeginPlay result so the failed-engine-begin path is reachable from a test. */
		void ConfigureBeginPlayResult(MicroWorld::ERuntimeResult InResult) noexcept { ConfiguredBeginPlayResult = InResult; }

		/** Observes how many times BeginPlay fired, since double-begin must not re-invoke it. */
		int BeginPlayCount{0};

		/** Observes how many times Tick fired, since rejected lifecycle or backward time must not reach it. */
		int TickCount{0};

		/** Observes whether EndPlay fired exactly once across repeated EndPlay calls. */
		int EndPlayCount{0};

		MicroWorld::ERuntimeResult BeginPlay(MicroWorld::TimePointMilliseconds) noexcept override
		{
			++BeginPlayCount;
			return ConfiguredBeginPlayResult;
		}

		MicroWorld::ERuntimeResult Tick(MicroWorld::TimePointMilliseconds) noexcept override
		{
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
		/** Holds the result BeginPlay will return, seeded to Success so the happy path needs no setup. */
		MicroWorld::ERuntimeResult ConfiguredBeginPlayResult{MicroWorld::ERuntimeResult::Success};

		/** Raw storage for the world/store pointers the contract requires but these tests never use. */
		std::uint64_t WorldStorage{0};
		std::uint64_t StoreStorage{0};
	};

	/**
	 * FApplication double whose only override is OnConfigure, so the new single-hook contract is
	 * observed: it counts OnConfigure invocations and can return a configured failure to drive the
	 * failed-configure path, exactly as a real subclass would.
	 */
	class FConfiguringApplication final : public MicroWorld::Application::FApplication
	{
	public:
		explicit FConfiguringApplication(MicroWorld::IEngine& InEngine) noexcept : MicroWorld::Application::FApplication(InEngine) {}

		/** Drives the next OnConfigure result so the failed-configure path is reachable from a test. */
		void ConfigureConfigureResult(MicroWorld::ERuntimeResult InResult) noexcept { ConfiguredConfigureResult = InResult; }

		/** Observes how many times OnConfigure fired, since double-begin must not re-invoke it. */
		int ConfigureCount{0};

		/** Observes whether the rollback hook fired exactly once after a failed configure. */
		int BeginPlayFailedCount{0};

	protected:
		MicroWorld::ERuntimeResult OnConfigure(MicroWorld::IEngine& InEngine, MicroWorld::TimePointMilliseconds) noexcept override
		{
			(void)InEngine;
			++ConfigureCount;
			return ConfiguredConfigureResult;
		}

		void OnBeginPlayFailed() noexcept override { ++BeginPlayFailedCount; }

	private:
		/** Holds the result OnConfigure will return, seeded to Success so the happy path needs no setup. */
		MicroWorld::ERuntimeResult ConfiguredConfigureResult{MicroWorld::ERuntimeResult::Success};
	};

} // namespace

/**
 * Scenario: Invoke BeginPlay once on a freshly constructed application.
 * Expected: OnConfigure runs once; the engine's BeginPlay runs once after it; BeginPlay reports Success.
 */
MW_TEST_CASE(ApplicationBeginPlayInvokesOnConfigureThenEngineBeginPlay)
{
	// Arrange
	FRecordingEngine Engine;
	FConfiguringApplication Application{Engine};

	// Act
	const MicroWorld::ERuntimeResult BeginResult = Application.BeginPlay(DispatcherStartTime);

	// Assert
	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::Success, BeginResult, "First BeginPlay should succeed");
	MW_EXPECT_EQ(Test, 1, Application.ConfigureCount, "First BeginPlay should invoke OnConfigure once");
	MW_EXPECT_EQ(Test, 1, Engine.BeginPlayCount, "First BeginPlay should invoke the engine BeginPlay once");
}

/**
 * Scenario: Configure OnConfigure to fail, then invoke BeginPlay, Advance, and EndPlay after the failed begin.
 * Expected: The rollback hook fires once; the engine BeginPlay is never reached; Advance and EndPlay are rejected as terminal.
 */
MW_TEST_CASE(ApplicationFailedConfigureInvokesFailureHookAndLatchesTerminal)
{
	// Arrange
	FRecordingEngine Engine;
	FConfiguringApplication Application{Engine};
	Application.ConfigureConfigureResult(MicroWorld::ERuntimeResult::CapacityExceeded);

	// Act
	const MicroWorld::ERuntimeResult BeginResult = Application.BeginPlay(DispatcherStartTime);
	const MicroWorld::ERuntimeResult AdvanceAfterFailedBeginResult = Application.Advance(DispatcherStartTime);
	const MicroWorld::ERuntimeResult EndAfterFailedBeginResult = Application.EndPlay();

	// Assert
	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::CapacityExceeded, BeginResult, "Failed configure should surface the OnConfigure result");
	MW_EXPECT_EQ(Test, 0, Engine.BeginPlayCount, "Failed configure must not reach the engine BeginPlay");
	MW_EXPECT_EQ(Test, 1, Application.BeginPlayFailedCount, "Failed configure should invoke the rollback hook once");
	MW_EXPECT_EQ(
		Test, MicroWorld::ERuntimeResult::InvalidLifecycle, AdvanceAfterFailedBeginResult, "Advance after a failed begin should be rejected");
	MW_EXPECT_EQ(Test, 0, Engine.TickCount, "Advance after a failed begin must not reach the engine Tick");
	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::InvalidLifecycle, EndAfterFailedBeginResult, "EndPlay after a failed begin should be rejected");
}

/**
 * Scenario: Complete one BeginPlay, then invoke BeginPlay a second time.
 * Expected: The second BeginPlay is rejected by the lifecycle guard; OnConfigure and the engine BeginPlay are not re-invoked.
 */
MW_TEST_CASE(ApplicationSecondBeginPlayIsRejected)
{
	// Arrange
	FRecordingEngine Engine;
	FConfiguringApplication Application{Engine};
	Application.BeginPlay(DispatcherStartTime);

	// Act
	const MicroWorld::ERuntimeResult SecondBeginResult = Application.BeginPlay(DispatcherStartTime);

	// Assert
	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::InvalidLifecycle, SecondBeginResult, "Second BeginPlay should be rejected");
	MW_EXPECT_EQ(Test, 1, Application.ConfigureCount, "Second BeginPlay should not re-invoke OnConfigure");
	MW_EXPECT_EQ(Test, 1, Engine.BeginPlayCount, "Second BeginPlay should not re-invoke the engine BeginPlay");
}

/**
 * Scenario: Invoke Advance before any BeginPlay has been called.
 * Expected: Advance is rejected as a lifecycle violation; the engine Tick is never reached.
 */
MW_TEST_CASE(ApplicationAdvanceBeforeBeginPlayIsRejected)
{
	// Arrange
	FRecordingEngine Engine;
	FConfiguringApplication Application{Engine};

	// Act
	const MicroWorld::ERuntimeResult AdvanceResult = Application.Advance(DispatcherStartTime);

	// Assert
	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::InvalidLifecycle, AdvanceResult, "Advance before BeginPlay should be rejected");
	MW_EXPECT_EQ(Test, 0, Engine.TickCount, "Advance before BeginPlay should not invoke the engine Tick");
}

/**
 * Scenario: After one advancing Advance, invoke Advance again with a timestamp strictly earlier than the previous one.
 * Expected: The backward timestamp is rejected; the engine Tick is not invoked for that call.
 */
MW_TEST_CASE(ApplicationAdvanceRejectsBackwardTime)
{
	// Arrange
	FRecordingEngine Engine;
	FConfiguringApplication Application{Engine};
	Application.BeginPlay(DispatcherStartTime);
	Application.Advance(DispatcherStartTime);

	// Act
	const MicroWorld::ERuntimeResult BackwardResult = Application.Advance(DispatcherStartTime - MicroWorld::TimePointMilliseconds{1});

	// Assert
	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::NonMonotonicTime, BackwardResult, "Backward time should be rejected");
	MW_EXPECT_EQ(Test, 1, Engine.TickCount, "Backward Advance should not invoke the engine Tick");
}

/**
 * Scenario: After one advancing Advance, invoke Advance again with the same timestamp.
 * Expected: The repeated timestamp is accepted as monotonic-equivalent; the engine Tick still dispatches.
 */
MW_TEST_CASE(ApplicationAdvanceAcceptsRepeatedSameTimestamp)
{
	// Arrange
	FRecordingEngine Engine;
	FConfiguringApplication Application{Engine};
	Application.BeginPlay(DispatcherStartTime);
	Application.Advance(DispatcherStartTime);

	// Act
	const MicroWorld::ERuntimeResult RepeatedTimeResult = Application.Advance(DispatcherStartTime);

	// Assert
	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::Success, RepeatedTimeResult, "Repeated timestamp should be accepted as monotonic");
	MW_EXPECT_EQ(Test, 2, Engine.TickCount, "Repeated-timestamp Advance should still invoke the engine Tick");
}

/**
 * Scenario: After BeginPlay, invoke EndPlay twice.
 * Expected: The first EndPlay succeeds; the second remains successful; the engine EndPlay runs only once.
 */
MW_TEST_CASE(ApplicationEndPlayIsIdempotent)
{
	// Arrange
	FRecordingEngine Engine;
	FConfiguringApplication Application{Engine};
	Application.BeginPlay(DispatcherStartTime);

	// Act
	const MicroWorld::ERuntimeResult FirstEndResult = Application.EndPlay();
	const MicroWorld::ERuntimeResult SecondEndResult = Application.EndPlay();

	// Assert
	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::Success, FirstEndResult, "First EndPlay should succeed");
	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::Success, SecondEndResult, "Second EndPlay should remain successful");
	MW_EXPECT_EQ(Test, 1, Engine.EndPlayCount, "Idempotent EndPlay should invoke the engine EndPlay once");
}

/**
 * Scenario: Complete BeginPlay then EndPlay, then invoke Advance.
 * Expected: Advance is rejected as a lifecycle violation; the engine Tick is never reached.
 */
MW_TEST_CASE(ApplicationAdvanceAfterEndPlayIsRejected)
{
	// Arrange
	FRecordingEngine Engine;
	FConfiguringApplication Application{Engine};
	Application.BeginPlay(DispatcherStartTime);
	Application.EndPlay();

	// Act
	const MicroWorld::ERuntimeResult AdvanceResult = Application.Advance(DispatcherStartTime);

	// Assert
	MW_EXPECT_EQ(Test, MicroWorld::ERuntimeResult::InvalidLifecycle, AdvanceResult, "Advance after EndPlay should be rejected");
	MW_EXPECT_EQ(Test, 0, Engine.TickCount, "Advance after EndPlay should not invoke the engine Tick");
}

} // namespace MicroWorld::Tests
