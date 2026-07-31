#include <FreeRTOS.h>
#include <task.h>

namespace
{

/** Motivation: Reserves the minimum static task stack for the inert Core-test link image. */
constexpr configSTACK_DEPTH_TYPE CoreTestsTaskStackDepth = 256;

/** Motivation: Owns the FreeRTOS task metadata for the full test firmware lifetime. */
StaticTask_t CoreTestsTaskControlBlock;

/** Motivation: Owns the static stack while the compile/link evidence task is scheduled. */
StackType_t CoreTestsTaskStack[CoreTestsTaskStackDepth];

/**
 * Motivation: Deliberately avoids running stack-heavy Core tests on the RP2040.
 * Responsibilities: Park the task permanently so the link image proves linkage without running tests.
 */
void SuspendCoreTestsTask(void*)
{
	vTaskSuspend(nullptr);
}

} // namespace

/**
 * Motivation: Starts the inert static task that proves the Core tests link with FreeRTOS.
 * Responsibilities: Create the one static task and hand control to the scheduler.
 */
int main()
{
	TaskHandle_t TestsTask = xTaskCreateStatic(
		SuspendCoreTestsTask,
		"MicroWorldCoreTests",
		CoreTestsTaskStackDepth,
		nullptr,
		tskIDLE_PRIORITY + 1,
		CoreTestsTaskStack,
		&CoreTestsTaskControlBlock);

	if (TestsTask == nullptr)
	{
		return 1;
	}

	vTaskStartScheduler();
	return 2;
}
