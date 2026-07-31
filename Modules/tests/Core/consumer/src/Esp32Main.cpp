#include "CoreConsumerProbe.h"

namespace
{

/** Motivation: Retains the compile probe outcome so optimization cannot erase representative public calls. */
volatile int CoreConsumerProbeResult = -1;

} // namespace

/**
 * Motivation: Proves ESP-IDF can link the exact package without platform dependencies entering it.
 * Responsibilities: Store the probe result where the host can observe it.
 */
extern "C" void app_main()
{
	CoreConsumerProbeResult = RunCoreConsumerProbe();
}
