#include "TestSupport.h"

#include <MicroWorld/Platform/Host/HostTimeSource.h>
#include <MicroWorld/Core/Time.h>

#include <cstddef>

namespace
{

using MicroWorld::FHostTimeSource;
using MicroWorld::Core::TimePointMilliseconds;

/** Iteration count large enough that two Now() readings are reliably separated on the host steady clock. */
constexpr std::uint64_t MonotonicProbeIterations = 100000;

/** Bounded busy work that exercises the clock without sleeping or allocating. */
TimePointMilliseconds BurnCyclesAndRead(FHostTimeSource& Clock) noexcept
{
	volatile std::uint64_t Sink = 0;
	for (std::uint64_t Index = 0; Index < MonotonicProbeIterations; ++Index)
	{
		Sink += Index;
	}
	(void)Sink;
	return Clock.Now();
}

} // namespace

/**
 * Scenario: Read Now() before and after a bounded span of synchronous work driven against the host steady clock.
 * Expected: The second Now() reading never moves backward relative to the first across the bounded work.
 */
MW_TEST_CASE(HostTimeSourceNowIsNonDecreasingAcrossBoundedWork)
{
	// Arrange
	FHostTimeSource Clock;
	const TimePointMilliseconds Before = Clock.Now();

	// Act
	const TimePointMilliseconds After = BurnCyclesAndRead(Clock);

	// Assert
	MW_EXPECT_TRUE(Test, After >= Before, "Now() never moves backward across bounded work");
}
