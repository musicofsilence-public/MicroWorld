#pragma once

#include <MicroWorld/Core/Time.h>

#include <esp_timer.h>

#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Feeds the engine's caller-supplied monotonic time contract from the ESP32 hardware clock so the ESP32
 *   platform is the single source of real time and no engine path reads a hidden clock.
 * Responsibilities: Report whole milliseconds since boot from esp_timer_get_time(), dropping sub-millisecond
 *   precision the engine does not consume; capture no baseline because the microsecond source is already monotonic.
 * Example:
 *   FEsp32TimeSource Clock;
 *   Manager.Advance(Clock.Now());
 */
class FEsp32TimeSource final
{
public:
	/**
	 * Motivation: Lets the engine query the canonical ESP32 time point on each frame without a hidden clock.
	 * Responsibilities: Report whole milliseconds elapsed since boot.
	 */
	Core::TimePointMilliseconds Now() const noexcept { return static_cast<Core::TimePointMilliseconds>(esp_timer_get_time() / 1000); }
};

} // namespace MicroWorld::Platform::Esp32
