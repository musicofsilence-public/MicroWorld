#pragma once

#include <MicroWorld/Application/Application.h>
#include <MicroWorld/RuntimeResult.h>
#include <MicroWorld/Time.h>

namespace MicroWorld
{

/**
 * The pacing function a runner calls between frames.
 *
 * noexcept is part of the type, so a platform's existing sleep function binds with
 * no wrapper and the compiler rejects one that could throw into a noexcept Run.
 */
using FSleepFunction = void (*)(DurationMilliseconds InSleepDurationMilliseconds) noexcept;

/**
 * Runs one application: begin once, then advance and sleep until a frame fails.
 *
 * This is the loop a platform entry point would otherwise hand-roll, so it supplies
 * a clock and a sleep function instead of writing one. Core still reads no real
 * time of its own — both arrive from the caller.
 */
template<typename TimeSourceType>
class TApplicationRunner final
{
public:
	/** Binds the clock, the sleep function, and the frame period; none of the three can change later. */
	TApplicationRunner(TimeSourceType& InTimeSource, const FSleepFunction InSleep, const DurationMilliseconds InPacingMilliseconds) noexcept
		: TimeSource(InTimeSource), SleepFunction(InSleep), PacingMilliseconds(InPacingMilliseconds)
	{
	}

	/** No copying: two runners would advance the same application twice per frame. */
	TApplicationRunner(const TApplicationRunner&) = delete;

	/** No copy assignment: all three bindings are fixed at construction. */
	TApplicationRunner& operator=(const TApplicationRunner&) = delete;

	/**
	 * Runs the application and returns the result of the frame that failed.
	 *
	 * A healthy application never fails a frame, so in normal operation this call
	 * does not return and nothing written after it runs. A failed BeginPlay returns
	 * at once without EndPlay: FApplication has already run OnBeginPlayFailed and
	 * latched Failed, so EndPlay could only answer InvalidLifecycle and would hide
	 * the real reason.
	 */
	ERuntimeResult Run(FApplication& InApplication) noexcept
	{
		const ERuntimeResult BeginResult = InApplication.BeginPlay(TimeSource.Now());
		if (BeginResult != ERuntimeResult::Success)
		{
			return BeginResult;
		}

		for (;;)
		{
			const ERuntimeResult FrameResult = InApplication.Advance(TimeSource.Now());
			if (FrameResult != ERuntimeResult::Success)
			{
				(void)InApplication.EndPlay();
				return FrameResult;
			}

			SleepFunction(PacingMilliseconds);
		}
	}

private:
	/** The clock, held by reference: it must outlive the runner. */
	TimeSourceType& TimeSource;

	/** Hands the processor back between frames so the platform's own tasks still run. */
	FSleepFunction SleepFunction;

	/** How long to sleep each frame; too short and the platform's tasks starve. */
	DurationMilliseconds PacingMilliseconds;
};

} // namespace MicroWorld
