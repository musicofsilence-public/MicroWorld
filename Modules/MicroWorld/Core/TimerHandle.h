#pragma once

#include <cstdint>
#include <limits>

namespace MicroWorld::Core
{

/**
 * Motivation: Lets a caller carry one live timer identity without exposing storage or extending the callback's lifetime.
 * Responsibilities: Pair a slot index with a generation and never mutate on its own; a handle is local to the
 *   manager that issued it and must not be carried between managers.
 * Example:
 *   FTimerHandle Handle;
 *   if (Handle.IsValid()) { Manager.Cancel(Handle); }
 */
struct FTimerHandle final
{
	/** Motivation: Reserves the maximum index as the invalid sentinel independent of manager capacity. */
	static constexpr std::uint16_t InvalidIndex = std::numeric_limits<std::uint16_t>::max();

	/** Motivation: Selects the fixed slot while preserving an explicit invalid sentinel. */
	std::uint16_t Index{InvalidIndex};

	/** Motivation: Distinguishes successive schedules that occupy the same slot. */
	std::uint32_t Generation{0};

	/**
	 * Motivation: Lets a caller reject a default or stale value before consulting its owning manager.
	 * Responsibilities: Report true only when the index and generation together look like a live timer.
	 */
	constexpr bool IsValid() const noexcept { return Index != InvalidIndex && Generation != 0; }

	/**
	 * Motivation: Lets containers compare two handles by complete stable identity.
	 * Responsibilities: Return true only when both index and generation match.
	 */
	friend constexpr bool operator==(const FTimerHandle InLeft, const FTimerHandle InRight) noexcept
	{
		return InLeft.Index == InRight.Index && InLeft.Generation == InRight.Generation;
	}

	/**
	 * Motivation: Lets a caller tell two handles apart by stable identity.
	 * Responsibilities: Return true whenever the slot or generation identity differs.
	 */
	friend constexpr bool operator!=(const FTimerHandle InLeft, const FTimerHandle InRight) noexcept { return !(InLeft == InRight); }
};

/**
 * Motivation: Confirms that one more live generation can be published without wrapping.
 * Responsibilities: Report whether the generation is below its maximum, so a manager can retire a slot before
 *   wrapping would make an old handle valid again.
 */
constexpr bool CanAdvanceTimerGeneration(const std::uint32_t InCurrentGeneration) noexcept
{
	return InCurrentGeneration < std::numeric_limits<std::uint32_t>::max();
}

} // namespace MicroWorld::Core
