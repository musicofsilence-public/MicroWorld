#include "TestSupport.h"

#include <MicroWorld/PlatformHost/HostTimeSource.h>
#include <MicroWorld/Time.h>

#include <cstddef>

namespace
{

using MicroWorld::FHostTimeSource;
using MicroWorld::TimePointMilliseconds;

/** Bounded busy work that exercises the clock without sleeping or allocating. */
TimePointMilliseconds BurnCyclesAndRead(FHostTimeSource& Clock) noexcept
{
	volatile std::uint64_t Sink = 0;
	for (std::uint64_t Index = 0; Index < 100000; ++Index)
	{
		Sink += Index;
	}
	(void)Sink;
	return Clock.Now();
}

} // namespace

/** Now() is monotonic across bounded work on the host steady clock. */
MW_TEST_CASE(HostTimeSourceNowIsNonDecreasingAcrossBoundedWork)
{
	FHostTimeSource Clock;
	const TimePointMilliseconds Before = Clock.Now();
	const TimePointMilliseconds After = BurnCyclesAndRead(Clock);
	MW_EXPECT_TRUE(Test, After >= Before, "Now() never moves backward across bounded work");
}
