#include <MicroWorld/TickFunction.h>
#include <MicroWorld/Version.h>

#include <MicroWorld/PlatformEsp32/Esp32TimeSource.h>

#include <cstdio>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace
{
/** Single real-time source; every MicroWorld deadline in this example reads it. */
MicroWorld::FEsp32TimeSource GTimeSource{};

/** The cadence this example is about: the tick function fires at most this often. */
constexpr MicroWorld::DurationMilliseconds TickIntervalMilliseconds = 500;

/** Bounds the run so the trace is a fixed seven lines instead of looping forever. */
constexpr unsigned TargetTickCount = 5;

/** Poll far faster than the cadence so the FreeRTOS idle task (and its watchdog)
 *  always runs; the tick function, not this delay, decides when a tick is due. */
constexpr unsigned PollPacingMilliseconds = 10;
} // namespace

/** Composition root: drives one 500 ms tick schedule off real time for five ticks. */
extern "C" void app_main(void)
{
	// Announce the exact package contract this image was built against.
	std::printf(
		"[ex01] microworld %u.%u.%u\n",
		static_cast<unsigned>(MicroWorld::Version.Major),
		static_cast<unsigned>(MicroWorld::Version.Minor),
		static_cast<unsigned>(MicroWorld::Version.Patch));

	// Static, never on the app_main stack (the ESP32-S3 stack lesson, §2.2).
	static MicroWorld::FTickFunction SensorTick{MicroWorld::FTickConfiguration::EnabledEvery(TickIntervalMilliseconds)};

	// Start scheduling from a caller-supplied time point — no hidden clock read.
	const MicroWorld::TimePointMilliseconds BootTime = GTimeSource.Now();
	SensorTick.BeginPlay(BootTime);

	// Poll on a fast pace; print only when the decision reports a due tick, and
	// take the delta from the decision rather than subtracting clocks ourselves.
	unsigned TickCount = 0;
	while (TickCount < TargetTickCount)
	{
		const MicroWorld::FTickDecision Decision = SensorTick.Advance(GTimeSource.Now());
		if (Decision.bShouldTick)
		{
			++TickCount;
			std::printf("[ex01] tick n=%u delta=%u\n", TickCount, static_cast<unsigned>(Decision.Context.DeltaMilliseconds));
		}
		vTaskDelay(pdMS_TO_TICKS(PollPacingMilliseconds));
	}

	// Close the schedule and report the bounded run finished.
	SensorTick.EndPlay();
	std::printf("[ex01] done ticks=%u\n", TickCount);
}
