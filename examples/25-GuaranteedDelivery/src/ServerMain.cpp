#include "GuaranteedDeliveryShared.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Delegates/Delegate.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessageChannelBinding.h>
#include <MicroWorld/Messaging/MessageRouter.h>
#include <MicroWorld/Engine/EngineSystem.h>
#include <MicroWorld/Messaging/ReliableChannel.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/TransportHost.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiLink.h>

#include <cstddef>
#include <cstdint>
#include <utility>

using namespace MicroWorld;
using namespace MicroWorld::Engine;
using namespace MicroWorld::Transport;
using namespace MicroWorld::Messaging;
using namespace Ex25;

namespace
{
/** Single real-time source for the server board. */
FEsp32TimeSource GTimeSource{};

/**
 * Subscribes to both counter message ids and logs one line per arrival, one column per channel:
 * `rx best-effort n=` for BestEffortCounterMessageId, `rx guaranteed n=` for GuaranteedCounterMessageId.
 *
 * Takes the router by constructor injection (D9); never ticks, since it only reacts to messages.
 */
class FLedgerActor final : public AActor
{
public:
	/** Stores the injected router; this actor's tick is disabled, matching example 24's sink actor. */
	explicit FLedgerActor(IMessageRouter& InRouter) noexcept
		: AActor({/*bCanEverTick*/ false, /*bStartWithTickEnabled*/ false, /*TickIntervalMilliseconds*/ 0}), Router(InRouter)
	{
	}

protected:
	/** Registers one handler per channel, both targeted at this actor's own LedgerActorId. */
	void BeginPlay() noexcept override
	{
		FMessageHandlerBinding BestEffortHandler;
		const EDelegateResult BestEffortBindResult = BestEffortHandler.Bind([this](const FMessageView& View) noexcept { this->OnBestEffort(View); });
		if (BestEffortBindResult != EDelegateResult::Success)
		{
			MW_LOG(Error, "ex25", "ledger best-effort handler bind failed");
		}
		else
		{
			// This unbounded example never removes handlers (the run never ends), so the returned handle is not retained.
			FMessageHandlerHandle BestEffortHandle;
			const EMessageResult BestEffortAddResult =
				Router.AddMessageHandler(BestEffortCounterMessageId, LedgerActorId, std::move(BestEffortHandler), BestEffortHandle);
			if (BestEffortAddResult != EMessageResult::Success)
			{
				MW_LOG(Error, "ex25", "ledger best-effort handler registration failed");
			}
		}

		FMessageHandlerBinding GuaranteedHandler;
		const EDelegateResult GuaranteedBindResult = GuaranteedHandler.Bind([this](const FMessageView& View) noexcept { this->OnGuaranteed(View); });
		if (GuaranteedBindResult != EDelegateResult::Success)
		{
			MW_LOG(Error, "ex25", "ledger guaranteed handler bind failed");
		}
		else
		{
			FMessageHandlerHandle GuaranteedHandle;
			const EMessageResult GuaranteedAddResult =
				Router.AddMessageHandler(GuaranteedCounterMessageId, LedgerActorId, std::move(GuaranteedHandler), GuaranteedHandle);
			if (GuaranteedAddResult != EMessageResult::Success)
			{
				MW_LOG(Error, "ex25", "ledger guaranteed handler registration failed");
			}
		}
	}

private:
	/** Logs one arrival on the best-effort column; this column is expected to show gaps. */
	void OnBestEffort(const FMessageView& View) noexcept
	{
		if (View.Payload.Size() < 1)
		{
			MW_LOG(Error, "ex25", "ledger received undersized best-effort payload");
			return;
		}
		MW_LOG(Log, "ex25", "rx best-effort n=%u", static_cast<unsigned>(View.Payload.Data()[0]));
	}

	/** Logs one arrival on the guaranteed column; this column is expected to be complete. */
	void OnGuaranteed(const FMessageView& View) noexcept
	{
		if (View.Payload.Size() < 1)
		{
			MW_LOG(Error, "ex25", "ledger received undersized guaranteed payload");
			return;
		}
		MW_LOG(Log, "ex25", "rx guaranteed n=%u", static_cast<unsigned>(View.Payload.Data()[0]));
	}

	/** Router this actor listens through; injected at construction (D9), never a global. */
	IMessageRouter& Router;
};
} // namespace

/**
 * Server board: hosts the WiFi SoftAP and runs FLedgerActor over one TMessageRouter wired to ONE
 * UDP transport through TWO TMessageChannelBinding -- best-effort straight to the router, guaranteed
 * wrapped in TReliableChannel -- with the engine holding the host play system, the reliable channel, and
 * the router behind one TPlaySystemSet<3>. The server's own device is never wrapped in
 * FPacketDropDevice: only the client injects loss (§0 of the roadmap brief).
 */
void RunServer() noexcept
{
	static FEsp32WifiLink WifiLink;
	if (WifiLink.StartAccessPoint(FEsp32AccessPointConfig{DemoApSsid, DemoApPassword, /*WifiChannel*/ 1, /*MaxStations*/ 4})
		!= ETransportResult::Success)
	{
		MW_LOG(Error, "ex25", "wifi failed; halting");
		return;
	}
	MW_LOG(Log, "ex25", "wifi softap up, gateway 192.168.4.1");

	// The device is constructed only after WiFi/netif is up (lwIP must exist first).
	static FEsp32WifiDevice UdpDevice(ServerPort);
	MW_LOG(Log, "ex25", "udp open=%d udp_port=%u", UdpDevice.IsOpen() ? 1 : 0, static_cast<unsigned>(UdpDevice.BoundPort()));
	if (!UdpDevice.IsOpen())
	{
		MW_LOG(Error, "ex25", "udp socket failed; halting");
		return;
	}

	// All composition objects are static (the ESP32-S3 stack lesson, §2.2).
	static FWorldTransport Transport{UdpDevice};
	static FWorldRouter Router;

	// Best-effort channel: a plain binding straight to the router, no reliable wrapper.
	static FChannelBinding BestEffortWire{Transport, BestEffortWireChannelByte, BestEffortChannelId, EChannelSendTarget::AllPeers, Router};

	// Guaranteed channel: same cycle-break order as the client -- construct the reliable wrapper
	// (forward sink = Router), construct the binding (inbound sink = the wrapper), then bind the
	// wrapper to the binding via SetInnerChannel (ReliableChannel.h).
	static FGuaranteedChannel Guaranteed{Router, FReliableChannelConfig{}};
	static FChannelBinding GuaranteedWire{Transport, GuaranteedWireChannelByte, GuaranteedChannelId, EChannelSendTarget::AllPeers, Guaranteed};
	Guaranteed.SetInnerChannel(GuaranteedWire);

	static FHostPlay HostPlay{Transport};

	// D3 frame-set order: transport first, reliable channel second, router last -- see
	// GuaranteedDeliveryShared.h's FWorldFrameSet alias.
	static FWorldFrameSet Frames;
	if (Frames.Add(HostPlay) != EEngineResult::Success || Frames.Add(Guaranteed) != EEngineResult::Success
		|| Frames.Add(Router) != EEngineResult::Success)
	{
		MW_LOG(Error, "ex25", "server frame set rejected a frame; halting");
		return;
	}
	static FWorldEngine Engine{FGarbageCollectionBudget{1, 4, 8}, Frames};

	if (!BestEffortWire.IsAttached() || !GuaranteedWire.IsAttached())
	{
		MW_LOG(Error, "ex25", "server binding failed to attach; halting");
		return;
	}
	// AddChannel(Guaranteed) must follow SetInnerChannel above: GetChannelId() reads the inner id.
	if (Router.AddChannel(BestEffortWire) != EMessageResult::Success || Router.AddChannel(Guaranteed) != EMessageResult::Success)
	{
		MW_LOG(Error, "ex25", "server router rejected a channel; halting");
		return;
	}

	if (Engine.RegisterClass<FLedgerActor>(LedgerActorTypeId, "LedgerActor") != EObjectResult::Success)
	{
		MW_LOG(Error, "ex25", "server class registration failed; halting");
		return;
	}

	const TObjectPtr<UWorld> World = Engine.CreateWorld();
	const TObjectPtr<FLedgerActor> Ledger = Engine.CreateObject<FLedgerActor>(LedgerActorTypeId, Router).Object;
	if (World.Get() == nullptr || Ledger.Get() == nullptr)
	{
		MW_LOG(Error, "ex25", "server world or actor creation failed; halting");
		return;
	}

	if (Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Ledger}) != EEngineResult::Success)
	{
		MW_LOG(Error, "ex25", "server actor registration failed; halting");
		return;
	}

	(void)Transport.Configure(ENetworkMode::DedicatedServer, MakeHostConfig());
	(void)Transport.Start(GTimeSource.Now());

	const TimePointMilliseconds BootTime = GTimeSource.Now();
	if (Engine.BeginPlay(BootTime) != ERuntimeResult::Success)
	{
		MW_LOG(Error, "ex25", "server engine begin play failed; halting");
		return;
	}
	MW_LOG(Log, "ex25", "server up (best-effort + guaranteed over one UDP link)");

	// This is a two-board two-channel demo, not a self-terminating trace (matching 16-TwoBoardUdp and
	// 24-TwoChannelWorld's server), so the loop runs unbounded rather than stopping after N messages.
	std::uint32_t LastDedup = 0;
	for (;;)
	{
		(void)Engine.Tick(GTimeSource.Now());
		const std::uint32_t Dedup = Guaranteed.DuplicateDroppedCount();
		if (Dedup != LastDedup)
		{
			MW_LOG(Log, "ex25", "guaranteed dedup dropped=%u", static_cast<unsigned>(Dedup));
			LastDedup = Dedup;
		}
		SleepMilliseconds(PollPacingMilliseconds);
	}
}
