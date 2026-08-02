#include "TwoBoardWireShared.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Delegates/DelegateResult.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Core/TickContext.h>
#include <MicroWorld/Messaging/ChannelInformation.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessagingResult.h>
#include <MicroWorld/Messaging/MessagingSystem.h>
#include <MicroWorld/Messaging/MessagingSystemInformation.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/GarbageCollectionBudget.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>
#include <MicroWorld/Platform/Esp32/Esp32UartDevice.h>

#include <cstddef>
#include <cstdint>

using namespace MicroWorld::Core;
using namespace MicroWorld::Platform::Esp32;
using namespace MicroWorld::Engine;
using namespace MicroWorld::Messaging;
using namespace Ex23;

namespace
{
/** Motivation: Single real-time source for the client board. */
FEsp32TimeSource GTimeSource{};

/** Motivation: Cadence the switch toggles the lamp and broadcasts a heartbeat, per the roadmap's "every 2 s". */
constexpr DurationMilliseconds SwitchToggleIntervalMilliseconds = 2000;

/**
 * Motivation: Toggles a lamp state and sends it to the remote lamp every SwitchToggleIntervalMilliseconds,
 *   and sends an incrementing heartbeat counter alongside it. Takes Messaging by constructor injection
 *   and owns no components.
 * Responsibilities: Tick on the 2 s cadence, send one lamp toggle, and send one heartbeat.
 * Example:
 *   auto Switch = Engine.CreateObject<FSwitchActor>(SwitchActorTypeId, Messaging).Object;
 *   Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Switch});
 */
class FSwitchActor final : public AActor
{
public:
	/**
	 * Motivation: Aligns this actor's own tick to the 2 s toggle cadence and stores injected Messaging.
	 * Responsibilities: Construct on the toggle cadence and capture the Messaging system reference.
	 */
	explicit FSwitchActor(FMessagingSystem& InMessaging) noexcept
		: AActor(FTickConfiguration::EnabledEvery(SwitchToggleIntervalMilliseconds)), Messaging(InMessaging)
	{
	}

protected:
	/**
	 * Motivation: Drives the per-tick exchange so the lamp toggle and heartbeat stay in step.
	 * Responsibilities: Send the named lamp toggle then application heartbeat counter, each tick.
	 */
	void Tick(const FTickContext&) noexcept override
	{
		SendLampToggle();
		BroadcastHeartbeat();
	}

private:
	/**
	 * Motivation: Flips bLampOn and sends its new value as a 1-byte message the remote lamp subscribes to.
	 * Responsibilities: Toggle the state, send it on the application channel, and log the outcome.
	 */
	void SendLampToggle() noexcept
	{
		bLampOn = !bLampOn;
		const std::uint8_t StateByte = bLampOn ? 1 : 0;
		FMessage Message;
		Message.SetMessageNameId(SetLampStateMessageName);
		Message.SetPayload(TSpan<const std::uint8_t>(&StateByte, 1));
		const EMessagingResult SendResult = Messaging.SendMessageToChannel(Message, AppChannelName);
		if (SendResult != EMessagingResult::Success)
		{
			MW_LOG(Error, "ex23", "switch lamp send failed");
			return;
		}
		MW_LOG(Log, "ex23", "switch -> lamp %s", bLampOn ? "ON" : "OFF");
	}

	/**
	 * Motivation: Bumps HeartbeatCount and sends it as a 1-byte message the remote display subscribes to.
	 *   This is the example's own application heartbeat, unrelated to any transport-level liveness.
	 * Responsibilities: Increment the counter, send it, and log the outcome.
	 */
	void BroadcastHeartbeat() noexcept
	{
		++HeartbeatCount;
		FMessage Message;
		Message.SetMessageNameId(HeartbeatCountMessageName);
		Message.SetPayload(TSpan<const std::uint8_t>(&HeartbeatCount, 1));
		const EMessagingResult SendResult = Messaging.SendMessageToChannel(Message, AppChannelName);
		if (SendResult != EMessagingResult::Success)
		{
			MW_LOG(Error, "ex23", "switch heartbeat broadcast failed");
			return;
		}
		MW_LOG(Log, "ex23", "switch broadcast heartbeat=%u", static_cast<unsigned>(HeartbeatCount));
	}

	/** Motivation: Messaging system this actor sends through; injected at construction, never a global. */
	FMessagingSystem& Messaging;

	/** Motivation: Current toggled lamp state; flips every tick, starting OFF -> first send is ON. */
	bool bLampOn{false};

	/** Motivation: Counts every heartbeat sent since BeginPlay; wraps at 256 (accepted for this bounded-value demo). */
	std::uint8_t HeartbeatCount{0};
};
} // namespace

/**
 * Motivation: Lets Board B (node 2) run FSwitchActor over one Messaging channel on its UART device, so
 *   the client half of the two-board wire demo can be reasoned about in one place.
 * Responsibilities: Open the UART, create the Messaging system and its channel, spawn the switch, and
 *   tick the engine in an unbounded loop.
 */
void RunClient() noexcept
{
	static FEsp32UartDevice Device{MakeUartConfig(ClientNodeId)};
	MW_LOG(Log, "ex23", "client node=%u open=%d", static_cast<unsigned>(ClientNodeId), Device.IsOpen() ? 1 : 0);
	if (!Device.IsOpen())
	{
		MW_LOG(Error, "ex23", "uart failed to open; halting");
		return;
	}

	// All composition objects are static (the ESP32-S3 stack lesson, §2.2).
	static FWireEngine Engine{FGarbageCollectionBudget{1, 4, 8}};
	if (Engine.CreateMessagingSystem(FMessagingSystemInformation{}) != ERuntimeResult::Success)
	{
		MW_LOG(Error, "ex23", "client Messaging system creation failed; halting");
		return;
	}
	FMessagingSystem* const Messaging = Engine.GetMessagingSystem();
	if (Messaging == nullptr)
	{
		MW_LOG(Error, "ex23", "client Messaging system unavailable; halting");
		return;
	}
	// UART is point-to-point, so its device ignores this empty destination address.
	if (Messaging->CreateChannel({AppChannelName, false, &Device, {}}) != EMessagingResult::Success)
	{
		MW_LOG(Error, "ex23", "client Messaging channel creation failed; halting");
		return;
	}

	if (Engine.RegisterClass<FSwitchActor>(SwitchActorTypeId, "SwitchActor") != EObjectResult::Success)
	{
		MW_LOG(Error, "ex23", "client class registration failed; halting");
		return;
	}

	const TObjectPtr<UWorld> World = Engine.CreateWorld();
	const TObjectPtr<FSwitchActor> Switch = Engine.CreateObject<FSwitchActor>(SwitchActorTypeId, *Messaging).Object;
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
		(void)Engine.Tick(GTimeSource.Now());
		SleepMilliseconds(PollPacingMilliseconds);
	}
}
