#include "GuaranteedDeliveryShared.h"

#include <MicroWorld/Core/Containers/Span.h>
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
#include <MicroWorld/Transport/PacketDropDevice.h>
#include <MicroWorld/Transport/Wifi/UdpAddressCodec.h>
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

using namespace MicroWorld::Core;
using namespace MicroWorld::Platform::Esp32;
using namespace MicroWorld::Engine;
using namespace MicroWorld::Transport;
using namespace MicroWorld::Messaging;
using namespace Ex25;

namespace
{
/** Motivation: Single real-time source for the client board. */
FEsp32TimeSource GTimeSource{};

/**
 * Motivation: Every CounterIntervalMilliseconds, sends the next value in 1..LastCounterValue to the
 *   server's FLedgerActor on BOTH the best-effort and the guaranteed channel, then idles once all are
 *   sent. Takes the router by constructor injection, owns no components, and has no handlers -- it only
 *   sends, and acknowledgements are consumed inside the guaranteed channel's wrapper.
 * Responsibilities: Tick on the counter cadence, send one value on both channels, then idle once done.
 * Example:
 *   auto Counter = Engine.CreateObject<FCounterActor>(CounterActorTypeId, Router).Object;
 *   Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Counter});
 */
class FCounterActor final : public AActor
{
public:
	/**
	 * Motivation: Aligns this actor's own tick to the counter cadence and stores the injected router.
	 * Responsibilities: Construct on the counter cadence and capture the router reference.
	 */
	explicit FCounterActor(IMessageRouter& InRouter) noexcept
		: AActor(FTickConfiguration::EnabledEvery(CounterIntervalMilliseconds)), Router(InRouter)
	{
	}

protected:
	/**
	 * Motivation: Sends the next counter value on both channels so the demo's lossy link exercises both
	 *   paths, idling once all values are out.
	 * Responsibilities: Send one value on the best-effort and guaranteed channels, then advance the counter.
	 */
	void Tick(const FTickContext&) noexcept override
	{
		if (NextValue > LastCounterValue)
		{
			return;
		}

		std::uint8_t Payload[1] = {NextValue};
		const TSpan<const std::uint8_t> PayloadSpan(Payload, 1);

		const EMessageResult BestEffortResult =
			Router.SendMessageToActor(BestEffortChannelId, BestEffortCounterMessageId, LedgerActorId, CounterActorId, PayloadSpan);
		if (BestEffortResult != EMessageResult::Success)
		{
			MW_LOG(Error, "ex25", "counter best-effort send failed n=%u", static_cast<unsigned>(NextValue));
		}

		const EMessageResult GuaranteedResult =
			Router.SendMessageToActor(GuaranteedChannelId, GuaranteedCounterMessageId, LedgerActorId, CounterActorId, PayloadSpan);
		if (GuaranteedResult != EMessageResult::Success)
		{
			MW_LOG(Error, "ex25", "counter guaranteed send failed n=%u", static_cast<unsigned>(NextValue));
		}

		if (BestEffortResult == EMessageResult::Success && GuaranteedResult == EMessageResult::Success)
		{
			MW_LOG(Log, "ex25", "tx n=%u (best-effort + guaranteed)", static_cast<unsigned>(NextValue));
		}

		++NextValue;
	}

private:
	/** Motivation: Router this actor sends through; injected at construction, never a global. */
	IMessageRouter& Router;

	/** Motivation: Next counter value to send; once past LastCounterValue, Tick idles without sending. */
	std::uint8_t NextValue{FirstCounterValue};
};
} // namespace

/**
 * Motivation: Lets Board B join the WiFi SoftAP and run FCounterActor over one router wired to one UDP
 *   transport through two bindings -- best-effort straight to the router, guaranteed wrapped in
 *   TReliableChannel -- behind one TPlaySystemSet, with the UDP device wrapped in FPacketDropDevice so
 *   every third outgoing packet is dropped (the point of the demo).
 * Responsibilities: Join the SoftAP, wire the drop device, transport, both bindings, the reliable channel,
 *   the frame set, and the engine, spawn the counter, start as a client, and tick the engine in an unbounded loop.
 */
void RunClient() noexcept
{
	static FEsp32WifiLink WifiLink;
	if (WifiLink.JoinAccessPoint(FEsp32StationConfig{DemoApSsid, DemoApPassword, /*ConnectTimeoutMilliseconds*/ 15000}) != ETransportResult::Success)
	{
		MW_LOG(Error, "ex25", "wifi failed; halting");
		return;
	}
	MW_LOG(Log, "ex25", "wifi joined AP");

	// The client binds an ephemeral local port (0): it only needs to reach the server, and TTransportHost
	// learns the client's address server-side from its Hello.
	static FEsp32WifiDevice UdpDevice(0);
	MW_LOG(Log, "ex25", "udp open=%d", UdpDevice.IsOpen() ? 1 : 0);
	if (!UdpDevice.IsOpen())
	{
		MW_LOG(Error, "ex25", "udp socket failed; halting");
		return;
	}

	// All composition objects are static (the ESP32-S3 stack lesson, §2.2). The drop device wraps
	// the real UDP device, so the transport below sends and receives through the loss injector.
	static FPacketDropDevice DropDevice{UdpDevice, DropEveryNthSend};
	static FWorldTransport Transport{DropDevice};
	static FWorldRouter Router;

	// Best-effort channel: a plain binding straight to the router, no reliable wrapper.
	static FChannelBinding BestEffortWire{Transport, BestEffortWireChannelByte, BestEffortChannelId, EChannelSendTarget::Server, Router};

	// Guaranteed channel: break the wrapper<->binding reference cycle in this exact order --
	// construct the reliable wrapper (forward sink = Router), construct the binding (inbound sink =
	// the wrapper), then bind the wrapper to the binding via SetInnerChannel (ReliableChannel.h).
	static FGuaranteedChannel Guaranteed{Router, FReliableChannelConfig{}};
	static FChannelBinding GuaranteedWire{Transport, GuaranteedWireChannelByte, GuaranteedChannelId, EChannelSendTarget::Server, Guaranteed};
	Guaranteed.SetInnerChannel(GuaranteedWire);

	static FHostPlay HostPlay{Transport};

	// D3 frame-set order: transport first (delivers inbound traffic), reliable channel second (its
	// PostAdvance paces retries), router last (dispatches what the transport and the reliable channel just
	// delivered) -- see GuaranteedDeliveryShared.h's FWorldFrameSet alias.
	static FWorldFrameSet Frames;
	if (Frames.Add(HostPlay) != EEngineResult::Success || Frames.Add(Guaranteed) != EEngineResult::Success
		|| Frames.Add(Router) != EEngineResult::Success)
	{
		MW_LOG(Error, "ex25", "client frame set rejected a frame; halting");
		return;
	}
	static FWorldEngine Engine{FGarbageCollectionBudget{1, 4, 8}, Frames};

	if (!BestEffortWire.IsAttached() || !GuaranteedWire.IsAttached())
	{
		MW_LOG(Error, "ex25", "client binding failed to attach; halting");
		return;
	}
	// AddChannel(Guaranteed) must follow SetInnerChannel above: GetChannelId() reads the inner id.
	if (Router.AddChannel(BestEffortWire) != EMessageResult::Success || Router.AddChannel(Guaranteed) != EMessageResult::Success)
	{
		MW_LOG(Error, "ex25", "client router rejected a channel; halting");
		return;
	}

	if (Engine.RegisterClass<FCounterActor>(CounterActorTypeId, "CounterActor") != EObjectResult::Success)
	{
		MW_LOG(Error, "ex25", "client class registration failed; halting");
		return;
	}

	const TObjectPtr<UWorld> World = Engine.CreateWorld();
	const TObjectPtr<FCounterActor> Counter = Engine.CreateObject<FCounterActor>(CounterActorTypeId, Router).Object;
	if (World.Get() == nullptr || Counter.Get() == nullptr)
	{
		MW_LOG(Error, "ex25", "client world or actor creation failed; halting");
		return;
	}

	if (Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Counter}) != EEngineResult::Success)
	{
		MW_LOG(Error, "ex25", "client actor registration failed; halting");
		return;
	}

	FTransportHostConfig Config = MakeHostConfig();
	Config.ServerAddress = MakeUdpAddress(ServerIpv4[0], ServerIpv4[1], ServerIpv4[2], ServerIpv4[3], ServerPort);
	(void)Transport.Configure(ENetworkMode::Client, Config);
	(void)Transport.Start(GTimeSource.Now());

	const TimePointMilliseconds BootTime = GTimeSource.Now();
	if (Engine.BeginPlay(BootTime) != ERuntimeResult::Success)
	{
		MW_LOG(Error, "ex25", "client engine begin play failed; halting");
		return;
	}
	MW_LOG(Log, "ex25", "client up (best-effort + guaranteed over one UDP link, dropping every %u-th send)", static_cast<unsigned>(DropEveryNthSend));

	// This is a two-board two-channel demo, not a self-terminating trace (matching 16-TwoBoardUdp and
	// 24-TwoChannelWorld's client), so the loop runs unbounded rather than stopping after N messages.
	std::uint32_t LastResent = 0;
	for (;;)
	{
		(void)Engine.Tick(GTimeSource.Now());
		const std::uint32_t Resent = Guaranteed.ResentCount();
		if (Resent != LastResent)
		{
			MW_LOG(Log, "ex25", "guaranteed resent=%u pending=%u", static_cast<unsigned>(Resent), static_cast<unsigned>(Guaranteed.PendingCount()));
			LastResent = Resent;
		}
		SleepMilliseconds(PollPacingMilliseconds);
	}
}
