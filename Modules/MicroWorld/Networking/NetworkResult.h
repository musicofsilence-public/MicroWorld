#pragma once

#include <cstdint>

namespace MicroWorld::Networking
{

/**
 * Motivation: Gives Network operations a narrow outcome vocabulary independent of device-level results.
 * Responsibilities: Report accepted work, policy rejection, bounded exhaustion, and fan-out partial failure.
 * Example: if (Network.SendTo(Peer, Channel, Message) == ENetworkResult::Success) { Continue(); }
 */
enum class ENetworkResult : std::uint8_t
{
	Success, ///< Motivation: Reports that the requested Network operation completed.
	Partial, ///< Motivation: Reports that a broadcast reached at least one eligible peer but another send failed.
	NotConnected, ///< Motivation: Reports that no live session satisfies the requested operation.
	WrongRole, ///< Motivation: Reports that the operation belongs to the opposite Network role.
	Invalid, ///< Motivation: Reports malformed input, stale peer identity, or an invalid application channel.
	Full, ///< Motivation: Reports fixed Messaging or peer capacity exhaustion.
	Exhausted, ///< Motivation: Reports a generation or attempt counter that cannot safely advance.
};

} // namespace MicroWorld::Networking
