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
#include <MicroWorld/Engine/EngineSystem.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Log.h>
#include <MicroWorld/Net/NetHost.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/Net/UdpAddressCodec.h>
#include <MicroWorld/Object/ClassDescriptor.h>
#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/ObjectPtr.h>
#include <MicroWorld/PlatformEsp32/Esp32Sleep.h>
#include <MicroWorld/PlatformEsp32/Esp32TimeSource.h>
#include <MicroWorld/PlatformEsp32/Esp32UartDriver.h>
#include <MicroWorld/PlatformEsp32/Esp32UdpDriver.h>
#include <MicroWorld/PlatformEsp32/Esp32WifiLink.h>
#include <MicroWorld/PlatformEsp32/UartAddress.h>

#include <cstddef>
#include <cstdint>
#include <utility>

using namespace MicroWorld;
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
 * Takes the router by constructor injection (D9); this actor owns no components (TInlineActor<0>).
 */
class FSensorActor final : public TInlineActor<0>
{
public:
	/** Aligns this actor's own tick to the base reporting cadence and stores the injected router. */
	explicit FSensorActor(IMessageRouter& InRouter) noexcept
		: TInlineActor<0>(FTickConfiguration::EnabledEvery(BaseReportingIntervalMilliseconds)), Router(InRouter)
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
 * Client board: joins the WiFi SoftAP and runs FSensorActor over one TMessageRouter wired to two
 * nets (UDP telemetry, UART commands) through two TMessageChannelBinding, with the engine holding
 * both net frames and the router behind one TEngineSystemSet<3>.
 */
void RunClient() noexcept
{
	static FEsp32WifiLink WifiLink;
	if (WifiLink.JoinAccessPoint(FEsp32StationConfig{DemoApSsid, DemoApPassword, /*ConnectTimeoutMilliseconds*/ 15000}) != ENetResult::Success)
	{
		MW_LOG(Error, "ex24", "wifi failed; halting");
		return;
	}
	MW_LOG(Log, "ex24", "wifi joined AP");

	// The client binds an ephemeral local port (0): it only needs to reach the server, and TNetHost
	// learns the client's address server-side from its Hello.
	static FEsp32UdpDriver TelemetryDriver(0);
	MW_LOG(Log, "ex24", "telemetry open=%d", TelemetryDriver.IsOpen() ? 1 : 0);
	if (!TelemetryDriver.IsOpen())
	{
		MW_LOG(Error, "ex24", "telemetry socket failed; halting");
		return;
	}

	static FEsp32UartDriver CommandDriver{MakeUartConfig(ClientNodeId)};
	MW_LOG(Log, "ex24", "commands node=%u open=%d", static_cast<unsigned>(ClientNodeId), CommandDriver.IsOpen() ? 1 : 0);
	if (!CommandDriver.IsOpen())
	{
		MW_LOG(Error, "ex24", "commands uart failed to open; halting");
		return;
	}

	// All composition objects are static (the ESP32-S3 stack lesson, §2.2).
	static FTelemetryNet TelemetryNet{TelemetryDriver};
	static FCommandNet CommandNet{CommandDriver};
	static FWorldRouter Router;
	static FTelemetryBinding TelemetryWire{TelemetryNet, TelemetryWireChannelByte, TelemetryChannelId, EChannelSendTarget::Server, Router};
	static FCommandBinding CommandWire{CommandNet, CommandsWireChannelByte, CommandsChannelId, EChannelSendTarget::Server, Router};
	static FTelemetryFrame TelemetryFrame{TelemetryNet};
	static FCommandFrame CommandFrame{CommandNet};

	// D3 frame-set order: nets first (each delivers its own inbound traffic), router last (it then
	// dispatches what the nets just delivered) -- see TwoChannelWorldShared.h's FWorldFrameSet alias.
	static FWorldFrameSet Frames;
	if (Frames.Add(TelemetryFrame) != EEngineResult::Success || Frames.Add(CommandFrame) != EEngineResult::Success
		|| Frames.Add(Router) != EEngineResult::Success)
	{
		MW_LOG(Error, "ex24", "client frame set rejected a frame; halting");
		return;
	}
	static FWorldEngine Engine{FGarbageCollectionBudget{1, 4, 8}, Frames};

	if (!TelemetryWire.IsAttached() || !CommandWire.IsAttached())
	{
		MW_LOG(Error, "ex24", "client binding failed to attach; halting");
		return;
	}
	if (Router.AddChannel(TelemetryWire) != EMessageResult::Success || Router.AddChannel(CommandWire) != EMessageResult::Success)
	{
		MW_LOG(Error, "ex24", "client router rejected a channel; halting");
		return;
	}

	if (Engine.RegisterClass<FSensorActor>(SensorActorTypeId, "SensorActor") != EObjectResult::Success)
	{
		MW_LOG(Error, "ex24", "client class registration failed; halting");
		return;
	}

	const TObjectPtr<UWorld> World = Engine.CreateWorld();
	const TObjectPtr<FSensorActor> Sensor = Engine.CreateObject<FSensorActor>(SensorActorTypeId, Router).Object;
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

	FNetHostConfig TelemetryConfig = MakeHostConfig();
	TelemetryConfig.ServerAddress = MakeUdpAddress(ServerIpv4[0], ServerIpv4[1], ServerIpv4[2], ServerIpv4[3], ServerPort);
	(void)TelemetryNet.Configure(ENetMode::Client, TelemetryConfig);
	(void)TelemetryNet.Start(GTimeSource.Now());

	FNetHostConfig CommandConfig = MakeHostConfig();
	CommandConfig.ServerAddress = MakeUartAddress(ServerNodeId);
	(void)CommandNet.Configure(ENetMode::Client, CommandConfig);
	(void)CommandNet.Start(GTimeSource.Now());

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
