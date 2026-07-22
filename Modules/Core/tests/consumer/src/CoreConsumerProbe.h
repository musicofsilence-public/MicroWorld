#pragma once

#include <MicroWorld/Application.h>
#include <MicroWorld/TickFunction.h>
#include <MicroWorld/Version.h>

#include <type_traits>

static_assert(__cplusplus >= 201703L);
static_assert(std::is_nothrow_destructible_v<MicroWorld::FApplication>);
static_assert(MicroWorld::Version.Major == 0);
static_assert(MicroWorld::Version.Minor == 2);
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
