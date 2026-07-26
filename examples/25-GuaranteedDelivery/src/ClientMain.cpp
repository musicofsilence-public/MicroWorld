#include "GuaranteedDeliveryShared.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/InlineTypes.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessageChannelBinding.h>
#include <MicroWorld/Messaging/MessageRouter.h>
#include <MicroWorld/Engine/EngineSystem.h>
#include <MicroWorld/Messaging/ReliableChannel.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Log.h>
#include <MicroWorld/Net/NetHost.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/Net/PacketDropDriver.h>
#include <MicroWorld/Net/UdpAddressCodec.h>
#include <MicroWorld/Object/ClassDescriptor.h>
#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/ObjectPtr.h>
#include <MicroWorld/PlatformEsp32/Esp32Sleep.h>
#include <MicroWorld/PlatformEsp32/Esp32TimeSource.h>
#include <MicroWorld/PlatformEsp32/Esp32UdpDriver.h>
#include <MicroWorld/PlatformEsp32/Esp32WifiLink.h>

#include <cstddef>
#include <cstdint>
#include <utility>

using namespace MicroWorld;
using namespace Ex25;

namespace
{
/** Single real-time source for the client board. */
FEsp32TimeSource GTimeSource{};

/**
 * Every CounterIntervalMilliseconds, sends the next value in 1..LastCounterValue to the server's
 * FLedgerActor on BOTH the best-effort and the guaranteed channel, then idles once all 30 are sent.
 *
 * Takes the router by constructor injection (D9); this actor owns no components (TInlineActor<0>)
 * and has no handlers -- it only sends, and acknowledgements are consumed entirely inside the
 * guaranteed channel's own wrapper, never surfaced as an actor message.
 */
class FCounterActor final : public TInlineActor<0>
{
public:
	/** Aligns this actor's own tick to the counter cadence and stores the injected router. */
	explicit FCounterActor(IMessageRouter& InRouter) noexcept
		: TInlineActor<0>(FTickConfiguration::EnabledEvery(CounterIntervalMilliseconds)), Router(InRouter)
	{
	}

protected:
	/** Sends the next counter value on both channels; stops sending (but keeps idling) once all 30 are out. */
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
	/** Router this actor sends through; injected at construction (D9), never a global. */
	IMessageRouter& Router;

	/** Next counter value to send; once past LastCounterValue, Tick idles without sending. */
	std::uint8_t NextValue{FirstCounterValue};
};
} // namespace

/**
 * Client board: joins the WiFi SoftAP and runs FCounterActor over one TMessageRouter wired to ONE
 * UDP net through TWO TMessageChannelBinding -- best-effort straight to the router, guaranteed
 * wrapped in TReliableChannel -- with the engine holding the net frame, the reliable channel, and
 * the router behind one TEngineSystemSet<3>. The UDP driver is itself wrapped in FPacketDropDriver
 * so every third outgoing packet, of any kind, is silently dropped -- the whole point of the demo.
 */
void RunClient() noexcept
{
	static FEsp32WifiLink WifiLink;
	if (WifiLink.JoinAccessPoint(FEsp32StationConfig{DemoApSsid, DemoApPassword, /*ConnectTimeoutMilliseconds*/ 15000}) != ENetResult::Success)
	{
		MW_LOG(Error, "ex25", "wifi failed; halting");
		return;
	}
	MW_LOG(Log, "ex25", "wifi joined AP");

	// The client binds an ephemeral local port (0): it only needs to reach the server, and TNetHost
	// learns the client's address server-side from its Hello.
	static FEsp32UdpDriver UdpDriver(0);
	MW_LOG(Log, "ex25", "udp open=%d", UdpDriver.IsOpen() ? 1 : 0);
	if (!UdpDriver.IsOpen())
	{
		MW_LOG(Error, "ex25", "udp socket failed; halting");
		return;
	}

	// All composition objects are static (the ESP32-S3 stack lesson, §2.2). The drop driver wraps
	// the real UDP driver, so the net below sends and receives through the loss injector.
	static FPacketDropDriver DropDriver{UdpDriver, DropEveryNthSend};
	static FWorldNet Net{DropDriver};
	static FWorldRouter Router;

	// Best-effort channel: a plain binding straight to the router, no reliable wrapper.
	static FChannelBinding BestEffortWire{Net, BestEffortWireChannelByte, BestEffortChannelId, EChannelSendTarget::Server, Router};

	// Guaranteed channel: break the wrapper<->binding reference cycle in this exact order --
	// construct the reliable wrapper (forward sink = Router), construct the binding (inbound sink =
	// the wrapper), then bind the wrapper to the binding via SetInnerChannel (ReliableChannel.h).
	static FGuaranteedChannel Guaranteed{Router, FReliableChannelConfig{}};
	static FChannelBinding GuaranteedWire{Net, GuaranteedWireChannelByte, GuaranteedChannelId, EChannelSendTarget::Server, Guaranteed};
	Guaranteed.SetInnerChannel(GuaranteedWire);

	static FNetFrame NetFrame{Net};

	// D3 frame-set order: net first (delivers inbound traffic), reliable channel second (its
	// PostAdvance paces retries), router last (dispatches what the net and the reliable channel just
	// delivered) -- see GuaranteedDeliveryShared.h's FWorldFrameSet alias.
	static FWorldFrameSet Frames;
	if (Frames.Add(NetFrame) != EEngineResult::Success || Frames.Add(Guaranteed) != EEngineResult::Success
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

	FNetHostConfig Config = MakeHostConfig();
	Config.ServerAddress = MakeUdpAddress(ServerIpv4[0], ServerIpv4[1], ServerIpv4[2], ServerIpv4[3], ServerPort);
	(void)Net.Configure(ENetMode::Client, Config);
	(void)Net.Start(GTimeSource.Now());

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
