#include "EngineConsumerProbe.h"

namespace
{

/** Retains the Engine profile probe result without issuing target hardware I/O. */
volatile int EngineConsumerProbeResult = -1;

} // namespace

/** Proves ESP-IDF compiles the Engine profile without executing hardware I/O. */
extern "C" void app_main()
{
	EngineConsumerProbeResult = RunEngineConsumerProbe();
}
