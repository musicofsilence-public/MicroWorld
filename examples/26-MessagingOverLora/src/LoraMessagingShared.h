#pragma once

#include <MicroWorld/Transport/TransportHostConfig.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Platform/Esp32/Esp32LoraDevice.h>

#include <cstdint>

/**
 * Motivation: Holds the shared protocol constants and config builders for example 26's two roles, so
 *   both translation units (ServerMain.cpp, ClientMain.cpp) define the channel numbers, opcode, node
 *   ids, and session/LoRa configuration exactly once, so both roles share one definition.
 */
namespace Ex26
{
/** Motivation: Server stamps frames with node id 1; the client greets that id as its server. */
constexpr std::uint8_t ServerNodeId = 1;

/** Motivation: Client stamps frames with node id 2; the ids identify peers but never route on the air. */
constexpr std::uint8_t ClientNodeId = 2;

/** Motivation: Fixed UART port and the two data GPIOs wired to the E32 module, identical on both boards. */
constexpr std::int32_t UartPortNumber = 1;
constexpr std::int32_t TxGpioNumber = 17;
constexpr std::int32_t RxGpioNumber = 18;

/** Motivation: E32 module UART baud rate (factory default 8N1, D7); the module's over-the-air data rate is separate and far slower. */
constexpr std::uint32_t UartBaudRate = 9600;

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

/** Motivation: Server paces its state broadcast at this period (D8): a full E32 frame costs hundreds of
 *  milliseconds of airtime, so broadcasting every tick (as the wired example does) would
 *  congest the channel. */
constexpr std::uint64_t StateBroadcastPeriodMilliseconds = 1000;

/**
 * Motivation: Lets both roles build a board's E32 LoRa device configuration from one source, so the
 *   fixed pins and baud are never restated.
 * Responsibilities: Fill the E32 config with the shared UART, GPIO, baud, and node id values.
 */
inline MicroWorld::Platform::Esp32::FEsp32E32LoraConfig MakeLoraConfig(const std::uint8_t NodeId) noexcept
{
	MicroWorld::Platform::Esp32::FEsp32E32LoraConfig Config;
	Config.UartPort = UartPortNumber;
	Config.TxGpio = TxGpioNumber;
	Config.RxGpio = RxGpioNumber;
	Config.BaudRate = UartBaudRate;
	Config.LocalNodeId = NodeId;
	return Config;
}

/**
 * Motivation: Lets both roles build the same session configuration from one source, with the LoRa airtime
 *   profile relaxing heartbeat and timeout so the channel is not congested.
 * Responsibilities: Return a config carrying the relaxed heartbeat, timeout, and protocol version.
 */
inline MicroWorld::Transport::FTransportHostConfig MakeHostConfig() noexcept
{
	MicroWorld::Transport::FTransportHostConfig Config{};
	Config.HeartbeatIntervalMilliseconds = 3000;
	Config.PeerTimeoutMilliseconds = 15000;
	Config.ProtocolVersion = ProtocolVersion;
	return Config;
}
} // namespace Ex26
