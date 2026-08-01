#pragma once

#include <cstdint>

namespace MicroWorld::Core
{

/**
 * Motivation: Lets systems retain a subscriber's liveness without learning the owner's concrete type.
 * Responsibilities: Compare a stable generation counter with its captured generation and distinguish an absent owner from a dead one.
 * Example:
 *   if (Owner.IsLive()) { Deliver(); }
 */
struct FWeakOwner
{
	/** Motivation: Observes the owner's stable generation counter without retaining the owner itself. */
	const std::uint32_t* GenerationCounter{nullptr};

	/** Motivation: Records the generation observed when this token was captured. */
	std::uint32_t ExpectedGeneration{0};

	/** Motivation: Distinguishes an intentionally ownerless token from a token whose owner is already dead. */
	bool bHasOwner{false};

	/**
	 * Motivation: Lets subscribers avoid invoking callables whose owner no longer exists.
	 * Responsibilities: Return true for ownerless tokens and otherwise require a non-null matching generation counter.
	 */
	bool IsLive() const noexcept
	{
		if (!bHasOwner)
		{
			return true;
		}

		return GenerationCounter != nullptr && *GenerationCounter == ExpectedGeneration;
	}

	/**
	 * Motivation: Lets callers select subscriptions belonging to one specific owner.
	 * Responsibilities: Return false when either token is ownerless or counterless, and otherwise require matching counter addresses and
	 *   generations. Two tokens captured from already-destroyed owners both hold a null counter, so without the null check they would match each
	 *   other and a bulk removal could release a stranger's subscription.
	 */
	bool IsSameOwner(const FWeakOwner& InOther) const noexcept
	{
		return bHasOwner && InOther.bHasOwner && GenerationCounter != nullptr && GenerationCounter == InOther.GenerationCounter
			&& ExpectedGeneration == InOther.ExpectedGeneration;
	}
};

} // namespace MicroWorld::Core
