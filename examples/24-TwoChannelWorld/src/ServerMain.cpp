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
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>
#include <MicroWorld/Platform/Esp32/Esp32UartDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiLink.h>

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
/** Motivation: Single real-time source for the server board. */
FEsp32TimeSource GTimeSource{};

/**
 * Motivation: Subscribes to the broadcast TelemetryReadingMessageId (arrives over UDP) and logs every
 *   reading, so the server's telemetry half of the two-channel demo is observable. Takes the router by
 *   constructor injection and never ticks.
 * Responsibilities: Register a broadcast handler on play and log each decoded reading.
 * Example:
 *   auto Sink = Engine.CreateObject<FTelemetrySinkActor>(TelemetrySinkActorTypeId, Router).Object;
 *   Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Sink});
 */
class FTelemetrySinkActor final : public AActor
{
public:
	/**
	 * Motivation: Stores the injected router; this actor's tick is disabled because it only reacts to a message.
	 * Responsibilities: Construct with tick disabled and capture the router reference.
	 */
	explicit FTelemetrySinkActor(IMessageRouter& InRouter) noexcept
		: AActor({/*bCanEverTick*/ false, /*bStartWithTickEnabled*/ false, /*TickIntervalMilliseconds*/ 0}), Router(InRouter)
	{
	}

protected:
	/**
	 * Motivation: Subscribes to every broadcast TelemetryReadingMessageId, so the sink receives each reading.
	 * Responsibilities: Bind and register the telemetry handler under the broadcast listener id.
	 */
	void BeginPlay() noexcept override
	{
		FMessageHandlerBinding Handler;
		const EDelegateResult BindResult = Handler.Bind([this](const FMessageView& View) noexcept { this->OnTelemetryReceived(View); });
		if (BindResult != EDelegateResult::Success)
		{
			MW_LOG(Error, "ex24", "telemetry sink handler bind failed");
			return;
		}

		// This unbounded example never removes handlers (the run never ends), so the returned handle is not retained.
		FMessageHandlerHandle HandlerHandle;
		const EMessageResult AddResult = Router.AddMessageHandler(TelemetryReadingMessageId, BroadcastActorId, std::move(Handler), HandlerHandle);
		if (AddResult != EMessageResult::Success)
		{
			MW_LOG(Error, "ex24", "telemetry sink handler registration failed");
		}
	}

private:
	/**
	 * Motivation: Decodes the 2-byte LE reading and logs it; this is the handler bound in BeginPlay.
	 * Responsibilities: Validate the payload, decode the reading, and log it.
	 */
	void OnTelemetryReceived(const FMessageView& View) noexcept
	{
		if (View.Payload.Size() < 2)
		{
			MW_LOG(Error, "ex24", "telemetry sink received undersized reading payload");
			return;
		}
		const std::uint16_t Reading = DecodeUint16LittleEndian(View.Payload.Data());
		MW_LOG(Log, "ex24", "rx telemetry reading=%u", static_cast<unsigned>(Reading));
	}

	/** Motivation: Router this actor listens through; injected at construction, never a global. */
	IMessageRouter& Router;
};

/**
 * Motivation: Every CommandIntervalMilliseconds, sends a targeted SetReportingRateMessageId (over UART)
 *   to the client's sensor actor, alternating its reporting interval between halved and base. Takes the
 *   router by constructor injection and owns no components.
 * Responsibilities: Tick on the command cadence and send one alternated reporting-rate command each tick.
 * Example:
 *   auto Commander = Engine.CreateObject<FCommanderActor>(CommanderActorTypeId, Router).Object;
 *   Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Commander});
 */
class FCommanderActor final : public AActor
{
public:
	/**
	 * Motivation: Aligns this actor's own tick to the command cadence and stores the injected router.
	 * Responsibilities: Construct on the command cadence and capture the router reference.
	 */
	explicit FCommanderActor(IMessageRouter& InRouter) noexcept
		: AActor(FTickConfiguration::EnabledEvery(CommandIntervalMilliseconds)), Router(InRouter)
	{
	}

protected:
	/**
	 * Motivation: Alternates the commanded interval each tick, so the sensor's rate swings between halved and base.
	 * Responsibilities: Flip the halved flag, encode the interval, and send one targeted command.
	 */
	void Tick(const FTickContext&) noexcept override
	{
		bHalved = !bHalved;
		const DurationMilliseconds Interval = bHalved ? HalvedReportingIntervalMilliseconds : BaseReportingIntervalMilliseconds;

		std::uint8_t Payload[2];
		EncodeUint16LittleEndian(static_cast<std::uint16_t>(Interval), Payload);
		const EMessageResult SendResult = Router.SendMessageToActor(
			CommandsChannelId, SetReportingRateMessageId, SensorActorId, CommanderActorId, TSpan<const std::uint8_t>(Payload, 2));
		if (SendResult != EMessageResult::Success)
		{
			MW_LOG(Error, "ex24", "commander send failed");
			return;
		}
		MW_LOG(Log, "ex24", "tx command -> sensor rate=%u ms", static_cast<unsigned>(Interval));
	}

private:
	/** Motivation: Router this actor sends through; injected at construction, never a global. */
	IMessageRouter& Router;

	/** Motivation: Alternates the commanded interval every tick; starts false so the first command halves the rate. */
	bool bHalved{false};
};
} // namespace

/**
 * Motivation: Lets Board A host the WiFi SoftAP and run FTelemetrySinkActor + FCommanderActor over the
 *   shared router owned by one TNetworking, so the two-channel server (UDP telemetry + UART commands)
 *   composes in one place.
 * Responsibilities: Host the SoftAP, open the two devices, register them and their channels, spawn the
 *   actors, start as a dedicated server, and tick the engine in an unbounded loop.
 */
void RunServer() noexcept
{
	static FEsp32WifiLink WifiLink;
	if (WifiLink.StartAccessPoint(FEsp32AccessPointConfig{DemoApSsid, DemoApPassword, /*WifiChannel*/ 1, /*MaxStations*/ 4})
		!= ETransportResult::Success)
	{
		MW_LOG(Error, "ex24", "wifi failed; halting");
		return;
	}
	MW_LOG(Log, "ex24", "wifi softap up, gateway 192.168.4.1");

	// The device is constructed only after WiFi/netif is up (lwIP must exist first).
	static FEsp32WifiDevice TelemetryDevice(ServerPort);
	MW_LOG(Log, "ex24", "telemetry open=%d udp_port=%u", TelemetryDevice.IsOpen() ? 1 : 0, static_cast<unsigned>(TelemetryDevice.BoundPort()));
	if (!TelemetryDevice.IsOpen())
	{
		MW_LOG(Error, "ex24", "telemetry socket failed; halting");
		return;
	}

	static FEsp32UartDevice CommandDevice{MakeUartConfig(ServerNodeId)};
	MW_LOG(Log, "ex24", "commands node=%u open=%d", static_cast<unsigned>(ServerNodeId), CommandDevice.IsOpen() ? 1 : 0);
	if (!CommandDevice.IsOpen())
	{
		MW_LOG(Error, "ex24", "commands uart failed to open; halting");
		return;
	}

	// TNetworking owns all hosts, bindings, and the shared router; the engine starts the hosts at BeginPlay.
	static FWorldNetworking Networking;
	const FDeviceHandle TelemetryHandle = Networking.AddDevice(TelemetryDevice, ENetworkMode::DedicatedServer, MakeHostConfig());
	const FDeviceHandle CommandHandle = Networking.AddDevice(CommandDevice, ENetworkMode::DedicatedServer, MakeHostConfig());
	if (!TelemetryHandle.IsValid() || !CommandHandle.IsValid())
	{
		MW_LOG(Error, "ex24", "server networking system rejected a device; halting");
		return;
	}
	const FChannelHandle TelemetryChannel = Networking.AddChannel(TelemetryHandle, TelemetryChannelId, EChannelReliability::BestEffort);
	const FChannelHandle CommandsChannel = Networking.AddChannel(CommandHandle, CommandsChannelId, EChannelReliability::BestEffort);
	if (!TelemetryChannel.IsValid() || !CommandsChannel.IsValid())
	{
		MW_LOG(Error, "ex24", "server networking system rejected a channel; halting");
		return;
	}
	static FWorldEngine Engine{FGarbageCollectionBudget{1, 4, 8}, Networking};

	if (Engine.RegisterClass<FTelemetrySinkActor>(TelemetrySinkActorTypeId, "TelemetrySinkActor") != EObjectResult::Success
		|| Engine.RegisterClass<FCommanderActor>(CommanderActorTypeId, "CommanderActor") != EObjectResult::Success)
	{
		MW_LOG(Error, "ex24", "server class registration failed; halting");
		return;
	}

	const TObjectPtr<UWorld> World = Engine.CreateWorld();
	const TObjectPtr<FTelemetrySinkActor> Sink = Engine.CreateObject<FTelemetrySinkActor>(TelemetrySinkActorTypeId, Networking.GetRouter()).Object;
	const TObjectPtr<FCommanderActor> Commander = Engine.CreateObject<FCommanderActor>(CommanderActorTypeId, Networking.GetRouter()).Object;
	if (World.Get() == nullptr || Sink.Get() == nullptr || Commander.Get() == nullptr)
	{
		MW_LOG(Error, "ex24", "server world or actor creation failed; halting");
		return;
	}

	if (Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Sink}) != EEngineResult::Success
		|| Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Commander}) != EEngineResult::Success)
	{
		MW_LOG(Error, "ex24", "server actor registration failed; halting");
		return;
	}

	const TimePointMilliseconds BootTime = GTimeSource.Now();
	if (Engine.BeginPlay(BootTime) != ERuntimeResult::Success)
	{
		MW_LOG(Error, "ex24", "server engine begin play failed; halting");
		return;
	}
	MW_LOG(Log, "ex24", "server up (telemetry=UDP, commands=UART)");

	// This is a two-board two-channel demo, not a self-terminating trace (matching 16-TwoBoardUdp and
	// 23-TwoBoardWire's server), so the loop runs unbounded rather than stopping after N messages.
	for (;;)
	{
		(void)Engine.Tick(GTimeSource.Now());
		SleepMilliseconds(PollPacingMilliseconds);
	}
}
