#pragma once

#include "CoreAllocationCounters.h"
#include "TestSupport.h"

#include <MicroWorld/Core/Delegates/Delegate.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Core/TimerManager.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Tests
{

using MicroWorld::Core::CanAdvanceTimerGeneration;
using MicroWorld::Core::DurationMilliseconds;
using MicroWorld::Core::ETimerMode;
using MicroWorld::Core::ETimerResult;
using MicroWorld::Core::FTimerHandle;
using MicroWorld::Core::TDelegate;
using MicroWorld::Core::TimePointMilliseconds;
using MicroWorld::Core::TTimerManager;
using MicroWorld::Tests::GlobalAllocationCount;

/** Asserts a timer operation returned Success without discarding the result. */
#define MW_EXPECT_SUCCESS(TestContext, Result, Message) MW_EXPECT_EQ(TestContext, ETimerResult::Success, Result, Message)

/** Motivation: Inline callback storage shared by every timer test so capturing lambdas fit one fixed size. */
constexpr std::size_t TestInlineCallbackBytes = 64;

/** Motivation: Capacity large enough for ordering and mutation tests without masking capacity behavior. */
constexpr std::size_t TestTimerCapacity = 4;

/** Motivation: Timer period most scheduling tests use so their deadlines land on round Advance timestamps. */
constexpr DurationMilliseconds StandardTimerPeriod{100};

/** Motivation: Initial clock the zero-delay tests start at so a first Advance at the same timestamp is due. */
constexpr TimePointMilliseconds SaturatedTestInitialNow{1000};

using FTestManager = TTimerManager<TestTimerCapacity, TestInlineCallbackBytes>;
using FTestDelegate = TDelegate<void(), TestInlineCallbackBytes>;

/** Motivation: A valid-looking handle value used to prove failed Schedule calls clear their output. */
constexpr FTimerHandle CanaryHandle{0u, 1u};

/**
 * Motivation: Counts callback invocations for one timer without allocating.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FFireCounter final
{
	/** Motivation: Records one observed callback invocation. */
	std::uint32_t Count{0};
};

/**
 * Motivation: Binds a nothrow inline callback that increments the supplied counter when invoked.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
inline FTestDelegate MakeCounterCallback(FFireCounter& InCounter) noexcept
{
	FTestDelegate Delegate;
	(void)Delegate.Bind([&InCounter]() noexcept { ++InCounter.Count; });
	return Delegate;
}

/**
 * Motivation: Produces an out-of-range ETimerMode value at runtime.
 * Responsibilities: Routed through a function rather than a `const` initializer so the cast is not a constant
 *   expression: Clang's default `-Wenum-constexpr-conversion` rejects
 *   `static_cast<ETimerMode>(non-enumerator)` only in constant contexts, and this regression test
 *   deliberately targets the runtime rejection path.
 */
inline ETimerMode MakeOutOfRangeTimerMode() noexcept
{
	return static_cast<ETimerMode>(3);
}

/**
 * Motivation: Records the identity of each callback in stable dispatch order without allocating.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FDispatchOrderRecorder final
{
	/** Motivation: Bounds the recorded sequence so the test fixtures stay allocation-free and fixed-size. */
	static constexpr std::size_t MaximumEntries = 8;

	/** Motivation: Tracks the next write position so later reads observe insertion-order dispatch. */
	std::size_t Count{0};

	/** Motivation: Stores the caller-supplied identity of each fired callback. */
	int Identities[MaximumEntries]{0};

	/**
	 * Motivation: Appends one observed identity when space remains.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void Record(const int InIdentity) noexcept
	{
		if (Count < MaximumEntries)
		{
			Identities[Count] = InIdentity;
			++Count;
		}
	}
};

/**
 * Motivation: Binds a callback that records its identity in the shared recorder.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
inline FTestDelegate MakeOrderCallback(FDispatchOrderRecorder& InRecorder, const int InIdentity) noexcept
{
	FTestDelegate Delegate;
	(void)Delegate.Bind([&InRecorder, InIdentity]() noexcept { InRecorder.Record(InIdentity); });
	return Delegate;
}

/**
 * Motivation: Records the result of one attempted mutation performed from inside a callback.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FCapturedMutation final
{
	/** Motivation: Holds the result of the attempted in-callback operation. */
	ETimerResult Result{ETimerResult::Success};

	/** Motivation: Remembers whether the captured operation has executed. */
	bool bObserved{false};
};

} // namespace MicroWorld::Tests
