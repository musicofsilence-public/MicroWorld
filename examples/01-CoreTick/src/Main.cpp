#include "CoreTickExample.h"

#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Core/Version.h>

#include <MicroWorld/Platform/Esp32/Esp32OutputDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>

namespace
{
/** Single real-time source; every MicroWorld deadline in this example reads it. */
MicroWorld::FEsp32TimeSource GTimeSource{};

/** Poll far faster than the cadence so the FreeRTOS idle task (and its watchdog)
 *  always runs; the tick function, not this delay, decides when a tick is due. */
constexpr unsigned PollPacingMilliseconds = 10;
} // namespace

/** Composition root: drives one 500 ms tick schedule off real time for five ticks. */
extern "C" void app_main(void)
{
	MicroWorld::Core::SetOutputDevice(&MicroWorld::WriteEsp32LogRecord);

	// Announce the exact package contract this image was built against.
	MW_LOG(
		Log,
		"ex01",
		"microworld %u.%u.%u",
		static_cast<unsigned>(MicroWorld::Version.Major),
		static_cast<unsigned>(MicroWorld::Version.Minor),
		static_cast<unsigned>(MicroWorld::Version.Patch));

	// Static, never on the app_main stack (the ESP32-S3 stack lesson, §2.2).
	static FCoreTickExample CoreTickExample{};

	// Start scheduling from a caller-supplied time point — no hidden clock read.
	CoreTickExample.Begin(GTimeSource.Now());

	// Poll on a fast pace; print only when the decision reports a due tick, and
	// take the delta from the decision rather than subtracting clocks ourselves.
	unsigned TickCount = 0;
	while (!CoreTickExample.IsFinished())
	{
		const FCoreTickExampleStep Step = CoreTickExample.Advance(GTimeSource.Now());
		const MicroWorld::Core::FTickDecision& Decision = Step.Decision;
		if (Decision.bShouldTick)
		{
			++TickCount;
			MW_LOG(Log, "ex01", "tick n=%u delta=%u", TickCount, static_cast<unsigned>(Decision.Context.DeltaMilliseconds));
		}
		MicroWorld::SleepMilliseconds(PollPacingMilliseconds);
	}

	// The shared bounded behavior closes its schedule on the fifth due tick.
	MW_LOG(Log, "ex01", "done ticks=%u", TickCount);
}
