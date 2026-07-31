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

using namespace MicroWorld;
using namespace MicroWorld::Messaging;
using namespace Ex24;

namespace
{
/** Single real-time source for the client board. */
FEsp32TimeSource GTimeSource{};

/**
 * Every reporting interval (starting at BaseReportingIntervalMilliseconds), broadcasts a 2-byte
 * synthetic reading over the telemetry (UDP) channel; re-times its own cadence when the server
 * commands a new rate over the commands (UART) channel.
 *
 * Takes the router by constructor injection (D9); this actor owns no components (AActor).
 */
class FSensorActor final : public AActor
{
public:
	/** Aligns this actor's own tick to the base reporting cadence and stores the injected router. */
	explicit FSensorActor(IMessageRouter& InRouter) noexcept
		: AActor(FTickConfiguration::EnabledEvery(BaseReportingIntervalMilliseconds)), Router(InRouter)
	{
	}

protected:
	/** Subscribes to SetReportingRateMessageId targeted at this actor's own SensorActorId. */
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

	/** Bumps the synthetic reading counter and broadcasts it as a 2-byte LE telemetry message. */
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
	 * Decodes the commanded 2-byte LE interval and re-times this actor's own reporting cadence.
	 *
	 * SetTickInterval is called from inside this handler, which runs during the engine's inbound
	 * network dispatch step -- before the world advance step ticks this same actor (TEngine::Tick's
	 * fixed frame order, EngineHost.h) -- so the new interval is already in effect the moment this
	 * frame's Tick would fire. FTickFunction::SetInterval (TickFunction.cpp) is a plain state mutation
	 * with no dispatch lock, so calling it mid-handler is safe.
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

	/** Router this actor sends and listens through; injected at construction (D9), never a global. */
	IMessageRouter& Router;

	/** Synthetic reading counter (ADR 0003 -- no GPIO/sensor peripheral); wraps at 65536, accepted for this demo. */
	std::uint16_t NextReading{0};
};
} // namespace

/**
 * Client board: joins the WiFi SoftAP and runs FSensorActor over the shared router owned by one
 * TNetworking. Its two client devices carry UDP telemetry and UART commands.
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
