#pragma once

#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Networking/NetworkSystem.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Engine
{

/**
 * Motivation: Lets an application compose one Engine-owned Network without naming Messaging, link, route, or Network implementation types.
 * Responsibilities: Carry the complete bounded role, initial-route, protocol, timing, and server-admission policy before Engine mutates state.
 * Example: FEngineNetworkSetup Setup{Networking::ENetworkRole::Client, ServerAddress, 1, 1000, 5000, 1};
 */
struct FEngineNetworkSetup final
{
	/** Motivation: Selects the client or server Network behavior that Engine will compose. */
	Networking::ENetworkRole Role{Networking::ENetworkRole::Client};
	/** Motivation: Names the only initial server address for a client; servers leave it empty. */
	Core::FDeviceAddress InitialServerAddress{};
	/** Motivation: Pins the protocol revision Engine supplies to the private Network policy. */
	std::uint8_t ProtocolVersion{1};
	/** Motivation: Paces initial retries and established-session heartbeats with caller-timed Network turns. */
	Core::DurationMilliseconds HeartbeatInterval{1000};
	/** Motivation: Bounds live-session silence before Network retires a peer. */
	Core::DurationMilliseconds PeerTimeout{5000};
	/** Motivation: Bounds distinct server routes admitted by this Engine-owned Network instance. */
	std::size_t MaximumAdmittedServerPeers{Networking::FNetworkSystem::MaxPeers};
};

/**
 * Motivation: Reports whether Engine completed its all-or-nothing high-level Network composition.
 * Responsibilities: Distinguish success, duplicate composition, invalid caller policy, and fixed-capacity exhaustion.
 * Example: if (Engine.ConfigureNetworking(Device, Setup) != EEngineNetworkSetupResult::Success) { return; }
 */
enum class EEngineNetworkSetupResult : std::uint8_t
{
	/** Motivation: Reports that Engine owns one fully initialized device, Messaging, and Network chain. */
	Success,
	/** Motivation: Rejects a second configuration without disturbing the existing chain. */
	AlreadyConfigured,
	/** Motivation: Rejects a role, address, timing, or admission policy that cannot produce a valid Network. */
	InvalidConfiguration,
	/** Motivation: Reports that fixed Engine or Messaging storage could not hold the requested chain. */
	CapacityExceeded,
};

} // namespace MicroWorld::Engine
