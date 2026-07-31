#include "CoreConsumerProbe.h"

/**
 * Motivation: Proves a standalone host executable can link and run the exact public Core primitives.
 * Responsibilities: Return the probe result so a host process can observe it.
 */
int main()
{
	return RunCoreConsumerProbe();
}
