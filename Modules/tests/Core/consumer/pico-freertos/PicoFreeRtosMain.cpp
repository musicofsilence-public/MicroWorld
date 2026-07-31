#include "../src/CoreConsumerProbe.h"

#include <FreeRTOS.h>
#include <task.h>

namespace
{

/** Motivation: Reserves bounded task stack storage for the one-shot Core probe. */
constexpr configSTACK_DEPTH_TYPE CoreProbeTaskStackDepth = 512;

/** Motivation: Owns the FreeRTOS task metadata for the full firmware lifetime. */
StaticTask_t CoreProbeTaskControlBlock;

/** Motivation: Owns the statically allocated stack for the full firmware lifetime. */
StackType_t CoreProbeTaskStack[CoreProbeTaskStackDepth];

/** Motivation: Retains the probe outcome so the linked behavior remains observable. */
volatile int CoreProbeResult = -1;

/**
 * Motivation: Runs the shared Core probe once, then removes itself from scheduling.
 * Responsibilities: Capture the probe result and suspend so the task never re-enters.
 */
void RunCoreProbeTask(void*)
{
	CoreProbeResult = RunCoreConsumerProbe();
	vTaskSuspend(nullptr);
}

} // namespace

/**
 * Motivation: Creates the bounded probe task and transfers control to FreeRTOS.
 * Responsibilities: Create the static probe task and hand control to the scheduler.
 */
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
