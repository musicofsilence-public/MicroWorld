#pragma once

#include <MicroWorld/Core/RuntimeResult.h>

#include <cstdio>

namespace MicroWorld::Tests
{

using namespace ::MicroWorld::Core;

/**
 * Motivation: Records assertion failures for one named test without dynamic storage.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FTestContext final
{
public:
	/**
	 * Motivation: Associates every failure with one behavior-oriented test name.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	explicit FTestContext(const char* const InTestName) noexcept : Name(InTestName) {}

	/**
	 * Motivation: Successful assertions remain allocation-free.
	 * Responsibilities: Records only unequal outcomes.
	 */
	template<typename ExpectedType, typename ActualType>
	void ExpectEqual(
		const ExpectedType& InExpected, const ActualType& InActual, const char* const InMessage, const char* const InFile, const int InLine) noexcept
	{
		if (InExpected == InActual)
		{
			return;
		}

		RecordFailure(InMessage, InFile, InLine);
	}

	/**
	 * Motivation: Records a failed predicate with caller location for actionable diagnostics.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void ExpectTrue(const bool bInCondition, const char* const InMessage, const char* const InFile, const int InLine) noexcept
	{
		if (bInCondition)
		{
			return;
		}

		RecordFailure(InMessage, InFile, InLine);
	}

	/**
	 * Motivation: Lets the runner classify the test without exposing mutable failure state.
	 * Responsibilities: Return the stored value and touch nothing else.
	 */
	bool HasFailures() const noexcept { return FailureCount != 0; }

private:
	/**
	 * Motivation: Assertion helpers stay consistent.
	 * Responsibilities: Centralizes bounded failure reporting.
	 */
	void RecordFailure(const char* const InMessage, const char* const InFile, const int InLine) noexcept
	{
		++FailureCount;
		std::printf("[ASSERT] %s: %s (%s:%d)\n", Name, InMessage, InFile, InLine);
	}

	/** Motivation: Keeps diagnostics tied to the behavior contract selected at registration. */
	const char* Name;

	/** Motivation: Avoids dynamic failure collections while preserving aggregate pass/fail status. */
	int FailureCount{0};
};

/** Motivation: Gives the static registry one uniform, non-throwing test function shape. */
using FTestFunction = void (*)(FTestContext&) noexcept;

class FTestRegistration;

/**
 * Motivation: The registry head is valid before registrations.
 * Responsibilities: Uses function-local initialization.
 */
inline FTestRegistration*& GetTestRegistrationHead() noexcept
{
	/** Motivation: Avoids cross-translation-unit initialization order dependencies. */
	static FTestRegistration* Head = nullptr;
	return Head;
}

/**
 * Motivation: Adds one statically declared test to the allocation-free test registry.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FTestRegistration final
{
public:
	/**
	 * Motivation: Prepends one static test without heap allocation or external registration code.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FTestRegistration(const char* const InTestName, const FTestFunction InTestFunction) noexcept
		: Name(InTestName), Function(InTestFunction), Next(GetTestRegistrationHead())
	{
		GetTestRegistrationHead() = this;
	}

	/**
	 * Motivation: Gives runner diagnostics the behavior-oriented registration name.
	 * Responsibilities: Return the stored value and touch nothing else.
	 */
	const char* GetName() const noexcept { return Name; }

	/**
	 * Motivation: Gives the runner the test body without exposing registry mutation.
	 * Responsibilities: Return the stored value and touch nothing else.
	 */
	FTestFunction GetFunction() const noexcept { return Function; }

	/**
	 * Motivation: Lets the runner traverse the allocation-free intrusive registry.
	 * Responsibilities: Return the stored value and touch nothing else.
	 */
	FTestRegistration* GetNext() const noexcept { return Next; }

private:
	/** Motivation: Retains the static string used for pass/fail output. */
	const char* Name;

	/** Motivation: Retains the behavior body registered by the declaration macro. */
	FTestFunction Function;

	/** Motivation: Forms the intrusive list without a dynamic container. */
	FTestRegistration* Next;
};

/**
 * Motivation: Executes every statically registered test and returns one process-level result.
 * Responsibilities: Aggregate every registered result and return non-zero on any failure.
 */
inline int RunAllTests() noexcept
{
	int TestCount = 0;
	int FailedTestCount = 0;

	FTestRegistration* Registration = GetTestRegistrationHead();
	while (Registration != nullptr)
	{
		++TestCount;
		FTestContext Test(Registration->GetName());
		const FTestFunction TestFunction = Registration->GetFunction();
		TestFunction(Test);

		if (Test.HasFailures())
		{
			++FailedTestCount;
			std::printf("[FAIL] %s\n", Registration->GetName());
		}
		else
		{
			std::printf("[PASS] %s\n", Registration->GetName());
		}

		Registration = Registration->GetNext();
	}

	std::printf("[SUMMARY] %d tests, %d failures\n", TestCount, FailedTestCount);
	return FailedTestCount == 0 ? 0 : 1;
}

} // namespace MicroWorld::Tests

/** Declares, registers, and defines one behavior test without manual registry wiring. */
#define MW_TEST_CASE(Name)                                                            \
	static void Name(MicroWorld::Tests::FTestContext& Test) noexcept;                 \
	namespace                                                                         \
	{                                                                                 \
		const MicroWorld::Tests::FTestRegistration Name##_Registration{#Name, &Name}; \
	}                                                                                 \
	static void Name(MicroWorld::Tests::FTestContext& Test) noexcept

/** Preserves expected/actual evaluation before forwarding source diagnostics. */
#define MW_EXPECT_EQ(Test, ExpectedLocal, ActualLocal, Message) (Test).ExpectEqual((ExpectedLocal), (ActualLocal), (Message), __FILE__, __LINE__)

/** Preserves predicate evaluation before forwarding source diagnostics. */
#define MW_EXPECT_TRUE(Test, BoolLocal, Message) (Test).ExpectTrue((BoolLocal), (Message), __FILE__, __LINE__)
