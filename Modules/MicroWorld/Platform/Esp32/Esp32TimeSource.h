#pragma once

#include <MicroWorld/Core/Time.h>

#include <esp_timer.h>

#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/**
 * ESP32 clock that feeds the engine's caller-supplied monotonic time contract.
 *
 * Reports `esp_timer_get_time()` divided by one thousand as whole milliseconds
 * since boot, so the ESP32 platform is the single source of real time and no
 * engine path reads a hidden clock. The microsecond source is already monotonic
 * since boot, so no baseline is captured; truncating to whole milliseconds drops
 * sub-millisecond precision the engine does not consume.
 */
class FEsp32TimeSource final
{
public:
	/** Reports whole milliseconds elapsed since boot as the engine's canonical time point. */
	Core::TimePointMilliseconds Now() const noexcept { return static_cast<Core::TimePointMilliseconds>(esp_timer_get_time() / 1000); }
};

} // namespace MicroWorld::Platform::Esp32
