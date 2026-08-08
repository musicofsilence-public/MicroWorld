#pragma once

#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Messaging/MessagingRoute.h>
#include <MicroWorld/Networking/NetworkRole.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Networking
{

/** Motivation: Supplies the default application protocol revision shared by freshly constructed Network instances. */
inline constexpr std::uint8_t DefaultNetworkProtocolVersion = 1;

/** Motivation: Supplies the default heartbeat cadence while preserving caller-timed Network work. */
inline constexpr Core::DurationMilliseconds DefaultNetworkHeartbeatIntervalMilliseconds = 1000;

/** Motivation: Supplies the default silence deadline used to retire a live session. */
inline constexpr Core::DurationMilliseconds DefaultNetworkPeerTimeoutMilliseconds = 5000;

/** Motivation: Preserves the general-purpose four-peer server capacity unless a composition owner deliberately narrows it. */
inline constexpr std::size_t DefaultMaximumAdmittedServerPeers = 4;

/**
 * Motivation: Collects the small fixed policy required by one Network system before it accepts session work.
 * Responsibilities: Carry role, protocol version, heartbeat cadence, and timeout without owning routes or devices.
 * Example: FNetworkSystemInformation Information{ENetworkRole::Client};
 */
struct FNetworkSystemInformation final
{
	/** Motivation: Selects client or server-only public operation policy. */
	ENetworkRole Role{ENetworkRole::Client};

	/** Motivation: Pins the compatible protocol schema revision for this session instance. */
	std::uint8_t ProtocolVersion{DefaultNetworkProtocolVersion};

	/** Motivation: Paces connect retries and best-effort heartbeats. */
	Core::DurationMilliseconds HeartbeatIntervalMilliseconds{DefaultNetworkHeartbeatIntervalMilliseconds};

	/** Motivation: Defines the maximum silent interval before a peer is invalidated. */
	Core::DurationMilliseconds PeerTimeoutMilliseconds{DefaultNetworkPeerTimeoutMilliseconds};

	/** Motivation: Lets Engine provide one client-owned initial server route that starts only on Network's first frame. */
	Messaging::FMessagingRoute InitialServerRoute{};

	/** Motivation: Narrows server admission without changing Network's fixed four-slot storage contract. */
	std::size_t MaximumAdmittedServerPeers{DefaultMaximumAdmittedServerPeers};
};

} // namespace MicroWorld::Networking
