#include "LoraMessagingShared.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Delegates/DelegateHandle.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/EngineSystem.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/NetworkMode.h>
#include <MicroWorld/Transport/PeerId.h>
#include <MicroWorld/Transport/TransportHost.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Platform/Esp32/Esp32LoraDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>

#include <cstddef>
#include <cstdint>

using namespace MicroWorld::Core;
using namespace MicroWorld::Platform::Esp32;
using namespace MicroWorld::Engine;
using namespace MicroWorld::Transport;
using namespace Ex26;

namespace
{
/** Motivation: Single real-time source for the server board. */
FEsp32TimeSource GTimeSource{};

/**
 * Motivation: Carries the exact capacities FServerEngine sized before the traits refactor, so the
 *   server store is unchanged. Bounds tuned so one GC slice {1,4,8} finishes a full cycle each
 *   tick, so a spawn arriving mid-tick never fails LifecycleLocked (the proven two-node-demo profile).
 * Responsibilities: Name the class, object, slot, root, actor, and timer capacities the server uses.
 * Example:
 *   using FServerEngine = TEngine<FServerEngineTraits>;
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

/** Motivation: Server session host; two peer slots leave headroom above the single LoRa client. */
using FServerTransport = TTransportHost<2, 58>;

/**
 * Motivation: Minimal actor spawned on demand so a remote input event visibly changes the world,
 *   keeping the demo's "client request produces a server-side actor" loop observable.
 * Responsibilities: Bump one begin counter when play begins, and stay descriptor-destroyable.
 * Example:
 *   auto Creation = ServerHost.CreateObject<FDemoSpawnedActor>(DemoSpawnedActorTypeId, Count);
 *   ServerHost.GetWorld().SpawnActor(TObjectPtr<AActor>{Creation.Object});
 */
class FDemoSpawnedActor final : public AActor
{
public:
	/**
	 * Motivation: Binds the begin counter this actor bumps on play, so the run loop can observe spawns.
	 * Responsibilities: Store the counter reference and forward to the actor base.
	 */
	FDemoSpawnedActor(int& InBeginCount) noexcept : AActor(), BeginCount(InBeginCount) {}

	/**
	 * Motivation: Keeps exact descriptor-driven destruction publicly instantiable.
	 * Responsibilities: Default the destructor so descriptor-driven teardown stays available.
	 */
	~FDemoSpawnedActor() noexcept override = default;

protected:
	/**
	 * Motivation: Records that this spawned actor began on the server world exactly once.
	 * Responsibilities: Bump the bound begin counter on play and do nothing else.
	 */
	void BeginPlay() noexcept override { ++BeginCount; }

private:
	/** Motivation: Begin-count reference owned by the run loop; not owned by this actor. */
	int& BeginCount;
};
} // namespace

/**
 * Motivation: Lets one board act as the dedicated server half of example 26 over a single E32 LoRa radio,
 *   so the engine plus transport composition can be reasoned about in one place.
 * Responsibilities: Open the radio, run the engine and a TTransportHost dedicated server, spawn actors
 *   on client requests, and broadcast world state at the airtime-paced period each cycle.
 */
void RunServer() noexcept
{
	static FEsp32LoraDevice Device{MakeLoraConfig(ServerNodeId)};
	MW_LOG(Log, "ex26", "server node=%u open=%d", static_cast<unsigned>(ServerNodeId), Device.IsOpen() ? 1 : 0);
	if (!Device.IsOpen())
	{
		MW_LOG(Error, "ex26", "uart failed to open; halting");
		return;
	}

	// All composition objects are static (the ESP32-S3 stack lesson, §2.2).
	static FServerTransport ServerTransport{Device};
	static THostPlaySystem<FServerTransport> ServerFrame{ServerTransport};
	static FServerEngine ServerHost{FGarbageCollectionBudget{1, 4, 8}, ServerFrame};
	static int SpawnSequence = 0;
	static int SpawnedBeginCount = 0;
	static int WorldActorCount = 0;

	if (ServerHost.RegisterClass<FDemoSpawnedActor>(DemoSpawnedActorTypeId, "DemoSpawnedActor") != EObjectResult::Success
		|| ServerHost.CreateWorld().Get() == nullptr)
	{
		MW_LOG(Error, "ex26", "server world setup failed; halting");
		return;
	}

	// Channel-1 spawn handler. The lambda captures nothing: it names the static
	// run-loop state directly, which keeps it inside the delegate's inline budget.
	FServerTransport::FMessageHandlerBinding Binding;
	Binding.Bind(
		[](const FPeerId, const std::uint8_t Channel, TSpan<const std::uint8_t> Payload) noexcept
		{
			if (Channel != InputEventChannel || Payload.Size() < 1 || Payload[0] != SpawnRequestOpcode || SpawnSequence >= MaxSpawns)
			{
				return;
			}
			++SpawnSequence;
			const auto Creation = ServerHost.CreateObject<FDemoSpawnedActor>(DemoSpawnedActorTypeId, SpawnedBeginCount);
			if (Creation.Result != EObjectResult::Success
				|| ServerHost.GetWorld().SpawnActor(TObjectPtr<AActor>{Creation.Object}) != EEngineResult::Success)
			{
				return;
			}
			++WorldActorCount;
			MW_LOG(Log, "ex26", "server spawned actor -> world actor count=%d", WorldActorCount);
		});
	FDelegateHandle Handle{};
	(void)ServerTransport.AddMessageHandler(std::move(Binding), Handle);

	(void)ServerTransport.Configure(ENetworkMode::DedicatedServer, MakeHostConfig());
	(void)ServerTransport.Start(GTimeSource.Now());
	(void)ServerHost.BeginPlay(GTimeSource.Now());
	MW_LOG(Log, "ex26", "server listening (no WiFi -- LoRa radio only)");

	std::uint8_t StateTick = 0;
	bool bDoneAnnounced = false;
	std::uint64_t NextStateBroadcastDueMilliseconds = GTimeSource.Now();
	for (;;)
	{
		// Tick runs PumpReceive (delivers spawn requests -> handler) then PumpSend
		// (flushes the Welcome/heartbeats and any broadcast queued below). Ticking the
		// engine every poll is local and cheap; only the radio broadcast below is paced
		// (a full LoRa frame costs hundreds of ms of airtime, so it cannot fire every tick).
		const std::uint64_t Now = GTimeSource.Now();
		(void)ServerHost.Tick(Now);
		if (Now >= NextStateBroadcastDueMilliseconds)
		{
			++StateTick;
			const std::uint8_t StatePayload[2] = {StateTick, static_cast<std::uint8_t>(WorldActorCount)};
			(void)ServerTransport.Broadcast(StateBroadcastChannel, TSpan<const std::uint8_t>(StatePayload, sizeof(StatePayload)));
			NextStateBroadcastDueMilliseconds = Now + StateBroadcastPeriodMilliseconds;
		}
		if (!bDoneAnnounced && WorldActorCount >= MaxSpawns)
		{
			MW_LOG(Log, "ex26", "done (server spawned %d actors)", WorldActorCount);
			bDoneAnnounced = true;
		}
		SleepMilliseconds(PollPacingMilliseconds);
	}
}
