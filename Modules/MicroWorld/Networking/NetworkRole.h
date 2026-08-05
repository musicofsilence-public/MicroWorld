#pragma once

#include <cstdint>

namespace MicroWorld::Networking
{

/**
 * Motivation: Selects the one session responsibility a Network instance owns.
 * Responsibilities: Distinguish a client with one server route from a server with a bounded remote-peer registry.
 * Example: FNetworkSystemInformation Information{ENetworkRole::Server};
 */
enum class ENetworkRole : std::uint8_t
{
	Client, ///< Motivation: Connects to and communicates with exactly one server.
	Server, ///< Motivation: Admits and communicates with up to FNetworkSystem::MaxPeers remote clients.
};

} // namespace MicroWorld::Networking
