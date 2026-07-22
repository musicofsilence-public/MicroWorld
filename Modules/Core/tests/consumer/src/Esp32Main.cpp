#include "CoreConsumerProbe.h"

namespace
{

/** Retains the compile probe outcome so optimization cannot erase representative public calls. */
volatile int CoreConsumerProbeResult = -1;

} // namespace

/** Proves ESP-IDF can link the exact package without platform dependencies entering it. */
extern "C" void app_main()
{
	CoreConsumerProbeResult = RunCoreConsumerProbe();
}
