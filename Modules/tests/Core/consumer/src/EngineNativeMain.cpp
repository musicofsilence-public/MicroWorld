#include "EngineConsumerProbe.h"

/**
 * Motivation: Proves the standalone Engine profile compiles without exceptions or RTTI.
 * Responsibilities: Return the probe result so a host process can observe it.
 */
int main()
{
	return RunEngineConsumerProbe();
}
