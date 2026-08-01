#pragma once

#include <cstdint>

namespace MicroWorld::Messaging
{

/**
 * Motivation: Identifies messages and channels without retaining dynamically allocated name strings.
 * Responsibilities: Store the stable 32-bit hash associated with one null-terminated name.
 * Example:
 *   const FNameId ChannelNameId = "Telemetry";
 */
struct FNameId final
{
	/** Motivation: Provides the explicit unset name-id value. */
	std::uint32_t Value{0};

	/**
	 * Motivation: Lets values begin in the explicit unset state without requiring a sentinel conversion.
	 * Responsibilities: Initialize Value to zero.
	 */
	constexpr FNameId() noexcept = default;

	/**
	 * Motivation: Keeps readable string literals at Messaging call sites without forcing a hash helper at each use.
	 * Responsibilities: Intentionally convert implicitly so names such as "Telemetry" remain readable while MakeNameId
	 *   remains the single hashing implementation and only its compact result is stored.
	 */
	constexpr FNameId(const char* const InName) noexcept;

	/**
	 * Motivation: Lets protocol code restore an already-computed id without accepting accidental integer conversions.
	 * Responsibilities: Store InValue unchanged.
	 */
	explicit constexpr FNameId(const std::uint32_t InValue) noexcept : Value(InValue) {}

	/**
	 * Motivation: Lets callers compare message and channel identities directly.
	 * Responsibilities: Return true only when both stored hash values match.
	 */
	constexpr bool operator==(const FNameId InOther) const noexcept { return Value == InOther.Value; }

	/**
	 * Motivation: Lets callers reject a mismatched message or channel identity directly.
	 * Responsibilities: Return true only when the stored hash values differ.
	 */
	constexpr bool operator!=(const FNameId InOther) const noexcept { return Value != InOther.Value; }
};

/**
 * Motivation: Lets callers derive deterministic ids from readable message and channel names at compile time.
 * Responsibilities: Return the 32-bit FNV-1a hash of InName's bytes before its null terminator, or the FNV offset basis for null.
 * Example:
 *   const FNameId MessageNameId = MakeNameId("TemperatureUpdated");
 */
constexpr FNameId MakeNameId(const char* InName) noexcept
{
	/** Motivation: Standard FNV-1a 32-bit starting accumulator, so two boards agree on every id they exchange. */
	constexpr std::uint32_t OffsetBasis = 2166136261u;

	/** Motivation: Standard FNV-1a 32-bit multiplier, fixed for the same cross-board agreement. */
	constexpr std::uint32_t Prime = 16777619u;

	std::uint32_t NameId = OffsetBasis;
	if (InName == nullptr)
	{
		return FNameId{NameId};
	}

	while (*InName != '\0')
	{
		NameId ^= static_cast<std::uint8_t>(*InName);
		NameId *= Prime;
		++InName;
	}

	return FNameId{NameId};
}

constexpr FNameId::FNameId(const char* const InName) noexcept : Value(MakeNameId(InName).Value) {}

/**
 * Motivation: Reserves one explicit unset value even though a real name can hash to zero, albeit astronomically unlikely.
 * Responsibilities: Represent an absent name id only; Messaging treats zero as unset and cannot distinguish that rare collision.
 * Example:
 *   if (MessageNameId == InvalidNameId) { return; }
 */
inline constexpr FNameId InvalidNameId{};

} // namespace MicroWorld::Messaging
