#include "EngineConsumerProbe.h"

namespace
{

/** Motivation: Retains the Engine profile probe result without issuing target hardware I/O. */
volatile int EngineConsumerProbeResult = -1;

} // namespace

/**
 * Motivation: Proves ESP-IDF compiles the Engine profile without executing hardware I/O.
 * Responsibilities: Store the probe result where the host can observe it.
 */
extern "C" void app_main()
{
	EngineConsumerProbeResult = RunEngineConsumerProbe();
}
