#pragma once

#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessageChannelBinding.h>
#include <MicroWorld/Messaging/MessageRouter.h>
#include <MicroWorld/Engine/EngineSystem.h>
#include <MicroWorld/Transport/TransportHost.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Platform/Esp32/Esp32UartDevice.h>

#include <cstdint>

/**
 * Motivation: Holds the shared protocol ids, config builders, and composition-type aliases for
 *   example 23's two roles, so both translation units (ServerMain.cpp, ClientMain.cpp) define the
 *   message ids, actor ids, node ids, UART/session configuration, and the TTransportHost/TMessageRouter/
 *   TEngine shapes exactly once, so both roles share one definition.
 */
namespace Ex23
{
/** Motivation: Targeted message id: switch actor -> lamp actor, 1-byte payload (0 = off, 1 = on). */
inline constexpr MicroWorld::Messaging::FMessageTypeId SetLampStateMessageId = 1;

/** Motivation: Broadcast message id: switch actor -> every subscriber, 1-byte heartbeat counter. */
inline constexpr MicroWorld::Messaging::FMessageTypeId HeartbeatCountMessageId = 2;

/** Motivation: Actor id FLampActor registers its SetLampState handler under and the switch targets. */
inline constexpr MicroWorld::Messaging::FMessageActorId LampActorId = 10;

/** Motivation: Actor id recorded as the switch actor's sender on every message it sends. */
inline constexpr MicroWorld::Messaging::FMessageActorId SwitchActorId = 11;

/** Motivation: Actor id recorded as the display actor's sender; nothing ever targets a send at it. */
inline constexpr MicroWorld::Messaging::FMessageActorId DisplayActorId = 12;

/** Motivation: Router-facing channel id both roles register their TMessageChannelBinding under. */
inline constexpr MicroWorld::Messaging::FMessageChannelId AppChannelId = 1;

/** Motivation: TTransportHost wire-level channel byte the binding reads and writes (channel 0 is reserved control). */
inline constexpr std::uint8_t AppWireChannelByte = 1;

/** Motivation: Server stamps frames with node id 1; the client greets that id as its server. */
constexpr std::uint8_t ServerNodeId = 1;

/** Motivation: Client stamps frames with node id 2; the point-to-point wire never routes either id. */
constexpr std::uint8_t ClientNodeId = 2;

/** Motivation: Fixed UART port and the two crossover data GPIOs, identical to examples 18 and 19. */
constexpr std::int32_t UartPortNumber = 1;
constexpr std::int32_t TxGpioNumber = 17;
constexpr std::int32_t RxGpioNumber = 18;

/** Motivation: A wire is fast, so 115200 baud. */
constexpr std::uint32_t UartBaudRate = 115200;

/** Motivation: Protocol version both hosts advertise in Hello/Welcome. */
constexpr std::uint8_t ProtocolVersion = 1;

/** Motivation: Poll pace for both boards; far faster than the 2 s switch cadence so the watchdog idle task runs. */
constexpr unsigned PollPacingMilliseconds = 20;

/** Motivation: Stable descriptor id for the managed FLampActor type (0x0017 == example 23). */
constexpr MicroWorld::Engine::FTypeId LampActorTypeId{0x00170001u};

/** Motivation: Stable descriptor id for the managed FDisplayActor type. */
constexpr MicroWorld::Engine::FTypeId DisplayActorTypeId{0x00170002u};

/** Motivation: Stable descriptor id for the managed FSwitchActor type. */
constexpr MicroWorld::Engine::FTypeId SwitchActorTypeId{0x00170003u};

/** Motivation: The wired network host both roles compose their board's UART link through. */
using FWireTransport = MicroWorld::Transport::TTransportHost<2, 120>;

/** Motivation: The local actor-message router both roles compose, sized for this example's one channel and few handlers. */
using FWireRouter = MicroWorld::Messaging::TMessageRouter<16, 8, 96, 1>;

/** Motivation: Adapts FWireTransport to the engine's per-frame network slot (only PreAdvance/PostAdvance; the router is pumped separately, see §4).
 */
using FWireFrame = MicroWorld::Engine::THostPlaySystem<FWireTransport>;

/** Motivation: Two-way adapter binding one FWireTransport wire channel to the local FWireRouter. */
using FWireBinding = MicroWorld::Messaging::TMessageChannelBinding<FWireTransport>;

/** Motivation: The engine both roles compose; sized for one world with a couple of small actors using direct component storage. */
using FWireEngine = MicroWorld::Engine::TEngine<>;

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

/**
 * Motivation: Runs one board's manual per-frame router pump, so both roles flush the router's outbound
 *   queue before the tick and dispatch its inbound queue after, in the order the link proved correct.
 * Responsibilities: Call PostAdvance, tick the engine, then PreAdvance, in that order each frame.
 */
inline void PumpOneFrame(FWireRouter& Router, FWireEngine& Engine, const MicroWorld::Core::TimePointMilliseconds NowMilliseconds) noexcept
{
	Router.PostAdvance(NowMilliseconds);
	(void)Engine.Tick(NowMilliseconds);
	Router.PreAdvance(NowMilliseconds);
}
} // namespace Ex23
