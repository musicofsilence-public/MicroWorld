#pragma once

#include <MicroWorld/Core/TickFunction.h>
#include <MicroWorld/Core/Version.h>

static_assert(__cplusplus >= 201703L);
static_assert(MicroWorld::Core::Version.Major == 0);
static_assert(MicroWorld::Core::Version.Minor == 4);
static_assert(MicroWorld::Core::Version.Patch == 0);

/**
 * Motivation: Exercises the exact public Core primitives a downstream host links and runs against.
 * Responsibilities: Drive one full Tick lifecycle and report success only when it ticks as expected.
 */
inline int RunCoreConsumerProbe() noexcept
{
	MicroWorld::Core::FTickFunction CoreArchiveProbe({true, true, 0});
	CoreArchiveProbe.BeginPlay(0);
	const MicroWorld::Core::FTickDecision TickDecision = CoreArchiveProbe.Advance(0);
	CoreArchiveProbe.EndPlay();

	return TickDecision.Result == MicroWorld::Core::ERuntimeResult::Success && TickDecision.bShouldTick ? 0 : 1;
}
