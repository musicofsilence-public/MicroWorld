#pragma once

#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Networking/Networking.h>
#include <MicroWorld/Transport/Wifi/UdpAddressCodec.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Platform/Esp32/Esp32UartDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiLink.h>

#include <cstddef>
#include <cstdint>

/**
 * Motivation: Holds the shared protocol ids, config builders, and composition-type aliases for
 *   example 24's two roles, so both translation units (ServerMain.cpp, ClientMain.cpp) define the
 *   message/actor/channel ids, UART/WiFi/UDP configuration, and the TNetworking and engine shapes
 *   exactly once (DRY).
 */
namespace Ex24
{
/** Motivation: Broadcast message id: client sensor -> server, 2-byte LE reading, delivered over UDP. */
inline constexpr MicroWorld::Messaging::FMessageTypeId TelemetryReadingMessageId = 1;

/** Motivation: Targeted message id: server -> client sensor, 2-byte LE interval milliseconds, delivered over UART. */
inline constexpr MicroWorld::Messaging::FMessageTypeId SetReportingRateMessageId = 2;

/** Motivation: Actor id FSensorActor registers its SetReportingRate handler under and the commander targets. */
inline constexpr MicroWorld::Messaging::FMessageActorId SensorActorId = 10;

/** Motivation: Actor id recorded as the telemetry sink's sender; nothing ever targets a send at it. */
inline constexpr MicroWorld::Messaging::FMessageActorId TelemetrySinkActorId = 11;

/** Motivation: Actor id recorded as the commander's sender; nothing ever targets a send at it. */
inline constexpr MicroWorld::Messaging::FMessageActorId CommanderActorId = 12;

/** Motivation: Router-facing channel id both roles register their UDP telemetry binding under. */
inline constexpr MicroWorld::Messaging::FMessageChannelId TelemetryChannelId = 1;

/** Motivation: Router-facing channel id both roles register their UART commands binding under. */
inline constexpr MicroWorld::Messaging::FMessageChannelId CommandsChannelId = 2;

/** Motivation: The sensor's start/restored reporting cadence. */
inline constexpr MicroWorld::Core::DurationMilliseconds BaseReportingIntervalMilliseconds = 1000;

/** Motivation: The sensor's "halved" reporting cadence, alternated with the base cadence by the commander. */
inline constexpr MicroWorld::Core::DurationMilliseconds HalvedReportingIntervalMilliseconds = 500;

/** Motivation: Cadence the commander alternates the sensor's reporting rate at. */
inline constexpr MicroWorld::Core::DurationMilliseconds CommandIntervalMilliseconds = 10000;

/** Motivation: Poll pace for both boards; far faster than any app cadence so the watchdog idle task runs. */
inline constexpr unsigned PollPacingMilliseconds = 20;

/** Motivation: Server stamps command frames with node id 1; the client greets that id as its server. */
constexpr std::uint8_t ServerNodeId = 1;

/** Motivation: Client stamps command frames with node id 2; the point-to-point wire never routes either id. */
constexpr std::uint8_t ClientNodeId = 2;

/** Motivation: Fixed UART port and the two crossover data GPIOs, identical to examples 18, 19, and 23. */
constexpr std::int32_t UartPortNumber = 1;
constexpr std::int32_t TxGpioNumber = 17;
constexpr std::int32_t RxGpioNumber = 18;

/** Motivation: A wire is fast, so 115200 baud. */
constexpr std::uint32_t UartBaudRate = 115200;

/** Motivation: Protocol version both hosts advertise in Hello/Welcome, over both transports. */
constexpr std::uint8_t ProtocolVersion = 1;

/** Motivation: SoftAP the server hosts and the client joins -- demo-only values, not a secret, so
 *  they commit safely; no home router and no real credentials are involved. */
constexpr const char* DemoApSsid = "microworld-ex24";

/** Motivation: Demo WPA2 password (>= 8 chars); throwaway, never a real network's password. */
constexpr const char* DemoApPassword = "microworld";

/** Motivation: The server's fixed SoftAP gateway IPv4; the client addresses this, no discovery needed. */
constexpr std::uint8_t ServerIpv4[4] = {192, 168, 4, 1};

/** Motivation: UDP port the server binds and the client targets. */
constexpr std::uint16_t ServerPort = 40404;

/** Motivation: Stable descriptor id for the managed FSensorActor type (0x0018 == example 24). */
constexpr MicroWorld::Engine::FTypeId SensorActorTypeId{0x00180001u};

/** Motivation: Stable descriptor id for the managed FTelemetrySinkActor type. */
constexpr MicroWorld::Engine::FTypeId TelemetrySinkActorTypeId{0x00180002u};

/** Motivation: Stable descriptor id for the managed FCommanderActor type. */
constexpr MicroWorld::Engine::FTypeId CommanderActorTypeId{0x00180003u};

/**
 * Motivation: Sizes the one TNetworking for two links, two router channels, and the example's existing
 *   96-byte messages, so the composition fits without restating the bounds in each role.
 * Responsibilities: Name the device, router-channel, and channel capacities the world networking uses.
 * Example:
 *   using FWorldNetworking = MicroWorld::TNetworking<FWorldNetworkingTraits>;
 */
struct FWorldNetworkingTraits : MicroWorld::FDefaultNetworkingTraits
{
	/** Motivation: The example configures one UDP and one UART device. */
	static constexpr std::size_t MaxDevices = 2;

	/** Motivation: The shared router owns exactly the telemetry and command channels. */
	static constexpr std::size_t MaxRouterChannels = 2;

	/** Motivation: The composition front door accepts exactly the telemetry and command channels. */
	static constexpr std::size_t MaxChannels = 2;
};

/** Motivation: The one networked engine system both roles compose before their engine begins play. */
using FWorldNetworking = MicroWorld::TNetworking<FWorldNetworkingTraits>;

/** Motivation: The engine both roles compose; sized for one world with a couple of small actors using direct component storage. */
using FWorldEngine = MicroWorld::Engine::TEngine<>;

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
 *   each point-to-point peer alive between sends without each role restating the values.
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
 * Motivation: Lets the four call sites across both roles share one 2-byte little-endian encoding,
 *   mirroring Message.h's private codec so the bit-shifts are not repeated.
 * Responsibilities: Write the low byte then the high byte into the caller's 2-byte buffer.
 */
inline void EncodeUint16LittleEndian(const std::uint16_t Value, std::uint8_t* const OutBytes) noexcept
{
	OutBytes[0] = static_cast<std::uint8_t>(Value & 0xFFu);
	OutBytes[1] = static_cast<std::uint8_t>((Value >> 8) & 0xFFu);
}

/**
 * Motivation: Lets both roles read one little-endian 16-bit value from a 2-byte payload, mirroring the
 *   encoder so the two halves agree on the format.
 * Responsibilities: Reassemble the value low-byte-first from the caller's 2-byte buffer.
 */
inline std::uint16_t DecodeUint16LittleEndian(const std::uint8_t* const Bytes) noexcept
{
	return static_cast<std::uint16_t>(static_cast<std::uint16_t>(Bytes[0]) | (static_cast<std::uint16_t>(Bytes[1]) << 8));
}
} // namespace Ex24
