#include "ObjectConsumerProbe.h"

namespace
{

/** Retains the Object profile probe result without issuing target hardware I/O. */
volatile int ObjectConsumerProbeResult = -1;

} // namespace

/** Proves ESP-IDF compiles the Object profile without executing hardware I/O. */
extern "C" void app_main()
{
	ObjectConsumerProbeResult = RunObjectConsumerProbe();
}
