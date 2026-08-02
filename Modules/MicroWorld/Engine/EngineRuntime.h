#pragma once

#include <MicroWorld/Core/RuntimeResult.h>
#include <MicroWorld/Core/Time.h>

namespace MicroWorld::Engine
{

/**
 * Motivation: Lets an application drive an engine without naming its compile-time traits or concrete configuration facilities.
 * Responsibilities: Declare only the begin, tick, and end turns an application owns; implementations preserve their runtime behavior.
 * Example:
 *   constexpr Core::DurationMilliseconds FrameIntervalMilliseconds{16};
 *   IEngineRuntime& Runtime = Engine;
 *   (void)Runtime.BeginPlay(Now); Runtime.Tick(Now + FrameIntervalMilliseconds);
 */
class IEngineRuntime
{
public:
	/**
	 * Motivation: Lets a runtime implementation be destroyed through this contract.
	 * Responsibilities: Default virtual destruction so derived teardown runs.
	 */
	virtual ~IEngineRuntime() noexcept = default;

	/**
	 * Motivation: Starts the runtime at one canonical time so its live systems share a baseline.
	 * Responsibilities: Return success or the authoritative lifecycle error.
	 */
	virtual Core::ERuntimeResult BeginPlay(Core::TimePointMilliseconds InNowMilliseconds) noexcept = 0;

	/**
	 * Motivation: Runs one runtime frame at the caller-supplied time.
	 * Responsibilities: Return the authoritative per-frame outcome.
	 */
	virtual Core::ERuntimeResult Tick(Core::TimePointMilliseconds InNowMilliseconds) noexcept = 0;

	/**
	 * Motivation: Stops the runtime when application shutdown begins.
	 * Responsibilities: Return the authoritative shutdown outcome.
	 */
	virtual Core::ERuntimeResult EndPlay() noexcept = 0;
};

} // namespace MicroWorld::Engine
