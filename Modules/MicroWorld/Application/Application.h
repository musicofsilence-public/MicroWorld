#pragma once

#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Core/Lifecycle.h>
#include <MicroWorld/Core/RuntimeResult.h>
#include <MicroWorld/Core/Time.h>

namespace MicroWorld::Application
{

/**
 * Motivation: Names the pacing function a runner calls between frames so a platform's existing sleep binds directly.
 * Responsibilities: Carry the noexcept in the type so a sleep that could throw is rejected at compile time and needs no wrapper.
 * Example:
 *   FSleepFunction Sleep = &Platform::SleepMilliseconds;
 *   Sleep(16);
 */
using FSleepFunction = void (*)(Core::DurationMilliseconds InSleepDurationMilliseconds) noexcept;

/**
 * Motivation: Owns one engine and seals its lifecycle order so a subclass cannot get begin/tick/end wrong; the subclass supplies only what
 *   happens when startup fails (OnBeginPlayFailed) and may override what runs once at configure (OnConfigure).
 * Responsibilities: Run BeginPlay once, Advance each frame, and EndPlay once; reject backward time; drive the engine through private non-virtual
 *   forwarders so the per-frame order is fixed.
 * Example:
 *   FMyApplication App(Engine);
 *   App.Run(TimeSource, &Platform::SleepMilliseconds, 16);
 */
class FApplication
{
public:
	/**
	 * Motivation: Prevents a second object from claiming the same started application.
	 * Responsibilities: Reject copy construction so application lifecycle stays singular.
	 */
	FApplication(const FApplication&) = delete;

	/**
	 * Motivation: Prevents assignment from overwriting the lifecycle of an application already running.
	 * Responsibilities: Reject copy assignment so application lifecycle stays singular.
	 */
	FApplication& operator=(const FApplication&) = delete;

	/**
	 * Motivation: Keeps this application's IEngine& and any retained FApplication& valid for life.
	 * Responsibilities: Reject move construction so every reference stays at one address.
	 */
	FApplication(FApplication&&) = delete;

	/**
	 * Motivation: Keeps this application's IEngine& and any retained FApplication& valid for life.
	 * Responsibilities: Reject move assignment so every reference stays at one address.
	 */
	FApplication& operator=(FApplication&&) = delete;

	/**
	 * Motivation: Lets a subclass be destroyed through its FApplication base.
	 * Responsibilities: Run the derived destructor when deletion happens through an FApplication&.
	 */
	virtual ~FApplication() = default;

	/**
	 * Motivation: Starts the application once at the caller's current time before any frame runs.
	 * Responsibilities: Move the lifecycle to Playing, latch the first time sample, and run configure-then-begin; on begin failure run
	 * OnBeginPlayFailed and latch Failed.
	 */
	Core::ERuntimeResult BeginPlay(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/**
	 * Motivation: Runs one framed step while the application is playing.
	 * Responsibilities: Reject a time earlier than the last accepted time, then forward the frame to the engine's Tick.
	 */
	Core::ERuntimeResult Advance(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/**
	 * Motivation: Stops the application cleanly, idempotently, on shutdown.
	 * Responsibilities: Move Playing to Ended; a second call succeeds without doing anything.
	 */
	Core::ERuntimeResult EndPlay() noexcept;

	/**
	 * Motivation: Runs the framed loop until a frame fails and returns that frame's result, so a healthy application never returns from it.
	 * Responsibilities: BeginPlay once, loop Advance until failure, then EndPlay; a failed BeginPlay returns at once without EndPlay since the
	 * failure is already latched.
	 */
	template<typename TimeSourceType>
	Core::ERuntimeResult Run(
		TimeSourceType& InTimeSource, const FSleepFunction InSleep, const Core::DurationMilliseconds InPacingMilliseconds) noexcept
	{
		const Core::ERuntimeResult BeginResult = BeginPlay(InTimeSource.Now());
		if (BeginResult != Core::ERuntimeResult::Success)
		{
			return BeginResult;
		}

		for (;;)
		{
			const Core::ERuntimeResult FrameResult = Advance(InTimeSource.Now());
			if (FrameResult != Core::ERuntimeResult::Success)
			{
				(void)EndPlay();
				return FrameResult;
			}

			InSleep(InPacingMilliseconds);
		}
	}

protected:
	/**
	 * Motivation: Binds this application to the one engine it will drive for its lifetime.
	 * Responsibilities: Store the engine reference; no configuration or lifecycle change happens here.
	 */
	explicit FApplication(::MicroWorld::Engine::IEngine& InEngine) noexcept : Engine(InEngine) {}

	/**
	 * Motivation: Gives a subclass its one chance to spawn actors and configure systems into a world that exists but has not begun.
	 * Responsibilities: Run once at BeginPlay before the engine begins; return non-success to abort the start.
	 */
	virtual Core::ERuntimeResult OnConfigure(::MicroWorld::Engine::IEngine&, Core::TimePointMilliseconds) { return Core::ERuntimeResult::Success; }

	/**
	 * Motivation: Lets a subclass react to a failed startup after BeginPlay has already latched the failure.
	 * Responsibilities: Undo whatever OnConfigure started; must not throw because it runs on the failure path.
	 */
	virtual void OnBeginPlayFailed() noexcept = 0;

private:
	/**
	 * Motivation: Runs the subclass configure hook before the engine begins, surfacing the first failure.
	 * Responsibilities: Call OnConfigure then Engine.BeginPlay and return the first non-success result.
	 */
	Core::ERuntimeResult OnBeginPlay(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/**
	 * Motivation: Forwards one framed step to the engine.
	 * Responsibilities: Call Engine.Tick with the accepted time.
	 */
	Core::ERuntimeResult OnAdvance(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/**
	 * Motivation: Forwards the stop to the engine once the lifecycle has moved to Ended.
	 * Responsibilities: Call Engine.EndPlay.
	 */
	Core::ERuntimeResult OnEndPlay() noexcept;

	/** Motivation: Tracks the current phase so a failed start stays dead and a second EndPlay is harmless. */
	Core::FLifecycleGuard Lifecycle;

	/** Motivation: The last time Advance accepted; a smaller one is rejected before the engine sees it. */
	Core::TimePointMilliseconds LastUpdateMilliseconds{0};

	/** Motivation: The one engine this application drives; bound at construction and never rebound. */
	::MicroWorld::Engine::IEngine& Engine;
};

} // namespace MicroWorld::Application
