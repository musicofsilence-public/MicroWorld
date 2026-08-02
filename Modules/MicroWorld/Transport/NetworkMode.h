#pragma once

#include <cstdint>

namespace MicroWorld::Transport
{

/**
 * Motivation: Names the UE5-style role this host plays so session traffic it originates and accepts reads as one value.
 * Responsibilities: Distinguish standalone, client, listen server, and dedicated server roles.
 * Example:
 *   if (Host.GetMode() == ENetworkMode::ListenServer) { Admit(); }
 */
enum class ENetworkMode : std::uint8_t
{
	Standalone, ///< Motivation: Runs no device traffic; every send reports Unavailable.

	Client, ///< Motivation: Holds exactly one peer (the server) and sends Hello until admitted.

	ListenServer, ///< Motivation: Admits remote peers and additionally owns a directly dispatched local peer.

	DedicatedServer, ///< Motivation: Admits remote peers with no local peer of its own.
};

} // namespace MicroWorld::Transport
