#include "TwoChannelWorldShared.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Delegates/Delegate.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Transport/Wifi/UdpAddressCodec.h>
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
/** Motivation: Single real-time source for the client board. */
FEsp32TimeSource GTimeSource{};

/**
 * Motivation: Sends UDP readings and re-times itself when the UART channel delivers a rate command.
 * Responsibilities: Subscribe on play, tick on the reporting cadence, and send the current reading.
 * Example:
 *   auto Sensor = Engine.CreateObject<FSensorActor>(SensorActorTypeId, Messaging).Object;
 *   Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Sensor});
 */
class FSensorActor final : public AActor
{
public:
	/**
	 * Motivation: Aligns this actor's own tick to the base reporting cadence and stores injected Messaging.
	 * Responsibilities: Construct on the base cadence and capture the Messaging system reference.
	 */
	explicit FSensorActor(FMessagingSystem& InMessaging) noexcept
		: AActor(FTickConfiguration::EnabledEvery(BaseReportingIntervalMilliseconds)), Messaging(InMessaging)
	{
	}

protected:
	/**
	 * Motivation: Subscribes to the UART rate command so it can re-time the sensor without actor addressing.
	 * Responsibilities: Bind and register the reporting-rate subscriber under this actor's weak owner.
	 */
	void BeginPlay() noexcept override
	{
		FMessagingSystem::FSubscriberDelegate Subscriber;
		const EDelegateResult BindResult = Subscriber.Bind([this](const FMessage& Message) noexcept { this->OnReportingRateReceived(Message); });
		if (BindResult != EDelegateResult::Success)
		{
			MW_LOG(Error, "ex24", "sensor subscriber bind failed");
			return;
		}

		FObjectStore* const ObjectStore = GetObjectStore();
		if (ObjectStore == nullptr)
		{
			MW_LOG(Error, "ex24", "sensor has no object store");
			return;
		}

		const EMessagingResult SubscribeResult = Messaging.SubscribeToChannel(
			CommandsChannelName, SetReportingRateMessageName, std::move(Subscriber), MakeWeakOwner(*ObjectStore, GetObjectHandle()));
		if (SubscribeResult != EMessagingResult::Success)
		{
			MW_LOG(Error, "ex24", "sensor subscription failed");
		}
	}

	/**
	 * Motivation: Bumps the synthetic reading counter and broadcasts it, so the server receives one
	 *   telemetry reading per cadence tick.
	 * Responsibilities: Increment the reading, encode it little-endian, and send it on the telemetry channel.
	 */
	void Tick(const FTickContext&) noexcept override
	{
		++NextReading;

		std::uint8_t PayloadBytes[2];
		EncodeUint16LittleEndian(NextReading, PayloadBytes);
		FMessage Message;
		Message.SetMessageNameId(TelemetryReadingMessageName);
		Message.SetPayload(TSpan<const std::uint8_t>(PayloadBytes, sizeof(PayloadBytes)));
		const EMessagingResult SendResult = Messaging.SendMessageToChannel(Message, TelemetryChannelName);
		if (SendResult != EMessagingResult::Success)
		{
			MW_LOG(Error, "ex24", "sensor telemetry send failed");
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
	void OnReportingRateReceived(const FMessage& Message) noexcept
	{
		const TSpan<const std::uint8_t> Payload = Message.GetPayload();
		if (Payload.Size() < 2)
		{
			MW_LOG(Error, "ex24", "sensor received undersized rate payload");
			return;
		}
		const std::uint16_t Interval = DecodeUint16LittleEndian(Payload.Data());
		const ERuntimeResult Result = this->SetTickInterval(Interval);
		if (Result != ERuntimeResult::Success)
		{
			MW_LOG(Error, "ex24", "sensor reporting rate change rejected");
			return;
		}
		MW_LOG(Log, "ex24", "sensor reporting rate -> %u ms", static_cast<unsigned>(Interval));
	}

	/** Motivation: Messaging system this actor sends and listens through; injected at construction, never a global. */
	FMessagingSystem& Messaging;

	/** Motivation: Synthetic reading counter (ADR 0003 -- no GPIO/sensor peripheral); wraps at 65536, accepted for this demo. */
	std::uint16_t NextReading{0};
};
} // namespace

/**
 * Motivation: Composes the SoftAP client with UDP telemetry and UART commands in one engine.
 * Responsibilities: Open devices, create the Messaging channels, spawn the sensor, and tick without stopping.
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

	// The client binds an ephemeral local port (0): only its telemetry channel sends UDP, addressed directly to the server.
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

	static FWorldEngine Engine{FGarbageCollectionBudget{1, 4, 8}};
	if (Engine.CreateMessagingSystem(FMessagingSystemInformation{}) != ERuntimeResult::Success)
	{
		MW_LOG(Error, "ex24", "client Messaging system creation failed; halting");
		return;
	}
	FMessagingSystem* const Messaging = Engine.GetMessagingSystem();
	if (Messaging == nullptr)
	{
		MW_LOG(Error, "ex24", "client Messaging system unavailable; halting");
		return;
	}
	const FDeviceAddress ServerTelemetryAddress = MakeUdpAddress(ServerIpv4[0], ServerIpv4[1], ServerIpv4[2], ServerIpv4[3], ServerPort);
	const EMessagingResult TelemetryChannelResult = Messaging->CreateChannel({TelemetryChannelName, false, &TelemetryDevice, ServerTelemetryAddress});
	// UART is point-to-point, so its device ignores this empty destination address.
	const EMessagingResult CommandsChannelResult = Messaging->CreateChannel({CommandsChannelName, false, &CommandDevice, {}});
	if (TelemetryChannelResult != EMessagingResult::Success || CommandsChannelResult != EMessagingResult::Success)
	{
		MW_LOG(Error, "ex24", "client Messaging channel creation failed; halting");
		return;
	}

	if (Engine.RegisterClass<FSensorActor>(SensorActorTypeId, "SensorActor") != EObjectResult::Success)
	{
		MW_LOG(Error, "ex24", "client class registration failed; halting");
		return;
	}

	const TObjectPtr<UWorld> World = Engine.CreateWorld();
	const TObjectPtr<FSensorActor> Sensor = Engine.CreateObject<FSensorActor>(SensorActorTypeId, *Messaging).Object;
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
