#pragma once

#include <MicroWorld/Time.h>

namespace MicroWorld
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
};

/** Owns the bounded scheduling state for one independently tickable object. */
class FTickFunction final
{
public:
	/** Captures immutable capability and the consumer-selected initial schedule. */
	explicit FTickFunction(FTickConfiguration Configuration) noexcept;

	/** Starts scheduling from canonical dispatcher time. */
	void BeginPlay(TimePointMilliseconds NowMilliseconds) noexcept;

	/** Ends scheduling without invoking object policy. */
	void EndPlay() noexcept;

	/** Changes enablement without taking an independent clock sample. */
	ERuntimeResult SetEnabled(bool bEnabled) noexcept;

	/** Changes the minimum interval without changing enablement. */
	ERuntimeResult SetInterval(DurationMilliseconds IntervalMilliseconds) noexcept;

	/** Returns at most one due tick and rejects backward dispatcher time. */
	FTickDecision Advance(TimePointMilliseconds NowMilliseconds) noexcept;

	/** Lets owners expose enablement without leaking scheduler representation. */
	bool IsEnabled() const noexcept;

	/** Lets owners report cadence using the same explicit millisecond unit. */
	DurationMilliseconds GetInterval() const noexcept;

private:
	/** Saturates deadlines so a long-running clock cannot wrap into an early tick. */
	TimePointMilliseconds CalculateNextDue(TimePointMilliseconds NowMilliseconds) const noexcept;

	/** Saturates elapsed time because the public tick context uses a bounded duration. */
	DurationMilliseconds CalculateDelta(TimePointMilliseconds NowMilliseconds) const noexcept;

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
	bool bScheduleReset{true};
};

} // namespace MicroWorld
