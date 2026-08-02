#pragma once

#include <cstdint>

namespace MicroWorld::Transport
{

/**
 * Motivation: Names the control message types a channel-0 payload may carry so the host routes each by intent.
 * Responsibilities: Distinguish the Hello, Welcome, Heartbeat, and Bye control shapes from application channels.
 * Example:
 *   if (Message.Type == EControlMessageType::Welcome) { Admit(Message.PeerIndex); }
 */
enum class EControlMessageType : std::uint8_t
{
	Hello = 1, ///< Motivation: Client-to-server greeting carrying the caller's protocol version.

	Welcome = 2, ///< Motivation: Server-to-client admission carrying the assigned peer index and generation.

	Heartbeat = 3, ///< Motivation: Keepalive exchanged in both directions on a configured interval.

	Bye = 4, ///< Motivation: Disconnect notice exchanged in both directions.
};

} // namespace MicroWorld::Transport
