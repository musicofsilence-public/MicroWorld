#include "ObjectConsumerProbe.h"

/**
 * Motivation: Proves the standalone Object profile compiles without exceptions or RTTI.
 * Responsibilities: Return the probe result so a host process can observe it.
 */
int main()
{
	return RunObjectConsumerProbe();
}
