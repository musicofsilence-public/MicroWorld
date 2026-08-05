#include "LoraMessagingShared.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/DefaultEngineTraits.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/GarbageCollectionBudget.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Engine/PlaySystemSet.h>
#include <MicroWorld/Messaging/ChannelInformation.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessagingSystem.h>
#include <MicroWorld/Networking/NetworkSystem.h>
#include <MicroWorld/Platform/Esp32/Esp32LoraDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>

#include <cstddef>
#include <cstdint>

using namespace MicroWorld::Core;
using namespace MicroWorld::Engine;
using namespace MicroWorld::Messaging;
using namespace MicroWorld::Networking;
using namespace MicroWorld::Platform::Esp32;
using namespace Ex26;

namespace
{
/** Motivation: Supplies the server board's monotonic session time. */
FEsp32TimeSource GTimeSource{};
/**
 * Motivation: Bounds the server world around the two requested demo spawns.
 * Responsibilities: Supply the fixed server-side Engine capacities used by this example.
 * Example: FServerEngine ServerHost{Budget, Frames};
 */
struct FServerEngineTraits : FDefaultEngineTraits
{
	static constexpr std::size_t MaxClasses = 6;
	static constexpr std::size_t MaxObjects = 8;
	static constexpr std::size_t SlotSizeBytes = 256;
	static constexpr std::size_t MaxRoots = 1;
	static constexpr std::size_t MaxActors = 4;
	static constexpr std::size_t MaxTimers = 4;
};
using FServerEngine = TEngine<FServerEngineTraits>;

/**
 * Motivation: Makes each accepted remote request visible in the server world.
 * Responsibilities: Increment the composition-owned begin counter when the actor enters play.
 * Example: ServerHost.CreateObject<FDemoSpawnedActor>(DemoSpawnedActorTypeId, BeginCount);
 */
class FDemoSpawnedActor final : public AActor
{
public:
	/**
	 * Motivation: Gives descriptor construction the counter owned by the composition root.
	 * Responsibilities: Retain the reference.
	 */
	explicit FDemoSpawnedActor(int& InBeginCount) noexcept : BeginCount(InBeginCount) {}
	/**
	 * Motivation: Preserves descriptor-driven teardown.
	 * Responsibilities: Release no owned state.
	 */
	~FDemoSpawnedActor() noexcept override = default;

protected:
	/**
	 * Motivation: Confirms the spawned actor reached play.
	 * Responsibilities: Increment the supplied counter once.
	 */
	void BeginPlay() noexcept override { ++BeginCount; }

private:
	/** Motivation: Records confirmed actor begins outside managed object storage. */
	int& BeginCount;
};

/** Motivation: Counts bounded client requests accepted by the server. */
int GSpawnSequence = 0;
/** Motivation: Counts spawned actors that completed BeginPlay. */
int GSpawnedBeginCount = 0;
/** Motivation: Reports the world-visible actor count to connected clients. */
int GWorldActorCount = 0;
/** Motivation: Lets the static input callback use the composed server without capturing. */
FServerEngine* GServerHost = nullptr;
/** Motivation: Lets the static input callback validate its Network-provided sender. */
FNetworkSystem* GServerNetwork = nullptr;

/**
 * Motivation: Accepts one valid remote spawn request without exposing a device address.
 * Responsibilities: Resolve the peer and mutate the world only for the declared opcode.
 */
void HandleSpawnRequest(const FMessage& InMessage) noexcept
{
	if (GServerHost == nullptr || GServerNetwork == nullptr || !GServerNetwork->ResolveSenderPeer(InMessage).IsValid()
		|| InMessage.GetPayload().Size() != 1 || InMessage.GetPayload()[0] != SpawnRequestOpcode || GSpawnSequence >= MaxSpawns)
	{
		return;
	}
	const TObjectCreationResult<FDemoSpawnedActor> Creation =
		GServerHost->CreateObject<FDemoSpawnedActor>(DemoSpawnedActorTypeId, GSpawnedBeginCount);
	if (Creation.Result != EObjectResult::Success
		|| GServerHost->GetWorld().SpawnActor(TObjectPtr<AActor>{Creation.Object}) != EEngineResult::Success)
	{
		return;
	}
	++GSpawnSequence;
	++GWorldActorCount;
	MW_LOG(Log, "ex26", "server spawned actor -> world actor count=%d", GWorldActorCount);
}

/**
 * Motivation: Builds the Engine-owned LoRa server composition before it accepts clients.
 * Responsibilities: Register the device, create systems and world, and install the bounded spawn handler.
 */
bool SetupServer(FEsp32LoraDevice& Device, TPlaySystemSet<1>& DeviceFrames, FServerEngine& ServerHost, FNetworkSystem*& OutNetwork) noexcept
{
	if (DeviceFrames.Add(Device) != EEngineResult::Success || ServerHost.CreateMessagingSystem({}) != ERuntimeResult::Success)
	{
		MW_LOG(Error, "ex26", "server messaging setup failed; halting");
		return false;
	}

	FMessagingSystem* const Messaging = ServerHost.GetMessagingSystem();
	FMessagingLinkId LinkId{};
	if (Messaging == nullptr || Messaging->RegisterLink(Device, LinkId) != EMessagingResult::Success
		|| ServerHost.CreateNetworkSystem(MakeNetworkInformation(ENetworkRole::Server)) != ENetworkResult::Success
		|| Messaging->CreateChannel({InputEventChannel, false, nullptr, {}}) != EMessagingResult::Success
		|| Messaging->CreateChannel({StateBroadcastChannel, false, nullptr, {}}) != EMessagingResult::Success)
	{
		MW_LOG(Error, "ex26", "server network setup failed; halting");
		return false;
	}

	OutNetwork = ServerHost.GetNetworkSystem();
	GServerHost = &ServerHost;
	GServerNetwork = OutNetwork;
	FMessagingSystem::FSubscriberDelegate SpawnSubscriber;
	if (OutNetwork == nullptr || SpawnSubscriber.Bind(HandleSpawnRequest) != EDelegateResult::Success
		|| Messaging->SubscribeToChannel(InputEventChannel, SpawnRequestMessageName, std::move(SpawnSubscriber)) != EMessagingResult::Success
		|| ServerHost.RegisterClass<FDemoSpawnedActor>(DemoSpawnedActorTypeId, "DemoSpawnedActor") != EObjectResult::Success
		|| ServerHost.CreateWorld().Get() == nullptr)
	{
		MW_LOG(Error, "ex26", "server world setup failed; halting");
		return false;
	}

	return true;
}

/**
 * Motivation: Starts the complete Engine-owned server lifecycle only after setup succeeds.
 * Responsibilities: Begin play with caller-supplied time so Networking can admit peers from the message loop.
 */
bool StartServer(FServerEngine& ServerHost) noexcept
{
	if (ServerHost.BeginPlay(GTimeSource.Now()) != ERuntimeResult::Success)
	{
		MW_LOG(Error, "ex26", "server world setup failed; halting");
		return false;
	}

	return true;
}

/**
 * Motivation: Keeps LoRa state publication within the measured airtime cadence.
 * Responsibilities: Tick the Engine, broadcast at StateBroadcastPeriodMilliseconds, and announce completion once.
 */
void RunServerMessageLoop(FServerEngine& ServerHost, FNetworkSystem& Network) noexcept
{
	std::uint8_t StateTick = 0;
	TimePointMilliseconds NextStateBroadcastDueMilliseconds = GTimeSource.Now();
	bool bDoneAnnounced = false;

	for (;;)
	{
		const TimePointMilliseconds Now = GTimeSource.Now();
		(void)ServerHost.Tick(Now);
		if (Now >= NextStateBroadcastDueMilliseconds)
		{
			const std::uint8_t StatePayload[2] = {++StateTick, static_cast<std::uint8_t>(GWorldActorCount)};
			FMessage StateMessage;
			StateMessage.SetMessageNameId(StateMessageName);
			StateMessage.SetPayload(TSpan<const std::uint8_t>(StatePayload, sizeof(StatePayload)));
			(void)Network.Broadcast(StateBroadcastChannel, StateMessage);
			NextStateBroadcastDueMilliseconds = Now + StateBroadcastPeriodMilliseconds;
		}
		if (!bDoneAnnounced && GWorldActorCount >= MaxSpawns)
		{
			MW_LOG(Log, "ex26", "done (server spawned %d actors)", GWorldActorCount);
			bDoneAnnounced = true;
		}
		SleepMilliseconds(PollPacingMilliseconds);
	}
}
} // namespace

/**
 * Motivation: Runs the LoRa server role with device ownership below Messaging and Network.
 * Responsibilities: Admit clients, spawn bounded actors, and broadcast at the measured airtime pace.
 */
void RunServer() noexcept
{
	static FEsp32LoraDevice Device{MakeLoraConfig(ServerNodeId)};
	if (!Device.IsOpen())
	{
		MW_LOG(Error, "ex26", "radio failed to open; halting");
		return;
	}

	static TPlaySystemSet<1> DeviceFrames;
	static FServerEngine ServerHost{FGarbageCollectionBudget{1, 4, 8}, DeviceFrames};
	FNetworkSystem* Network = nullptr;
	if (!SetupServer(Device, DeviceFrames, ServerHost, Network))
	{
		return;
	}

	if (!StartServer(ServerHost))
	{
		return;
	}

	RunServerMessageLoop(ServerHost, *Network);
}
