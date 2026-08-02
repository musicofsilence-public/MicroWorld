#pragma once

#include <cstdint>

namespace MicroWorld::Transport
{

/**
 * Motivation: Names the observable session state so a client can track admission and a server can report readiness.
 * Responsibilities: Distinguish idle, connecting, connected, and listening states.
 * Example:
 *   if (Host.GetState() == ETransportHostState::Connected) { Send(); }
 */
enum class ETransportHostState : std::uint8_t
{
	Idle, ///< Motivation: Marks that no session is in progress (not started, standalone, or stopped).

	Connecting, ///< Motivation: Marks that a client has sent Hello and is awaiting Welcome.

	Connected, ///< Motivation: Marks that a client has been admitted and heartbeats are flowing.

	Listening, ///< Motivation: Marks that a server is started and accepting Hello up to its peer capacity.
};

} // namespace MicroWorld::Transport
