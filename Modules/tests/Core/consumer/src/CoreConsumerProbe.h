#pragma once

#include <MicroWorld/Core/TickFunction.h>
#include <MicroWorld/Core/Version.h>

static_assert(__cplusplus >= 201703L);
static_assert(MicroWorld::Version.Major == 0);
static_assert(MicroWorld::Version.Minor == 4);
static_assert(MicroWorld::Version.Patch == 0);

/** Exercises the exact public Core primitives a downstream host links and runs against. */
inline int RunCoreConsumerProbe() noexcept
{
	MicroWorld::FTickFunction CoreArchiveProbe({true, true, 0});
	CoreArchiveProbe.BeginPlay(0);
	const MicroWorld::FTickDecision TickDecision = CoreArchiveProbe.Advance(0);
	CoreArchiveProbe.EndPlay();

	return TickDecision.Result == MicroWorld::ERuntimeResult::Success && TickDecision.bShouldTick ? 0 : 1;
}
