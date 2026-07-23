#pragma once

#include <MicroWorld/Net/NetHost.h>
#include <MicroWorld/Object/ClassDescriptor.h>
#include <MicroWorld/PlatformEsp32/Esp32UartDriver.h>

#include <cstdint>

/**
 * Shared protocol constants and config builders for example 19's two roles.
 *
 * Both role translation units (ServerMain.cpp, ClientMain.cpp) include this so
 * the channel numbers, opcode, node ids, and session/UART configuration are
 * defined exactly once — DRY within this one example.
 */
namespace Ex19
{
/** Server stamps frames with node id 1; the client greets that id as its server. */
constexpr std::uint8_t ServerNodeId = 1;

/** Client stamps frames with node id 2; the point-to-point wire never routes either id. */
constexpr std::uint8_t ClientNodeId = 2;

/** Fixed UART port and the two crossover data GPIOs, identical on both boards. */
constexpr std::int32_t UartPortNumber = 1;
constexpr std::int32_t TxGpioNumber = 17;
constexpr std::int32_t RxGpioNumber = 18;

/** A wire is fast, so 115200 baud. */
constexpr std::uint32_t UartBaudRate = 115200;

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

/** Poll pace for both boards; far faster than the volley so the watchdog idle task runs. */
constexpr unsigned PollPacingMilliseconds = 20;

/** Builds a board's UART driver configuration from the fixed pins and baud. */
inline MicroWorld::FEsp32UartConfig MakeUartConfig(const std::uint8_t NodeId) noexcept
{
	MicroWorld::FEsp32UartConfig Config;
	Config.UartPort = UartPortNumber;
	Config.TxGpio = TxGpioNumber;
	Config.RxGpio = RxGpioNumber;
	Config.BaudRate = UartBaudRate;
	Config.LocalNodeId = NodeId;
	return Config;
}

/** Builds the shared session config; heartbeats keep the point-to-point peer alive between sends. */
inline MicroWorld::FNetHostConfig MakeHostConfig() noexcept
{
	MicroWorld::FNetHostConfig Config{};
	Config.HeartbeatIntervalMilliseconds = 1000;
	Config.PeerTimeoutMilliseconds = 5000;
	Config.ProtocolVersion = ProtocolVersion;
	return Config;
}
} // namespace Ex19
