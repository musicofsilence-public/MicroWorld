#pragma once

#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/Message.h>
#include <MicroWorld/Engine/MessageChannelBinding.h>
#include <MicroWorld/Engine/MessageRouter.h>
#include <MicroWorld/Engine/EngineSystem.h>
#include <MicroWorld/Net/NetHost.h>
#include <MicroWorld/Object/ClassDescriptor.h>
#include <MicroWorld/PlatformEsp32/Esp32UartDriver.h>

#include <cstdint>

/**
 * Shared protocol ids, config builders, and composition-type aliases for example
 * 23's two roles.
 *
 * Both role translation units (ServerMain.cpp, ClientMain.cpp) include this so the
 * message ids, actor ids, node ids, UART/session configuration, and the
 * TNetHost/TMessageRouter/TEngine shapes are defined exactly once — DRY within
 * this one example (mirrors 19-UartMessaging's UartMessagingShared.h).
 */
namespace Ex23
{
/** Targeted message id: switch actor -> lamp actor, 1-byte payload (0 = off, 1 = on). */
inline constexpr MicroWorld::FMessageTypeId SetLampStateMessageId = 1;

/** Broadcast message id: switch actor -> every subscriber, 1-byte heartbeat counter. */
inline constexpr MicroWorld::FMessageTypeId HeartbeatCountMessageId = 2;

/** Actor id FLampActor registers its SetLampState handler under and the switch targets. */
inline constexpr MicroWorld::FMessageActorId LampActorId = 10;

/** Actor id recorded as the switch actor's sender on every message it sends. */
inline constexpr MicroWorld::FMessageActorId SwitchActorId = 11;

/** Actor id recorded as the display actor's sender; nothing ever targets a send at it. */
inline constexpr MicroWorld::FMessageActorId DisplayActorId = 12;

/** Router-facing channel id both roles register their TMessageChannelBinding under. */
inline constexpr MicroWorld::FMessageChannelId AppChannelId = 1;

/** TNetHost wire-level channel byte the binding reads and writes (channel 0 is reserved control). */
inline constexpr std::uint8_t AppWireChannelByte = 1;

/** Server stamps frames with node id 1; the client greets that id as its server. */
constexpr std::uint8_t ServerNodeId = 1;

/** Client stamps frames with node id 2; the point-to-point wire never routes either id. */
constexpr std::uint8_t ClientNodeId = 2;

/** Fixed UART port and the two crossover data GPIOs, identical to examples 18 and 19. */
constexpr std::int32_t UartPortNumber = 1;
constexpr std::int32_t TxGpioNumber = 17;
constexpr std::int32_t RxGpioNumber = 18;

/** A wire is fast, so 115200 baud. */
constexpr std::uint32_t UartBaudRate = 115200;

/** Protocol version both hosts advertise in Hello/Welcome. */
constexpr std::uint8_t ProtocolVersion = 1;

/** Poll pace for both boards; far faster than the 2 s switch cadence so the watchdog idle task runs. */
constexpr unsigned PollPacingMilliseconds = 20;

/** Stable descriptor id for the managed FLampActor type (0x0017 == example 23). */
constexpr MicroWorld::FTypeId LampActorTypeId{0x00170001u};

/** Stable descriptor id for the managed FDisplayActor type. */
constexpr MicroWorld::FTypeId DisplayActorTypeId{0x00170002u};

/** Stable descriptor id for the managed FSwitchActor type. */
constexpr MicroWorld::FTypeId SwitchActorTypeId{0x00170003u};

/** The wired network host both roles compose their board's UART link through. */
using FWireNet = MicroWorld::TNetHost<2, 120>;

/** The local actor-message router both roles compose, sized for this example's one channel and few handlers. */
using FWireRouter = MicroWorld::TMessageRouter<16, 8, 96, 1>;

/** Adapts FWireNet to the engine's per-frame network slot (only PreAdvance/PostAdvance; the router is pumped separately, see §4). */
using FWireFrame = MicroWorld::TNetHostSystem<FWireNet>;

/** Two-way adapter binding one FWireNet wire channel to the local FWireRouter. */
using FWireBinding = MicroWorld::TMessageChannelBinding<FWireNet>;

/** The engine both roles compose; sized for one world with a couple of small inline actors (the default ESP32-S3 traits). */
using FWireEngine = MicroWorld::TEngine<>;

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

/**
 * Runs one board's manual per-frame router pump, identical on both roles.
 *
 * Manual frame composition (Phase 4.1 folds this into TEngineSystemSet): flushes Router's outbound
 * queue to the wire before the engine tick, then dispatches its inbound queue after -- the same order
 * EngineMessageChannelTests.cpp's PumpSide proved correct. TEngine holds exactly one
 * IEngineSystem (the bound TNetHostSystem), so the router itself is pumped here rather than through
 * the engine.
 */
inline void PumpOneFrame(FWireRouter& Router, FWireEngine& Engine, const MicroWorld::TimePointMilliseconds NowMilliseconds) noexcept
{
	Router.PostAdvance(NowMilliseconds);
	(void)Engine.Tick(NowMilliseconds);
	Router.PreAdvance(NowMilliseconds);
}
} // namespace Ex23
