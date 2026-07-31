#pragma once

#include <MicroWorld/Transport/TransportHost.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Platform/Esp32/Esp32LoraDevice.h>

#include <cstdint>

/**
 * Shared protocol constants and config builders for example 26's two roles.
 *
 * Both role translation units (ServerMain.cpp, ClientMain.cpp) include this so
 * the channel numbers, opcode, node ids, and session/LoRa configuration are
 * defined exactly once — DRY within this one example.
 */
namespace Ex26
{
/** Server stamps frames with node id 1; the client greets that id as its server. */
constexpr std::uint8_t ServerNodeId = 1;

/** Client stamps frames with node id 2; the ids identify peers but never route on the air. */
constexpr std::uint8_t ClientNodeId = 2;

/** Fixed UART port and the two data GPIOs wired to the E32 module, identical on both boards. */
constexpr std::int32_t UartPortNumber = 1;
constexpr std::int32_t TxGpioNumber = 17;
constexpr std::int32_t RxGpioNumber = 18;

/** E32 module UART baud rate (factory default 8N1, D7); the module's over-the-air data rate is separate and far slower. */
constexpr std::uint32_t UartBaudRate = 9600;

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

/** Server paces its state broadcast at this period (D8): a full E32 frame costs hundreds of
 *  milliseconds of airtime, so broadcasting every tick (as the wired example does) would
 *  congest the channel. */
constexpr std::uint64_t StateBroadcastPeriodMilliseconds = 1000;

/** Builds a board's E32 LoRa device configuration from the fixed pins and baud. */
inline MicroWorld::FEsp32E32LoraConfig MakeLoraConfig(const std::uint8_t NodeId) noexcept
{
	MicroWorld::FEsp32E32LoraConfig Config;
	Config.UartPort = UartPortNumber;
	Config.TxGpio = TxGpioNumber;
	Config.RxGpio = RxGpioNumber;
	Config.BaudRate = UartBaudRate;
	Config.LocalNodeId = NodeId;
	return Config;
}

/** Builds the shared session config; the D8 LoRa airtime profile relaxes the heartbeat and
 *  timeout so the channel isn't congested (the wired example's 1000 ms / 5000 ms would). */
inline MicroWorld::FTransportHostConfig MakeHostConfig() noexcept
{
	MicroWorld::FTransportHostConfig Config{};
	Config.HeartbeatIntervalMilliseconds = 3000;
	Config.PeerTimeoutMilliseconds = 15000;
	Config.ProtocolVersion = ProtocolVersion;
	return Config;
}
} // namespace Ex26
