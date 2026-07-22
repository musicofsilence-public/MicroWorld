#pragma once

#include <cstdint>

namespace MicroWorld::Tests
{

/**
 * Counts process-wide scalar, array, and aligned allocation calls after test setup.
 *
 * The engine test executable defines one set of global operator-new overrides
 * in EngineAllocationCounters.cpp; every translation unit linked into that
 * executable observes the same counter through this declaration.
 */
extern std::uint32_t GlobalAllocationCount;

} // namespace MicroWorld::Tests
