#pragma once

#include <cstdint>

namespace MicroWorld::Tests
{

/** Motivation: Counts process-wide scalar, array, and aligned allocation calls after test setup. This is the single
 *   shared counter for every host test executable: each links CoreAllocationCounters.cpp once, and every translation
 *   unit in that executable observes the same counter through this declaration. The folded-in timer, garbage-collector,
 *   frame-codec, transport-host, and allocation tests use it to prove their hot paths perform no observable allocation.
 */
extern std::uint32_t GlobalAllocationCount;

} // namespace MicroWorld::Tests
