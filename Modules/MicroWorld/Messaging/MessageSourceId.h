#pragma once

#include <cstdint>

namespace MicroWorld::Messaging
{

/**
 * Motivation: Carries higher-layer validated sender context through synchronous local delivery without serializing identity into application
 * payloads.
 * Responsibilities: Hold one opaque 64-bit source value and reserve zero for an absent source.
 * Example: Message.SetSourceId(FMessageSourceId{PeerValue});
 */
struct FMessageSourceId final
{
	/** Motivation: Distinguishes messages without a higher-layer validated source. */
	static constexpr std::uint64_t InvalidValue = 0;

	/** Motivation: Retains the opaque source value supplied by a validating higher layer. */
	std::uint64_t Value{InvalidValue};

	/**
	 * Motivation: Lets consumers distinguish local or unvalidated messages from validated local delivery.
	 * Responsibilities: Report whether this source id differs from the reserved invalid value.
	 */
	constexpr bool IsValid() const noexcept { return Value != InvalidValue; }
};

} // namespace MicroWorld::Messaging
