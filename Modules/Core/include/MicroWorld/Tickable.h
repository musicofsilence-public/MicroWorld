#pragma once

#include <MicroWorld/TickFunction.h>

namespace MicroWorld
{

/** Adds one primary tick to a runtime type without defining its behavior. */
class FTickable
{
public:
	/** Preserves scheduler identity because registered runtime objects are pointer-stable. */
	FTickable(const FTickable&) = delete;

	/** Prevents one scheduler state from being assigned across registered objects. */
	FTickable& operator=(const FTickable&) = delete;

	/** Preserves scheduler identity because registered runtime objects are pointer-stable. */
	FTickable(FTickable&&) = delete;

	/** Prevents scheduler state from moving behind a registered object address. */
	FTickable& operator=(FTickable&&) = delete;

	/** Exposes safe runtime enablement without exposing the scheduler itself. */
	ERuntimeResult SetTickEnabled(const bool bEnabled) noexcept { return PrimaryTick.SetEnabled(bEnabled); }

	/** Lets consumers change cadence while keeping schedule-reset rules centralized. */
	ERuntimeResult SetTickInterval(const DurationMilliseconds IntervalMilliseconds) noexcept { return PrimaryTick.SetInterval(IntervalMilliseconds); }

	/** Reports current intent without granting mutable access to scheduling state. */
	bool IsTickEnabled() const noexcept { return PrimaryTick.IsEnabled(); }

	/** Reports current cadence using the public unit-explicit type. */
	DurationMilliseconds GetTickInterval() const noexcept { return PrimaryTick.GetInterval(); }

protected:
	/** Gives each derived runtime object one independent primary schedule. */
	explicit FTickable(const FTickConfiguration Configuration) noexcept : PrimaryTick(Configuration) {}

	/** Restricts scheduling decisions to lifecycle-aware derived dispatchers. */
	FTickDecision AdvancePrimaryTick(const TimePointMilliseconds NowMilliseconds) noexcept { return PrimaryTick.Advance(NowMilliseconds); }

	/** Aligns the first tick with the owning object's canonical begin time. */
	void BeginPrimaryTickLifecycle(const TimePointMilliseconds NowMilliseconds) noexcept { PrimaryTick.BeginPlay(NowMilliseconds); }

	/** Stops future decisions when the owning runtime object leaves play. */
	void EndPrimaryTickLifecycle() noexcept { PrimaryTick.EndPlay(); }

private:
	/** Keeps cadence state private so every runtime type obeys identical rules. */
	FTickFunction PrimaryTick;
};

} // namespace MicroWorld
