#include "TransportConsumerProbe.h"

namespace
{

/** Retains the compile probe outcome so optimization cannot erase representative public calls. */
volatile int TransportConsumerProbeResult = -1;

} // namespace

/** Proves ESP-IDF can compile and link the Core+Transport profile without executing hardware I/O. */
extern "C" void app_main()
{
	TransportConsumerProbeResult = RunTransportConsumerProbe();
}
