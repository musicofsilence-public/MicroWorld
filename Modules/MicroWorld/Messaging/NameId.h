#pragma once

#include <cstdint>

namespace MicroWorld::Messaging
{

/**
 * Motivation: Identifies messages and channels without retaining dynamically allocated name strings.
 * Responsibilities: Store the stable 32-bit hash associated with one null-terminated name.
 * Example:
 *   const FNameId ChannelNameId = MakeNameId("Telemetry");
 */
using FNameId = std::uint32_t;

/**
 * Motivation: Lets callers derive deterministic ids from readable message and channel names at compile time.
 * Responsibilities: Return the 32-bit FNV-1a hash of InName's bytes before its null terminator, or the FNV offset basis for null.
 * Example:
 *   const FNameId MessageNameId = MakeNameId("TemperatureUpdated");
 */
constexpr FNameId MakeNameId(const char* InName) noexcept
{
	/** Motivation: Standard FNV-1a 32-bit starting accumulator, so two boards agree on every id they exchange. */
	constexpr FNameId OffsetBasis = 2166136261u;

	/** Motivation: Standard FNV-1a 32-bit multiplier, fixed for the same cross-board agreement. */
	constexpr FNameId Prime = 16777619u;

	FNameId NameId = OffsetBasis;
	if (InName == nullptr)
	{
		return NameId;
	}

	while (*InName != '\0')
	{
		NameId ^= static_cast<std::uint8_t>(*InName);
		NameId *= Prime;
		++InName;
	}

	return NameId;
}

/**
 * Motivation: Reserves one explicit unset value even though a real name can hash to zero, albeit astronomically unlikely.
 * Responsibilities: Represent an absent name id only; Messaging treats zero as unset and cannot distinguish that rare collision.
 * Example:
 *   if (MessageNameId == InvalidNameId) { return; }
 */
inline constexpr FNameId InvalidNameId = 0;

} // namespace MicroWorld::Messaging
