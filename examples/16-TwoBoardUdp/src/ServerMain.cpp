#include "UdpMessagingShared.h"

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
#include <MicroWorld/PlatformEsp32/Esp32Sleep.h>
#include <MicroWorld/PlatformEsp32/Esp32TimeSource.h>
#include <MicroWorld/PlatformEsp32/Esp32UdpDriver.h>
#include <MicroWorld/PlatformEsp32/Esp32WifiLink.h>

#include <cstddef>
#include <cstdint>

using namespace MicroWorld;
using namespace Ex16;

namespace
{
/** Single real-time source for the server board. */
FEsp32TimeSource GTimeSource{};

/** Server engine-host profile: bounds tuned so one GC slice {1,4,8} finishes a full
 *  cycle each tick, so a spawn arriving mid-tick never fails LifecycleLocked (the
 *  proven EngineNetHostTests / two-node-demo profile). */
using FServerEngine = TEngineHost<6, 8, 256, 16, 1, 4, 4, 64>;

/** Server session host; two peer slots leave headroom above the single client. The
 *  256-byte packet capacity matches the host TwoNodeDemo (the UART example 19 used
 *  120 for its small wire MTU; UDP has room to spare). */
using FServerNet = TNetHost<2, 256>;

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

/** Server board: engine host + net frame + net host (DedicatedServer) over one UDP socket. */
void RunServer() noexcept
{
	static FEsp32WifiLink WifiLink;
	if (WifiLink.StartAccessPoint(FEsp32AccessPointConfig{DemoApSsid, DemoApPassword, /*WifiChannel*/ 1, /*MaxStations*/ 4}) != ENetResult::Success)
	{
		MW_LOG(Error, "ex16", "wifi failed; halting");
		return;
	}
	MW_LOG(Log, "ex16", "wifi softap up, gateway 192.168.4.1");

	// The driver is constructed only after WiFi/netif is up (lwIP must exist first).
	// The server hosts the SoftAP, so its address is the fixed gateway 192.168.4.1.
	static FEsp32UdpDriver Driver(ServerPort);
	MW_LOG(Log, "ex16", "server open=%d udp_port=%u", Driver.IsOpen() ? 1 : 0, static_cast<unsigned>(Driver.BoundPort()));
	if (!Driver.IsOpen())
	{
		MW_LOG(Error, "ex16", "socket failed; halting");
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
		MW_LOG(Error, "ex16", "server world setup failed; halting");
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
			MW_LOG(Log, "ex16", "server spawned actor -> world actor count=%d", WorldActorCount);
		});
	FDelegateHandle Handle{};
	(void)ServerNet.AddMessageHandler(std::move(Binding), Handle);

	(void)ServerNet.Configure(ENetMode::DedicatedServer, MakeHostConfig());
	(void)ServerNet.Start(GTimeSource.Now());
	(void)ServerHost.BeginPlay(GTimeSource.Now());
	MW_LOG(Log, "ex16", "server listening (udp)");

	std::uint8_t StateTick = 0;
	bool bDoneAnnounced = false;
	for (;;)
	{
		// Tick runs PumpReceive (delivers spawn requests -> handler) then PumpSend
		// (flushes the Welcome/heartbeats and the broadcast queued last iteration).
		(void)ServerHost.Tick(GTimeSource.Now());
		++StateTick;
		const std::uint8_t StatePayload[2] = {StateTick, static_cast<std::uint8_t>(WorldActorCount)};
		(void)ServerNet.Broadcast(StateBroadcastChannel, TSpan<const std::uint8_t>(StatePayload, sizeof(StatePayload)));
		if (!bDoneAnnounced && WorldActorCount >= MaxSpawns)
		{
			MW_LOG(Log, "ex16", "done (server spawned %d actors)", WorldActorCount);
			bDoneAnnounced = true;
		}
		SleepMilliseconds(PollPacingMilliseconds);
	}
}
