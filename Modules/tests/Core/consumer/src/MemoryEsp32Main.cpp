#include "MemoryConsumerProbe.h"

namespace
{

/** Motivation: Retains the compile probe outcome so optimization cannot erase representative public calls. */
volatile int MemoryConsumerProbeResult = -1;

} // namespace

/**
 * Motivation: Proves ESP-IDF can compile and link Core memory APIs without executing hardware I/O.
 * Responsibilities: Store the probe result where the host can observe it.
 */
extern "C" void app_main()
{
	MemoryConsumerProbeResult = RunMemoryConsumerProbe();
}
