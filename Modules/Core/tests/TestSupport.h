#pragma once

#include <cstdio>

namespace MicroWorld::Tests
{

/** Records assertion failures for one named test without dynamic storage. */
class FTestContext final
{
public:
	/** Associates every failure with one behavior-oriented test name. */
	explicit FTestContext(const char* const TestName) noexcept : Name(TestName) {}

	/** Records only unequal outcomes so successful assertions remain allocation-free. */
	template<typename ExpectedType, typename ActualType>
	void ExpectEqual(
		const ExpectedType& Expected, const ActualType& Actual, const char* const Message, const char* const File, const int Line) noexcept
	{
		if (Expected == Actual)
		{
			return;
		}

		RecordFailure(Message, File, Line);
	}

	/** Records a failed predicate with caller location for actionable diagnostics. */
	void ExpectTrue(const bool bCondition, const char* const Message, const char* const File, const int Line) noexcept
	{
		if (bCondition)
		{
			return;
		}

		RecordFailure(Message, File, Line);
	}

	/** Lets the runner classify the test without exposing mutable failure state. */
	bool HasFailures() const noexcept { return FailureCount != 0; }

private:
	/** Centralizes bounded failure reporting so assertion helpers stay consistent. */
	void RecordFailure(const char* const Message, const char* const File, const int Line) noexcept
	{
		++FailureCount;
		std::printf("[ASSERT] %s: %s (%s:%d)\n", Name, Message, File, Line);
	}

	/** Keeps diagnostics tied to the behavior contract selected at registration. */
	const char* Name;

	/** Avoids dynamic failure collections while preserving aggregate pass/fail status. */
	int FailureCount{0};
};

/** Gives the static registry one uniform, non-throwing test function shape. */
using FTestFunction = void (*)(FTestContext&) noexcept;

class FTestRegistration;

/** Uses function-local initialization so the registry head is valid before registrations. */
inline FTestRegistration*& GetTestRegistrationHead() noexcept
{
	/** Avoids cross-translation-unit initialization order dependencies. */
	static FTestRegistration* Head = nullptr;
	return Head;
}

/** Adds one statically declared test to the allocation-free test registry. */
class FTestRegistration final
{
public:
	/** Prepends one static test without heap allocation or external registration code. */
	FTestRegistration(const char* const TestName, const FTestFunction TestFunction) noexcept
		: Name(TestName), Function(TestFunction), Next(GetTestRegistrationHead())
	{
		GetTestRegistrationHead() = this;
	}

	/** Gives runner diagnostics the behavior-oriented registration name. */
	const char* GetName() const noexcept { return Name; }

	/** Gives the runner the test body without exposing registry mutation. */
	FTestFunction GetFunction() const noexcept { return Function; }

	/** Lets the runner traverse the allocation-free intrusive registry. */
	FTestRegistration* GetNext() const noexcept { return Next; }

private:
	/** Retains the static string used for pass/fail output. */
	const char* Name;

	/** Retains the behavior body registered by the declaration macro. */
	FTestFunction Function;

	/** Forms the intrusive list without a dynamic container. */
	FTestRegistration* Next;
};

/** Executes every statically registered test and returns one process-level result. */
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
