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
/** Single real-time source for the server board. */
FEsp32TimeSource GTimeSource{};

/**
 * Subscribes to a targeted SetLampStateMessageId (its own actor id) and logs the decoded state.
 *
 * Takes the router by constructor injection (D9); never ticks, since it only reacts to a message.
 */
class FLampActor final : public AActor
{
public:
	/** Stores the injected router; this actor's tick is disabled, matching FDisplayActor in example 22. */
	explicit FLampActor(IMessageRouter& InRouter) noexcept
		: AActor({/*bCanEverTick*/ false, /*bStartWithTickEnabled*/ false, /*TickIntervalMilliseconds*/ 0}), Router(InRouter)
	{
	}

protected:
	/** Subscribes to SetLampStateMessageId targeted at this actor's own LampActorId. */
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
	/** Decodes the 1-byte state and logs the lamp's new state; this is the handler bound in BeginPlay. */
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

	/** Router this actor listens through; injected at construction (D9), never a global. */
	IMessageRouter& Router;
};

/**
 * Subscribes to the broadcast HeartbeatCountMessageId and logs every count it receives.
 *
 * Takes the router by constructor injection (D9); never ticks, since it only reacts to a message.
 */
class FDisplayActor final : public AActor
{
public:
	/** Stores the injected router; this actor's tick is disabled, matching FDisplayActor in example 22. */
	explicit FDisplayActor(IMessageRouter& InRouter) noexcept
		: AActor({/*bCanEverTick*/ false, /*bStartWithTickEnabled*/ false, /*TickIntervalMilliseconds*/ 0}), Router(InRouter)
	{
	}

protected:
	/** Subscribes to every broadcast HeartbeatCountMessageId (BroadcastActorId receives broadcasts). */
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
	/** Decodes the 1-byte counter and logs it; this is the handler bound in BeginPlay. */
	void OnHeartbeatReceived(const FMessageView& View) noexcept
	{
		if (View.Payload.Size() < 1)
		{
			MW_LOG(Error, "ex23", "display received undersized heartbeat payload");
			return;
		}
		MW_LOG(Log, "ex23", "heartbeat=%u", static_cast<unsigned>(View.Payload.Data()[0]));
	}

	/** Router this actor listens through; injected at construction (D9), never a global. */
	IMessageRouter& Router;
};
} // namespace

/**
 * Server board (node 1): FLampActor + FDisplayActor over a TMessageRouter wired to TTransportHost
 * (DedicatedServer) through TMessageChannelBinding, with the engine holding the host play system and the
 * loop pumping the router manually (Phase 4.1 will fold this into TPlaySystemSet — see §4).
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
