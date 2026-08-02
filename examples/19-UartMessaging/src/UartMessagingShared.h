#pragma once

#include <MicroWorld/Transport/TransportHostConfig.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Platform/Esp32/Esp32UartDevice.h>

#include <cstdint>

/**
 * Motivation: Holds the shared protocol constants and config builders for example 19's two roles,
 *   so both translation units (ServerMain.cpp, ClientMain.cpp) define the channel numbers, opcode,
 *   node ids, and session/UART configuration exactly once, so both roles share one definition.
 */
namespace Ex19
{
/** Motivation: Server stamps frames with node id 1; the client greets that id as its server. */
constexpr std::uint8_t ServerNodeId = 1;

/** Motivation: Client stamps frames with node id 2; the point-to-point wire never routes either id. */
constexpr std::uint8_t ClientNodeId = 2;

/** Motivation: Fixed UART port and the two crossover data GPIOs, identical on both boards. */
constexpr std::int32_t UartPortNumber = 1;
constexpr std::int32_t TxGpioNumber = 17;
constexpr std::int32_t RxGpioNumber = 18;

/** Motivation: A wire is fast, so 115200 baud. */
constexpr std::uint32_t UartBaudRate = 115200;

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

/** Motivation: Poll pace for both boards; far faster than the volley so the watchdog idle task runs. */
constexpr unsigned PollPacingMilliseconds = 20;

/**
 * Motivation: Lets both roles build a board's UART device configuration from one source, so the fixed
 *   pins and baud are never restated.
 * Responsibilities: Fill the UART config with the shared port, GPIO, baud, and node id values.
 */
inline MicroWorld::Platform::Esp32::FEsp32UartConfig MakeUartConfig(const std::uint8_t NodeId) noexcept
{
	MicroWorld::Platform::Esp32::FEsp32UartConfig Config;
	Config.UartPort = UartPortNumber;
	Config.TxGpio = TxGpioNumber;
	Config.RxGpio = RxGpioNumber;
	Config.BaudRate = UartBaudRate;
	Config.LocalNodeId = NodeId;
	return Config;
}

/**
 * Motivation: Lets both roles build the same session configuration from one source, so heartbeats keep
 *   the point-to-point peer alive between sends without each role restating the values.
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
} // namespace Ex19
