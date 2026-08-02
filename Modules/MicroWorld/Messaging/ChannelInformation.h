#pragma once

#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Messaging/NameId.h>

namespace MicroWorld::Messaging
{

/**
 * Motivation: Supplies the fixed configuration required to create one named Messaging channel.
 * Responsibilities: Describe identity, reliability, optional transport device, and destination address without owning them.
 * Example:
 *   FChannelInformation Information{ "Telemetry", false, &Device, Address };
 */
struct FChannelInformation
{
	/** Motivation: Identifies this channel independently of its device or destination, with an unset name invalid for creation. */
	FNameId ChannelNameId{};

	/** Motivation: Selects resend-until-ack behavior; false leaves the channel best-effort and local delivery still occurs. */
	bool bIsReliable{false};

	/** Motivation: Reaches the optional non-owning device used for remote delivery; null is the normal local-only channel shape. */
	Core::ITransportDevice* TransportDevice{nullptr};

	/** Motivation: Selects the device-defined destination route; zero active bytes select its default route, and multiple channels may share one
	 * device while using different addresses. */
	Core::FDeviceAddress Address{};
};

} // namespace MicroWorld::Messaging
