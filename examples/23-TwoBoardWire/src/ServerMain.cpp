#include "TwoBoardWireShared.h"

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

#include <cstddef>
#include <cstdint>
#include <utility>

using namespace MicroWorld;
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
class FLampActor final : public TInlineActor<0>
{
public:
	/** Stores the injected router; this actor's tick is disabled, matching FDisplayActor in example 22. */
	explicit FLampActor(IMessageRouter& InRouter) noexcept
		: TInlineActor<0>({/*bCanEverTick*/ false, /*bStartWithTickEnabled*/ false, /*TickIntervalMilliseconds*/ 0}), Router(InRouter)
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
class FDisplayActor final : public TInlineActor<0>
{
public:
	/** Stores the injected router; this actor's tick is disabled, matching FDisplayActor in example 22. */
	explicit FDisplayActor(IMessageRouter& InRouter) noexcept
		: TInlineActor<0>({/*bCanEverTick*/ false, /*bStartWithTickEnabled*/ false, /*TickIntervalMilliseconds*/ 0}), Router(InRouter)
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
 * Server board (node 1): FLampActor + FDisplayActor over a TMessageRouter wired to TNetHost
 * (DedicatedServer) through TMessageChannelBinding, with the engine holding the net frame and the
 * loop pumping the router manually (Phase 4.1 will fold this into TNetworkFrameSet — see §4).
 */
void RunServer() noexcept
{
	static FEsp32UartDriver Driver{MakeUartConfig(ServerNodeId)};
	MW_LOG(Log, "ex23", "server node=%u open=%d", static_cast<unsigned>(ServerNodeId), Driver.IsOpen() ? 1 : 0);
	if (!Driver.IsOpen())
	{
		MW_LOG(Error, "ex23", "uart failed to open; halting");
		return;
	}

	// All composition objects are static (the ESP32-S3 stack lesson, §2.2).
	static FWireNet Net{Driver};
	static FWireRouter Router;
	static FWireBinding Wire{Net, AppWireChannelByte, AppChannelId, EChannelSendTarget::AllPeers, Router};
	static FWireFrame NetFrame{Net};
	static FWireEngine Engine{FGarbageCollectionBudget{1, 4, 8}, NetFrame};

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

	(void)Net.Configure(ENetMode::DedicatedServer, MakeHostConfig());
	(void)Net.Start(GTimeSource.Now());

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
