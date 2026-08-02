#pragma once

#include <cstdint>
#include <limits>

namespace MicroWorld::Core
{

/**
 * Motivation: Lets a caller carry one multicast binding identity without exposing storage or
 *   extending the callable's lifetime.
 * Responsibilities: Pair a slot index with a generation and never mutate on its own.
 * Example:
 *   FDelegateHandle Handle;
 *   if (Handle.IsValid()) { Delegate.Remove(Handle); }
 */
struct FDelegateHandle final
{
	/** Motivation: Reserves the maximum index as an invalid sentinel independent of delegate capacity. */
	static constexpr std::uint16_t InvalidIndex = std::numeric_limits<std::uint16_t>::max();

	/** Motivation: Selects the fixed slot while preserving an explicit invalid sentinel. */
	std::uint16_t Index{InvalidIndex};

	/** Motivation: Distinguishes successive bindings that occupy the same slot. */
	std::uint32_t Generation{0};

	/**
	 * Motivation: Lets a caller reject a default or stale value before consulting its owning delegate.
	 * Responsibilities: Report true only when the index and generation together look like a live binding.
	 */
	constexpr bool IsValid() const noexcept { return Index != InvalidIndex && Generation != 0; }

	/**
	 * Motivation: Lets containers compare two handles by complete stable identity.
	 * Responsibilities: Return true only when both index and generation match.
	 */
	friend constexpr bool operator==(const FDelegateHandle Left, const FDelegateHandle Right) noexcept
	{
		return Left.Index == Right.Index && Left.Generation == Right.Generation;
	}

	/**
	 * Motivation: Lets a caller tell two handles apart by stable identity.
	 * Responsibilities: Return true whenever the slot or generation identity differs.
	 */
	friend constexpr bool operator!=(const FDelegateHandle Left, const FDelegateHandle Right) noexcept { return !(Left == Right); }
};

} // namespace MicroWorld::Core
