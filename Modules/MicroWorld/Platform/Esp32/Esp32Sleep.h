#pragma once

#include <MicroWorld/Core/Time.h>

namespace MicroWorld
{

/** Yields the calling task for at least the given time (wraps vTaskDelay, rounded up to >= 1 tick). */
void SleepMilliseconds(DurationMilliseconds InSleepDurationMilliseconds) noexcept;

} // namespace MicroWorld
