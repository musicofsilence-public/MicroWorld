#include "../../CoreAllocationCounters.h"

namespace MicroWorld::Tests
{

/** Motivation: Satisfies existing test references without importing host-only allocation overrides. */
std::uint32_t GlobalAllocationCount{0};

} // namespace MicroWorld::Tests
