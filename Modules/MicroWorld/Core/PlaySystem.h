#pragma once

#include <MicroWorld/Core/Time.h>

namespace MicroWorld::Core
{

/**
 * The contract for a system a host drives around its lifecycle and advance:
 * BeginPlay opens the system before the host begins, PreAdvance and PostAdvance
 * bracket each frame's main work, and EndPlay closes the system after the host ends.
 *
 * This contract lives in Core so Core-only modules can implement it without
 * depending on Engine.
 */
class IPlaySystem
{
public:
	/** Defaulted virtual so a derived system adapter destructs through this interface. */
	virtual ~IPlaySystem() noexcept = default;

	/** Play-start turn: a bound system opens its session at the host's one canonical time. */
	virtual void BeginPlay(TimePointMilliseconds) noexcept {}

	/** Pre-advance turn: a bound system does its inbound work before the host advances its own state. */
	virtual void PreAdvance(TimePointMilliseconds InNowMilliseconds) noexcept = 0;

	/** Post-advance turn: a bound system does its outbound work after the host has advanced. */
	virtual void PostAdvance(TimePointMilliseconds InNowMilliseconds) noexcept = 0;

	/** Play-end turn: a bound system closes its session after the host has ended. */
	virtual void EndPlay() noexcept {}
};

} // namespace MicroWorld::Core
