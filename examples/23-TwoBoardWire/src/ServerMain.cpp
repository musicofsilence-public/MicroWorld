#include "TwoBoardWireShared.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Delegates/DelegateResult.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Messaging/MessageTypes.h>
#include <MicroWorld/Messaging/MessagingSystem.h>
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
using namespace MicroWorld::Messaging;
using namespace Ex23;

namespace
{
/** Motivation: Single real-time source for the server board. */
FEsp32TimeSource GTimeSource{};

/**
 * Motivation: Subscribes to the lamp command arriving over the wire and logs the decoded state, so the
 *   lamp half of the two-board wire demo is observable. Takes Messaging by constructor injection and
 *   never ticks.
 * Responsibilities: Register a message-filtered subscription on play and log each received state.
 * Example:
 *   auto Lamp = Engine.CreateObject<FLampActor>(LampActorTypeId, Messaging).Object;
 *   Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Lamp});
 */
class FLampActor final : public AActor
{
public:
	/**
	 * Motivation: Stores injected Messaging; this actor's tick is disabled because it only reacts to a message.
	 * Responsibilities: Construct with tick disabled and capture the Messaging system reference.
	 */
	explicit FLampActor(FMessagingSystem& InMessaging) noexcept
		: AActor({/*bCanEverTick*/ false, /*bStartWithTickEnabled*/ false, /*TickIntervalMilliseconds*/ 0}), Messaging(InMessaging)
	{
	}

protected:
	/**
	 * Motivation: Subscribes to the lamp command by message name, so later toggles from the remote switch
	 *   reach this lamp; the name filter is the whole of the addressing this needs.
	 * Responsibilities: Bind the lamp-state subscriber and register it under this actor's weak owner.
	 */
	void BeginPlay() noexcept override
	{
		FMessagingSystem::FSubscriberDelegate Subscriber;
		const EDelegateResult BindResult = Subscriber.Bind([this](const FMessage& Message) noexcept { this->OnLampStateReceived(Message); });
		if (BindResult != EDelegateResult::Success)
		{
			MW_LOG(Error, "ex23", "lamp subscriber bind failed");
			return;
		}

		FObjectStore* const ObjectStore = GetObjectStore();
		if (ObjectStore == nullptr)
		{
			MW_LOG(Error, "ex23", "lamp has no object store");
			return;
		}

		const EMessagingResult SubscribeResult = Messaging.SubscribeToChannel(
			AppChannelName, SetLampStateMessageName, std::move(Subscriber), MakeWeakOwner(*ObjectStore, GetObjectHandle()));
		if (SubscribeResult != EMessagingResult::Success)
		{
			MW_LOG(Error, "ex23", "lamp subscription failed");
		}
	}

private:
	/**
	 * Motivation: Decodes the 1-byte state and logs the lamp's new state; this is the handler bound in BeginPlay.
	 * Responsibilities: Validate the payload and log ON or OFF.
	 */
	void OnLampStateReceived(const FMessage& Message) noexcept
	{
		const TSpan<const std::uint8_t> Payload = Message.GetPayload();
		if (Payload.Size() < 1)
		{
			MW_LOG(Error, "ex23", "lamp received undersized state payload");
			return;
		}
		const bool bLampOn = Payload.Data()[0] != 0;
		MW_LOG(Log, "ex23", "lamp %s", bLampOn ? "ON" : "OFF");
	}

	/** Motivation: Messaging system this actor listens through; injected at construction, never a global. */
	FMessagingSystem& Messaging;
};

/**
 * Motivation: Subscribes to the heartbeat counter arriving over the wire and logs every count, so the
 *   heartbeat half of the two-board wire demo is observable. Takes Messaging by constructor injection
 *   and never ticks.
 * Responsibilities: Register a message-filtered subscription on play and log each received count.
 * Example:
 *   auto Display = Engine.CreateObject<FDisplayActor>(DisplayActorTypeId, Messaging).Object;
 *   Engine.GetWorld().RegisterActor(TObjectPtr<AActor>{Display});
 */
class FDisplayActor final : public AActor
{
public:
	/**
	 * Motivation: Stores injected Messaging; this actor's tick is disabled because it only reacts to a message.
	 * Responsibilities: Construct with tick disabled and capture the Messaging system reference.
	 */
	explicit FDisplayActor(FMessagingSystem& InMessaging) noexcept
		: AActor({/*bCanEverTick*/ false, /*bStartWithTickEnabled*/ false, /*TickIntervalMilliseconds*/ 0}), Messaging(InMessaging)
	{
	}

protected:
	/**
	 * Motivation: Subscribes to the heartbeat counter by message name, so the display receives each one
	 *   the remote switch sends on the shared application channel.
	 * Responsibilities: Bind the heartbeat subscriber and register it under this actor's weak owner.
	 */
	void BeginPlay() noexcept override
	{
		FMessagingSystem::FSubscriberDelegate Subscriber;
		const EDelegateResult BindResult = Subscriber.Bind([this](const FMessage& Message) noexcept { this->OnHeartbeatReceived(Message); });
		if (BindResult != EDelegateResult::Success)
		{
			MW_LOG(Error, "ex23", "display subscriber bind failed");
			return;
		}

		FObjectStore* const ObjectStore = GetObjectStore();
		if (ObjectStore == nullptr)
		{
			MW_LOG(Error, "ex23", "display has no object store");
			return;
		}

		const EMessagingResult SubscribeResult = Messaging.SubscribeToChannel(
			AppChannelName, HeartbeatCountMessageName, std::move(Subscriber), MakeWeakOwner(*ObjectStore, GetObjectHandle()));
		if (SubscribeResult != EMessagingResult::Success)
		{
			MW_LOG(Error, "ex23", "display subscription failed");
		}
	}

private:
	/**
	 * Motivation: Decodes the 1-byte counter and logs it; this is the handler bound in BeginPlay.
	 * Responsibilities: Validate the payload and log the received count.
	 */
	void OnHeartbeatReceived(const FMessage& Message) noexcept
	{
		const TSpan<const std::uint8_t> Payload = Message.GetPayload();
		if (Payload.Size() < 1)
		{
			MW_LOG(Error, "ex23", "display received undersized heartbeat payload");
			return;
		}
		MW_LOG(Log, "ex23", "heartbeat=%u", static_cast<unsigned>(Payload.Data()[0]));
	}

	/** Motivation: Messaging system this actor listens through; injected at construction, never a global. */
	FMessagingSystem& Messaging;
};
} // namespace

/**
 * Motivation: Lets Board A (node 1) run FLampActor and FDisplayActor over one Messaging channel on its
 *   UART device, so the server half of the two-board wire demo can be reasoned about in one place.
 * Responsibilities: Open the UART, create the Messaging system and its channel, spawn both actors, and
 *   tick the engine in an unbounded loop.
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
	static FWireEngine Engine{FGarbageCollectionBudget{1, 4, 8}};
	if (Engine.CreateMessagingSystem(FMessagingSystemInformation{}) != ERuntimeResult::Success)
	{
		MW_LOG(Error, "ex23", "server Messaging system creation failed; halting");
		return;
	}
	FMessagingSystem* const Messaging = Engine.GetMessagingSystem();
	if (Messaging == nullptr)
	{
		MW_LOG(Error, "ex23", "server Messaging system unavailable; halting");
		return;
	}
	// UART is point-to-point, so its device ignores this empty destination address.
	if (Messaging->CreateChannel({AppChannelName, false, &Device, {}}) != EMessagingResult::Success)
	{
		MW_LOG(Error, "ex23", "server Messaging channel creation failed; halting");
		return;
	}

	if (Engine.RegisterClass<FLampActor>(LampActorTypeId, "LampActor") != EObjectResult::Success
		|| Engine.RegisterClass<FDisplayActor>(DisplayActorTypeId, "DisplayActor") != EObjectResult::Success)
	{
		MW_LOG(Error, "ex23", "server class registration failed; halting");
		return;
	}

	const TObjectPtr<UWorld> World = Engine.CreateWorld();
	const TObjectPtr<FLampActor> Lamp = Engine.CreateObject<FLampActor>(LampActorTypeId, *Messaging).Object;
	const TObjectPtr<FDisplayActor> Display = Engine.CreateObject<FDisplayActor>(DisplayActorTypeId, *Messaging).Object;
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
		(void)Engine.Tick(GTimeSource.Now());
		SleepMilliseconds(PollPacingMilliseconds);
	}
}
