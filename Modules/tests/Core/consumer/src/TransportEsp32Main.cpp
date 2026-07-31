#include "TransportConsumerProbe.h"

namespace
{

/** Motivation: Retains the compile probe outcome so optimization cannot erase representative public calls. */
volatile int TransportConsumerProbeResult = -1;

} // namespace

/**
 * Motivation: Proves ESP-IDF can compile and link the Core+Transport profile without executing hardware I/O.
 * Responsibilities: Store the probe result where the host can observe it.
 */
extern "C" void app_main()
{
	TransportConsumerProbeResult = RunTransportConsumerProbe();
}
