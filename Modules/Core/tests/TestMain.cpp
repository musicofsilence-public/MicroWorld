#include "TestSupport.h"

/** Returns the aggregate behavior-test result to CTest and other host runners. */
int main()
{
	return MicroWorld::Tests::RunAllTests();
}
