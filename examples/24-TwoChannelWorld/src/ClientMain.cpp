#include "TwoChannelWorldShared.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Delegates/Delegate.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Networking/Networking.h>
#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Transport/Wifi/UdpAddressCodec.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>
#include <MicroWorld/Platform/Esp32/Esp32UartDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiLink.h>
#include <MicroWorld/Platform/Esp32/UartAddress.h>

#include <cstddef>
#include <cstdint>
#include <utility>

using namespace MicroWorld::Core;
using namespace MicroWorld::Platform::Esp32;
using namespace MicroWorld::Engine;
using namespace MicroWorld::Transport;
using namespace MicroWorld::Messaging;
using namespace Ex24;

namespace
{
/** Motivation: Single real-time source for the client board. */
FEsp32TimeSource GTimeSource{};

/**
 * Motivation: Every reporting interval, broadcasts a 2-byte synthetic reading over the telemetry (UDP)
 *   channel, and re-times its own cadence when the server commands a new rate over the commands (UART)
 *   channel. Takes the router by constructor injection and owns no components.
 * Responsibilities: Subscribe to SetReportingRate on play, tick on the reporting cadence, and broadcast
 *   the current reading; re-time the cadence on command.
 * Example:
 *   auto Sensor = Engine.CreateObject<FSensorActor>(SensorActorTypeId, Networking.GetRouter()).Object;
 *   Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Sensor});
 */
class FSensorActor final : public AActor
{
public:
	/**
	 * Motivation: Aligns this actor's own tick to the base reporting cadence and stores the injected router.
	 * Responsibilities: Construct on the base cadence and capture the router reference.
	 */
	explicit FSensorActor(IMessageRouter& InRouter) noexcept
		: AActor(FTickConfiguration::EnabledEvery(BaseReportingIntervalMilliseconds)), Router(InRouter)
	{
	}

protected:
	/**
	 * Motivation: Subscribes to SetReportingRateMessageId targeted at this actor's own SensorActorId, so a
	 *   later command can re-time this sensor.
	 * Responsibilities: Bind and register the reporting-rate handler under the sensor's actor id.
	 */
	void BeginPlay() noexcept override
	{
		FMessageHandlerBinding Handler;
		const EDelegateResult BindResult = Handler.Bind([this](const FMessageView& View) noexcept { this->OnReportingRateReceived(View); });
		if (BindResult != EDelegateResult::Success)
		{
			MW_LOG(Error, "ex24", "sensor handler bind failed");
			return;
		}

		// This unbounded example never removes handlers (the run never ends), so the returned handle is not retained.
		FMessageHandlerHandle HandlerHandle;
		const EMessageResult AddResult = Router.AddMessageHandler(SetReportingRateMessageId, SensorActorId, std::move(Handler), HandlerHandle);
		if (AddResult != EMessageResult::Success)
		{
			MW_LOG(Error, "ex24", "sensor handler registration failed");
		}
	}

	/**
	 * Motivation: Bumps the synthetic reading counter and broadcasts it, so the server receives one
	 *   telemetry reading per cadence tick.
	 * Responsibilities: Increment the reading, encode it little-endian, and broadcast it on the telemetry channel.
	 */
	void Tick(const FTickContext&) noexcept override
	{
		++NextReading;

		std::uint8_t Payload[2];
		EncodeUint16LittleEndian(NextReading, Payload);
		const EMessageResult SendResult =
			Router.BroadcastMessage(TelemetryChannelId, TelemetryReadingMessageId, SensorActorId, TSpan<const std::uint8_t>(Payload, 2));
		if (SendResult != EMessageResult::Success)
		{
			MW_LOG(Error, "ex24", "sensor telemetry broadcast failed");
			return;
		}
		MW_LOG(Log, "ex24", "tx telemetry reading=%u", static_cast<unsigned>(NextReading));
	}

private:
	/**
	 * Motivation: Decodes the commanded 2-byte LE interval and re-times this actor's own reporting cadence,
	 *   so a mid-frame command is in effect before this frame's Tick fires.
	 * Responsibilities: Validate the payload, decode the interval, and apply it via SetTickInterval.
	 */
	void OnReportingRateReceived(const FMessageView& View) noexcept
	{
		if (View.Payload.Size() < 2)
		{
			MW_LOG(Error, "ex24", "sensor received undersized rate payload");
			return;
		}
		const std::uint16_t Interval = DecodeUint16LittleEndian(View.Payload.Data());
		const ERuntimeResult Result = this->SetTickInterval(Interval);
		if (Result != ERuntimeResult::Success)
		{
			MW_LOG(Error, "ex24", "sensor reporting rate change rejected");
			return;
		}
		MW_LOG(Log, "ex24", "sensor reporting rate -> %u ms", static_cast<unsigned>(Interval));
	}

	/** Motivation: Router this actor sends and listens through; injected at construction, never a global. */
	IMessageRouter& Router;

	/** Motivation: Synthetic reading counter (ADR 0003 -- no GPIO/sensor peripheral); wraps at 65536, accepted for this demo. */
	std::uint16_t NextReading{0};
};
} // namespace

/**
 * Motivation: Lets Board B join the WiFi SoftAP and run FSensorActor over the shared router owned by one
 *   TNetworking, so the two-channel client (UDP telemetry + UART commands) composes in one place.
 * Responsibilities: Join the SoftAP, open the two devices, register them and their channels, spawn the
 *   sensor, start as a client, and tick the engine in an unbounded loop.
 */
void RunClient() noexcept
{
	static FEsp32WifiLink WifiLink;
	if (WifiLink.JoinAccessPoint(FEsp32StationConfig{DemoApSsid, DemoApPassword, /*ConnectTimeoutMilliseconds*/ 15000}) != ETransportResult::Success)
	{
		MW_LOG(Error, "ex24", "wifi failed; halting");
		return;
	}
	MW_LOG(Log, "ex24", "wifi joined AP");

	// The client binds an ephemeral local port (0): it only needs to reach the server, and TTransportHost
	// learns the client's address server-side from its Hello.
	static FEsp32WifiDevice TelemetryDevice(0);
	MW_LOG(Log, "ex24", "telemetry open=%d", TelemetryDevice.IsOpen() ? 1 : 0);
	if (!TelemetryDevice.IsOpen())
	{
		MW_LOG(Error, "ex24", "telemetry socket failed; halting");
		return;
	}

	static FEsp32UartDevice CommandDevice{MakeUartConfig(ClientNodeId)};
	MW_LOG(Log, "ex24", "commands node=%u open=%d", static_cast<unsigned>(ClientNodeId), CommandDevice.IsOpen() ? 1 : 0);
	if (!CommandDevice.IsOpen())
	{
		MW_LOG(Error, "ex24", "commands uart failed to open; halting");
		return;
	}

	FTransportHostConfig TelemetryConfig = MakeHostConfig();
	TelemetryConfig.ServerAddress = MakeUdpAddress(ServerIpv4[0], ServerIpv4[1], ServerIpv4[2], ServerIpv4[3], ServerPort);
	FTransportHostConfig CommandConfig = MakeHostConfig();
	CommandConfig.ServerAddress = MakeUartAddress(ServerNodeId);

	// TNetworking owns all hosts, bindings, and the shared router; the engine starts the hosts at BeginPlay.
	static FWorldNetworking Networking;
	const FDeviceHandle TelemetryHandle = Networking.AddDevice(TelemetryDevice, ENetworkMode::Client, TelemetryConfig);
	const FDeviceHandle CommandHandle = Networking.AddDevice(CommandDevice, ENetworkMode::Client, CommandConfig);
	if (!TelemetryHandle.IsValid() || !CommandHandle.IsValid())
	{
		MW_LOG(Error, "ex24", "client networking system rejected a device; halting");
		return;
	}
	const FChannelHandle TelemetryChannel = Networking.AddChannel(TelemetryHandle, TelemetryChannelId, EChannelReliability::BestEffort);
	const FChannelHandle CommandsChannel = Networking.AddChannel(CommandHandle, CommandsChannelId, EChannelReliability::BestEffort);
	if (!TelemetryChannel.IsValid() || !CommandsChannel.IsValid())
	{
		MW_LOG(Error, "ex24", "client networking system rejected a channel; halting");
		return;
	}
	static FWorldEngine Engine{FGarbageCollectionBudget{1, 4, 8}, Networking};

	if (Engine.RegisterClass<FSensorActor>(SensorActorTypeId, "SensorActor") != EObjectResult::Success)
	{
		MW_LOG(Error, "ex24", "client class registration failed; halting");
		return;
	}

	const TObjectPtr<UWorld> World = Engine.CreateWorld();
	const TObjectPtr<FSensorActor> Sensor = Engine.CreateObject<FSensorActor>(SensorActorTypeId, Networking.GetRouter()).Object;
	if (World.Get() == nullptr || Sensor.Get() == nullptr)
	{
		MW_LOG(Error, "ex24", "client world or actor creation failed; halting");
		return;
	}

	if (Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Sensor}) != EEngineResult::Success)
	{
		MW_LOG(Error, "ex24", "client actor registration failed; halting");
		return;
	}

	const TimePointMilliseconds BootTime = GTimeSource.Now();
	if (Engine.BeginPlay(BootTime) != ERuntimeResult::Success)
	{
		MW_LOG(Error, "ex24", "client engine begin play failed; halting");
		return;
	}
	MW_LOG(Log, "ex24", "client up (telemetry=UDP, commands=UART)");

	// This is a two-board two-channel demo, not a self-terminating trace (matching 16-TwoBoardUdp and
	// 23-TwoBoardWire's client), so the loop runs unbounded rather than stopping after N messages.
	for (;;)
	{
		(void)Engine.Tick(GTimeSource.Now());
		SleepMilliseconds(PollPacingMilliseconds);
	}
}
