#pragma once

#include <cstdint>

namespace MicroWorld::Networking
{

/**
 * Motivation: Explains why a server refused a connection request without adding authentication concepts.
 * Responsibilities: Distinguish protocol incompatibility from bounded peer-registry capacity.
 * Example: HandleRejected(EConnectionRejectReason::Full);
 */
enum class EConnectionRejectReason : std::uint8_t
{
	ProtocolMismatch, ///< Motivation: Marks a client whose declared protocol version differs from the server's configured version.
	Full, ///< Motivation: Marks a server whose fixed peer registry has no reusable slot.
};

} // namespace MicroWorld::Networking
