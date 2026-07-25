#pragma once

#include <MicroWorld/Application.h>
#include <MicroWorld/RuntimeResult.h>
#include <MicroWorld/Time.h>

namespace MicroWorld
{

/**
 * Signature of the pacing function a runner calls between frames.
 *
 * noexcept is part of the type, so a platform's existing sleep function binds
 * directly and the compiler rejects one that could throw into a noexcept Run.
 */
using FSleepFunction = void (*)(DurationMilliseconds SleepDurationMilliseconds) noexcept;

/**
 * Drives one FApplication through its whole lifecycle on an injected clock.
 *
 * Owns the begin/advance/end sequence every consumer would otherwise hand-roll,
 * so a platform entry point supplies a clock and a pacing function instead of a
 * loop. Core still never reads real time: both arrive from the caller.
 */
template<typename TimeSourceType>
class TApplicationRunner final
{
public:
	/** Binds one runner to the clock, pacing function, and cadence the platform selected. */
	TApplicationRunner(TimeSourceType& InTimeSource, const FSleepFunction InSleepFunction, const DurationMilliseconds InPacingMilliseconds) noexcept
		: TimeSource(InTimeSource), SleepFunction(InSleepFunction), PacingMilliseconds(InPacingMilliseconds)
	{
	}

	/** Keeps one runner bound to the single composition it was constructed for. */
	TApplicationRunner(const TApplicationRunner&) = delete;

	/** Prevents reassigning the injected clock and pacing function after construction. */
	TApplicationRunner& operator=(const TApplicationRunner&) = delete;

	/**
	 * Runs the application until one frame reports a non-success result.
	 *
	 * Returns the result that stopped the run. A failed BeginPlay returns
	 * immediately without EndPlay: FApplication has already invoked
	 * OnBeginPlayFailed and latched the Failed state, so EndPlay could only
	 * answer InvalidLifecycle and would hide the real begin failure.
	 */
	ERuntimeResult Run(FApplication& Application) noexcept
	{
		const ERuntimeResult BeginResult = Application.BeginPlay(TimeSource.Now());
		if (BeginResult != ERuntimeResult::Success)
		{
			return BeginResult;
		}

		for (;;)
		{
			const ERuntimeResult FrameResult = Application.Advance(TimeSource.Now());
			if (FrameResult != ERuntimeResult::Success)
			{
				(void)Application.EndPlay();
				return FrameResult;
			}

			SleepFunction(PacingMilliseconds);
		}
	}

private:
	/** Supplies monotonic milliseconds; owned by the platform entry point. */
	TimeSourceType& TimeSource;

	/** Yields the processor between frames so platform idle work still runs. */
	FSleepFunction SleepFunction;

	/** Paces the loop so one frame never starves the platform scheduler. */
	DurationMilliseconds PacingMilliseconds;
};

} // namespace MicroWorld
