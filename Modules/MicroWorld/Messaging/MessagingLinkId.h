#pragma once

#include <cstdint>

namespace MicroWorld::Messaging
{

/**
 * Motivation: Names one Messaging-owned transport-device registration without exposing the device pointer to higher layers.
 * Responsibilities: Hold one stable fixed link slot and distinguish the explicit invalid value.
 * Example: FMessagingLinkId LinkId; Messaging.RegisterLink(Device, LinkId);
 */
struct FMessagingLinkId final
{
	/** Motivation: Reserves one slot value that never identifies a registered device. */
	static constexpr std::uint8_t InvalidIndex = 0xFFu;

	/** Motivation: Identifies the stable Messaging link slot, or InvalidIndex before successful registration. */
	std::uint8_t Index{InvalidIndex};

	/**
	 * Motivation: Lets callers reject an unregistered route without knowing Messaging storage.
	 * Responsibilities: Report whether this id names a registered Messaging link.
	 */
	constexpr bool IsValid() const noexcept { return Index != InvalidIndex; }

	/**
	 * Motivation: Lets fixed route state compare opaque registrations directly.
	 * Responsibilities: Report whether both ids select the same fixed link slot.
	 */
	constexpr bool operator==(const FMessagingLinkId InOther) const noexcept { return Index == InOther.Index; }

	/**
	 * Motivation: Lets fixed route state reject a different registration directly.
	 * Responsibilities: Report whether both ids select different fixed link slots.
	 */
	constexpr bool operator!=(const FMessagingLinkId InOther) const noexcept { return !(*this == InOther); }
};

} // namespace MicroWorld::Messaging
