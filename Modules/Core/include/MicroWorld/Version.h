#pragma once

#include <cstdint>

namespace MicroWorld
{

/** Identifies the exact source-level MicroWorld package contract. */
struct FVersion
{
	/** Changes when compatibility-breaking public behavior is released. */
	std::uint16_t Major;

	/** Changes when backward-compatible public capability is added. */
	std::uint16_t Minor;

	/** Changes when compatible fixes clarify the current contract. */
	std::uint16_t Patch;
};

/** Lets downstream probes reject a package that does not match their API contract. */
inline constexpr FVersion Version{0, 2, 0};

} // namespace MicroWorld
