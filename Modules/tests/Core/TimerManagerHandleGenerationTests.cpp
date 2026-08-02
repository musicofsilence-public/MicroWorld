#include "TestSupport.h"
#include "TimerManagerTestHelpers.h"

#include <MicroWorld/Core/TimerManager.h>

#include <cstdint>
#include <limits>

namespace
{

using namespace ::MicroWorld::Tests;

// ---------------------------------------------------------------------------
// Category 4: Handles and generation safety
// ---------------------------------------------------------------------------

/**
 * Motivation: Cancel with default, sentinel, capacity-boundary, and zero-generation handles.
 * Responsibilities: Each invalid handle returns InvalidHandle.
 */
MW_TEST_CASE(EngineTimerInvalidHandleIndicesRejected)
{
	// Arrange
	FTestManager Manager{0};

	const FTimerHandle DefaultHandle{};
	const FTimerHandle SentinelHandle{FTimerHandle::InvalidIndex, 1u};
	const FTimerHandle IndexAtCapacity{static_cast<std::uint16_t>(TestTimerCapacity), 1u};
	const FTimerHandle ZeroGeneration{0u, 0u};

	// Act
	// Assert
	MW_EXPECT_EQ(Test, ETimerResult::InvalidHandle, Manager.Cancel(DefaultHandle), "A default handle must be rejected as InvalidHandle");
	MW_EXPECT_EQ(Test, ETimerResult::InvalidHandle, Manager.Cancel(SentinelHandle), "A sentinel-index handle must be rejected as InvalidHandle");
	MW_EXPECT_EQ(
		Test, ETimerResult::InvalidHandle, Manager.Cancel(IndexAtCapacity), "A handle at Index == Capacity must be rejected as InvalidHandle");
	MW_EXPECT_EQ(
		Test, ETimerResult::InvalidHandle, Manager.Cancel(ZeroGeneration), "A handle with Generation == 0 must be rejected as InvalidHandle");
}

/**
 * Motivation: Cancel an active timer, then attempt to cancel the same-slot handle with the retired generation and
 *   a mismatched generation.
 * Responsibilities: The canceled handle and the generation-mismatched handle both return StaleHandle.
 */
MW_TEST_CASE(EngineTimerStaleAndGenerationMismatchedHandlesRejected)
{
	// Arrange
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle Handle{};

	// Act
	// Assert
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeCounterCallback(Counter), StandardTimerPeriod, ETimerMode::Looping, Handle), "Schedule should succeed");
	const std::uint16_t SlotIndex = Handle.Index;
	const std::uint32_t PublishedGeneration = Handle.Generation;

	MW_EXPECT_SUCCESS(Test, Manager.Cancel(Handle), "The first cancellation should succeed");
	const FTimerHandle CanceledHandle{SlotIndex, PublishedGeneration};
	const FTimerHandle MismatchedHandle{SlotIndex, PublishedGeneration + 1u};

	// Act
	// Assert
	MW_EXPECT_EQ(Test, ETimerResult::StaleHandle, Manager.Cancel(CanceledHandle), "A canceled handle must return StaleHandle");
	MW_EXPECT_EQ(Test, ETimerResult::StaleHandle, Manager.Cancel(MismatchedHandle), "A generation-mismatched handle must return StaleHandle");
}

/**
 * Motivation: Schedule and cancel a one-shot timer, then schedule a second timer that reuses the freed slot.
 * Responsibilities: Slot reuse reuses the same index but publishes a different generation than the retired handle.
 */
MW_TEST_CASE(EngineTimerSlotReusePublishesDifferentGeneration)
{
	// Arrange
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle FirstHandle{};

	// Act
	// Assert
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeCounterCallback(Counter), StandardTimerPeriod, ETimerMode::OneShot, FirstHandle), "First schedule should succeed");
	MW_EXPECT_SUCCESS(Test, Manager.Cancel(FirstHandle), "Canceling the first timer should succeed");

	FTimerHandle SecondHandle{};
	MW_EXPECT_SUCCESS(
		Test,
		Manager.Schedule(MakeCounterCallback(Counter), StandardTimerPeriod, ETimerMode::OneShot, SecondHandle),
		"Second schedule should succeed");

	// Assert
	MW_EXPECT_TRUE(Test, FirstHandle.Index == SecondHandle.Index, "A freed slot should be reused first");
	MW_EXPECT_TRUE(Test, FirstHandle.Generation != SecondHandle.Generation, "Reused slot must publish a different generation");
}

/**
 * Motivation: Cancel a timer, schedule a replacement into the freed slot, then attempt to cancel the replacement
 *   with the retired handle.
 * Responsibilities: The stale handle cannot cancel the replacement and leaves occupancy unchanged; the live replacement
 *   handle still cancels.
 */
MW_TEST_CASE(EngineTimerStaleHandleCannotAffectReplacement)
{
	// Arrange
	FFireCounter Counter;
	FTestManager Manager{0};
	FTimerHandle FirstHandle{};

	// Act
	// Assert
	MW_EXPECT_SUCCESS(
		Test, Manager.Schedule(MakeCounterCallback(Counter), StandardTimerPeriod, ETimerMode::Looping, FirstHandle), "First schedule should succeed");
	MW_EXPECT_SUCCESS(Test, Manager.Cancel(FirstHandle), "Canceling the first timer should succeed");

	FTimerHandle SecondHandle{};
	MW_EXPECT_SUCCESS(
		Test,
		Manager.Schedule(MakeCounterCallback(Counter), StandardTimerPeriod, ETimerMode::Looping, SecondHandle),
		"Replacement schedule should succeed");
	MW_EXPECT_EQ(Test, 1u, Manager.TimerCount(), "The replacement should occupy one slot");

	// Act
	const ETimerResult StaleCancel = Manager.Cancel(FirstHandle);

	// Assert
	MW_EXPECT_EQ(Test, ETimerResult::StaleHandle, StaleCancel, "The retired handle must not cancel the replacement");
	MW_EXPECT_EQ(Test, 1u, Manager.TimerCount(), "A stale cancel must not change occupancy");

	// Act
	const ETimerResult LiveCancel = Manager.Cancel(SecondHandle);

	// Assert
	MW_EXPECT_SUCCESS(Test, LiveCancel, "The replacement handle should still cancel successfully");
	MW_EXPECT_EQ(Test, 0u, Manager.TimerCount(), "The live cancel should release the slot");
}

/**
 * Motivation: Evaluate the generation helper at zero, one, the last finite value, and the type maximum.
 * Responsibilities: The helper accepts every earlier value and refuses to advance at the type maximum.
 */
MW_TEST_CASE(EngineTimerGenerationHelperRefusesWrap)
{
	// Act
	constexpr bool AtZero = CanAdvanceTimerGeneration(0u);
	constexpr bool AtFirst = CanAdvanceTimerGeneration(1u);
	constexpr bool BeforeWrap = CanAdvanceTimerGeneration(std::numeric_limits<std::uint32_t>::max() - 1u);
	constexpr bool AtWrap = CanAdvanceTimerGeneration(std::numeric_limits<std::uint32_t>::max());

	// Assert
	MW_EXPECT_TRUE(Test, AtZero, "Generation zero must be advanceable");
	MW_EXPECT_TRUE(Test, AtFirst, "Generation one must be advanceable");
	MW_EXPECT_TRUE(Test, BeforeWrap, "The last finite generation must be advanceable");
	MW_EXPECT_TRUE(Test, !AtWrap, "The maximum generation must refuse to advance and trigger retirement");
}

} // namespace
