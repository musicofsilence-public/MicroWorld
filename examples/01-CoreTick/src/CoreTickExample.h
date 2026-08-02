#pragma once

#include <MicroWorld/Core/TickDecision.h>
#include <MicroWorld/Core/TickFunction.h>

/**
 * Motivation: Lets one loop step carry both the scheduler's decision and the trace's completion
 *   back to a platform adapter without leaking the example's internals.
 * Responsibilities: Hold one decision plus a single done flag, and mutate them only as a result
 *   of advancing the schedule.
 * Example:
 *   FCoreTickExampleStep Step = Example.Advance(Now);
 *   if (Step.bFinished) { Stop(); }
 */
struct FCoreTickExampleStep
{
	/** Motivation: Preserves the scheduler's due-tick decision for the platform adapter. */
	MicroWorld::Core::FTickDecision Decision{};

	/** Motivation: Lets the platform adapter end its bounded five-tick loop. */
	bool bFinished{false};
};

/**
 * Motivation: Gives one example a single platform-neutral home for the bounded five-tick behavior
 *   so platform adapters stay thin and the trace stays reproducible.
 * Responsibilities: Drive one fixed cadence for exactly five accepted ticks, then report done,
 *   without depending on platform sleep timing.
 * Example:
 *   FCoreTickExample Example;
 *   Example.Begin(Now);
 *   while (!Example.IsFinished()) { Example.Advance(Now); }
 */
class FCoreTickExample final
{
public:
	/**
	 * Motivation: Lets the platform adapter start the bounded schedule from its own monotonic clock.
	 * Responsibilities: Reset the tick count, clear the done flag, and prime the schedule's start time.
	 */
	void Begin(MicroWorld::Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/**
	 * Motivation: Lets a platform loop advance the trace one observable step at a time.
	 * Responsibilities: Apply one clock sample, count an accepted due tick toward the five-tick
	 *   bound, and report both the decision and completion.
	 */
	FCoreTickExampleStep Advance(MicroWorld::Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/**
	 * Motivation: Lets platform loops stop after the fifth due tick without inspecting state.
	 * Responsibilities: Report only whether the trace has reached its five-tick completion.
	 */
	bool IsFinished() const noexcept;

private:
	/** Motivation: Keeps the demonstrated cadence independent of platform sleep scheduling. */
	static constexpr MicroWorld::Core::DurationMilliseconds TickIntervalMilliseconds = 500;

	/** Motivation: Bounds the trace and exposes its completion through the returned step. */
	static constexpr unsigned TargetTickCount = 5;

	/** Motivation: Owns the caller-time tick schedule for this one bounded example. */
	MicroWorld::Core::FTickFunction SensorTick{MicroWorld::Core::FTickConfiguration::EnabledEvery(TickIntervalMilliseconds)};

	/** Motivation: Counts accepted due ticks so polling frequency cannot change the trace length. */
	unsigned TickCount{0};

	/** Motivation: Prevents post-completion calls from advancing a schedule that has ended. */
	bool bFinished{false};
};
