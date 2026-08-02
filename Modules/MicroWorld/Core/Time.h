#pragma once

#include <cstdint>

namespace MicroWorld::Core
{

/** Motivation: Uses a wide monotonic domain so long-running consumers do not need wrap policy. */
using TimePointMilliseconds = std::uint64_t;

/** Motivation: Bounds per-tick deltas while keeping the hot context compact on MCUs. */
using DurationMilliseconds = std::uint32_t;

} // namespace MicroWorld::Core
