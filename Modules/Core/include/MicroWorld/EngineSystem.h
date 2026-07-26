#pragma once

#include <MicroWorld/Time.h>

namespace MicroWorld
{

/**
 * The contract for a system a host drives around its lifecycle and advance:
 * BeginPlay opens the system before the host begins, PreAdvance and PostAdvance
 * bracket each frame's main work, and EndPlay closes the system after the host ends.
 *
 * This contract lives in Core so Core-only modules can implement it without
 * depending on Engine.
 */
class IEngineSystem
{
public:
	/** Defaulted virtual so a derived system adapter destructs through this interface. */
	virtual ~IEngineSystem() noexcept = default;

	/** Play-start turn: a bound system opens its session at the engine's one canonical time. */
	virtual void BeginPlay(TimePointMilliseconds) noexcept {}

	/** Step 1 turn: a bound system does its pre-advance work (for a net host, drain inbound traffic, dispatch messages, age peers). */
	virtual void PreAdvance(TimePointMilliseconds InNowMilliseconds) noexcept = 0;

	/** Step 7 turn: a bound system does its post-advance work (for a net host, flush the queue and emit due heartbeats). */
	virtual void PostAdvance(TimePointMilliseconds InNowMilliseconds) noexcept = 0;

	/** Play-end turn: a bound system closes its session after the world has ended. */
	virtual void EndPlay() noexcept {}
};

} // namespace MicroWorld
