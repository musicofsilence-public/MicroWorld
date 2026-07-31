#pragma once

#include <MicroWorld/Core/Time.h>

namespace MicroWorld::Core
{

/** Configures one object's primary tick before lifecycle start. */
struct FTickConfiguration
{
	/** Freezes whether the object may ever enter a ticking state. */
	bool bCanEverTick{false};

	/** Separates initial enablement from permanent tick capability. */
	bool bStartWithTickEnabled{false};

	/** Expresses the minimum cadence without prescribing a platform timer. */
	DurationMilliseconds TickIntervalMilliseconds{0};

	/** Builds a config that may tick, starts enabled, and repeats on the given interval. */
	static FTickConfiguration EnabledEvery(DurationMilliseconds InIntervalMilliseconds) noexcept
	{
		FTickConfiguration Configuration;
		Configuration.bCanEverTick = true;
		Configuration.bStartWithTickEnabled = true;
		Configuration.TickIntervalMilliseconds = InIntervalMilliseconds;
		return Configuration;
	}
};

/** Owns the bounded scheduling state for one independently tickable object. */
class FTickFunction final
{
public:
	/** Captures immutable capability and the consumer-selected initial schedule. */
	explicit FTickFunction(FTickConfiguration InConfiguration) noexcept;

	/** Starts scheduling from canonical dispatcher time. */
	void BeginPlay(TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Ends scheduling without invoking object policy. */
	void EndPlay() noexcept;

	/** Changes enablement without taking an independent clock sample. */
	ERuntimeResult SetEnabled(bool bInEnabled) noexcept;

	/** Changes the minimum interval without changing enablement. */
	ERuntimeResult SetInterval(DurationMilliseconds InIntervalMilliseconds) noexcept;

	/** Returns at most one due tick and rejects backward dispatcher time. */
	FTickDecision Advance(TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Lets owners expose enablement without leaking scheduler representation. */
	bool IsEnabled() const noexcept;

	/** Lets owners report cadence using the same explicit millisecond unit. */
	DurationMilliseconds GetInterval() const noexcept;

private:
	/** Saturates deadlines so a long-running clock cannot wrap into an early tick. */
	TimePointMilliseconds CalculateNextDueMilliseconds(TimePointMilliseconds InNowMilliseconds) const noexcept;

	/** Saturates elapsed time because the public tick context uses a bounded duration. */
	DurationMilliseconds CalculateDeltaMilliseconds(TimePointMilliseconds InNowMilliseconds) const noexcept;

	/** Applies the first-tick schedule reset and returns its due tick. */
	FTickDecision BeginResetSchedule(TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Reports whether the cadence gate allows a tick at this time. */
	bool IsTickDueNow(TimePointMilliseconds InNowMilliseconds) const noexcept;

	/** Advances the schedule for an accepted tick and returns it. */
	FTickDecision ProduceDueTick(TimePointMilliseconds InNowMilliseconds) noexcept;

	/** Detects caller time rollback even while ticking is disabled. */
	TimePointMilliseconds LastObservedMilliseconds{0};

	/** Gives this tickable its own delta independent of sibling schedules. */
	TimePointMilliseconds PreviousTickMilliseconds{0};

	/** Avoids interval arithmetic on every not-due update. */
	TimePointMilliseconds NextDueMilliseconds{0};

	/** Retains the consumer cadence without consulting external configuration. */
	DurationMilliseconds IntervalMilliseconds{0};

	/** Prevents runtime enablement from overriding construction-time capability. */
	bool bCanEverTick{false};

	/** Represents current consumer intent independently from capability. */
	bool bEnabled{false};

	/** Rejects schedule advancement outside the owning object's lifecycle. */
	bool bPlaying{false};

	/** Forces the next accepted advance to establish a fresh zero-delta schedule. */
	bool bMustResetSchedule{true};
};

} // namespace MicroWorld::Core
