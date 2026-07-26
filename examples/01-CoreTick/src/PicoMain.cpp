#include "CoreTickExample.h"

#include <FreeRTOS.h>
#include <task.h>

#include <pico/time.h>

namespace
{

/** Reserves bounded task stack storage for the Pico CoreTick composition root. */
constexpr configSTACK_DEPTH_TYPE CoreTickTaskStackDepth = 512;

/** Preserves the polling pace while the tick function owns cadence decisions. */
constexpr unsigned PollPacingMilliseconds = 10;

/** Owns the shared example state for the Pico firmware lifetime. */
FCoreTickExample CoreTickExample;

/** Owns the FreeRTOS task metadata for the Pico example lifetime. */
StaticTask_t CoreTickTaskControlBlock;

/** Owns the statically allocated stack for the Pico example task. */
StackType_t CoreTickTaskStack[CoreTickTaskStackDepth];

/** Retains whether the Pico adapter completed its shared example behavior. */
volatile int CoreTickResult = -1;

/** Advances the shared example from Pico monotonic time, then suspends. */
void RunCoreTickTask(void*)
{
	CoreTickExample.Begin(to_ms_since_boot(get_absolute_time()));
	bool bFailed = false;

	while (!CoreTickExample.IsFinished())
	{
		const FCoreTickExampleStep Step = CoreTickExample.Advance(to_ms_since_boot(get_absolute_time()));
		if (Step.Decision.Result != MicroWorld::ERuntimeResult::Success)
		{
			bFailed = true;
			break;
		}

		vTaskDelay(pdMS_TO_TICKS(PollPacingMilliseconds));
	}

	CoreTickResult = bFailed ? 1 : 0;
	vTaskSuspend(nullptr);
}

} // namespace

/** Creates the static Pico task and transfers control to FreeRTOS. */
int main()
{
	TaskHandle_t CoreTickTask = xTaskCreateStatic(
		RunCoreTickTask, "MicroWorldCoreTick", CoreTickTaskStackDepth, nullptr, tskIDLE_PRIORITY + 1, CoreTickTaskStack, &CoreTickTaskControlBlock);

	if (CoreTickTask == nullptr)
	{
		return 1;
	}

	vTaskStartScheduler();
	return 2;
}
