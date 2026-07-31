#pragma once

#include <MicroWorld/Core/Time.h>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Yields the calling FreeRTOS task for at least the requested duration so the idle task and lower
 *   priority work can run.
 * Responsibilities: Wrap vTaskDelay and round the duration up to at least one tick so a zero-tick call still yields.
 */
void SleepMilliseconds(Core::DurationMilliseconds InSleepDurationMilliseconds) noexcept;

} // namespace MicroWorld::Platform::Esp32
