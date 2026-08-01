#include "TwoChannelWorldShared.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Delegates/Delegate.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/World.h>
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
 * Motivation: Logs each client telemetry reading received over the UDP channel.
 * Responsibilities: Register a message-filtered subscription on play and log each decoded reading.
 * Example:
 *   auto Sink = Engine.CreateObject<FTelemetrySinkActor>(TelemetrySinkActorTypeId, Messaging).Object;
 *   Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Sink});
 */
class FTelemetrySinkActor final : public AActor
{
public:
	/**
	 * Motivation: Stores Messaging while tick stays disabled because this actor only reacts to messages.
	 * Responsibilities: Construct with tick disabled and capture the Messaging system reference.
	 */
	explicit FTelemetrySinkActor(FMessagingSystem& InMessaging) noexcept
		: AActor({/*bCanEverTick*/ false, /*bStartWithTickEnabled*/ false, /*TickIntervalMilliseconds*/ 0}), Messaging(InMessaging)
	{
	}

protected:
	/**
	 * Motivation: Subscribes to telemetry readings so the sink receives each one.
	 * Responsibilities: Bind and register a subscriber with this actor's weak owner.
	 */
	void BeginPlay() noexcept override
	{
		FMessagingSystem::FSubscriberDelegate Subscriber;
		const EDelegateResult BindResult = Subscriber.Bind([this](const FMessage& Message) noexcept { this->OnTelemetryReceived(Message); });
		if (BindResult != EDelegateResult::Success)
		{
			MW_LOG(Error, "ex24", "telemetry sink subscriber bind failed");
			return;
		}

		FObjectStore* const ObjectStore = GetObjectStore();
		if (ObjectStore == nullptr)
		{
			MW_LOG(Error, "ex24", "telemetry sink has no object store");
			return;
		}

		const EMessagingResult SubscribeResult = Messaging.SubscribeToChannel(
			TelemetryChannelName, TelemetryReadingMessageName, std::move(Subscriber), MakeWeakOwner(*ObjectStore, GetObjectHandle()));
		if (SubscribeResult != EMessagingResult::Success)
		{
			MW_LOG(Error, "ex24", "telemetry sink subscription failed");
		}
	}

private:
	/**
	 * Motivation: Decodes the 2-byte LE reading from the subscriber bound in BeginPlay.
	 * Responsibilities: Validate the payload, decode the reading, and log it.
	 */
	void OnTelemetryReceived(const FMessage& Message) noexcept
	{
		const TSpan<const std::uint8_t> Payload = Message.GetPayload();
		if (Payload.Size() < 2)
		{
			MW_LOG(Error, "ex24", "telemetry sink received undersized reading payload");
			return;
		}
		const std::uint16_t Reading = DecodeUint16LittleEndian(Payload.Data());
		MW_LOG(Log, "ex24", "rx telemetry reading=%u", static_cast<unsigned>(Reading));
	}

	/** Motivation: Messaging system this actor listens through; injected at construction, never a global. */
	FMessagingSystem& Messaging;
};

/**
 * Motivation: Alternates the remote sensor's reporting rate through the point-to-point UART channel.
 * Responsibilities: Tick on the command cadence and send one named reporting-rate command each tick.
 * Example:
 *   auto Commander = Engine.CreateObject<FCommanderActor>(CommanderActorTypeId, Messaging).Object;
 *   Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Commander});
 */
class FCommanderActor final : public AActor
{
public:
	/**
	 * Motivation: Aligns this actor's own tick to the command cadence and stores injected Messaging.
	 * Responsibilities: Construct on the command cadence and capture the Messaging system reference.
	 */
	explicit FCommanderActor(FMessagingSystem& InMessaging) noexcept
		: AActor(FTickConfiguration::EnabledEvery(CommandIntervalMilliseconds)), Messaging(InMessaging)
	{
	}

protected:
	/**
	 * Motivation: Alternates the commanded interval each tick, so the sensor's rate swings between halved and base.
	 * Responsibilities: Flip the halved flag, encode the interval, and send one named channel command.
	 */
	void Tick(const FTickContext&) noexcept override
	{
		bHalved = !bHalved;
		const DurationMilliseconds Interval = bHalved ? HalvedReportingIntervalMilliseconds : BaseReportingIntervalMilliseconds;

		std::uint8_t PayloadBytes[2];
		EncodeUint16LittleEndian(static_cast<std::uint16_t>(Interval), PayloadBytes);
		FMessage Message;
		Message.SetMessageNameId(SetReportingRateMessageName);
		Message.SetPayload(TSpan<const std::uint8_t>(PayloadBytes, sizeof(PayloadBytes)));
		// The UART channel is point-to-point, so its message name already identifies the remote sensor without actor addressing.
		const EMessagingResult SendResult = Messaging.SendMessageToChannel(Message, CommandsChannelName);
		if (SendResult != EMessagingResult::Success)
		{
			MW_LOG(Error, "ex24", "commander send failed");
			return;
		}
		MW_LOG(Log, "ex24", "tx command -> sensor rate=%u ms", static_cast<unsigned>(Interval));
	}

private:
	/** Motivation: Messaging system this actor sends through; injected at construction, never a global. */
	FMessagingSystem& Messaging;

	/** Motivation: Alternates the commanded interval every tick; starts false so the first command halves the rate. */
	bool bHalved{false};
};
} // namespace

/**
 * Motivation: Composes the SoftAP server with UDP telemetry and UART commands in one engine.
 * Responsibilities: Open devices, create the Messaging channels, spawn the actors, and tick without stopping.
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

	static FWorldEngine Engine{FGarbageCollectionBudget{1, 4, 8}};
	if (Engine.CreateMessagingSystem(FMessagingSystemInformation{}) != ERuntimeResult::Success)
	{
		MW_LOG(Error, "ex24", "server Messaging system creation failed; halting");
		return;
	}
	FMessagingSystem* const Messaging = Engine.GetMessagingSystem();
	if (Messaging == nullptr)
	{
		MW_LOG(Error, "ex24", "server Messaging system unavailable; halting");
		return;
	}
	const EMessagingResult TelemetryChannelResult = Messaging->CreateChannel({TelemetryChannelName, false, &TelemetryDevice, {}});
	// UART is point-to-point, so its device ignores this empty destination address.
	const EMessagingResult CommandsChannelResult = Messaging->CreateChannel({CommandsChannelName, false, &CommandDevice, {}});
	if (TelemetryChannelResult != EMessagingResult::Success || CommandsChannelResult != EMessagingResult::Success)
	{
		MW_LOG(Error, "ex24", "server Messaging channel creation failed; halting");
		return;
	}

	if (Engine.RegisterClass<FTelemetrySinkActor>(TelemetrySinkActorTypeId, "TelemetrySinkActor") != EObjectResult::Success
		|| Engine.RegisterClass<FCommanderActor>(CommanderActorTypeId, "CommanderActor") != EObjectResult::Success)
	{
		MW_LOG(Error, "ex24", "server class registration failed; halting");
		return;
	}

	const TObjectPtr<UWorld> World = Engine.CreateWorld();
	const TObjectPtr<FTelemetrySinkActor> Sink = Engine.CreateObject<FTelemetrySinkActor>(TelemetrySinkActorTypeId, *Messaging).Object;
	const TObjectPtr<FCommanderActor> Commander = Engine.CreateObject<FCommanderActor>(CommanderActorTypeId, *Messaging).Object;
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
