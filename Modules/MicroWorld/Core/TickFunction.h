#pragma once

#include <MicroWorld/Core/Time.h>

namespace MicroWorld::Core
{

/**
 * Motivation: Captures one object's primary tick capability and initial schedule before lifecycle start.
 * Responsibilities: Freeze whether the object may ever tick and carry the consumer-selected cadence and enablement.
 * Example:
 *   FTickConfiguration Config = FTickConfiguration::EnabledEvery(16);
 */
struct FTickConfiguration
{
	/** Motivation: Freezes whether the object may ever enter a ticking state. */
	bool bCanEverTick{false};

	/** Motivation: Separates initial enablement from permanent tick capability. */
	bool bStartWithTickEnabled{false};

	/** Motivation: Expresses the minimum cadence without prescribing a platform timer. */
	DurationMilliseconds TickIntervalMilliseconds{0};

	/**
	 * Motivation: Builds a config for an object that may tick, starts enabled, and repeats on the given interval.
	 * Responsibilities: Set the capability, initial enablement, and interval in one call.
	 */
	static FTickConfiguration EnabledEvery(DurationMilliseconds InIntervalMilliseconds) noexcept
	{
		FTickConfiguration Configuration;
		Configuration.bCanEverTick = true;
		Configuration.bStartWithTickEnabled = true;
		Configuration.TickIntervalMilliseconds = InIntervalMilliseconds;
		return Configuration;
	}
};

/**
 * Motivation: Owns the bounded scheduling state for one independently tickable object.
 * Responsibilities: Track capability, enablement, cadence, and lifecycle, and return at most one due tick per
 *   advance, rejecting backward dispatcher time.
 * Example:
 *   FTickFunction Tick(FTickConfiguration::EnabledEvery(16));
 *   Tick.BeginPlay(Now);
 *   FTickDecision Decision = Tick.Advance(Now);
 */
class FTickFunction final
{
public:
	/**
	 * Motivation: Binds immutable capability and the consumer-selected initial schedule to one tickable.
	 * Responsibilities: Capture the configuration at construction and never re-derive it.
	 */
	explicit FTickFunction(FTickConfiguration InConfiguration) noexcept;

	/**
	 * Motivation: Aligns the first tick with the owning object's canonical begin time.
	 * Responsibilities: Seed the scheduling timestamps and mark this tick function as playing.
	 */
	void BeginPlay(TimePointMilliseconds InNowMilliseconds) noexcept;

	/**
	 * Motivation: Stops scheduling when the owning object leaves play.
	 * Responsibilities: Mark this tick function as not playing without invoking object policy.
	 */
	void EndPlay() noexcept;

	/**
	 * Motivation: Lets an owner change enablement without taking an independent clock sample.
	 * Responsibilities: Honor the construction-time capability and report CannotEverTick when it forbids ticking.
	 */
	ERuntimeResult SetEnabled(bool bInEnabled) noexcept;

	/**
	 * Motivation: Lets an owner change cadence without changing enablement.
	 * Responsibilities: Update the interval and force the next accepted advance to reset the schedule.
	 */
	ERuntimeResult SetInterval(DurationMilliseconds InIntervalMilliseconds) noexcept;

	/**
	 * Motivation: Lets a dispatcher pull at most one due tick from this tickable per caller time.
	 * Responsibilities: Reject backward dispatcher time and return one due tick or a not-due decision.
	 */
	FTickDecision Advance(TimePointMilliseconds InNowMilliseconds) noexcept;

	/**
	 * Motivation: Lets owners expose enablement without leaking scheduler representation.
	 * Responsibilities: Report the current enabled state and nothing else.
	 */
	bool IsEnabled() const noexcept;

	/**
	 * Motivation: Lets owners report cadence using the same explicit millisecond unit.
	 * Responsibilities: Return the current interval and nothing else.
	 */
	DurationMilliseconds GetInterval() const noexcept;

private:
	/**
	 * Motivation: Keeps a long-running clock from wrapping into an early tick.
	 * Responsibilities: Return the next deadline, saturating at the time-point maximum.
	 */
	TimePointMilliseconds CalculateNextDueMilliseconds(TimePointMilliseconds InNowMilliseconds) const noexcept;

	/**
	 * Motivation: Keeps elapsed time within the bounded duration the public tick context uses.
	 * Responsibilities: Return the delta since the previous tick, saturating at the duration maximum.
	 */
	DurationMilliseconds CalculateDeltaMilliseconds(TimePointMilliseconds InNowMilliseconds) const noexcept;

	/**
	 * Motivation: Applies the deferred first-tick schedule reset and returns its due tick.
	 * Responsibilities: Clear the reset flag, reseed the schedule, and return a zero-delta due tick.
	 */
	FTickDecision BeginResetSchedule(TimePointMilliseconds InNowMilliseconds) noexcept;

	/**
	 * Motivation: Lets Advance decide whether the cadence gate allows a tick at this time.
	 * Responsibilities: Report whether the time has passed the deadline without re-firing at the same instant.
	 */
	bool IsTickDueNow(TimePointMilliseconds InNowMilliseconds) const noexcept;

	/**
	 * Motivation: Advances the schedule for an accepted tick and returns it.
	 * Responsibilities: Compute the delta, reseed the next deadline, and return the due tick.
	 */
	FTickDecision ProduceDueTick(TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Motivation: Detects caller time rollback even while ticking is disabled. */
	TimePointMilliseconds LastObservedMilliseconds{0};

	/** Motivation: Gives this tickable its own delta independent of sibling schedules. */
	TimePointMilliseconds PreviousTickMilliseconds{0};

	/** Motivation: Avoids interval arithmetic on every not-due update. */
	TimePointMilliseconds NextDueMilliseconds{0};

	/** Motivation: Retains the consumer cadence without consulting external configuration. */
	DurationMilliseconds IntervalMilliseconds{0};

	/** Motivation: Prevents runtime enablement from overriding construction-time capability. */
	bool bCanEverTick{false};

	/** Motivation: Represents current consumer intent independently from capability. */
	bool bEnabled{false};

	/** Motivation: Rejects schedule advancement outside the owning object's lifecycle. */
	bool bPlaying{false};

	/** Motivation: Forces the next accepted advance to establish a fresh zero-delta schedule. */
	bool bMustResetSchedule{true};
};

} // namespace MicroWorld::Core
