#pragma once

#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Messaging/MessagingLinkId.h>

namespace MicroWorld::Messaging
{

/**
 * Motivation: Identifies the complete destination or inbound origin required when one Messaging system serves several devices and peers.
 * Responsibilities: Pair one registered link with its device-defined address without owning either resource.
 * Example: FMessagingRoute Route{LinkId, PeerAddress};
 */
struct FMessagingRoute final
{
	/** Motivation: Selects the registered device that transmits or supplied this message. */
	FMessagingLinkId LinkId{};

	/** Motivation: Selects the device-defined peer or default destination on LinkId. */
	Core::FDeviceAddress Address{};

	/**
	 * Motivation: Lets send paths reject a route that cannot name a live registration.
	 * Responsibilities: Report whether the route holds a valid link id.
	 */
	constexpr bool IsValid() const noexcept { return LinkId.IsValid(); }

	/**
	 * Motivation: Lets acknowledgement and retry state match complete peer destinations exactly.
	 * Responsibilities: Report whether link and address both match InOther.
	 */
	constexpr bool operator==(const FMessagingRoute& InOther) const noexcept { return LinkId == InOther.LinkId && Address == InOther.Address; }

	/**
	 * Motivation: Lets callers reject a route that differs by link or destination address.
	 * Responsibilities: Report whether its link or address differs from InOther.
	 */
	constexpr bool operator!=(const FMessagingRoute& InOther) const noexcept { return !(*this == InOther); }
};

} // namespace MicroWorld::Messaging
