#include "GuaranteedDeliveryShared.h"

#include <MicroWorld/Core/Delegates/DelegateResult.h>
#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Messaging/MessagingSystem.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiLink.h>
#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Transport/Wifi/UdpAddressCodec.h>

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
/** Motivation: Single real-time source for the server board. */
FEsp32TimeSource GTimeSource{};

/**
 * Motivation: Subscribes to the same counter message on both channels and logs one line per arrival, one
 *   column per channel, so the demo's best-effort gaps and guaranteed completeness are visible side by side.
 *   Takes Messaging by constructor injection and never ticks.
 * Responsibilities: Register one subscription per channel on play, log each arrival, and report the final
 *   distinct-value tallies when the guaranteed channel is complete.
 * Example:
 *   auto Ledger = Engine.CreateObject<FLedgerActor>(LedgerActorTypeId, Messaging).Object;
 *   Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Ledger});
 */
class FLedgerActor final : public AActor
{
public:
	/**
	 * Motivation: Stores injected Messaging; this actor's tick is disabled because it only reacts to messages.
	 * Responsibilities: Construct with tick disabled and capture the Messaging system reference.
	 */
	explicit FLedgerActor(FMessagingSystem& InMessaging) noexcept
		: AActor({/*bCanEverTick*/ false, /*bStartWithTickEnabled*/ false, /*TickIntervalMilliseconds*/ 0}), Messaging(InMessaging)
	{
	}

protected:
	/**
	 * Motivation: Registers one subscription per channel under this actor's weak owner, so both delivery
	 *   paths are observable without actor addressing.
	 * Responsibilities: Bind and register the best-effort and guaranteed subscribers.
	 */
	void BeginPlay() noexcept override
	{
		FObjectStore* const ObjectStore = GetObjectStore();
		if (ObjectStore == nullptr)
		{
			MW_LOG(Error, "ex25", "ledger has no object store");
			return;
		}

		FMessagingSystem::FSubscriberDelegate BestEffortSubscriber;
		const EDelegateResult BestEffortBindResult =
			BestEffortSubscriber.Bind([this](const FMessage& Message) noexcept { this->OnBestEffort(Message); });
		if (BestEffortBindResult != EDelegateResult::Success)
		{
			MW_LOG(Error, "ex25", "ledger best-effort subscriber bind failed");
		}
		else
		{
			const EMessagingResult BestEffortSubscribeResult = Messaging.SubscribeToChannel(
				BestEffortChannelName, CounterMessageName, std::move(BestEffortSubscriber), MakeWeakOwner(*ObjectStore, GetObjectHandle()));
			if (BestEffortSubscribeResult != EMessagingResult::Success)
			{
				MW_LOG(Error, "ex25", "ledger best-effort subscription failed");
			}
		}

		FMessagingSystem::FSubscriberDelegate GuaranteedSubscriber;
		const EDelegateResult GuaranteedBindResult =
			GuaranteedSubscriber.Bind([this](const FMessage& Message) noexcept { this->OnGuaranteed(Message); });
		if (GuaranteedBindResult != EDelegateResult::Success)
		{
			MW_LOG(Error, "ex25", "ledger guaranteed subscriber bind failed");
		}
		else
		{
			const EMessagingResult GuaranteedSubscribeResult = Messaging.SubscribeToChannel(
				GuaranteedChannelName, CounterMessageName, std::move(GuaranteedSubscriber), MakeWeakOwner(*ObjectStore, GetObjectHandle()));
			if (GuaranteedSubscribeResult != EMessagingResult::Success)
			{
				MW_LOG(Error, "ex25", "ledger guaranteed subscription failed");
			}
		}
	}

private:
	/**
	 * Motivation: Records one best-effort arrival, where gaps are expected under injected loss.
	 * Responsibilities: Validate the payload, log the counter value, and update the best-effort tally.
	 */
	void OnBestEffort(const FMessage& Message) noexcept { RecordCounter(Message, "best-effort", BestEffortReceivedValues, BestEffortReceivedCount); }

	/**
	 * Motivation: Records one guaranteed arrival, where the full sequence is expected after retries.
	 * Responsibilities: Validate the payload, log the counter value, update the guaranteed tally, and report completeness once.
	 */
	void OnGuaranteed(const FMessage& Message) noexcept
	{
		RecordCounter(Message, "guaranteed", GuaranteedReceivedValues, GuaranteedReceivedCount);
		if (GuaranteedReceivedCount == CounterValueCount && !bHasLoggedGuaranteedComplete)
		{
			MW_LOG(
				Log,
				"ex25",
				"guaranteed complete %u/%u; best-effort %u/%u",
				static_cast<unsigned>(GuaranteedReceivedCount),
				static_cast<unsigned>(CounterValueCount),
				static_cast<unsigned>(BestEffortReceivedCount),
				static_cast<unsigned>(CounterValueCount));
			bHasLoggedGuaranteedComplete = true;
		}
	}

	/**
	 * Motivation: Keeps bounded distinct-value accounting identical for the two channel callbacks without
	 *   duplicating range validation and bit-mask updates.
	 * Responsibilities: Validate one counter payload, log its channel column, and add its value to the supplied tally once.
	 */
	void RecordCounter(
		const FMessage& Message, const char* InChannelLabel, std::uint32_t& InOutReceivedValues, std::uint8_t& InOutReceivedCount) noexcept
	{
		const TSpan<const std::uint8_t> Payload = Message.GetPayload();
		if (Payload.Size() < 1)
		{
			MW_LOG(Error, "ex25", "ledger received undersized %s payload", InChannelLabel);
			return;
		}

		const std::uint8_t CounterValue = Payload.Data()[0];
		if (CounterValue < FirstCounterValue || CounterValue > LastCounterValue)
		{
			MW_LOG(Error, "ex25", "ledger received out-of-range %s n=%u", InChannelLabel, static_cast<unsigned>(CounterValue));
			return;
		}

		MW_LOG(Log, "ex25", "rx %s n=%u", InChannelLabel, static_cast<unsigned>(CounterValue));
		const std::uint32_t ValueMask = 1u << static_cast<std::uint32_t>(CounterValue - FirstCounterValue);
		if ((InOutReceivedValues & ValueMask) == 0)
		{
			InOutReceivedValues |= ValueMask;
			++InOutReceivedCount;
		}
	}

	/** Motivation: Messaging system this actor listens through; injected at construction, never a global. */
	FMessagingSystem& Messaging;

	/** Motivation: Marks every distinct best-effort counter value that reached this server. */
	std::uint32_t BestEffortReceivedValues{0};

	/** Motivation: Counts distinct best-effort counter values for the final proof line. */
	std::uint8_t BestEffortReceivedCount{0};

	/** Motivation: Marks every distinct guaranteed counter value that reached this server. */
	std::uint32_t GuaranteedReceivedValues{0};

	/** Motivation: Counts distinct guaranteed counter values for the final proof line. */
	std::uint8_t GuaranteedReceivedCount{0};

	/** Motivation: Prevents duplicate reliable frames from repeating the single completion proof line. */
	bool bHasLoggedGuaranteedComplete{false};
};
} // namespace

/**
 * Motivation: Lets Board A host the WiFi SoftAP and run FLedgerActor over two named channels that share
 *   one UDP device, while Messaging owns framing, acknowledgements, pending messages, and retries.
 * Responsibilities: Host the SoftAP, open the device, create the Messaging channels, spawn the ledger,
 *   start the engine, and tick it in an unbounded loop.
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

	static FWorldEngine Engine{FGarbageCollectionBudget{1, 4, 8}};
	if (Engine.CreateMessagingSystem(FMessagingSystemInformation{}) != ERuntimeResult::Success)
	{
		MW_LOG(Error, "ex25", "server Messaging system creation failed; halting");
		return;
	}
	FMessagingSystem* const Messaging = Engine.GetMessagingSystem();
	if (Messaging == nullptr)
	{
		MW_LOG(Error, "ex25", "server Messaging system unavailable; halting");
		return;
	}

	const FDeviceAddress ClientAddress = MakeUdpAddress(ClientIpv4[0], ClientIpv4[1], ClientIpv4[2], ClientIpv4[3], ClientPort);
	// The server names the client even though it sends no application message of its own: a reliable
	// acknowledgement goes to its channel's configured address, so without this the client is unreachable.
	const EMessagingResult BestEffortChannelResult = Messaging->CreateChannel({BestEffortChannelName, false, &UdpDevice, ClientAddress});
	const EMessagingResult GuaranteedChannelResult = Messaging->CreateChannel({GuaranteedChannelName, true, &UdpDevice, ClientAddress});
	if (BestEffortChannelResult != EMessagingResult::Success || GuaranteedChannelResult != EMessagingResult::Success)
	{
		MW_LOG(Error, "ex25", "server Messaging channel creation failed; halting");
		return;
	}

	if (Engine.RegisterClass<FLedgerActor>(LedgerActorTypeId, "LedgerActor") != EObjectResult::Success)
	{
		MW_LOG(Error, "ex25", "server class registration failed; halting");
		return;
	}

	const TObjectPtr<UWorld> World = Engine.CreateWorld();
	const TObjectPtr<FLedgerActor> Ledger = Engine.CreateObject<FLedgerActor>(LedgerActorTypeId, *Messaging).Object;
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

	const TimePointMilliseconds BootTime = GTimeSource.Now();
	if (Engine.BeginPlay(BootTime) != ERuntimeResult::Success)
	{
		MW_LOG(Error, "ex25", "server engine begin play failed; halting");
		return;
	}
	MW_LOG(Log, "ex25", "server up (best-effort + guaranteed over one UDP link)");

	for (;;)
	{
		(void)Engine.Tick(GTimeSource.Now());
		SleepMilliseconds(PollPacingMilliseconds);
	}
}
