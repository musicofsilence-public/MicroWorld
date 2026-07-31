#pragma once

#include <cstdint>

namespace MicroWorld::Core
{

/**
 * Motivation: Identifies the exact source-level MicroWorld package contract.
 * Responsibilities: Carry the Major, Minor, and Patch numbers that downstream probes compare against.
 * Example:
 *   FVersion Current = Version;
 */
struct FVersion
{
	/** Motivation: Changes when compatibility-breaking public behavior is released. */
	std::uint16_t Major;

	/** Motivation: Changes when backward-compatible public capability is added. */
	std::uint16_t Minor;

	/** Motivation: Changes when compatible fixes clarify the current contract. */
	std::uint16_t Patch;
};

/** Motivation: Lets downstream probes reject a package that does not match their API contract. */
inline constexpr FVersion Version{0, 4, 0};

} // namespace MicroWorld::Core
