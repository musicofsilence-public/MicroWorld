#pragma once

#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessageChannelBinding.h>
#include <MicroWorld/Messaging/MessageRouter.h>
#include <MicroWorld/Engine/EngineSystem.h>
#include <MicroWorld/Messaging/ReliableChannel.h>
#include <MicroWorld/Transport/TransportHost.h>
#include <MicroWorld/Engine/ClassDescriptor.h>

#include <cstdint>

/**
 * Shared protocol ids, config, and composition-type aliases for example 25's two roles.
 * Both role translation units include this so the message/actor/channel ids, WiFi/UDP
 * configuration, and the TTransportHost/TMessageRouter/TReliableChannel/TEngine/
 * TPlaySystemSet shapes are defined exactly once (DRY within this one example).
 */
namespace Ex25
{
/** Targeted message id carrying one counter value on the BEST-EFFORT channel. */
inline constexpr MicroWorld::Messaging::FMessageTypeId BestEffortCounterMessageId = 1;

/** Targeted message id carrying one counter value on the GUARANTEED channel. */
inline constexpr MicroWorld::Messaging::FMessageTypeId GuaranteedCounterMessageId = 2;

/** Actor id the client's counter records as sender; nothing ever targets a send at it. */
inline constexpr MicroWorld::Messaging::FMessageActorId CounterActorId = 10;

/** Actor id the server's ledger registers its two handlers under and the counter targets. */
inline constexpr MicroWorld::Messaging::FMessageActorId LedgerActorId = 11;

/** Router-facing channel id both roles register their best-effort binding under. */
inline constexpr MicroWorld::Messaging::FMessageChannelId BestEffortChannelId = 1;

/** Router-facing channel id both roles register their guaranteed (reliable) channel under. */
inline constexpr MicroWorld::Messaging::FMessageChannelId GuaranteedChannelId = 2;

/** Wire-level channel byte the best-effort binding reads/writes (channel 0 is reserved control). */
inline constexpr std::uint8_t BestEffortWireChannelByte = 1;

/** Wire-level channel byte the guaranteed binding reads/writes; MUST differ from best-effort's
 *  since BOTH channels share one UDP transport and the wire byte is how the transport demuxes them. */
inline constexpr std::uint8_t GuaranteedWireChannelByte = 2;

/** First and last counter value the client sends on both channels (inclusive). */
inline constexpr std::uint8_t FirstCounterValue = 1;
inline constexpr std::uint8_t LastCounterValue = 30;

/** How often the client emits the next counter value on both channels. */
inline constexpr MicroWorld::Core::DurationMilliseconds CounterIntervalMilliseconds = 500;

/** The client drops every third outgoing packet through this injector (Task 5.1). */
inline constexpr std::uint32_t DropEveryNthSend = 3;

/** Poll pace for both boards; far faster than any app cadence so the watchdog idle task runs. */
inline constexpr unsigned PollPacingMilliseconds = 20;

/** Protocol version both hosts advertise in Hello/Welcome. */
inline constexpr std::uint8_t ProtocolVersion = 1;

/** SoftAP the server hosts and the client joins -- demo-only values, not a secret. */
constexpr const char* DemoApSsid = "microworld-ex25";

/** Demo WPA2 password (>= 8 chars); throwaway, never a real network's password. */
constexpr const char* DemoApPassword = "microworld";

/** The server's fixed SoftAP gateway IPv4; the client addresses this, no discovery needed. */
constexpr std::uint8_t ServerIpv4[4] = {192, 168, 4, 1};

/** UDP port the server binds and the client targets. */
constexpr std::uint16_t ServerPort = 40404;

/** Stable descriptor id for the managed FCounterActor type (0x0019 == example 25). */
constexpr MicroWorld::Engine::FTypeId CounterActorTypeId{0x00190001u};

/** Stable descriptor id for the managed FLedgerActor type. */
constexpr MicroWorld::Engine::FTypeId LedgerActorTypeId{0x00190002u};

/** The one WiFi-backed network host both roles compose (256-byte packet, matching example 16/24). */
using FWorldTransport = MicroWorld::Transport::TTransportHost<2, 256>;

/** The one local actor-message router both roles compose, sized for this example's two channels. */
using FWorldRouter = MicroWorld::Messaging::TMessageRouter<16, 8, 96, 2>;

/** Adapts FWorldTransport to the engine's per-frame network slot inside the frame set. */
using FHostPlay = MicroWorld::Engine::THostPlaySystem<FWorldTransport>;

/** Two-way adapter binding one wire channel to the shared router; both channels use this type. */
using FChannelBinding = MicroWorld::Messaging::TMessageChannelBinding<FWorldTransport>;

/** Guaranteed-delivery wrapper for channel 2 (8 pending slots, 96-byte wrapped-packet budget). */
using FGuaranteedChannel = MicroWorld::Messaging::TReliableChannel<8, 96>;

/** Pumps the host play system, the reliable channel, and the router behind one IPlaySystem slot (D3 order). */
using FWorldFrameSet = MicroWorld::TPlaySystemSet<3>;

/** The engine both roles compose; sized for one world with a couple of small actors using direct component storage. */
using FWorldEngine = MicroWorld::Engine::TEngine<>;

/** Builds the shared session config; heartbeats keep the point-to-point peer alive between sends. */
inline MicroWorld::Transport::FTransportHostConfig MakeHostConfig() noexcept
{
	MicroWorld::Transport::FTransportHostConfig Config{};
	Config.HeartbeatIntervalMilliseconds = 1000;
	Config.PeerTimeoutMilliseconds = 5000;
	Config.ProtocolVersion = ProtocolVersion;
	return Config;
}
} // namespace Ex25
