#pragma once

#include <MicroWorld/Net/NetHost.h>
#include <MicroWorld/Object/ClassDescriptor.h>

#include <cstdint>

/**
 * Shared protocol constants and the session-config builder for example 16's two
 * roles. Both role translation units (ServerMain.cpp, ClientMain.cpp) include
 * this so the channel numbers, opcode, spawn count, and session configuration
 * are defined exactly once — DRY within this one example.
 *
 * There is no node id here (unlike the UART example 19): a UDP peer is
 * identified by its socket address, so `TNetHost` learns each peer from the
 * datagram it arrives on rather than from a driver-stamped node id.
 */
namespace Ex16
{
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
constexpr MicroWorld::FTypeId DemoSpawnedActorTypeId{0x00080001u};

/** Poll pace for both boards; far faster than the exchange so the watchdog idle task runs. */
constexpr unsigned PollPacingMilliseconds = 20;

/** Builds the shared session config; heartbeats keep the peer alive between explicit sends. */
inline MicroWorld::FNetHostConfig MakeHostConfig() noexcept
{
	MicroWorld::FNetHostConfig Config{};
	Config.HeartbeatIntervalMilliseconds = 1000;
	Config.PeerTimeoutMilliseconds = 5000;
	Config.ProtocolVersion = ProtocolVersion;
	return Config;
}
} // namespace Ex16
