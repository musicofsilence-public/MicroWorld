#pragma once

#include <MicroWorld/Core/Time.h>

namespace MicroWorld::Platform::Esp32
{

/** Yields the calling task for at least the given time (wraps vTaskDelay, rounded up to >= 1 tick). */
void SleepMilliseconds(Core::DurationMilliseconds InSleepDurationMilliseconds) noexcept;

} // namespace MicroWorld::Platform::Esp32
