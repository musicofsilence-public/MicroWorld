#include "CoreTickExample.h"
#include "TestSupport.h"

#include <cstdint>

/**
 * Scenario: Advance the example across an initial tick, an early poll before the deadline, and a deadline poll.
 * Expected: The early poll does not tick; only the initial and deadline polls consume ticks.
 */
MW_TEST_CASE(CoreTickExampleIgnoresEarlyPolls)
{
	// Arrange
	FCoreTickExample Example;
	Example.Begin(0);

	// Act
	const FCoreTickExampleStep FirstStep = Example.Advance(0);
	const FCoreTickExampleStep EarlyStep = Example.Advance(499);
	const FCoreTickExampleStep SecondStep = Example.Advance(500);

	// Assert
	MW_EXPECT_TRUE(Test, FirstStep.Decision.bShouldTick, "initial schedule should tick");
	MW_EXPECT_TRUE(Test, !FirstStep.bFinished, "first tick should not finish the example");
	MW_EXPECT_TRUE(Test, !EarlyStep.Decision.bShouldTick, "early poll should not tick");
	MW_EXPECT_TRUE(Test, !EarlyStep.bFinished, "early poll should not finish the example");
	MW_EXPECT_TRUE(Test, SecondStep.Decision.bShouldTick, "deadline poll should tick");
	MW_EXPECT_EQ(Test, std::uint32_t{500}, SecondStep.Decision.Context.DeltaMilliseconds, "second due tick should preserve the cadence delta");
}

/**
 * Scenario: Drive the example through four due ticks, then a fifth, then a post-completion poll.
 * Expected: The fifth due tick finishes the trace; later calls cannot tick again.
 */
MW_TEST_CASE(CoreTickExampleFinishesOnTheFifthDueTick)
{
	// Arrange
	FCoreTickExample Example;
	Example.Begin(0);

	// Act
	for (MicroWorld::TimePointMilliseconds DueTime = 0; DueTime < 2000; DueTime += 500)
	{
		const FCoreTickExampleStep Step = Example.Advance(DueTime);
		MW_EXPECT_TRUE(Test, Step.Decision.bShouldTick, "each scheduled deadline before the fifth should tick");
		MW_EXPECT_TRUE(Test, !Step.bFinished, "only the fifth due tick should finish the example");
	}

	const FCoreTickExampleStep FinalStep = Example.Advance(2000);
	const FCoreTickExampleStep AfterCompletionStep = Example.Advance(2500);

	// Assert
	MW_EXPECT_TRUE(Test, FinalStep.Decision.bShouldTick, "fifth scheduled deadline should tick");
	MW_EXPECT_TRUE(Test, FinalStep.bFinished, "fifth due tick should finish the example");
	MW_EXPECT_TRUE(Test, Example.IsFinished(), "finished state should be observable to platform adapters");
	MW_EXPECT_TRUE(Test, !AfterCompletionStep.Decision.bShouldTick, "completed example should not tick again");
	MW_EXPECT_TRUE(Test, AfterCompletionStep.bFinished, "post-completion step should remain finished");
}
