#pragma once

#include <cstdint>

namespace MicroWorld::Engine
{

/**
 * Motivation: Names the store-owned lifecycle phase of one caller-supplied slot so lifecycle, destruction, retirement,
 *   and reuse decisions read as one value instead of several flags.
 * Responsibilities: Distinguish vacant, constructing, live, pending-destroy, destroying, and retired phases.
 * Example:
 *   if (Slot.State == EObjectSlotState::Live) { Resolve(); }
 */
enum class EObjectSlotState : std::uint8_t
{
	/** Motivation: Allows the slot to publish another generation when one remains. */
	Vacant,

	/** Motivation: Hides storage while a nothrow placement constructor is running. */
	Constructing,

	/** Motivation: Makes the constructed object resolvable and eligible for tracing. */
	Live,

	/** Motivation: Hides the object immediately until the destruction barrier reclaims it. */
	PendingDestroy,

	/** Motivation: Prevents lifecycle callbacks from recursively destroying the same slot. */
	Destroying,

	/** Motivation: Permanently prevents reuse after the generation space is exhausted. */
	Retired,
};

} // namespace MicroWorld::Engine
