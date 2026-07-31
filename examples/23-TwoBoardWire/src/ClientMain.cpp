#include "TwoBoardWireShared.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessageChannelBinding.h>
#include <MicroWorld/Messaging/MessageRouter.h>
#include <MicroWorld/Engine/EngineSystem.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/TransportHost.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>
#include <MicroWorld/Platform/Esp32/Esp32UartDriver.h>
#include <MicroWorld/Platform/Esp32/UartAddress.h>

#include <cstddef>
#include <cstdint>

using namespace MicroWorld;
using namespace Ex23;

namespace
{
/** Single real-time source for the client board. */
FEsp32TimeSource GTimeSource{};

/** Cadence the switch toggles the lamp and broadcasts a heartbeat, per the roadmap's "every 2 s". */
constexpr DurationMilliseconds SwitchToggleIntervalMilliseconds = 2000;

/**
 * Every SwitchToggleIntervalMilliseconds, toggles a lamp state and sends it to FLampActor, and
 * broadcasts an incrementing heartbeat counter.
 *
 * Takes the router by constructor injection (D9); this actor owns no components (AActor).
 */
class FSwitchActor final : public AActor
{
public:
	/** Aligns this actor's own tick to the 2 s toggle cadence and stores the injected router. */
	explicit FSwitchActor(IMessageRouter& InRouter) noexcept
		: AActor(FTickConfiguration::EnabledEvery(SwitchToggleIntervalMilliseconds)), Router(InRouter)
	{
	}

protected:
	/** Toggles the lamp state (targeted send) then bumps and broadcasts the heartbeat counter. */
	void Tick(const FTickContext&) noexcept override
	{
		SendLampToggle();
		BroadcastHeartbeat();
	}

private:
	/** Flips bLampOn and sends its new value to LampActorId as a 1-byte targeted message. */
	void SendLampToggle() noexcept
	{
		bLampOn = !bLampOn;
		const std::uint8_t StateByte = bLampOn ? 1 : 0;
		const EMessageResult SendResult =
			Router.SendMessageToActor(AppChannelId, SetLampStateMessageId, LampActorId, SwitchActorId, TSpan<const std::uint8_t>(&StateByte, 1));
		if (SendResult != EMessageResult::Success)
		{
			MW_LOG(Error, "ex23", "switch lamp send failed");
			return;
		}
		MW_LOG(Log, "ex23", "switch -> lamp %s", bLampOn ? "ON" : "OFF");
	}

	/** Bumps HeartbeatCount and broadcasts it as a 1-byte message to every subscriber. */
	void BroadcastHeartbeat() noexcept
	{
		++HeartbeatCount;
		const EMessageResult SendResult =
			Router.BroadcastMessage(AppChannelId, HeartbeatCountMessageId, SwitchActorId, TSpan<const std::uint8_t>(&HeartbeatCount, 1));
		if (SendResult != EMessageResult::Success)
		{
			MW_LOG(Error, "ex23", "switch heartbeat broadcast failed");
			return;
		}
		MW_LOG(Log, "ex23", "switch broadcast heartbeat=%u", static_cast<unsigned>(HeartbeatCount));
	}

	/** Router this actor sends through; injected at construction (D9), never a global. */
	IMessageRouter& Router;

	/** Current toggled lamp state; flips every tick, starting OFF -> first send is ON. */
	bool bLampOn{false};

	/** Counts every heartbeat sent since BeginPlay; wraps at 256 (accepted for this bounded-value demo). */
	std::uint8_t HeartbeatCount{0};
};
} // namespace

/**
 * Client board (node 2): FSwitchActor over a TMessageRouter wired to TTransportHost (Client, greeting the
 * server's UART address) through TMessageChannelBinding, with the engine holding the net frame and
 * the loop pumping the router manually (Phase 4.1 will fold this into TPlaySystemSet -- see §4).
 */
void RunClient() noexcept
{
	static FEsp32UartDriver Driver{MakeUartConfig(ClientNodeId)};
	MW_LOG(Log, "ex23", "client node=%u open=%d", static_cast<unsigned>(ClientNodeId), Driver.IsOpen() ? 1 : 0);
	if (!Driver.IsOpen())
	{
		MW_LOG(Error, "ex23", "uart failed to open; halting");
		return;
	}

	// All composition objects are static (the ESP32-S3 stack lesson, §2.2).
	static FWireTransport Transport{Driver};
	static FWireRouter Router;
	static FWireBinding Wire{Transport, AppWireChannelByte, AppChannelId, EChannelSendTarget::Server, Router};
	static FWireFrame WireFrame{Transport};
	static FWireEngine Engine{FGarbageCollectionBudget{1, 4, 8}, WireFrame};

	if (!Wire.IsAttached())
	{
		MW_LOG(Error, "ex23", "client wire binding failed to attach; halting");
		return;
	}
	if (Router.AddChannel(Wire) != EMessageResult::Success)
	{
		MW_LOG(Error, "ex23", "client router rejected its wired channel; halting");
		return;
	}

	if (Engine.RegisterClass<FSwitchActor>(SwitchActorTypeId, "SwitchActor") != EObjectResult::Success)
	{
		MW_LOG(Error, "ex23", "client class registration failed; halting");
		return;
	}

	const TObjectPtr<UWorld> World = Engine.CreateWorld();
	const TObjectPtr<FSwitchActor> Switch = Engine.CreateObject<FSwitchActor>(SwitchActorTypeId, Router).Object;
	if (World.Get() == nullptr || Switch.Get() == nullptr)
	{
		MW_LOG(Error, "ex23", "client world or actor creation failed; halting");
		return;
	}

	if (Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Switch}) != EEngineResult::Success)
	{
		MW_LOG(Error, "ex23", "client actor registration failed; halting");
		return;
	}

	FTransportHostConfig ClientConfig = MakeHostConfig();
	ClientConfig.ServerAddress = MakeUartAddress(ServerNodeId);
	(void)Transport.Configure(ENetworkMode::Client, ClientConfig);
	(void)Transport.Start(GTimeSource.Now());

	const TimePointMilliseconds BootTime = GTimeSource.Now();
	if (Engine.BeginPlay(BootTime) != ERuntimeResult::Success)
	{
		MW_LOG(Error, "ex23", "client engine begin play failed; halting");
		return;
	}
	MW_LOG(Log, "ex23", "client connecting (no WiFi -- UART only)");

	// This is a two-board wire demo, not a self-terminating trace (matching 18-TwoBoardUart and
	// 19-UartMessaging's client), so the loop runs unbounded rather than stopping after N messages.
	for (;;)
	{
		PumpOneFrame(Router, Engine, GTimeSource.Now());
		SleepMilliseconds(PollPacingMilliseconds);
	}
}
