#include "MemoryConsumerProbe.h"

/**
 * Motivation: Proves a standalone host executable can link and run Core memory APIs.
 * Responsibilities: Return the probe result so a host process can observe it.
 */
int main()
{
	return RunMemoryConsumerProbe();
}
