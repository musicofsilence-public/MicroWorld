#pragma once

#include <MicroWorld/Transport/TransportHost.h>
#include <MicroWorld/Engine/ClassDescriptor.h>

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

/** Motivation: Application channel carrying the client's spawn request (channel 0 is reserved control). */
constexpr std::uint8_t InputEventChannel = 1;

/** Motivation: Application channel the server broadcasts world state on. */
constexpr std::uint8_t StateBroadcastChannel = 2;

/** Motivation: One-byte opcode the client sends to request one server-world spawn. */
constexpr std::uint8_t SpawnRequestOpcode = 0x42;

/** Motivation: Number of spawn requests the client issues, and pre-allocated server registries. */
constexpr int MaxSpawns = 2;

/** Motivation: Protocol version both hosts advertise in Hello/Welcome. */
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
inline MicroWorld::Transport::FTransportHostConfig MakeHostConfig() noexcept
{
	MicroWorld::Transport::FTransportHostConfig Config{};
	Config.HeartbeatIntervalMilliseconds = 1000;
	Config.PeerTimeoutMilliseconds = 5000;
	Config.ProtocolVersion = ProtocolVersion;
	return Config;
}
} // namespace Ex16
