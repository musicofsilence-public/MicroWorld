#pragma once

#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Core/Lifecycle.h>
#include <MicroWorld/Core/RuntimeResult.h>
#include <MicroWorld/Core/Time.h>

namespace MicroWorld::Application
{

/**
 * The pacing function a runner calls between frames.
 *
 * noexcept is part of the type, so a platform's existing sleep function binds with
 * no
 * wrapper and the compiler rejects one that could throw into a noexcept Run.
 */
using FSleepFunction = void (*)(Core::DurationMilliseconds InSleepDurationMilliseconds) noexcept;

/**
 * Base class for an application that owns one engine: BeginPlay runs once,
 * Advance every frame, EndPlay once.
 *
 * The application holds its engine by IEngine& so a subclass cannot get the
 * lifecycle wrong: the per-frame BeginPlay/Tick/EndPlay calls are sealed behind
 * private non-virtual forwarders, the one thing a subclass must supply is what
 * happens when startup fails (OnBeginPlayFailed), and the one thing it may
 * override is OnConfigure, which runs once at BeginPlay (before the engine
 * begins) to spawn actors and configure systems. This class still enforces
 * begin/tick/end order and rejects time that moves backward, so OnConfigure
 * never runs out of order and the engine never sees time go back.
 */
class FApplication
{
public:
	/** No copying: a copy would be a second object claiming the same started application. */
	FApplication(const FApplication&) = delete;

	/** No copy assignment: it would overwrite the lifecycle of an application already running. */
	FApplication& operator=(const FApplication&) = delete;

	/** No moving: this application holds its IEngine& for life, and callers can retain its FApplication&. */
	FApplication(FApplication&&) = delete;

	/** No move assignment: same reason — every reference to this object must stay valid. */
	FApplication& operator=(FApplication&&) = delete;

	/** Virtual so deleting through an FApplication& also destroys the derived class. */
	virtual ~FApplication() = default;

	/** Starts the application once, at the caller's current time; fails if already started. */
	Core::ERuntimeResult BeginPlay(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Runs one frame; rejects a time earlier than the last one instead of forwarding it. */
	Core::ERuntimeResult Advance(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Stops the application; calling it again after success does nothing and still succeeds. */
	Core::ERuntimeResult EndPlay() noexcept;

	/**
	 * Runs the application and returns the result of the frame that failed.
	 *
	 * A healthy application never fails a frame, so in normal
	 * operation this call
	 * does not return and nothing written after it runs. A failed BeginPlay returns
	 * at once without EndPlay:
	 * FApplication has already run OnBeginPlayFailed and
	 * latched Failed, so EndPlay could only answer InvalidLifecycle and would hide
	 * the
	 * real reason.
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
	/** Binds this application to the one engine it will drive for its lifetime. */
	explicit FApplication(::MicroWorld::Engine::IEngine& InEngine) noexcept : Engine(InEngine) {}

	/**
	 * The world exists and nothing has begun. Spawn actors and configure systems here.
	 *
	 * Defaulted to success so an application with nothing to configure writes no body
	 * at all, rather than a hook that discards both parameters to satisfy the compiler.
	 */
	virtual Core::ERuntimeResult OnConfigure(::MicroWorld::Engine::IEngine&, Core::TimePointMilliseconds) { return Core::ERuntimeResult::Success; }

	/** Undoes whatever OnConfigure started before it failed; it runs on the failure path, so it cannot throw. */
	virtual void OnBeginPlayFailed() noexcept = 0;

private:
	/** Runs OnConfigure first, then forwards to the engine's BeginPlay, returning the first failure. */
	Core::ERuntimeResult OnBeginPlay(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Forwards one frame to the engine's Tick. */
	Core::ERuntimeResult OnAdvance(Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Forwards the stop to the engine's EndPlay. */
	Core::ERuntimeResult OnEndPlay() noexcept;

	/** Tracks the current phase, so a failed start stays dead and a second EndPlay is harmless. */
	Core::FLifecycleGuard Lifecycle;

	/** The last time Advance accepted; a smaller one is rejected before the engine sees it. */
	Core::TimePointMilliseconds LastUpdateMilliseconds{0};

	/** The one engine this application drives; bound at construction and never rebound. */
	::MicroWorld::Engine::IEngine& Engine;
};

} // namespace MicroWorld::Application
