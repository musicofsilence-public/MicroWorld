#include "LoraMessagingShared.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Delegates/Delegate.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/NetworkFrame.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Log.h>
#include <MicroWorld/Net/NetHost.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/Object/ClassDescriptor.h>
#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/ObjectPtr.h>
#include <MicroWorld/PlatformEsp32/Esp32E32LoraDriver.h>
#include <MicroWorld/PlatformEsp32/Esp32Sleep.h>
#include <MicroWorld/PlatformEsp32/Esp32TimeSource.h>

#include <cstddef>
#include <cstdint>

using namespace MicroWorld;
using namespace Ex26;

namespace
{
/** Single real-time source for the server board. */
FEsp32TimeSource GTimeSource{};

/** Server engine-host profile: bounds tuned so one GC slice {1,4,8} finishes a full
 *  cycle each tick, so a spawn arriving mid-tick never fails LifecycleLocked (the
 *  proven EngineNetHostTests / two-node-demo profile). */
using FServerEngine = TEngineHost<6, 8, 256, 16, 1, 4, 4, 64>;

/** Server session host; two peer slots leave headroom above the single LoRa client. */
using FServerNet = TNetHost<2, 58>;

/** Minimal actor spawned on demand so a remote input event visibly changes the world. */
class FDemoSpawnedActor final : public AActor
{
public:
	/** Forwards the one-shot component reference and the begin counter it bumps on play. */
	FDemoSpawnedActor(FActorComponentRegistryReference Components, int& InBeginCount) noexcept
		: AActor(std::move(Components)), BeginCount(InBeginCount)
	{
	}

	/** Keeps exact descriptor-driven destruction publicly instantiable. */
	~FDemoSpawnedActor() noexcept override = default;

protected:
	/** Records that this spawned actor began on the server world exactly once. */
	void BeginPlay() noexcept override { ++BeginCount; }

private:
	/** Begin-count reference owned by the run loop; not owned by this actor. */
	int& BeginCount;
};
} // namespace

/** Server board: engine host + net frame + net host (DedicatedServer) over one E32 LoRa radio. */
void RunServer() noexcept
{
	static FEsp32E32LoraDriver Driver{MakeLoraConfig(ServerNodeId)};
	MW_LOG(Log, "ex26", "server node=%u open=%d", static_cast<unsigned>(ServerNodeId), Driver.IsOpen() ? 1 : 0);
	if (!Driver.IsOpen())
	{
		MW_LOG(Error, "ex26", "uart failed to open; halting");
		return;
	}

	// All composition objects are static (the ESP32-S3 stack lesson, §2.2).
	static FServerNet ServerNet{Driver};
	static TNetHostFrame<FServerNet> ServerFrame{ServerNet};
	static FServerEngine ServerHost{FGarbageCollectionBudget{1, 4, 8}, ServerFrame};
	static FActorComponentRegistry<0> SpawnedRegistries[MaxSpawns]{};
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
	FServerNet::FMessageHandlerBinding Binding;
	Binding.Bind(
		[](const FPeerId, const std::uint8_t Channel, TSpan<const std::uint8_t> Payload) noexcept
		{
			if (Channel != InputEventChannel || Payload.Size() < 1 || Payload[0] != SpawnRequestOpcode || SpawnSequence >= MaxSpawns)
			{
				return;
			}
			const std::size_t Slot = static_cast<std::size_t>(SpawnSequence);
			++SpawnSequence;
			const auto Creation =
				ServerHost.CreateObject<FDemoSpawnedActor>(DemoSpawnedActorTypeId, SpawnedRegistries[Slot].MakeReference(), SpawnedBeginCount);
			if (Creation.Result != EObjectResult::Success
				|| ServerHost.GetWorld().SpawnActor(TObjectPtr<AActor>{Creation.Object}) != EEngineResult::Success)
			{
				return;
			}
			++WorldActorCount;
			MW_LOG(Log, "ex26", "server spawned actor -> world actor count=%d", WorldActorCount);
		});
	FDelegateHandle Handle{};
	(void)ServerNet.AddMessageHandler(std::move(Binding), Handle);

	(void)ServerNet.Configure(ENetMode::DedicatedServer, MakeHostConfig());
	(void)ServerNet.Start(GTimeSource.Now());
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
			(void)ServerNet.Broadcast(StateBroadcastChannel, TSpan<const std::uint8_t>(StatePayload, sizeof(StatePayload)));
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
