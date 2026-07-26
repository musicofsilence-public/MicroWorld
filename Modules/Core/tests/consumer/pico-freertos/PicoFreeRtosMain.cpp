#include "../src/CoreConsumerProbe.h"

#include <FreeRTOS.h>
#include <task.h>

namespace
{

/** Reserves bounded task stack storage for the one-shot Core probe. */
constexpr configSTACK_DEPTH_TYPE CoreProbeTaskStackDepth = 512;

/** Owns the FreeRTOS task metadata for the full firmware lifetime. */
StaticTask_t CoreProbeTaskControlBlock;

/** Owns the statically allocated stack for the full firmware lifetime. */
StackType_t CoreProbeTaskStack[CoreProbeTaskStackDepth];

/** Retains the probe outcome so the linked behavior remains observable. */
volatile int CoreProbeResult = -1;

/** Runs the shared Core probe once, then removes itself from scheduling. */
void RunCoreProbeTask(void*)
{
	CoreProbeResult = RunCoreConsumerProbe();
	vTaskSuspend(nullptr);
}

} // namespace

/** Creates the bounded probe task and transfers control to FreeRTOS. */
int main()
{
	TaskHandle_t ProbeTask = xTaskCreateStatic(
		RunCoreProbeTask,
		"MicroWorldCoreProbe",
		CoreProbeTaskStackDepth,
		nullptr,
		tskIDLE_PRIORITY + 1,
		CoreProbeTaskStack,
		&CoreProbeTaskControlBlock);

	if (ProbeTask == nullptr)
	{
		return 1;
	}

	vTaskStartScheduler();
	return 2;
}
