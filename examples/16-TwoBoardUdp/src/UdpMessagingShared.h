#pragma once

#include <MicroWorld/Transport/TransportHost.h>
#include <MicroWorld/Engine/ClassDescriptor.h>

#include <cstdint>

/**
 * Shared protocol constants and the session-config builder for example 16's two
 * roles. Both role translation units (ServerMain.cpp, ClientMain.cpp) include
 * this so the channel numbers, opcode, spawn count, and session configuration
 * are defined exactly once — DRY within this one example.
 *
 * There is no node id here (unlike the UART example 19): a UDP peer is
 * identified by its socket address, so `TTransportHost` learns each peer from the
 * datagram it arrives on rather than from a device-stamped node id.
 */
namespace Ex16
{
/** SoftAP the server hosts and the client joins — demo-only values, not a secret, so
 *  they commit safely; no home router and no real credentials are involved. */
constexpr const char* DemoApSsid = "microworld-ex16";

/** Demo WPA2 password (>= 8 chars); throwaway, never a real network's password. */
constexpr const char* DemoApPassword = "microworld";

/** The server's fixed SoftAP gateway IPv4; the client addresses this, no discovery needed. */
constexpr std::uint8_t ServerIpv4[4] = {192, 168, 4, 1};

/** UDP port the server binds and the client targets. */
constexpr std::uint16_t ServerPort = 40404;

/** Application channel carrying the client's spawn request (channel 0 is reserved control). */
constexpr std::uint8_t InputEventChannel = 1;

/** Application channel the server broadcasts world state on. */
constexpr std::uint8_t StateBroadcastChannel = 2;

/** One-byte opcode the client sends to request one server-world spawn. */
constexpr std::uint8_t SpawnRequestOpcode = 0x42;

/** Number of spawn requests the client issues, and pre-allocated server registries. */
constexpr int MaxSpawns = 2;

/** Protocol version both hosts advertise in Hello/Welcome. */
constexpr std::uint8_t ProtocolVersion = 1;

/** Stable descriptor id for the actor the server spawns on a client request. */
constexpr MicroWorld::Engine::FTypeId DemoSpawnedActorTypeId{0x00080001u};

/** Poll pace for both boards; far faster than the exchange so the watchdog idle task runs. */
constexpr unsigned PollPacingMilliseconds = 20;

/** Builds the shared session config; heartbeats keep the peer alive between explicit sends. */
inline MicroWorld::Transport::FTransportHostConfig MakeHostConfig() noexcept
{
	MicroWorld::Transport::FTransportHostConfig Config{};
	Config.HeartbeatIntervalMilliseconds = 1000;
	Config.PeerTimeoutMilliseconds = 5000;
	Config.ProtocolVersion = ProtocolVersion;
	return Config;
}
} // namespace Ex16
