#include "MemoryConsumerProbe.h"

namespace
{

/** Retains the compile probe outcome so optimization cannot erase representative public calls. */
volatile int MemoryConsumerProbeResult = -1;

} // namespace

/** Proves ESP-IDF can compile and link the Core+Memory profile without executing hardware I/O. */
extern "C" void app_main()
{
	MemoryConsumerProbeResult = RunMemoryConsumerProbe();
}
