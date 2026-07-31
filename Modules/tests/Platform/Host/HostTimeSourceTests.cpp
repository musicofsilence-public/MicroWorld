#include "TestSupport.h"

#include <MicroWorld/Platform/Host/HostTimeSource.h>
#include <MicroWorld/Core/Time.h>

#include <cstddef>

namespace
{

using MicroWorld::Core::TimePointMilliseconds;
using MicroWorld::Platform::Host::FHostTimeSource;

/** Motivation: Iteration count large enough that two Now() readings are reliably separated on the host steady clock. */
constexpr std::uint64_t MonotonicProbeIterations = 100000;

/**
 * Motivation: Bounded busy work that exercises the clock without sleeping or allocating.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
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
 * Motivation: Read Now() before and after a bounded span of synchronous work driven against the host steady clock.
 * Responsibilities: The second Now() reading never moves backward relative to the first across the bounded work.
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
