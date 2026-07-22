#include "TestSupport.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Delegates/Delegate.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/NetworkFrame.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Net/HostLoopback.h>
#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetHost.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/Object.h>
#include <MicroWorld/Object/ObjectPtr.h>
#include <MicroWorld/Object/ObjectStore.h>
#include <MicroWorld/Time.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{
using MicroWorld::AActor;
using MicroWorld::EEngineResult;
using MicroWorld::ENetHostState;
using MicroWorld::ENetMode;
using MicroWorld::ENetResult;
using MicroWorld::EObjectResult;
using MicroWorld::ERuntimeResult;
using MicroWorld::FActorComponentRegistry;
using MicroWorld::FActorComponentRegistryBase;
using MicroWorld::FDelegateHandle;
using MicroWorld::FGarbageCollectionBudget;
using MicroWorld::FHostLoopback;
using MicroWorld::FNetHostConfig;
using MicroWorld::FPeerId;
using MicroWorld::INetworkFrame;
using MicroWorld::MakeLoopbackAddress;
using MicroWorld::TEngineHost;
using MicroWorld::TimePointMilliseconds;
using MicroWorld::TNetHost;
using MicroWorld::TNetHostFrame;
using MicroWorld::TObjectCreationResult;
using MicroWorld::TObjectPtr;
using MicroWorld::TSpan;
using MicroWorld::UWorld;

/** Host sized for a world plus one spawned actor, matching the engine host test profile. */
using FHost = TEngineHost<6, 8, 256, 16, 1, 2, 4, 64>;

/** The one application channel this suite exchanges; channel 0 stays reserved for control. */
constexpr std::uint8_t AppChannel = 1;

/** Stable type id for the actor a server spawns in response to a client message. */
constexpr MicroWorld::FTypeId NetSpawnedActorTypeId{0x00070001u};

/** Records how many times each frame slot ran and their order so a test can assert the contract. */
struct FFrameCallRecord
{
	/** Number of inbound-dispatch slot invocations observed. */
	int DispatchCount{0};

	/** Number of outbound-flush slot invocations observed. */
	int FlushCount{0};

	/** Monotonic stamp of the most recent dispatch, for proving dispatch precedes flush. */
	std::uint32_t DispatchOrder{0};

	/** Monotonic stamp of the most recent flush, for proving dispatch precedes flush. */
	std::uint32_t FlushOrder{0};

	/** Shared monotonic source stamped by each slot to order them within a tick. */
	std::uint32_t Sequence{0};
};

/** A network frame that only records its two slot calls, isolating the engine-side wiring contract. */
class FRecordingNetworkFrame final : public INetworkFrame
{
public:
	/** Binds this stub to the caller-owned record it stamps on every slot call. */
	explicit FRecordingNetworkFrame(FFrameCallRecord& InRecord) noexcept : Record(InRecord) {}

	/** Stamps the inbound-dispatch slot's count and order. */
	void TickDispatch(const TimePointMilliseconds) noexcept override
	{
		++Record.DispatchCount;
		Record.DispatchOrder = ++Record.Sequence;
	}

	/** Stamps the outbound-flush slot's count and order. */
	void TickFlush(const TimePointMilliseconds) noexcept override
	{
		++Record.FlushCount;
		Record.FlushOrder = ++Record.Sequence;
	}

private:
	/** Receives this stub's observed slot counts and ordering; never owned here. */
	FFrameCallRecord& Record;
};

/** A minimal actor that records its BeginPlay so a test can prove it began on the server world. */
class FNetSpawnedActor final : public AActor
{
public:
	/** Binds the component lease and the begin counter this actor increments when it starts. */
	FNetSpawnedActor(FActorComponentRegistryBase Components, int& InBeginCount) noexcept : AActor(std::move(Components)), BeginCount(InBeginCount) {}

protected:
	/** Records that the server world began this spawned actor exactly at the barrier. */
	void BeginPlay() noexcept override { ++BeginCount; }

private:
	/** Counts begin-play invocations so the test observes the spawn without touching the store. */
	int& BeginCount;
};

/** Everything a server message handler needs to spawn one actor in the server host's world. */
struct FServerSpawnContext
{
	/** The server engine host whose world receives the spawned actor. */
	FHost& Host;

	/** The caller-owned component lease the spawned actor holds for its lifetime. */
	FActorComponentRegistry<0>& SpawnedComponents;

	/** Counts how many application messages the server handler observed. */
	int& HandlerInvocationCount;

	/** Receives the spawned actor's begin count so the test proves it began. */
	int& SpawnedBeginCount;
};

/** Builds the shared fast-heartbeat, short-timeout config both hosts use for deterministic frames. */
FNetHostConfig MakeConfig() noexcept
{
	FNetHostConfig Config{};
	Config.HeartbeatIntervalMilliseconds = 100;
	Config.PeerTimeoutMilliseconds = 500;
	Config.ProtocolVersion = 1;
	return Config;
}

} // namespace

/** Proves a bound network frame's inbound slot runs before its outbound slot on every accepted tick, and neither runs on a rejected one. */
MW_TEST_CASE(EngineHostTickDrivesBoundNetworkFrameDispatchThenFlush)
{
	FFrameCallRecord Record{};
	FRecordingNetworkFrame Frame{Record};
	FHost Host{FGarbageCollectionBudget{1, 4, 8}, Frame};

	MW_EXPECT_TRUE(Test, Host.CreateWorld().Get() != nullptr, "CreateWorld roots the world before the frame-driven ticks");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "BeginPlay reports success at the canonical baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(10), "The first tick reports success");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(20), "The second tick reports success");

	MW_EXPECT_EQ(Test, 2, Record.DispatchCount, "Each accepted tick drives exactly one inbound dispatch");
	MW_EXPECT_EQ(Test, 2, Record.FlushCount, "Each accepted tick drives exactly one outbound flush");
	MW_EXPECT_TRUE(Test, Record.DispatchOrder < Record.FlushOrder, "The inbound dispatch runs before the outbound flush within a tick");

	MW_EXPECT_EQ(Test, ERuntimeResult::NonMonotonicTime, Host.Tick(15), "A rolled-back tick is rejected before any frame slot runs");
	MW_EXPECT_EQ(Test, 2, Record.DispatchCount, "A rejected tick drives no inbound dispatch");
	MW_EXPECT_EQ(Test, 2, Record.FlushCount, "A rejected tick drives no outbound flush");
}

/**
 * The concept proof that net and engine compose: two TEngineHost instances over one
 * loopback exchange a message that spawns an actor on the server world, driven only
 * through the canonical Tick frame order (never by calling the pumps directly).
 */
MW_TEST_CASE(EngineNetHostClientMessageSpawnsActorOnServerWorld)
{
	// One loopback network with two ports; the server drives port 0, the client port 1.
	FHostLoopback<2, 8, 64> Network;
	TNetHost<2, 64> ServerNet(Network.Port(0));
	TNetHost<1, 64> ClientNet(Network.Port(1));
	TNetHostFrame<TNetHost<2, 64>> ServerFrame{ServerNet};
	TNetHostFrame<TNetHost<1, 64>> ClientFrame{ClientNet};

	// The test owns the spawn observables so they outlive the host whose store holds the actor.
	int HandlerInvocationCount = 0;
	int SpawnedBeginCount = 0;
	FActorComponentRegistry<0> SpawnedComponents;

	FHost ServerHost{FGarbageCollectionBudget{1, 4, 8}, ServerFrame};
	FHost ClientHost{FGarbageCollectionBudget{1, 4, 8}, ClientFrame};
	FServerSpawnContext Context{ServerHost, SpawnedComponents, HandlerInvocationCount, SpawnedBeginCount};

	// The server can construct the actor its handler spawns; both hosts root a world.
	MW_EXPECT_EQ(
		Test,
		EObjectResult::Success,
		ServerHost.RegisterClass<FNetSpawnedActor>(NetSpawnedActorTypeId, "NetSpawnedActor"),
		"The server registers the actor type it will spawn on demand");
	MW_EXPECT_TRUE(Test, ServerHost.CreateWorld().Get() != nullptr, "The server roots its world");
	MW_EXPECT_TRUE(Test, ClientHost.CreateWorld().Get() != nullptr, "The client roots its world");

	// On any application message the server spawns one actor into its own world.
	TNetHost<2, 64>::FMessageHandlerBinding Binding;
	Binding.Bind(
		[&Context](const FPeerId, const std::uint8_t, TSpan<const std::uint8_t>) noexcept
		{
			++Context.HandlerInvocationCount;
			const TObjectCreationResult<FNetSpawnedActor> Creation =
				Context.Host.CreateObject<FNetSpawnedActor>(NetSpawnedActorTypeId, Context.SpawnedComponents.MakeView(), Context.SpawnedBeginCount);
			if (Creation.Result == EObjectResult::Success)
			{
				(void)Context.Host.GetWorld().SpawnActor(TObjectPtr<AActor>{Creation.Object});
			}
		});
	FDelegateHandle Handle{};
	(void)ServerNet.AddMessageHandler(std::move(Binding), Handle);

	FNetHostConfig ClientConfig = MakeConfig();
	ClientConfig.ServerAddress = MakeLoopbackAddress(0);
	(void)ServerNet.Configure(ENetMode::DedicatedServer, MakeConfig());
	(void)ClientNet.Configure(ENetMode::Client, ClientConfig);
	(void)ServerNet.Start(0);
	(void)ClientNet.Start(0);

	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ServerHost.BeginPlay(0), "The server world begins play at the baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ClientHost.BeginPlay(0), "The client world begins play at the baseline");

	// Drive both hosts frame by frame; the handshake rides the live frame slots, not direct pumps.
	TimePointMilliseconds Now = 0;
	for (int Frame = 0; Frame < 4 && ClientNet.GetState() != ENetHostState::Connected; ++Frame)
	{
		Now += 10;
		(void)ClientHost.Tick(Now);
		(void)ServerHost.Tick(Now);
	}
	MW_EXPECT_EQ(Test, ENetHostState::Connected, ClientNet.GetState(), "The client connects through the engine's live frame slots");

	const int BeginCountBeforeMessage = SpawnedBeginCount;
	const std::uint32_t ServerObjectsBeforeMessage = ServerHost.GetObjectStore().Stats().OccupiedSlots;

	const std::uint8_t Payload[1] = {0x42};
	const ENetResult SendResult = ClientNet.SendTo(ClientNet.GetServerPeer(), AppChannel, TSpan<const std::uint8_t>(Payload, 1));

	// One client tick flushes the message; one server tick dispatches it and applies the spawn.
	Now += 10;
	(void)ClientHost.Tick(Now);
	Now += 10;
	(void)ServerHost.Tick(Now);

	MW_EXPECT_EQ(Test, ENetResult::Success, SendResult, "The connected client queues the application message");
	MW_EXPECT_EQ(Test, 0, BeginCountBeforeMessage, "No actor spawned before the message crossed the network");
	MW_EXPECT_EQ(Test, 1, HandlerInvocationCount, "The server handler runs exactly once for the client message");
	MW_EXPECT_EQ(Test, 1, SpawnedBeginCount, "The message spawns exactly one actor that begins on the server world");
	MW_EXPECT_EQ(Test, std::size_t{0}, ServerHost.GetWorld().PendingSpawnCount(), "The server frame applied the spawn at its structural barrier");
	MW_EXPECT_EQ(
		Test,
		ServerObjectsBeforeMessage + std::uint32_t{1},
		ServerHost.GetObjectStore().Stats().OccupiedSlots,
		"Exactly one new object (the spawned actor) occupies the server store");
	MW_EXPECT_EQ(
		Test, std::uint32_t{1}, ClientHost.GetObjectStore().Stats().OccupiedSlots, "The client spawned nothing; only its world occupies a slot");
}
