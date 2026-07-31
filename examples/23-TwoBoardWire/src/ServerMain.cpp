#include "TwoBoardWireShared.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Delegates/Delegate.h>
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
#include <MicroWorld/Platform/Esp32/Esp32UartDevice.h>

#include <cstddef>
#include <cstdint>
#include <utility>

using namespace MicroWorld::Core;
using namespace MicroWorld::Platform::Esp32;
using namespace MicroWorld::Engine;
using namespace MicroWorld::Transport;
using namespace MicroWorld::Messaging;
using namespace Ex23;

namespace
{
/** Motivation: Single real-time source for the server board. */
FEsp32TimeSource GTimeSource{};

/**
 * Motivation: Subscribes to a targeted SetLampStateMessageId (its own actor id) and logs the decoded
 *   state, so the lamp half of the two-board wire demo is observable. Takes the router by constructor
 *   injection and never ticks.
 * Responsibilities: Register a handler for SetLampState on play and log each received state.
 * Example:
 *   auto Lamp = Engine.CreateObject<FLampActor>(LampActorTypeId, Router).Object;
 *   Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Lamp});
 */
class FLampActor final : public AActor
{
public:
	/**
	 * Motivation: Stores the injected router; this actor's tick is disabled because it only reacts to a message.
	 * Responsibilities: Construct with tick disabled and capture the router reference.
	 */
	explicit FLampActor(IMessageRouter& InRouter) noexcept
		: AActor({/*bCanEverTick*/ false, /*bStartWithTickEnabled*/ false, /*TickIntervalMilliseconds*/ 0}), Router(InRouter)
	{
	}

protected:
	/**
	 * Motivation: Subscribes to SetLampStateMessageId targeted at this actor's own LampActorId, so later
	 *   toggle sends reach this lamp.
	 * Responsibilities: Bind and register the lamp-state handler under the lamp's actor id.
	 */
	void BeginPlay() noexcept override
	{
		FMessageHandlerBinding Handler;
		const EDelegateResult BindResult = Handler.Bind([this](const FMessageView& View) noexcept { this->OnLampStateReceived(View); });
		if (BindResult != EDelegateResult::Success)
		{
			MW_LOG(Error, "ex23", "lamp handler bind failed");
			return;
		}

		// This unbounded example never removes handlers (the run never ends), so the returned handle is not retained.
		FMessageHandlerHandle HandlerHandle;
		const EMessageResult AddResult = Router.AddMessageHandler(SetLampStateMessageId, LampActorId, std::move(Handler), HandlerHandle);
		if (AddResult != EMessageResult::Success)
		{
			MW_LOG(Error, "ex23", "lamp handler registration failed");
		}
	}

private:
	/**
	 * Motivation: Decodes the 1-byte state and logs the lamp's new state; this is the handler bound in BeginPlay.
	 * Responsibilities: Validate the payload and log ON or OFF.
	 */
	void OnLampStateReceived(const FMessageView& View) noexcept
	{
		if (View.Payload.Size() < 1)
		{
			MW_LOG(Error, "ex23", "lamp received undersized state payload");
			return;
		}
		const bool bLampOn = View.Payload.Data()[0] != 0;
		MW_LOG(Log, "ex23", "lamp %s", bLampOn ? "ON" : "OFF");
	}

	/** Motivation: Router this actor listens through; injected at construction, never a global. */
	IMessageRouter& Router;
};

/**
 * Motivation: Subscribes to the broadcast HeartbeatCountMessageId and logs every count it receives, so
 *   the heartbeat half of the two-board wire demo is observable. Takes the router by constructor
 *   injection and never ticks.
 * Responsibilities: Register a broadcast handler on play and log each received count.
 * Example:
 *   auto Display = Engine.CreateObject<FDisplayActor>(DisplayActorTypeId, Router).Object;
 *   Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Display});
 */
class FDisplayActor final : public AActor
{
public:
	/**
	 * Motivation: Stores the injected router; this actor's tick is disabled because it only reacts to a message.
	 * Responsibilities: Construct with tick disabled and capture the router reference.
	 */
	explicit FDisplayActor(IMessageRouter& InRouter) noexcept
		: AActor({/*bCanEverTick*/ false, /*bStartWithTickEnabled*/ false, /*TickIntervalMilliseconds*/ 0}), Router(InRouter)
	{
	}

protected:
	/**
	 * Motivation: Subscribes to every broadcast HeartbeatCountMessageId, so the display receives each heartbeat.
	 * Responsibilities: Bind and register the heartbeat handler under the broadcast listener id.
	 */
	void BeginPlay() noexcept override
	{
		FMessageHandlerBinding Handler;
		const EDelegateResult BindResult = Handler.Bind([this](const FMessageView& View) noexcept { this->OnHeartbeatReceived(View); });
		if (BindResult != EDelegateResult::Success)
		{
			MW_LOG(Error, "ex23", "display handler bind failed");
			return;
		}

		// This unbounded example never removes handlers (the run never ends), so the returned handle is not retained.
		FMessageHandlerHandle HandlerHandle;
		const EMessageResult AddResult = Router.AddMessageHandler(HeartbeatCountMessageId, BroadcastActorId, std::move(Handler), HandlerHandle);
		if (AddResult != EMessageResult::Success)
		{
			MW_LOG(Error, "ex23", "display handler registration failed");
		}
	}

private:
	/**
	 * Motivation: Decodes the 1-byte counter and logs it; this is the handler bound in BeginPlay.
	 * Responsibilities: Validate the payload and log the received count.
	 */
	void OnHeartbeatReceived(const FMessageView& View) noexcept
	{
		if (View.Payload.Size() < 1)
		{
			MW_LOG(Error, "ex23", "display received undersized heartbeat payload");
			return;
		}
		MW_LOG(Log, "ex23", "heartbeat=%u", static_cast<unsigned>(View.Payload.Data()[0]));
	}

	/** Motivation: Router this actor listens through; injected at construction, never a global. */
	IMessageRouter& Router;
};
} // namespace

/**
 * Motivation: Lets Board A (node 1) run FLampActor + FDisplayActor over a TMessageRouter wired to
 *   TTransportHost (DedicatedServer) through TMessageChannelBinding, so the server half of the two-board
 *   wire demo can be reasoned about in one place.
 * Responsibilities: Open the UART, wire the transport, router, binding, and engine, spawn the actors,
 *   start as a dedicated server, and pump frames in an unbounded loop.
 */
void RunServer() noexcept
{
	static FEsp32UartDevice Device{MakeUartConfig(ServerNodeId)};
	MW_LOG(Log, "ex23", "server node=%u open=%d", static_cast<unsigned>(ServerNodeId), Device.IsOpen() ? 1 : 0);
	if (!Device.IsOpen())
	{
		MW_LOG(Error, "ex23", "uart failed to open; halting");
		return;
	}

	// All composition objects are static (the ESP32-S3 stack lesson, §2.2).
	static FWireTransport Transport{Device};
	static FWireRouter Router;
	static FWireBinding Wire{Transport, AppWireChannelByte, AppChannelId, EChannelSendTarget::AllPeers, Router};
	static FWireFrame WireFrame{Transport};
	static FWireEngine Engine{FGarbageCollectionBudget{1, 4, 8}, WireFrame};

	if (!Wire.IsAttached())
	{
		MW_LOG(Error, "ex23", "server wire binding failed to attach; halting");
		return;
	}
	if (Router.AddChannel(Wire) != EMessageResult::Success)
	{
		MW_LOG(Error, "ex23", "server router rejected its wired channel; halting");
		return;
	}

	if (Engine.RegisterClass<FLampActor>(LampActorTypeId, "LampActor") != EObjectResult::Success
		|| Engine.RegisterClass<FDisplayActor>(DisplayActorTypeId, "DisplayActor") != EObjectResult::Success)
	{
		MW_LOG(Error, "ex23", "server class registration failed; halting");
		return;
	}

	const TObjectPtr<UWorld> World = Engine.CreateWorld();
	const TObjectPtr<FLampActor> Lamp = Engine.CreateObject<FLampActor>(LampActorTypeId, Router).Object;
	const TObjectPtr<FDisplayActor> Display = Engine.CreateObject<FDisplayActor>(DisplayActorTypeId, Router).Object;
	if (World.Get() == nullptr || Lamp.Get() == nullptr || Display.Get() == nullptr)
	{
		MW_LOG(Error, "ex23", "server world or actor creation failed; halting");
		return;
	}

	if (Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Lamp}) != EEngineResult::Success
		|| Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Display}) != EEngineResult::Success)
	{
		MW_LOG(Error, "ex23", "server actor registration failed; halting");
		return;
	}

	(void)Transport.Configure(ENetworkMode::DedicatedServer, MakeHostConfig());
	(void)Transport.Start(GTimeSource.Now());

	const TimePointMilliseconds BootTime = GTimeSource.Now();
	if (Engine.BeginPlay(BootTime) != ERuntimeResult::Success)
	{
		MW_LOG(Error, "ex23", "server engine begin play failed; halting");
		return;
	}
	MW_LOG(Log, "ex23", "server listening (no WiFi -- UART only)");

	// This is a two-board wire demo, not a self-terminating trace (matching 18-TwoBoardUart and
	// 19-UartMessaging's server), so the loop runs unbounded rather than stopping after N messages.
	for (;;)
	{
		PumpOneFrame(Router, Engine, GTimeSource.Now());
		SleepMilliseconds(PollPacingMilliseconds);
	}
}
