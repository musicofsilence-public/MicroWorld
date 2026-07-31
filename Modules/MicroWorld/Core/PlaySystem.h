#pragma once

#include <MicroWorld/Core/Time.h>

namespace MicroWorld::Core
{

/**
 * Motivation: Gives a host one contract for driving a system around its lifecycle and frame advance.
 * Responsibilities: Open the system at play start, bracket each frame's main work with pre- and post-advance turns,
 *   and close the system at play end, without depending on Engine.
 * Example:
 *   class FCounter : public IPlaySystem { // system body
 *   };
 *   FCounter System;
 *   System.BeginPlay(Now);
 */
class IPlaySystem
{
public:
	/**
	 * Motivation: Lets a derived system adapter be destroyed through this interface.
	 * Responsibilities: Default destruction so concrete adapters clean up their own resources.
	 */
	virtual ~IPlaySystem() noexcept = default;

	/**
	 * Motivation: Gives a bound system one canonical time to open its session before the host begins.
	 * Responsibilities: Default to no-op so only systems that need play-start override it.
	 */
	virtual void BeginPlay(TimePointMilliseconds) noexcept {}

	/**
	 * Motivation: Lets a bound system do its inbound work before the host advances its own state.
	 * Responsibilities: Run the pre-advance turn at the host-supplied time.
	 */
	virtual void PreAdvance(TimePointMilliseconds InNowMilliseconds) noexcept = 0;

	/**
	 * Motivation: Lets a bound system do its outbound work after the host has advanced.
	 * Responsibilities: Run the post-advance turn at the host-supplied time.
	 */
	virtual void PostAdvance(TimePointMilliseconds InNowMilliseconds) noexcept = 0;

	/**
	 * Motivation: Gives a bound system one canonical time to close its session after the host ends.
	 * Responsibilities: Default to no-op so only systems that need play-end override it.
	 */
	virtual void EndPlay() noexcept {}
};

} // namespace MicroWorld::Core
