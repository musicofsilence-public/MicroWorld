#pragma once

#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Integration/NetSystem.h>
#include <MicroWorld/Net/UdpAddressCodec.h>
#include <MicroWorld/Object/ClassDescriptor.h>
#include <MicroWorld/PlatformEsp32/Esp32UartDriver.h>
#include <MicroWorld/PlatformEsp32/Esp32UdpDriver.h>
#include <MicroWorld/PlatformEsp32/Esp32WifiLink.h>

#include <cstddef>
#include <cstdint>

/**
 * Shared protocol ids, config builders, and composition-type aliases for example
 * 24's two roles.
 *
 * Both role translation units (ServerMain.cpp, ClientMain.cpp) include this so the
 * message/actor/channel ids, UART/WiFi/UDP configuration, and the TNetSystem
 * and engine shapes are defined exactly once -- DRY within this one example.
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

/** Sizes the one TNetSystem for two links, two router channels, and the example's existing 96-byte messages. */
struct FWorldNetSystemTraits : MicroWorld::FDefaultNetSystemTraits
{
	/** The example configures one UDP and one UART driver. */
	static constexpr std::size_t MaxNetDrivers = 2;

	/** The shared router owns exactly the telemetry and command channels. */
	static constexpr std::size_t MaxRouterChannels = 2;

	/** The composition front door accepts exactly the telemetry and command channels. */
	static constexpr std::size_t MaxChannels = 2;
};

/** The one networked engine system both roles compose before their engine begins play. */
using FWorldNetSystem = MicroWorld::TNetSystem<FWorldNetSystemTraits>;

/** The engine both roles compose; sized for one world with a couple of small inline actors (the default ESP32-S3 traits). */
using FWorldEngine = MicroWorld::TEngine<>;

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
