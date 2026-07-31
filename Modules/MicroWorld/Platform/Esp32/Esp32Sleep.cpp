#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace MicroWorld::Platform::Esp32
{

void SleepMilliseconds(Core::DurationMilliseconds InSleepDurationMilliseconds) noexcept
{
	TickType_t Ticks = pdMS_TO_TICKS(InSleepDurationMilliseconds);
	// A zero-tick delay would not yield to the idle task; guarantee the caller always yields at
	// least one tick.
	if (Ticks == 0)
	{
		Ticks = 1;
	}
	vTaskDelay(Ticks);
}

} // namespace MicroWorld::Platform::Esp32
