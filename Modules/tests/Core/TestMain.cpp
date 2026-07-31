#include "TestSupport.h"

/**
 * Motivation: Returns the aggregate behavior-test result to CTest and other host runners.
 * Responsibilities: Aggregate every registered result and return non-zero on any failure.
 */
int main()
{
	return MicroWorld::Tests::RunAllTests();
}
