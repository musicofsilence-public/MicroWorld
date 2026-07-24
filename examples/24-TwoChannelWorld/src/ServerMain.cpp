#include "TwoChannelWorldShared.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Delegates/Delegate.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/InlineTypes.h>
#include <MicroWorld/Engine/Message.h>
#include <MicroWorld/Engine/MessageChannelBinding.h>
#include <MicroWorld/Engine/MessageRouter.h>
#include <MicroWorld/Engine/NetworkFrame.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Log.h>
#include <MicroWorld/Net/NetHost.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/Object/ClassDescriptor.h>
#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/ObjectPtr.h>
#include <MicroWorld/PlatformEsp32/Esp32Sleep.h>
#include <MicroWorld/PlatformEsp32/Esp32TimeSource.h>
#include <MicroWorld/PlatformEsp32/Esp32UartDriver.h>
#include <MicroWorld/PlatformEsp32/Esp32UdpDriver.h>
#include <MicroWorld/PlatformEsp32/Esp32WifiLink.h>

#include <cstddef>
#include <cstdint>
#include <utility>

using namespace MicroWorld;
using namespace Ex24;

namespace
{
/** Single real-time source for the server board. */
FEsp32TimeSource GTimeSource{};

/**
 * Subscribes to the broadcast TelemetryReadingMessageId (arrives over UDP) and logs every reading.
 *
 * Takes the router by constructor injection (D9); never ticks, since it only reacts to a message.
 */
class FTelemetrySinkActor final : public TInlineActor<0>
{
public:
	/** Stores the injected router; this actor's tick is disabled, matching FDisplayActor in example 23. */
	explicit FTelemetrySinkActor(IMessageRouter& InRouter) noexcept
		: TInlineActor<0>({/*bCanEverTick*/ false, /*bStartWithTickEnabled*/ false, /*TickIntervalMilliseconds*/ 0}), Router(InRouter)
	{
	}

protected:
	/** Subscribes to every broadcast TelemetryReadingMessageId (BroadcastActorId receives broadcasts). */
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
	/** Decodes the 2-byte LE reading and logs it; this is the handler bound in BeginPlay. */
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

	/** Router this actor listens through; injected at construction (D9), never a global. */
	IMessageRouter& Router;
};

/**
 * Every CommandIntervalMilliseconds, sends a targeted SetReportingRateMessageId (over UART) to the
 * client's sensor actor, alternating its reporting interval between halved and base.
 *
 * Takes the router by constructor injection (D9); this actor owns no components (TInlineActor<0>).
 */
class FCommanderActor final : public TInlineActor<0>
{
public:
	/** Aligns this actor's own tick to the command cadence and stores the injected router. */
	explicit FCommanderActor(IMessageRouter& InRouter) noexcept
		: TInlineActor<0>(FTickConfiguration::EnabledEvery(CommandIntervalMilliseconds)), Router(InRouter)
	{
	}

protected:
	/** Flips bHalved and sends the resulting interval to SensorActorId as a 2-byte LE targeted message. */
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
	/** Router this actor sends through; injected at construction (D9), never a global. */
	IMessageRouter& Router;

	/** Alternates the commanded interval every tick; starts false so the first command halves the rate. */
	bool bHalved{false};
};
} // namespace

/**
 * Server board: hosts the WiFi SoftAP and runs FTelemetrySinkActor + FCommanderActor over one
 * TMessageRouter wired to two nets (UDP telemetry, UART commands) through two
 * TMessageChannelBinding, with the engine holding both net frames and the router behind one
 * TNetworkFrameSet<3> (Phase 4.1; see AGENTS.md for why this is the first example to use it).
 */
void RunServer() noexcept
{
	static FEsp32WifiLink WifiLink;
	if (WifiLink.StartAccessPoint(FEsp32AccessPointConfig{DemoApSsid, DemoApPassword, /*WifiChannel*/ 1, /*MaxStations*/ 4}) != ENetResult::Success)
	{
		MW_LOG(Error, "ex24", "wifi failed; halting");
		return;
	}
	MW_LOG(Log, "ex24", "wifi softap up, gateway 192.168.4.1");

	// The driver is constructed only after WiFi/netif is up (lwIP must exist first).
	static FEsp32UdpDriver TelemetryDriver(ServerPort);
	MW_LOG(Log, "ex24", "telemetry open=%d udp_port=%u", TelemetryDriver.IsOpen() ? 1 : 0, static_cast<unsigned>(TelemetryDriver.BoundPort()));
	if (!TelemetryDriver.IsOpen())
	{
		MW_LOG(Error, "ex24", "telemetry socket failed; halting");
		return;
	}

	static FEsp32UartDriver CommandDriver{MakeUartConfig(ServerNodeId)};
	MW_LOG(Log, "ex24", "commands node=%u open=%d", static_cast<unsigned>(ServerNodeId), CommandDriver.IsOpen() ? 1 : 0);
	if (!CommandDriver.IsOpen())
	{
		MW_LOG(Error, "ex24", "commands uart failed to open; halting");
		return;
	}

	// All composition objects are static (the ESP32-S3 stack lesson, §2.2).
	static FTelemetryNet TelemetryNet{TelemetryDriver};
	static FCommandNet CommandNet{CommandDriver};
	static FWorldRouter Router;
	static FTelemetryBinding TelemetryWire{TelemetryNet, TelemetryWireChannelByte, TelemetryChannelId, EChannelSendTarget::AllPeers, Router};
	static FCommandBinding CommandWire{CommandNet, CommandsWireChannelByte, CommandsChannelId, EChannelSendTarget::AllPeers, Router};
	static FTelemetryFrame TelemetryFrame{TelemetryNet};
	static FCommandFrame CommandFrame{CommandNet};

	// D3 frame-set order: nets first (each delivers its own inbound traffic), router last (it then
	// dispatches what the nets just delivered) -- see TwoChannelWorldShared.h's FWorldFrameSet alias.
	static FWorldFrameSet Frames;
	if (Frames.Add(TelemetryFrame) != EEngineResult::Success || Frames.Add(CommandFrame) != EEngineResult::Success
		|| Frames.Add(Router) != EEngineResult::Success)
	{
		MW_LOG(Error, "ex24", "server frame set rejected a frame; halting");
		return;
	}
	static FWorldEngine Engine{FGarbageCollectionBudget{1, 4, 8}, Frames};

	if (!TelemetryWire.IsAttached() || !CommandWire.IsAttached())
	{
		MW_LOG(Error, "ex24", "server binding failed to attach; halting");
		return;
	}
	if (Router.AddChannel(TelemetryWire) != EMessageResult::Success || Router.AddChannel(CommandWire) != EMessageResult::Success)
	{
		MW_LOG(Error, "ex24", "server router rejected a channel; halting");
		return;
	}

	if (Engine.RegisterClass<FTelemetrySinkActor>(TelemetrySinkActorTypeId, "TelemetrySinkActor") != EObjectResult::Success
		|| Engine.RegisterClass<FCommanderActor>(CommanderActorTypeId, "CommanderActor") != EObjectResult::Success)
	{
		MW_LOG(Error, "ex24", "server class registration failed; halting");
		return;
	}

	const TObjectPtr<UWorld> World = Engine.CreateWorld();
	const TObjectPtr<FTelemetrySinkActor> Sink = Engine.CreateObject<FTelemetrySinkActor>(TelemetrySinkActorTypeId, Router).Object;
	const TObjectPtr<FCommanderActor> Commander = Engine.CreateObject<FCommanderActor>(CommanderActorTypeId, Router).Object;
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

	(void)TelemetryNet.Configure(ENetMode::DedicatedServer, MakeHostConfig());
	(void)TelemetryNet.Start(GTimeSource.Now());
	(void)CommandNet.Configure(ENetMode::DedicatedServer, MakeHostConfig());
	(void)CommandNet.Start(GTimeSource.Now());

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
