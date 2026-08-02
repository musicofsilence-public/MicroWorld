#include "GuaranteedDeliveryShared.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Messaging/MessagingSystem.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiLink.h>
#include <MicroWorld/Transport/PacketDropDevice.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Transport/Wifi/UdpAddressCodec.h>

#include <cstdint>

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
 *   server on both the best-effort and guaranteed channels, then idles once all are sent. Takes Messaging
 *   by constructor injection, owns no components, and has no handlers because Messaging consumes acknowledgements.
 * Responsibilities: Tick on the counter cadence, send one value on both channels, react to reliable
 *   backpressure, then advance the counter.
 * Example:
 *   auto Counter = Engine.CreateObject<FCounterActor>(CounterActorTypeId, Messaging).Object;
 *   Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Counter});
 */
class FCounterActor final : public AActor
{
public:
	/**
	 * Motivation: Aligns this actor's own tick to the counter cadence and stores injected Messaging.
	 * Responsibilities: Construct on the counter cadence and capture the Messaging system reference.
	 */
	explicit FCounterActor(FMessagingSystem& InMessaging) noexcept
		: AActor(FTickConfiguration::EnabledEvery(CounterIntervalMilliseconds)), Messaging(InMessaging)
	{
	}

protected:
	/**
	 * Motivation: Sends the next counter value on both channels so the demo's lossy link exercises both
	 *   paths, idling once all values are out.
	 * Responsibilities: Send one named value on the best-effort and guaranteed channels, react to capacity,
	 *   then advance the counter.
	 */
	void Tick(const FTickContext&) noexcept override
	{
		if (NextValue > LastCounterValue)
		{
			return;
		}

		const std::uint8_t PayloadBytes[1] = {NextValue};
		FMessage Message;
		Message.SetMessageNameId(CounterMessageName);
		Message.SetPayload(TSpan<const std::uint8_t>(PayloadBytes, sizeof(PayloadBytes)));

		const EMessagingResult BestEffortResult = Messaging.SendMessageToChannel(Message, BestEffortChannelName);
		if (BestEffortResult != EMessagingResult::Success)
		{
			MW_LOG(Error, "ex25", "counter best-effort send failed n=%u", static_cast<unsigned>(NextValue));
		}

		const EMessagingResult GuaranteedResult = Messaging.SendMessageToChannel(Message, GuaranteedChannelName);
		if (GuaranteedResult == EMessagingResult::Full)
		{
			MW_LOG(Error, "ex25", "counter guaranteed capacity full n=%u", static_cast<unsigned>(NextValue));
		}
		else if (GuaranteedResult != EMessagingResult::Success)
		{
			MW_LOG(Error, "ex25", "counter guaranteed send failed n=%u", static_cast<unsigned>(NextValue));
		}

		if (BestEffortResult == EMessagingResult::Success && GuaranteedResult == EMessagingResult::Success)
		{
			MW_LOG(Log, "ex25", "tx n=%u (best-effort + guaranteed)", static_cast<unsigned>(NextValue));
		}

		++NextValue;
	}

private:
	/** Motivation: Messaging system this actor sends through; injected at construction, never a global. */
	FMessagingSystem& Messaging;

	/** Motivation: Next counter value to send; once past LastCounterValue, Tick idles without sending. */
	std::uint8_t NextValue{FirstCounterValue};
};
} // namespace

/**
 * Motivation: Lets Board B join the WiFi SoftAP and run FCounterActor over two named channels that share
 *   one client-side loss-injecting UDP device, while Messaging owns framing, acknowledgements, pending messages, and retries.
 * Responsibilities: Join the SoftAP, open the fixed-port device, create the Messaging channels, spawn the
 *   counter, start the engine, observe injected loss and reliable abandonment, and tick in an unbounded loop.
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

	static FEsp32WifiDevice UdpDevice(ClientPort);
	MW_LOG(Log, "ex25", "udp open=%d udp_port=%u", UdpDevice.IsOpen() ? 1 : 0, static_cast<unsigned>(UdpDevice.BoundPort()));
	if (!UdpDevice.IsOpen())
	{
		MW_LOG(Error, "ex25", "udp socket failed; halting");
		return;
	}

	// This stays on the client send path only: dropping a server acknowledgement would retry and deliver the same counter value twice.
	static FPacketDropDevice DropDevice{UdpDevice, DropEveryNthSend};
	static FWorldEngine Engine{FGarbageCollectionBudget{1, 4, 8}};
	if (Engine.CreateMessagingSystem(FMessagingSystemInformation{}) != ERuntimeResult::Success)
	{
		MW_LOG(Error, "ex25", "client Messaging system creation failed; halting");
		return;
	}
	FMessagingSystem* const Messaging = Engine.GetMessagingSystem();
	if (Messaging == nullptr)
	{
		MW_LOG(Error, "ex25", "client Messaging system unavailable; halting");
		return;
	}

	const FDeviceAddress ServerAddress = MakeUdpAddress(ServerIpv4[0], ServerIpv4[1], ServerIpv4[2], ServerIpv4[3], ServerPort);
	const EMessagingResult BestEffortChannelResult = Messaging->CreateChannel({BestEffortChannelName, false, &DropDevice, ServerAddress});
	const EMessagingResult GuaranteedChannelResult = Messaging->CreateChannel({GuaranteedChannelName, true, &DropDevice, ServerAddress});
	if (BestEffortChannelResult != EMessagingResult::Success || GuaranteedChannelResult != EMessagingResult::Success)
	{
		MW_LOG(Error, "ex25", "client Messaging channel creation failed; halting");
		return;
	}

	if (Engine.RegisterClass<FCounterActor>(CounterActorTypeId, "CounterActor") != EObjectResult::Success)
	{
		MW_LOG(Error, "ex25", "client class registration failed; halting");
		return;
	}

	const TObjectPtr<UWorld> World = Engine.CreateWorld();
	const TObjectPtr<FCounterActor> Counter = Engine.CreateObject<FCounterActor>(CounterActorTypeId, *Messaging).Object;
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

	const TimePointMilliseconds BootTime = GTimeSource.Now();
	if (Engine.BeginPlay(BootTime) != ERuntimeResult::Success)
	{
		MW_LOG(Error, "ex25", "client engine begin play failed; halting");
		return;
	}
	MW_LOG(Log, "ex25", "client up (best-effort + guaranteed over one UDP link, dropping every %u-th send)", static_cast<unsigned>(DropEveryNthSend));

	std::uint32_t LastDroppedSendCount = 0;
	std::uint32_t LastAbandonedReliableMessageCount = 0;
	for (;;)
	{
		(void)Engine.Tick(GTimeSource.Now());
		const std::uint32_t DroppedSendCount = DropDevice.DroppedSendCount();
		if (DroppedSendCount != LastDroppedSendCount)
		{
			MW_LOG(Log, "ex25", "drop injector dropped sends=%u", static_cast<unsigned>(DroppedSendCount));
			LastDroppedSendCount = DroppedSendCount;
		}

		const std::uint32_t AbandonedReliableMessageCount = Messaging->GetAbandonedReliableMessageCount();
		if (AbandonedReliableMessageCount > 0 && AbandonedReliableMessageCount != LastAbandonedReliableMessageCount)
		{
			MW_LOG(Error, "ex25", "guaranteed abandoned=%u", static_cast<unsigned>(AbandonedReliableMessageCount));
			LastAbandonedReliableMessageCount = AbandonedReliableMessageCount;
		}
		SleepMilliseconds(PollPacingMilliseconds);
	}
}
