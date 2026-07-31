#include "UartMessagingShared.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Delegates/Delegate.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/EngineSystem.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/TransportHost.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>
#include <MicroWorld/Platform/Esp32/Esp32UartDriver.h>

#include <cstddef>
#include <cstdint>

using namespace MicroWorld;
using namespace Ex19;

namespace
{
/** Single real-time source for the server board. */
FEsp32TimeSource GTimeSource{};

/** Server engine traits: carries the exact capacities FServerEngine sized before the
 *  traits refactor, so the server store is unchanged. Bounds tuned so one GC slice
 *  {1,4,8} finishes a full cycle each tick, so a spawn arriving mid-tick never fails
 *  LifecycleLocked (the proven EngineHostTests / two-node-demo profile). */
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

/** Server session host; two peer slots leave headroom above the single wired client. */
using FServerTransport = TTransportHost<2, 120>;

/** Minimal actor spawned on demand so a remote input event visibly changes the world. */
class FDemoSpawnedActor final : public AActor
{
public:
	/** Binds the begin counter this actor bumps on play. */
	FDemoSpawnedActor(int& InBeginCount) noexcept : AActor(), BeginCount(InBeginCount) {}

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

/** Server board: engine host + net frame + net host (DedicatedServer) over one UART. */
void RunServer() noexcept
{
	static FEsp32UartDriver Driver{MakeUartConfig(ServerNodeId)};
	MW_LOG(Log, "ex19", "server node=%u open=%d", static_cast<unsigned>(ServerNodeId), Driver.IsOpen() ? 1 : 0);
	if (!Driver.IsOpen())
	{
		MW_LOG(Error, "ex19", "uart failed to open; halting");
		return;
	}

	// All composition objects are static (the ESP32-S3 stack lesson, §2.2).
	static FServerTransport ServerTransport{Driver};
	static THostPlaySystem<FServerTransport> ServerFrame{ServerTransport};
	static FServerEngine ServerHost{FGarbageCollectionBudget{1, 4, 8}, ServerFrame};
	static int SpawnSequence = 0;
	static int SpawnedBeginCount = 0;
	static int WorldActorCount = 0;

	if (ServerHost.RegisterClass<FDemoSpawnedActor>(DemoSpawnedActorTypeId, "DemoSpawnedActor") != EObjectResult::Success
		|| ServerHost.CreateWorld().Get() == nullptr)
	{
		MW_LOG(Error, "ex19", "server world setup failed; halting");
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
			MW_LOG(Log, "ex19", "server spawned actor -> world actor count=%d", WorldActorCount);
		});
	FDelegateHandle Handle{};
	(void)ServerTransport.AddMessageHandler(std::move(Binding), Handle);

	(void)ServerTransport.Configure(ENetworkMode::DedicatedServer, MakeHostConfig());
	(void)ServerTransport.Start(GTimeSource.Now());
	(void)ServerHost.BeginPlay(GTimeSource.Now());
	MW_LOG(Log, "ex19", "server listening (no WiFi -- UART only)");

	std::uint8_t StateTick = 0;
	bool bDoneAnnounced = false;
	for (;;)
	{
		// Tick runs PumpReceive (delivers spawn requests -> handler) then PumpSend
		// (flushes the Welcome/heartbeats and the broadcast queued last iteration).
		(void)ServerHost.Tick(GTimeSource.Now());
		++StateTick;
		const std::uint8_t StatePayload[2] = {StateTick, static_cast<std::uint8_t>(WorldActorCount)};
		(void)ServerTransport.Broadcast(StateBroadcastChannel, TSpan<const std::uint8_t>(StatePayload, sizeof(StatePayload)));
		if (!bDoneAnnounced && WorldActorCount >= MaxSpawns)
		{
			MW_LOG(Log, "ex19", "done (server spawned %d actors)", WorldActorCount);
			bDoneAnnounced = true;
		}
		SleepMilliseconds(PollPacingMilliseconds);
	}
}
