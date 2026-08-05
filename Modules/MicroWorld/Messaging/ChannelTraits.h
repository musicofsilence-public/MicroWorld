#pragma once

namespace MicroWorld::Messaging
{

/**
 * Motivation: Gives higher layers the narrow channel facts needed to select wire policy without exposing channel storage or routes.
 * Responsibilities: Report reliability and whether legacy creation supplied a default remote route.
 * Example: FChannelTraits Traits; Messaging.GetChannelTraits("Gameplay", Traits);
 */
struct FChannelTraits final
{
	/** Motivation: Reports whether remote sends on this channel retain frames until acknowledged. */
	bool bIsReliable{false};

	/** Motivation: Reports whether SendMessageToChannel also attempts the normalized legacy default route. */
	bool bHasDefaultRoute{false};
};

} // namespace MicroWorld::Messaging
