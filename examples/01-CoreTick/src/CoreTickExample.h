#pragma once

#include <MicroWorld/TickFunction.h>

/** Reports one example loop decision and whether its bounded trace is complete. */
struct FCoreTickExampleStep
{
	/** Preserves the scheduler's due-tick decision for the platform adapter. */
	MicroWorld::FTickDecision Decision{};

	/** Lets the platform adapter end its bounded five-tick loop. */
	bool bFinished{false};
};

/** Owns the platform-neutral five-tick behavior demonstrated by this example. */
class FCoreTickExample final
{
public:
	/** Starts the bounded schedule from the platform adapter's monotonic time. */
	void Begin(MicroWorld::TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Advances the schedule once and reports the resulting observable behavior. */
	FCoreTickExampleStep Advance(MicroWorld::TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Lets platform loops stop after the fifth due tick without inspecting state. */
	bool IsFinished() const noexcept;

private:
	/** Keeps the demonstrated cadence independent of platform sleep scheduling. */
	static constexpr MicroWorld::DurationMilliseconds TickIntervalMilliseconds = 500;

	/** Bounds the trace and exposes its completion through the returned step. */
	static constexpr unsigned TargetTickCount = 5;

	/** Owns the caller-time tick schedule for this one bounded example. */
	MicroWorld::FTickFunction SensorTick{MicroWorld::FTickConfiguration::EnabledEvery(TickIntervalMilliseconds)};

	/** Counts accepted due ticks so polling frequency cannot change the trace length. */
	unsigned TickCount{0};

	/** Prevents post-completion calls from advancing a schedule that has ended. */
	bool bFinished{false};
};
