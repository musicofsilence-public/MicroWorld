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

} // namespace MicroWorld::Core
