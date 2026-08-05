#include "ApplicationConsumerProbe.h"

/**
 * Motivation: Proves a standalone host executable can link and run the public Application profile.
 * Responsibilities: Return the probe result so a host process can observe it.
 */
int main()
{
	return RunApplicationConsumerProbe();
}
