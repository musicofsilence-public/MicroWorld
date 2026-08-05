#pragma once

#include <cstdint>

namespace MicroWorld::Networking
{

/**
 * Motivation: Explains why a live peer became unavailable to event listeners.
 * Responsibilities: Distinguish an explicit protocol close from liveness expiry.
 * Example: OnPeerDisconnected.Broadcast(Peer, EDisconnectReason::Timeout);
 */
enum class EDisconnectReason : std::uint8_t
{
	Requested, ///< Motivation: Marks a local or remote explicit disconnect request.
	Timeout, ///< Motivation: Marks a peer whose heartbeat silence exceeded the configured deadline.
};

} // namespace MicroWorld::Networking
