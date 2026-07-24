#pragma once

#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/Message.h>
#include <MicroWorld/Engine/MessageChannelBinding.h>
#include <MicroWorld/Engine/MessageRouter.h>
#include <MicroWorld/Engine/NetworkFrame.h>
#include <MicroWorld/Net/NetHost.h>
#include <MicroWorld/Net/UdpAddressCodec.h>
#include <MicroWorld/Object/ClassDescriptor.h>
#include <MicroWorld/PlatformEsp32/Esp32UartDriver.h>
#include <MicroWorld/PlatformEsp32/Esp32UdpDriver.h>
#include <MicroWorld/PlatformEsp32/Esp32WifiLink.h>

#include <cstdint>

/**
 * Shared protocol ids, config builders, and composition-type aliases for example
 * 24's two roles.
 *
 * Both role translation units (ServerMain.cpp, ClientMain.cpp) include this so the
 * message/actor/channel ids, UART/WiFi/UDP configuration, and the
 * TNetHost/TMessageRouter/TEngineHost/TNetworkFrameSet shapes are defined exactly
 * once -- DRY within this one example (mirrors 23-TwoBoardWire's
 * TwoBoardWireShared.h).
 */
namespace Ex24
{
/** Broadcast message id: client sensor -> server, 2-byte LE reading, delivered over UDP. */
inline constexpr MicroWorld::FMessageTypeId TelemetryReadingMessageId = 1;

/** Targeted message id: server -> client sensor, 2-byte LE interval milliseconds, delivered over UART. */
inline constexpr MicroWorld::FMessageTypeId SetReportingRateMessageId = 2;

/** Actor id FSensorActor registers its SetReportingRate handler under and the commander targets. */
inline constexpr MicroWorld::FMessageActorId SensorActorId = 10;

/** Actor id recorded as the telemetry sink's sender; nothing ever targets a send at it. */
inline constexpr MicroWorld::FMessageActorId TelemetrySinkActorId = 11;

/** Actor id recorded as the commander's sender; nothing ever targets a send at it. */
inline constexpr MicroWorld::FMessageActorId CommanderActorId = 12;

/** Router-facing channel id both roles register their UDP telemetry binding under. */
inline constexpr MicroWorld::FMessageChannelId TelemetryChannelId = 1;

/** Router-facing channel id both roles register their UART commands binding under. */
inline constexpr MicroWorld::FMessageChannelId CommandsChannelId = 2;

/** Wire-level channel byte the UDP host's binding reads and writes (channel 0 is reserved control). */
inline constexpr std::uint8_t TelemetryWireChannelByte = 1;

/** Wire-level channel byte the UART host's binding reads and writes; a different host, so the value may repeat. */
inline constexpr std::uint8_t CommandsWireChannelByte = 1;

/** The sensor's start/restored reporting cadence. */
inline constexpr MicroWorld::DurationMilliseconds BaseReportingIntervalMilliseconds = 1000;

/** The sensor's "halved" reporting cadence, alternated with the base cadence by the commander. */
inline constexpr MicroWorld::DurationMilliseconds HalvedReportingIntervalMilliseconds = 500;

/** Cadence the commander alternates the sensor's reporting rate at. */
inline constexpr MicroWorld::DurationMilliseconds CommandIntervalMilliseconds = 10000;

/** Poll pace for both boards; far faster than any app cadence so the watchdog idle task runs. */
inline constexpr unsigned PollPacingMilliseconds = 20;

/** Server stamps command frames with node id 1; the client greets that id as its server. */
constexpr std::uint8_t ServerNodeId = 1;

/** Client stamps command frames with node id 2; the point-to-point wire never routes either id. */
constexpr std::uint8_t ClientNodeId = 2;

/** Fixed UART port and the two crossover data GPIOs, identical to examples 18, 19, and 23. */
constexpr std::int32_t UartPortNumber = 1;
constexpr std::int32_t TxGpioNumber = 17;
constexpr std::int32_t RxGpioNumber = 18;

/** A wire is fast, so 115200 baud. */
constexpr std::uint32_t UartBaudRate = 115200;

/** Protocol version both hosts advertise in Hello/Welcome, over both nets. */
constexpr std::uint8_t ProtocolVersion = 1;

/** SoftAP the server hosts and the client joins -- demo-only values, not a secret, so
 *  they commit safely; no home router and no real credentials are involved. */
constexpr const char* DemoApSsid = "microworld-ex24";

/** Demo WPA2 password (>= 8 chars); throwaway, never a real network's password. */
constexpr const char* DemoApPassword = "microworld";

/** The server's fixed SoftAP gateway IPv4; the client addresses this, no discovery needed. */
constexpr std::uint8_t ServerIpv4[4] = {192, 168, 4, 1};

/** UDP port the server binds and the client targets. */
constexpr std::uint16_t ServerPort = 40404;

/** Stable descriptor id for the managed FSensorActor type (0x0018 == example 24). */
constexpr MicroWorld::FTypeId SensorActorTypeId{0x00180001u};

/** Stable descriptor id for the managed FTelemetrySinkActor type. */
constexpr MicroWorld::FTypeId TelemetrySinkActorTypeId{0x00180002u};

/** Stable descriptor id for the managed FCommanderActor type. */
constexpr MicroWorld::FTypeId CommanderActorTypeId{0x00180003u};

/** The WiFi-backed network host both roles compose their telemetry channel through (256-byte packet, matching example 16). */
using FTelemetryNet = MicroWorld::TNetHost<2, 256>;

/** The wired network host both roles compose their commands channel through (120-byte wire, matching example 23). */
using FCommandNet = MicroWorld::TNetHost<2, 120>;

/** The one local actor-message router both roles compose, sized for this example's two channels and few handlers. */
using FWorldRouter = MicroWorld::TMessageRouter<16, 8, 96, 2>;

/** Adapts FTelemetryNet to the engine's per-frame network slot inside the frame set. */
using FTelemetryFrame = MicroWorld::TNetHostFrame<FTelemetryNet>;

/** Adapts FCommandNet to the engine's per-frame network slot inside the frame set. */
using FCommandFrame = MicroWorld::TNetHostFrame<FCommandNet>;

/** Two-way adapter binding the telemetry wire channel to the shared FWorldRouter. */
using FTelemetryBinding = MicroWorld::TMessageChannelBinding<FTelemetryNet>;

/** Two-way adapter binding the commands wire channel to the shared FWorldRouter. */
using FCommandBinding = MicroWorld::TMessageChannelBinding<FCommandNet>;

/** Pumps both net frames and the router behind the one INetworkFrame slot TEngineHost drives (Phase 4.1's D3 order). */
using FWorldFrameSet = MicroWorld::TNetworkFrameSet<3>;

/** The engine host both roles compose; sized for one world with a couple of small inline actors. */
using FWorldEngine = MicroWorld::TEngineHost<8, 16, 512, 16, 2, 4, 8, 64>;

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

/** Builds the shared session config; heartbeats keep each point-to-point peer alive between sends. */
inline MicroWorld::FNetHostConfig MakeHostConfig() noexcept
{
	MicroWorld::FNetHostConfig Config{};
	Config.HeartbeatIntervalMilliseconds = 1000;
	Config.PeerTimeoutMilliseconds = 5000;
	Config.ProtocolVersion = ProtocolVersion;
	return Config;
}

/**
 * Writes one 16-bit value little-endian into a caller-owned 2-byte payload.
 *
 * Every message this example sends carries a 2-byte LE value (a reading or an interval); this
 * mirrors Message.h's own private wire codec so the four call sites across both roles share one
 * encoding instead of repeating the bit-shifts.
 */
inline void EncodeUint16LittleEndian(const std::uint16_t Value, std::uint8_t* const OutBytes) noexcept
{
	OutBytes[0] = static_cast<std::uint8_t>(Value & 0xFFu);
	OutBytes[1] = static_cast<std::uint8_t>((Value >> 8) & 0xFFu);
}

/** Reads one little-endian 16-bit value from a caller-owned 2-byte payload; the counterpart to EncodeUint16LittleEndian. */
inline std::uint16_t DecodeUint16LittleEndian(const std::uint8_t* const Bytes) noexcept
{
	return static_cast<std::uint16_t>(static_cast<std::uint16_t>(Bytes[0]) | (static_cast<std::uint16_t>(Bytes[1]) << 8));
}
} // namespace Ex24
