#include "ObjectConsumerProbe.h"

namespace
{

/** Motivation: Retains the Object profile probe result without issuing target hardware I/O. */
volatile int ObjectConsumerProbeResult = -1;

} // namespace

/**
 * Motivation: Proves ESP-IDF compiles the Object profile without executing hardware I/O.
 * Responsibilities: Store the probe result where the host can observe it.
 */
extern "C" void app_main()
{
	ObjectConsumerProbeResult = RunObjectConsumerProbe();
}
