#pragma once

#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/Time.h>

#include <cstdint>

namespace MicroWorld::Transport
{

/** Motivation: Supplies the default heartbeat cadence when a caller does not override FTransportHostConfig. */
inline constexpr Core::DurationMilliseconds DefaultHeartbeatIntervalMilliseconds = 1000;

/** Motivation: Supplies the default peer eviction window when a caller does not override FTransportHostConfig. */
inline constexpr Core::DurationMilliseconds DefaultPeerTimeoutMilliseconds = 5000;

/** Motivation: Supplies the default protocol version advertised in Hello/Welcome when a caller does not override it. */
inline constexpr std::uint8_t DefaultProtocolVersion = 1;

/**
 * Motivation: Bundles the session timing and identity a host needs so it can be supplied once before Start.
 * Responsibilities: Carry the heartbeat interval, peer timeout, server address, and protocol version as one value.
 * Example:
 *   FTransportHostConfig Config;
 *   Config.ServerAddress = MakeUdpAddress(192, 168, 1, 10, 1234);
 *   Host.Configure(ENetworkMode::Client, Config);
 */
struct FTransportHostConfig
{
	/** Motivation: Paces outgoing heartbeats (and client Hello retries while connecting). */
	Core::DurationMilliseconds HeartbeatIntervalMilliseconds{DefaultHeartbeatIntervalMilliseconds};

	/** Motivation: Defines the silence window after which a peer is evicted; must exceed the heartbeat interval. */
	Core::DurationMilliseconds PeerTimeoutMilliseconds{DefaultPeerTimeoutMilliseconds};

	/** Motivation: Names the server a client greets with Hello; ignored by every non-client mode. */
	Core::FDeviceAddress ServerAddress{};

	/** Motivation: Pins the protocol version advertised in Hello/Welcome; a mismatch is ignored, not admitted. */
	std::uint8_t ProtocolVersion{DefaultProtocolVersion};
};

} // namespace MicroWorld::Transport
