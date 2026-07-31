#pragma once

#include <cstdint>

namespace MicroWorld::Tests
{

/** Motivation: Counts process-wide scalar, array, and aligned allocation calls after test setup. The Core test
 *   executable defines one set of global operator-new overrides in CoreAllocationCounters.cpp; every
 *   translation unit linked into that executable observes the same counter through this declaration. The
 *   folded-in timer test uses it to prove TTimerManager performs no observable allocation.
 */
extern std::uint32_t GlobalAllocationCount;

} // namespace MicroWorld::Tests
