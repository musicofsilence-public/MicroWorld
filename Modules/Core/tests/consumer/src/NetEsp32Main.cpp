#include "NetConsumerProbe.h"

namespace
{

/** Retains the compile probe outcome so optimization cannot erase representative public calls. */
volatile int NetConsumerProbeResult = -1;

} // namespace

/** Proves ESP-IDF can compile and link the Core+Memory+Net profile without executing hardware I/O. */
extern "C" void app_main()
{
	NetConsumerProbeResult = RunNetConsumerProbe();
}
