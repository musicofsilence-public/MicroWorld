#include "TestSupport.h"

#include <MicroWorld/Core/WeakOwner.h>

#include <cstdint>

namespace
{

using MicroWorld::Core::FWeakOwner;

/**
 * Motivation: Keeps ownerless subscriptions valid for callables that are not tied to an object's lifetime.
 * Responsibilities: Confirm the default token reports live without requiring a generation counter.
 */
MW_TEST_CASE(DefaultConstructedWeakOwnerIsLive)
{
	// Arrange
	const FWeakOwner OwnerlessToken{};

	// Act
	const bool bIsLive = OwnerlessToken.IsLive();

	// Assert
	MW_EXPECT_TRUE(Test, bIsLive, "A default weak owner token should remain live");
}

/**
 * Motivation: Allows a subscription to remain callable while its captured owner still occupies the same generation.
 * Responsibilities: Confirm a matching counter and expected generation report live.
 */
MW_TEST_CASE(WeakOwnerWithMatchingGenerationIsLive)
{
	// Arrange
	/** Motivation: Represents the generation assigned to a still-live owner. */
	const std::uint32_t LiveOwnerGeneration{17};
	std::uint32_t OwnerGenerationCounter = LiveOwnerGeneration;
	/** Motivation: Marks this token as explicitly bound to an owner. */
	const bool bHasOwner{true};
	const FWeakOwner OwnerToken{&OwnerGenerationCounter, LiveOwnerGeneration, bHasOwner};

	// Act
	const bool bIsLive = OwnerToken.IsLive();

	// Assert
	MW_EXPECT_TRUE(Test, bIsLive, "A matching owner generation should report live");
}

/**
 * Motivation: Prevents a subscription from calling a callable after its owner has released and reused its generation slot.
 * Responsibilities: Confirm incrementing the observed generation makes the original token report not live.
 */
MW_TEST_CASE(WeakOwnerBecomesDeadAfterGenerationChanges)
{
	// Arrange
	/** Motivation: Represents the generation captured while the owner was live. */
	const std::uint32_t CapturedOwnerGeneration{23};
	/** Motivation: Advances the counter to represent the owner's destruction. */
	const std::uint32_t GenerationIncrement{1};
	std::uint32_t OwnerGenerationCounter = CapturedOwnerGeneration;
	/** Motivation: Marks this token as explicitly bound to an owner. */
	const bool bHasOwner{true};
	const FWeakOwner OwnerToken{&OwnerGenerationCounter, CapturedOwnerGeneration, bHasOwner};

	// Act
	OwnerGenerationCounter += GenerationIncrement;
	const bool bIsLive = OwnerToken.IsLive();

	// Assert
	MW_EXPECT_TRUE(Test, !bIsLive, "A changed owner generation should report not live");
}

/**
 * Motivation: Stops a stale token from being mistaken for an intentional ownerless subscription.
 * Responsibilities: Confirm a token that has an owner but no counter reports not live.
 */
MW_TEST_CASE(WeakOwnerWithOwnerAndNullCounterIsNotLive)
{
	// Arrange
	/** Motivation: Distinguishes the stale token from an intentionally ownerless token. */
	const bool bHasOwner{true};
	/** Motivation: Represents the generation that was expected before the owner's counter became unavailable. */
	const std::uint32_t ExpectedDeadOwnerGeneration{31};
	const FWeakOwner DeadOwnerToken{nullptr, ExpectedDeadOwnerGeneration, bHasOwner};

	// Act
	const bool bIsLive = DeadOwnerToken.IsLive();

	// Assert
	MW_EXPECT_TRUE(Test, !bIsLive, "A token with an owner and no counter should report not live");
}

/**
 * Motivation: Prevents an owner token from becoming live when it was captured with an incorrect generation.
 * Responsibilities: Confirm a counter that never matched the captured generation reports not live.
 */
MW_TEST_CASE(WeakOwnerWithNeverMatchingGenerationIsNotLive)
{
	// Arrange
	/** Motivation: Represents the generation currently held by the owner storage. */
	const std::uint32_t ActualOwnerGeneration{37};
	/** Motivation: Represents a generation that was never held by the owner storage. */
	const std::uint32_t UnmatchedExpectedGeneration{41};
	const std::uint32_t OwnerGenerationCounter = ActualOwnerGeneration;
	/** Motivation: Marks this token as explicitly bound to an owner. */
	const bool bHasOwner{true};
	const FWeakOwner OwnerToken{&OwnerGenerationCounter, UnmatchedExpectedGeneration, bHasOwner};

	// Act
	const bool bIsLive = OwnerToken.IsLive();

	// Assert
	MW_EXPECT_TRUE(Test, !bIsLive, "A generation that never matched should report not live");
}

/**
 * Motivation: Ensures bulk removal cannot treat an ownerless selector as an object-bound subscription.
 * Responsibilities: Confirm comparison fails when the left token is ownerless.
 */
MW_TEST_CASE(WeakOwnerComparisonIsFalseWhenLeftTokenIsOwnerless)
{
	// Arrange
	const FWeakOwner OwnerlessToken{};
	/** Motivation: Represents the generation assigned to the compared owner. */
	const std::uint32_t ComparedOwnerGeneration{43};
	const std::uint32_t OwnerGenerationCounter = ComparedOwnerGeneration;
	/** Motivation: Marks the compared token as explicitly bound to an owner. */
	const bool bComparedTokenHasOwner{true};
	const FWeakOwner OwnerToken{&OwnerGenerationCounter, ComparedOwnerGeneration, bComparedTokenHasOwner};

	// Act
	const bool bIsSameOwner = OwnerlessToken.IsSameOwner(OwnerToken);

	// Assert
	MW_EXPECT_TRUE(Test, !bIsSameOwner, "An ownerless left token should not match an owner");
}

/**
 * Motivation: Ensures bulk removal cannot treat an ownerless selector as an object-bound subscription.
 * Responsibilities: Confirm comparison fails when the right token is ownerless.
 */
MW_TEST_CASE(WeakOwnerComparisonIsFalseWhenRightTokenIsOwnerless)
{
	// Arrange
	/** Motivation: Represents the generation assigned to the compared owner. */
	const std::uint32_t ComparedOwnerGeneration{47};
	const std::uint32_t OwnerGenerationCounter = ComparedOwnerGeneration;
	/** Motivation: Marks the compared token as explicitly bound to an owner. */
	const bool bComparedTokenHasOwner{true};
	const FWeakOwner OwnerToken{&OwnerGenerationCounter, ComparedOwnerGeneration, bComparedTokenHasOwner};
	const FWeakOwner OwnerlessToken{};

	// Act
	const bool bIsSameOwner = OwnerToken.IsSameOwner(OwnerlessToken);

	// Assert
	MW_EXPECT_TRUE(Test, !bIsSameOwner, "An ownerless right token should not match an owner");
}

/**
 * Motivation: Keeps a default ownerless selector from matching every ownerless subscription.
 * Responsibilities: Confirm two default tokens do not report the same owner.
 */
MW_TEST_CASE(DefaultConstructedWeakOwnersAreNotTheSameOwner)
{
	// Arrange
	const FWeakOwner FirstOwnerlessToken{};
	const FWeakOwner SecondOwnerlessToken{};

	// Act
	const bool bIsSameOwner = FirstOwnerlessToken.IsSameOwner(SecondOwnerlessToken);

	// Assert
	MW_EXPECT_TRUE(Test, !bIsSameOwner, "Two ownerless tokens should not match");
}

/**
 * Motivation: Keeps two separately destroyed owners distinct, since both carry a null counter and a bulk removal
 *   selecting one must never release the other's subscriptions.
 * Responsibilities: Confirm two counterless owner-bound tokens do not report the same owner even when their expected
 *   generations agree.
 */
MW_TEST_CASE(CounterlessWeakOwnersAreNotTheSameOwner)
{
	// Arrange
	/** Motivation: Represents one generation value that two separately destroyed owners happen to share. */
	const std::uint32_t SharedDeadOwnerGeneration{71};
	/** Motivation: Marks both tokens as explicitly bound to an owner that is already gone. */
	const bool bBothTokensHaveOwner{true};
	const FWeakOwner FirstDeadOwnerToken{nullptr, SharedDeadOwnerGeneration, bBothTokensHaveOwner};
	const FWeakOwner SecondDeadOwnerToken{nullptr, SharedDeadOwnerGeneration, bBothTokensHaveOwner};

	// Act
	const bool bIsSameOwner = FirstDeadOwnerToken.IsSameOwner(SecondDeadOwnerToken);

	// Assert
	MW_EXPECT_TRUE(Test, !bIsSameOwner, "Two counterless owner tokens should not match");
}

/**
 * Motivation: Lets bulk removal identify all subscriptions captured from one owner generation.
 * Responsibilities: Confirm tokens over the same counter and generation report the same owner.
 */
MW_TEST_CASE(WeakOwnersOverSameCounterAndGenerationAreTheSameOwner)
{
	// Arrange
	/** Motivation: Represents the generation shared by both tokens for one owner. */
	const std::uint32_t SharedOwnerGeneration{53};
	const std::uint32_t OwnerGenerationCounter = SharedOwnerGeneration;
	/** Motivation: Marks both tokens as explicitly bound to an owner. */
	const bool bBothTokensHaveOwner{true};
	const FWeakOwner FirstOwnerToken{&OwnerGenerationCounter, SharedOwnerGeneration, bBothTokensHaveOwner};
	const FWeakOwner SecondOwnerToken{&OwnerGenerationCounter, SharedOwnerGeneration, bBothTokensHaveOwner};

	// Act
	const bool bIsSameOwner = FirstOwnerToken.IsSameOwner(SecondOwnerToken);

	// Assert
	MW_EXPECT_TRUE(Test, bIsSameOwner, "Tokens over the same counter and generation should match");
}

/**
 * Motivation: Prevents a reused owner slot from being mistaken for the previous owner of that slot.
 * Responsibilities: Confirm tokens over one counter but distinct generations do not report the same owner.
 */
MW_TEST_CASE(WeakOwnersOverSameCounterAndDifferentGenerationsAreNotTheSameOwner)
{
	// Arrange
	/** Motivation: Represents the generation captured by the first token. */
	const std::uint32_t FirstOwnerGeneration{59};
	/** Motivation: Represents a later generation captured by the second token. */
	const std::uint32_t SecondOwnerGeneration{61};
	const std::uint32_t OwnerGenerationCounter = FirstOwnerGeneration;
	/** Motivation: Marks both tokens as explicitly bound to an owner. */
	const bool bBothTokensHaveOwner{true};
	const FWeakOwner FirstOwnerToken{&OwnerGenerationCounter, FirstOwnerGeneration, bBothTokensHaveOwner};
	const FWeakOwner SecondOwnerToken{&OwnerGenerationCounter, SecondOwnerGeneration, bBothTokensHaveOwner};

	// Act
	const bool bIsSameOwner = FirstOwnerToken.IsSameOwner(SecondOwnerToken);

	// Assert
	MW_EXPECT_TRUE(Test, !bIsSameOwner, "Tokens with different expected generations should not match");
}

/**
 * Motivation: Keeps separate owner slots distinct even when both presently hold the same generation value.
 * Responsibilities: Confirm tokens over different counters do not report the same owner.
 */
MW_TEST_CASE(WeakOwnersOverDifferentCountersAndSameGenerationAreNotTheSameOwner)
{
	// Arrange
	/** Motivation: Represents the matching generation held by both separate owner slots. */
	const std::uint32_t SharedGenerationValue{67};
	const std::uint32_t FirstOwnerGenerationCounter = SharedGenerationValue;
	const std::uint32_t SecondOwnerGenerationCounter = SharedGenerationValue;
	/** Motivation: Marks both tokens as explicitly bound to an owner. */
	const bool bBothTokensHaveOwner{true};
	const FWeakOwner FirstOwnerToken{&FirstOwnerGenerationCounter, SharedGenerationValue, bBothTokensHaveOwner};
	const FWeakOwner SecondOwnerToken{&SecondOwnerGenerationCounter, SharedGenerationValue, bBothTokensHaveOwner};

	// Act
	const bool bIsSameOwner = FirstOwnerToken.IsSameOwner(SecondOwnerToken);

	// Assert
	MW_EXPECT_TRUE(Test, !bIsSameOwner, "Tokens over different counters should not match");
}

} // namespace
