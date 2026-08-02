#include "TestSupport.h"

#include <MicroWorld/Application/Application.h>
#include <MicroWorld/Engine/EngineHost.h>

#include <cstdint>
#include <utility>

namespace MicroWorld::Tests
{

namespace
{

	/** Motivation: Dispatcher timestamp every lifecycle test passes to BeginPlay and Advance. */
	constexpr MicroWorld::Core::TimePointMilliseconds DispatcherStartTime{100};

	/** Motivation: Bounds root scans when the concrete-engine configuration test begins its one world. */
	constexpr std::uint32_t ConcreteEngineMaxRootOperations{1};

	/** Motivation: Bounds marking work when the concrete-engine configuration test begins its one world. */
	constexpr std::uint32_t ConcreteEngineMaxMarkOperations{4};

	/** Motivation: Bounds reclamation scans when the concrete-engine configuration test begins its one world. */
	constexpr std::uint32_t ConcreteEngineMaxSweepOperations{8};

	/** Motivation: Names the bounded collector work used only by the concrete-engine configuration fixture. */
	constexpr MicroWorld::Engine::FGarbageCollectionBudget ConcreteEngineCollectionBudget{
		ConcreteEngineMaxRootOperations,
		ConcreteEngineMaxMarkOperations,
		ConcreteEngineMaxSweepOperations,
	};

	/**
	 * Motivation: Records every runtime call so FApplication's sealed forwarding is observed behaviourally.
	 *   Carries configurable BeginPlay
	 * and OnConfigure results so a test can drive failure paths without
	 *   duplicating the application base's own state machine.
	 *
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 *
	 * Example: FRecordingEngineRuntime Runtime;
	 */
	class FRecordingEngineRuntime final : public MicroWorld::Engine::IEngineRuntime
	{
	public:
		/**
		 * Motivation: The failed-engine-begin path is reachable from a test.
		 * Responsibilities: Drives the next BeginPlay result.
		 */
		void ConfigureBeginPlayResult(MicroWorld::Core::ERuntimeResult InResult) noexcept { ConfiguredBeginPlayResult = InResult; }

		/** Motivation: Observes how many times BeginPlay fired, since double-begin must not re-invoke it. */
		int BeginPlayCount{0};

		/** Motivation: Observes how many times Tick fired, since rejected lifecycle or backward time must not reach it. */
		int TickCount{0};

		/** Motivation: Observes whether EndPlay fired exactly once across repeated EndPlay calls. */
		int EndPlayCount{0};

		MicroWorld::Core::ERuntimeResult BeginPlay(MicroWorld::Core::TimePointMilliseconds) noexcept override
		{
			++BeginPlayCount;
			return ConfiguredBeginPlayResult;
		}

		MicroWorld::Core::ERuntimeResult Tick(MicroWorld::Core::TimePointMilliseconds) noexcept override
		{
			++TickCount;
			return MicroWorld::Core::ERuntimeResult::Success;
		}

		MicroWorld::Core::ERuntimeResult EndPlay() noexcept override
		{
			++EndPlayCount;
			return MicroWorld::Core::ERuntimeResult::Success;
		}

	private:
		/** Motivation: Holds the result BeginPlay will return, seeded to Success so the happy path needs no setup. */
		MicroWorld::Core::ERuntimeResult ConfiguredBeginPlayResult{MicroWorld::Core::ERuntimeResult::Success};
	};

	/**
	 * Motivation: FApplication double whose only override is OnConfigure, so the new single-hook contract is observed:
	 *   it counts OnConfigure invocations and can return a configured failure to drive the failed-configure
	 *   path, exactly as a real subclass would.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 * Example:
	 *   // Construct and exercise the type in one behavior test.
	 */
	class FConfiguringApplication final : public MicroWorld::Application::FApplication
	{
	public:
		explicit FConfiguringApplication(MicroWorld::Engine::IEngineRuntime& InEngineRuntime) noexcept
			: MicroWorld::Application::FApplication(InEngineRuntime)
		{
		}

		/**
		 * Motivation: The failed-configure path is reachable from a test.
		 * Responsibilities: Drives the next OnConfigure result.
		 */
		void ConfigureConfigureResult(MicroWorld::Core::ERuntimeResult InResult) noexcept { ConfiguredConfigureResult = InResult; }

		/** Motivation: Observes how many times OnConfigure fired, since double-begin must not re-invoke it. */
		int ConfigureCount{0};

		/** Motivation: Observes whether the rollback hook fired exactly once after a failed configure. */
		int BeginPlayFailedCount{0};

	protected:
		MicroWorld::Core::ERuntimeResult OnConfigure(MicroWorld::Core::TimePointMilliseconds) noexcept override
		{
			++ConfigureCount;
			return ConfiguredConfigureResult;
		}

		void OnBeginPlayFailed() noexcept override { ++BeginPlayFailedCount; }

	private:
		/** Motivation: Holds the result OnConfigure will return, seeded to Success so the happy path needs no setup. */
		MicroWorld::Core::ERuntimeResult ConfiguredConfigureResult{MicroWorld::Core::ERuntimeResult::Success};
	};

	/**
	 * Motivation: Proves configuration can retain the concrete engine without widening FApplication's runtime dependency.
	 *
	 * Responsibilities: Create the world before the runtime begins and report whether the concrete setup succeeded.
	 * Example: Construct with a
	 * concrete engine and call BeginPlay.
	 */
	class FWorldConfiguringApplication final : public MicroWorld::Application::FApplication
	{
	public:
		/**
		 * Motivation: Binds runtime forwarding while retaining the concrete engine for configuration.
		 * Responsibilities: Keep the
		 * concrete engine valid for OnConfigure without widening the base-class dependency.
		 */
		explicit FWorldConfiguringApplication(MicroWorld::Engine::TEngine<>& InConcreteEngine) noexcept
			: MicroWorld::Application::FApplication(InConcreteEngine), ConcreteEngine(InConcreteEngine)
		{
		}

		/** Motivation: Observes whether concrete configuration created the required world before runtime begin. */
		bool bCreatedWorld{false};

	protected:
		/**
		 * Motivation: Creates the world before runtime begin because TEngine requires it for a successful start.
		 *
		 * Responsibilities: Record whether creation succeeded and return the matching runtime result.
		 */
		MicroWorld::Core::ERuntimeResult OnConfigure(MicroWorld::Core::TimePointMilliseconds) noexcept override
		{
			bCreatedWorld = ConcreteEngine.CreateWorld().Get() != nullptr;
			return bCreatedWorld ? MicroWorld::Core::ERuntimeResult::Success : MicroWorld::Core::ERuntimeResult::CapacityExceeded;
		}

		/**
		 * Motivation: Fulfills the failure-hook contract for configuration with no additional rollback work.
		 * Responsibilities:
		 * Make no additional state change when the test configuration fails.
		 */
		void OnBeginPlayFailed() noexcept override {}

	private:
		/** Motivation: Retains the concrete dependency configuration needs while the base holds only its runtime contract. */
		MicroWorld::Engine::TEngine<>& ConcreteEngine;
	};

} // namespace

/**
 * Motivation: Invoke BeginPlay once on a freshly constructed application.
 * Responsibilities: OnConfigure runs once; the engine's BeginPlay runs once after it; BeginPlay reports Success.
 */
MW_TEST_CASE(ApplicationBeginPlayInvokesOnConfigureThenEngineBeginPlay)
{
	// Arrange
	FRecordingEngineRuntime EngineRuntime;
	FConfiguringApplication Application{EngineRuntime};

	// Act
	const MicroWorld::Core::ERuntimeResult BeginResult = Application.BeginPlay(DispatcherStartTime);

	// Assert
	MW_EXPECT_EQ(Test, MicroWorld::Core::ERuntimeResult::Success, BeginResult, "First BeginPlay should succeed");
	MW_EXPECT_EQ(Test, 1, Application.ConfigureCount, "First BeginPlay should invoke OnConfigure once");
	MW_EXPECT_EQ(Test, 1, EngineRuntime.BeginPlayCount, "First BeginPlay should invoke runtime BeginPlay once");
}

/**
 * Motivation: Configure OnConfigure to fail, then invoke BeginPlay, Advance, and EndPlay after the failed begin.
 * Responsibilities: The rollback hook fires once; the engine BeginPlay is never reached; Advance and EndPlay are
 *   rejected as terminal.
 */
MW_TEST_CASE(ApplicationFailedConfigureInvokesFailureHookAndLatchesTerminal)
{
	// Arrange
	FRecordingEngineRuntime EngineRuntime;
	FConfiguringApplication Application{EngineRuntime};
	Application.ConfigureConfigureResult(MicroWorld::Core::ERuntimeResult::CapacityExceeded);

	// Act
	const MicroWorld::Core::ERuntimeResult BeginResult = Application.BeginPlay(DispatcherStartTime);
	const MicroWorld::Core::ERuntimeResult AdvanceAfterFailedBeginResult = Application.Advance(DispatcherStartTime);
	const MicroWorld::Core::ERuntimeResult EndAfterFailedBeginResult = Application.EndPlay();

	// Assert
	MW_EXPECT_EQ(Test, MicroWorld::Core::ERuntimeResult::CapacityExceeded, BeginResult, "Failed configure should surface the OnConfigure result");
	MW_EXPECT_EQ(Test, 0, EngineRuntime.BeginPlayCount, "Failed configure must not reach runtime BeginPlay");
	MW_EXPECT_EQ(Test, 1, Application.BeginPlayFailedCount, "Failed configure should invoke the rollback hook once");
	MW_EXPECT_EQ(
		Test, MicroWorld::Core::ERuntimeResult::InvalidLifecycle, AdvanceAfterFailedBeginResult, "Advance after a failed begin should be rejected");
	MW_EXPECT_EQ(Test, 0, EngineRuntime.TickCount, "Advance after a failed begin must not reach runtime Tick");
	MW_EXPECT_EQ(
		Test, MicroWorld::Core::ERuntimeResult::InvalidLifecycle, EndAfterFailedBeginResult, "EndPlay after a failed begin should be rejected");
}

/**
 * Motivation: Complete one BeginPlay, then invoke BeginPlay a second time.
 * Responsibilities: The second BeginPlay is rejected by the lifecycle guard; OnConfigure and the engine BeginPlay are
 *   not re-invoked.
 */
MW_TEST_CASE(ApplicationSecondBeginPlayIsRejected)
{
	// Arrange
	FRecordingEngineRuntime EngineRuntime;
	FConfiguringApplication Application{EngineRuntime};
	Application.BeginPlay(DispatcherStartTime);

	// Act
	const MicroWorld::Core::ERuntimeResult SecondBeginResult = Application.BeginPlay(DispatcherStartTime);

	// Assert
	MW_EXPECT_EQ(Test, MicroWorld::Core::ERuntimeResult::InvalidLifecycle, SecondBeginResult, "Second BeginPlay should be rejected");
	MW_EXPECT_EQ(Test, 1, Application.ConfigureCount, "Second BeginPlay should not re-invoke OnConfigure");
	MW_EXPECT_EQ(Test, 1, EngineRuntime.BeginPlayCount, "Second BeginPlay should not re-invoke runtime BeginPlay");
}

/**
 * Motivation: Invoke Advance before any BeginPlay has been called.
 * Responsibilities: Advance is rejected as a lifecycle violation; the engine Tick is never reached.
 */
MW_TEST_CASE(ApplicationAdvanceBeforeBeginPlayIsRejected)
{
	// Arrange
	FRecordingEngineRuntime EngineRuntime;
	FConfiguringApplication Application{EngineRuntime};

	// Act
	const MicroWorld::Core::ERuntimeResult AdvanceResult = Application.Advance(DispatcherStartTime);

	// Assert
	MW_EXPECT_EQ(Test, MicroWorld::Core::ERuntimeResult::InvalidLifecycle, AdvanceResult, "Advance before BeginPlay should be rejected");
	MW_EXPECT_EQ(Test, 0, EngineRuntime.TickCount, "Advance before BeginPlay should not invoke runtime Tick");
}

/**
 * Motivation: After one advancing Advance, invoke Advance again with a timestamp strictly earlier than the
 *   previous one.
 * Responsibilities: The backward timestamp is rejected; the engine Tick is not invoked for that call.
 */
MW_TEST_CASE(ApplicationAdvanceRejectsBackwardTime)
{
	// Arrange
	FRecordingEngineRuntime EngineRuntime;
	FConfiguringApplication Application{EngineRuntime};
	Application.BeginPlay(DispatcherStartTime);
	Application.Advance(DispatcherStartTime);

	// Act
	const MicroWorld::Core::ERuntimeResult BackwardResult = Application.Advance(DispatcherStartTime - MicroWorld::Core::TimePointMilliseconds{1});

	// Assert
	MW_EXPECT_EQ(Test, MicroWorld::Core::ERuntimeResult::NonMonotonicTime, BackwardResult, "Backward time should be rejected");
	MW_EXPECT_EQ(Test, 1, EngineRuntime.TickCount, "Backward Advance should not invoke runtime Tick");
}

/**
 * Motivation: After one advancing Advance, invoke Advance again with the same timestamp.
 * Responsibilities: The repeated timestamp is accepted as monotonic-equivalent; the engine Tick still dispatches.
 */
MW_TEST_CASE(ApplicationAdvanceAcceptsRepeatedSameTimestamp)
{
	// Arrange
	FRecordingEngineRuntime EngineRuntime;
	FConfiguringApplication Application{EngineRuntime};
	Application.BeginPlay(DispatcherStartTime);
	Application.Advance(DispatcherStartTime);

	// Act
	const MicroWorld::Core::ERuntimeResult RepeatedTimeResult = Application.Advance(DispatcherStartTime);

	// Assert
	MW_EXPECT_EQ(Test, MicroWorld::Core::ERuntimeResult::Success, RepeatedTimeResult, "Repeated timestamp should be accepted as monotonic");
	MW_EXPECT_EQ(Test, 2, EngineRuntime.TickCount, "Repeated-timestamp Advance should still invoke runtime Tick");
}

/**
 * Motivation: After BeginPlay, invoke EndPlay twice.
 * Responsibilities: The first EndPlay succeeds; the second remains successful; the engine EndPlay runs only once.
 */
MW_TEST_CASE(ApplicationEndPlayIsIdempotent)
{
	// Arrange
	FRecordingEngineRuntime EngineRuntime;
	FConfiguringApplication Application{EngineRuntime};
	Application.BeginPlay(DispatcherStartTime);

	// Act
	const MicroWorld::Core::ERuntimeResult FirstEndResult = Application.EndPlay();
	const MicroWorld::Core::ERuntimeResult SecondEndResult = Application.EndPlay();

	// Assert
	MW_EXPECT_EQ(Test, MicroWorld::Core::ERuntimeResult::Success, FirstEndResult, "First EndPlay should succeed");
	MW_EXPECT_EQ(Test, MicroWorld::Core::ERuntimeResult::Success, SecondEndResult, "Second EndPlay should remain successful");
	MW_EXPECT_EQ(Test, 1, EngineRuntime.EndPlayCount, "Idempotent EndPlay should invoke runtime EndPlay once");
}

/**
 * Motivation: Complete BeginPlay then EndPlay, then invoke Advance.
 * Responsibilities: Advance is rejected as a lifecycle violation; the engine Tick is never reached.
 */
MW_TEST_CASE(ApplicationAdvanceAfterEndPlayIsRejected)
{
	// Arrange
	FRecordingEngineRuntime EngineRuntime;
	FConfiguringApplication Application{EngineRuntime};
	Application.BeginPlay(DispatcherStartTime);
	Application.EndPlay();

	// Act
	const MicroWorld::Core::ERuntimeResult AdvanceResult = Application.Advance(DispatcherStartTime);

	// Assert
	MW_EXPECT_EQ(Test, MicroWorld::Core::ERuntimeResult::InvalidLifecycle, AdvanceResult, "Advance after EndPlay should be rejected");
	MW_EXPECT_EQ(Test, 0, EngineRuntime.TickCount, "Advance after EndPlay should not invoke runtime Tick");
}

/**
 * Motivation: Begin an application that needs concrete engine access while configuring its world.
 * Responsibilities: Create the world before
 * runtime begin, allowing the application to start successfully.
 */
MW_TEST_CASE(ApplicationConfiguresRetainedConcreteEngineBeforeRuntimeBegin)
{
	// Arrange
	MicroWorld::Engine::TEngine<> ConcreteEngine{ConcreteEngineCollectionBudget};
	FWorldConfiguringApplication Application{ConcreteEngine};

	// Act
	const MicroWorld::Core::ERuntimeResult BeginResult = Application.BeginPlay(DispatcherStartTime);

	// Assert
	MW_EXPECT_EQ(Test, MicroWorld::Core::ERuntimeResult::Success, BeginResult, "Concrete configuration should create the world before runtime begin");
	MW_EXPECT_TRUE(Test, Application.bCreatedWorld, "Concrete configuration should create the retained engine world");
}

} // namespace MicroWorld::Tests
