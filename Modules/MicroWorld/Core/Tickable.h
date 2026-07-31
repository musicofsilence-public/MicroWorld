#pragma once

#include <MicroWorld/Core/TickFunction.h>

namespace MicroWorld::Core
{

/**
 * Motivation: Adds one primary tick to a runtime type without defining what that type does.
 * Responsibilities: Hold one independent tick schedule per instance and forward its lifecycle and cadence calls.
 * Example:
 *   class FSpawner : public FTickable { using FTickable::FTickable; };
 *   FSpawner Spawner(FTickConfiguration::EnabledEvery(16));
 */
class FTickable
{
public:
	/**
	 * Motivation: Preserves scheduler identity because registered runtime objects are pointer-stable.
	 * Responsibilities: Reject copy construction so a registered object's tick state never duplicates.
	 */
	FTickable(const FTickable&) = delete;

	/**
	 * Motivation: Prevents one scheduler state from being assigned across registered objects.
	 * Responsibilities: Reject copy assignment so a registered object's tick state never duplicates.
	 */
	FTickable& operator=(const FTickable&) = delete;

	/**
	 * Motivation: Preserves scheduler identity because registered runtime objects are pointer-stable.
	 * Responsibilities: Reject move construction so a registered object's tick address never relocates.
	 */
	FTickable(FTickable&&) = delete;

	/**
	 * Motivation: Prevents scheduler state from moving behind a registered object address.
	 * Responsibilities: Reject move assignment so a registered object's tick address never relocates.
	 */
	FTickable& operator=(FTickable&&) = delete;

	/**
	 * Motivation: Exposes safe runtime enablement without exposing the scheduler itself.
	 * Responsibilities: Forward the enablement change to the primary tick function.
	 */
	ERuntimeResult SetTickEnabled(const bool bInEnabled) noexcept { return PrimaryTick.SetEnabled(bInEnabled); }

	/**
	 * Motivation: Lets consumers change cadence while keeping schedule-reset rules centralized.
	 * Responsibilities: Forward the interval change to the primary tick function.
	 */
	ERuntimeResult SetTickInterval(const DurationMilliseconds InIntervalMilliseconds) noexcept
	{
		return PrimaryTick.SetInterval(InIntervalMilliseconds);
	}

	/**
	 * Motivation: Reports current intent without granting mutable access to scheduling state.
	 * Responsibilities: Forward the enablement query to the primary tick function.
	 */
	bool IsTickEnabled() const noexcept { return PrimaryTick.IsEnabled(); }

	/**
	 * Motivation: Reports current cadence using the public unit-explicit type.
	 * Responsibilities: Forward the interval query to the primary tick function.
	 */
	DurationMilliseconds GetTickInterval() const noexcept { return PrimaryTick.GetInterval(); }

protected:
	/**
	 * Motivation: Gives each derived runtime object one independent primary schedule.
	 * Responsibilities: Construct the primary tick function from the supplied configuration.
	 */
	explicit FTickable(const FTickConfiguration InConfiguration) noexcept : PrimaryTick(InConfiguration) {}

	/**
	 * Motivation: Restricts scheduling decisions to lifecycle-aware derived dispatchers.
	 * Responsibilities: Forward one advance to the primary tick function.
	 */
	FTickDecision AdvancePrimaryTick(const TimePointMilliseconds InNowMilliseconds) noexcept { return PrimaryTick.Advance(InNowMilliseconds); }

	/**
	 * Motivation: Aligns the first tick with the owning object's canonical begin time.
	 * Responsibilities: Forward the play-start to the primary tick function.
	 */
	void BeginPrimaryTickLifecycle(const TimePointMilliseconds InNowMilliseconds) noexcept { PrimaryTick.BeginPlay(InNowMilliseconds); }

	/**
	 * Motivation: Stops future decisions when the owning runtime object leaves play.
	 * Responsibilities: Forward the play-end to the primary tick function.
	 */
	void EndPrimaryTickLifecycle() noexcept { PrimaryTick.EndPlay(); }

private:
	/** Motivation: Keeps cadence state private so every runtime type obeys identical rules. */
	FTickFunction PrimaryTick;
};

} // namespace MicroWorld::Core
