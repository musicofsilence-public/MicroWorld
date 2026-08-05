#pragma once

#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Messaging/NameId.h>
#include <MicroWorld/Networking/NetworkSystemInformation.h>

#include <cstdint>

/**
 * Motivation: Holds the shared protocol constants and session-config builder for example 16's
 *   two roles, so both translation units (ServerMain.cpp, ClientMain.cpp) define the channel
 *   numbers, opcode, spawn count, and session configuration exactly once, so both roles share one definition.
 */
namespace Ex16
{
/** Motivation: SoftAP the server hosts and the client joins — demo-only values, not a secret, so
 *  they commit safely; no home router and no real credentials are involved. */
constexpr const char* DemoApSsid = "microworld-ex16";

/** Motivation: Demo WPA2 password (>= 8 chars); throwaway, never a real network's password. */
constexpr const char* DemoApPassword = "microworld";

/** Motivation: The server's fixed SoftAP gateway IPv4; the client addresses this, no discovery needed. */
constexpr std::uint8_t ServerIpv4[4] = {192, 168, 4, 1};

/** Motivation: UDP port the server binds and the client targets. */
constexpr std::uint16_t ServerPort = 40404;

/** Motivation: Identifies the local-only channel carrying client spawn requests through Network. */
constexpr MicroWorld::Messaging::FNameId InputEventChannel = MicroWorld::Messaging::MakeNameId("Ex16Input");

/** Motivation: Identifies the local-only channel carrying server state through Network. */
constexpr MicroWorld::Messaging::FNameId StateBroadcastChannel = MicroWorld::Messaging::MakeNameId("Ex16State");

/** Motivation: Filters the one application message accepted on the input channel. */
constexpr MicroWorld::Messaging::FNameId SpawnRequestMessageName = MicroWorld::Messaging::MakeNameId("Ex16Spawn");

/** Motivation: Filters the one application message accepted on the state channel. */
constexpr MicroWorld::Messaging::FNameId StateMessageName = MicroWorld::Messaging::MakeNameId("Ex16StateUpdate");

/** Motivation: One-byte opcode the client sends to request one server-world spawn. */
constexpr std::uint8_t SpawnRequestOpcode = 0x42;

/** Motivation: Number of spawn requests the client issues, and pre-allocated server registries. */
constexpr int MaxSpawns = 2;

/** Motivation: Protocol version both Network roles validate during admission. */
constexpr std::uint8_t ProtocolVersion = 1;

/** Motivation: Stable descriptor id for the actor the server spawns on a client request. */
constexpr MicroWorld::Engine::FTypeId DemoSpawnedActorTypeId{0x00080001u};

/** Motivation: Poll pace for both boards; far faster than the exchange so the watchdog idle task runs. */
constexpr unsigned PollPacingMilliseconds = 20;

/**
 * Motivation: Lets both roles build the same session configuration from one source, so heartbeats
 *   keep the peer alive between explicit sends without each role restating the values.
 * Responsibilities: Return a config carrying the shared heartbeat, timeout, and protocol version.
 */
inline MicroWorld::Networking::FNetworkSystemInformation MakeNetworkInformation(const MicroWorld::Networking::ENetworkRole Role) noexcept
{
	MicroWorld::Networking::FNetworkSystemInformation Information{};
	Information.Role = Role;
	Information.HeartbeatIntervalMilliseconds = 1000;
	Information.PeerTimeoutMilliseconds = 5000;
	Information.ProtocolVersion = ProtocolVersion;
	return Information;
}
} // namespace Ex16
