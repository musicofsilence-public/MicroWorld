#pragma once

#include <cstdint>

namespace MicroWorld::Networking
{

/**
 * Motivation: Exposes the client connection lifecycle without leaking routes or transport state.
 * Responsibilities: Distinguish no session, an outstanding connection attempt, and an admitted server session.
 * Example: if (Network.GetConnectionState() == EConnectionState::Connected) { Send(); }
 */
enum class EConnectionState : std::uint8_t
{
	Disconnected, ///< Motivation: Marks that no client session is active.
	Connecting, ///< Motivation: Marks that the latest connect attempt awaits admission.
	Connected, ///< Motivation: Marks that a live server route and assigned peer id are available.
};

} // namespace MicroWorld::Networking
