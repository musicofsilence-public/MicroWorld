#include "TestSupport.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Delegates/Delegate.h>
#include <MicroWorld/Core/PlaySystem.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/EngineSystem.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Transport/HostLoopback.h>
#include <MicroWorld/Transport/DeviceAddress.h>
#include <MicroWorld/Transport/TransportHost.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/Object.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Core/Time.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{
using MicroWorld::Core::ERuntimeResult;
using MicroWorld::Core::FDelegateHandle;
using MicroWorld::Core::IPlaySystem;
using MicroWorld::Core::TimePointMilliseconds;
using MicroWorld::Core::TSpan;
using MicroWorld::Engine::AActor;
using MicroWorld::Engine::EEngineResult;
using MicroWorld::Engine::EObjectResult;
using MicroWorld::Engine::FDefaultEngineTraits;
using MicroWorld::Engine::FGarbageCollectionBudget;
using MicroWorld::Engine::TEngine;
using MicroWorld::Engine::THostPlaySystem;
using MicroWorld::Engine::TObjectCreationResult;
using MicroWorld::Engine::TObjectPtr;
using MicroWorld::Engine::UWorld;
using MicroWorld::Transport::ENetworkMode;
using MicroWorld::Transport::ETransportHostState;
using MicroWorld::Transport::ETransportResult;
using MicroWorld::Transport::FPeerId;
using MicroWorld::Transport::FTransportHostConfig;
using MicroWorld::Transport::THostLoopback;
using MicroWorld::Transport::TTransportHost;
using MicroWorld::Transport::Address::MakeLoopbackAddress;

/** Carries the exact capacities FHost sized before the traits refactor, so the test store is unchanged. */
struct FHostTraits : FDefaultEngineTraits
{
	static constexpr std::size_t MaxClasses = 6;
	static constexpr std::size_t MaxObjects = 8;
	static constexpr std::size_t SlotSizeBytes = 256;
	static constexpr std::size_t MaxRoots = 1;
	static constexpr std::size_t MaxActors = 2;
	static constexpr std::size_t MaxTimers = 4;
};

/** Host sized for a world plus one spawned actor, matching the engine host test profile. */
using FHost = TEngine<FHostTraits>;

/** The one application channel this suite exchanges; channel 0 stays reserved for control. */
constexpr std::uint8_t AppChannel = 1;

/** Stable type id for the actor a server spawns in response to a client message. */
constexpr MicroWorld::Engine::FTypeId TransportSpawnedActorTypeId{0x00070001u};

/** Stable type id for the actor that observes the transport host on both world lifecycle boundaries. */
constexpr MicroWorld::Engine::FTypeId TransportHostLifecycleActorTypeId{0x00070002u};

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
class FRecordingPlaySystem final : public IPlaySystem
{
public:
	/** Binds this stub to the caller-owned record it stamps on every slot call. */
	explicit FRecordingPlaySystem(FFrameCallRecord& InRecord) noexcept : Record(InRecord) {}

	/** Stamps the inbound-dispatch slot's count and order. */
	void PreAdvance(const TimePointMilliseconds) noexcept override
	{
		++Record.DispatchCount;
		Record.DispatchOrder = ++Record.Sequence;
	}

	/** Stamps the outbound-flush slot's count and order. */
	void PostAdvance(const TimePointMilliseconds) noexcept override
	{
		++Record.FlushCount;
		Record.FlushOrder = ++Record.Sequence;
	}

private:
	/** Receives this stub's observed slot counts and ordering; never owned here. */
	FFrameCallRecord& Record;
};

/** A minimal actor that records its BeginPlay so a test can prove it began on the server world. */
class FTransportSpawnedActor final : public AActor
{
public:
	/** Binds the begin counter this actor increments when it starts. */
	FTransportSpawnedActor(int& InBeginCount) noexcept : AActor(), BeginCount(InBeginCount) {}

protected:
	/** Records that the server world began this spawned actor exactly at the barrier. */
	void BeginPlay() noexcept override { ++BeginCount; }

private:
	/** Counts begin-play invocations so the test observes the spawn without touching the store. */
	int& BeginCount;
};

/** Observes that the engine starts a bound host before actor BeginPlay and stops it after actor EndPlay. */
class FTransportHostLifecycleActor final : public AActor
{
public:
	/** Binds the host state observations to test-owned values that outlive the managed actor. */
	FTransportHostLifecycleActor(
		TTransportHost<1, 64>& InTransportHost, ETransportHostState& OutStateDuringBeginPlay, ETransportHostState& OutStateDuringEndPlay) noexcept
		: AActor(), TransportHost(InTransportHost), StateDuringBeginPlay(OutStateDuringBeginPlay), StateDuringEndPlay(OutStateDuringEndPlay)
	{
	}

protected:
	/** Captures the host state at the world start boundary. */
	void BeginPlay() noexcept override { StateDuringBeginPlay = TransportHost.GetState(); }

	/** Captures the host state before the engine gives the adapter its play-end turn. */
	void EndPlay() noexcept override { StateDuringEndPlay = TransportHost.GetState(); }

private:
	/** The caller-owned host whose lifecycle the engine adapter drives. */
	TTransportHost<1, 64>& TransportHost;

	/** Receives the host state observed while this actor's BeginPlay runs. */
	ETransportHostState& StateDuringBeginPlay;

	/** Receives the host state observed while this actor's EndPlay runs. */
	ETransportHostState& StateDuringEndPlay;
};

/** Everything a server message handler needs to spawn one actor in the server host's world. */
struct FServerSpawnContext
{
	/** The server engine whose world receives the spawned actor. */
	FHost& Host;

	/** Counts how many application messages the server handler observed. */
	int& HandlerInvocationCount;

	/** Receives the spawned actor's begin count so the test proves it began. */
	int& SpawnedBeginCount;
};

/** Builds the shared fast-heartbeat, short-timeout config both hosts use for deterministic frames. */
FTransportHostConfig MakeConfig() noexcept
{
	FTransportHostConfig Config{};
	Config.HeartbeatIntervalMilliseconds = 100;
	Config.PeerTimeoutMilliseconds = 500;
	Config.ProtocolVersion = 1;
	return Config;
}

} // namespace

/**
 * Scenario: Configure a host, root a world with a lifecycle observer, and drive one full BeginPlay/EndPlay turn.
 * Expected: The host starts before world BeginPlay and stops only after world EndPlay.
 */
MW_TEST_CASE(EngineHost_BeginPlayStartsHostBeforeWorldAndEndPlayStopsHostAfterWorld)
{
	// Arrange
	THostLoopback<2, 8, 64> Network;
	TTransportHost<1, 64> TransportHost(Network.Port(0));
	THostPlaySystem<TTransportHost<1, 64>> Networking{TransportHost};
	FHost Host{FGarbageCollectionBudget{1, 4, 8}, Networking};
	ETransportHostState StateDuringBeginPlay = ETransportHostState::Idle;
	ETransportHostState StateDuringEndPlay = ETransportHostState::Idle;
	const FTransportHostConfig Config = MakeConfig();

	// Act: configure the host, root a world with a lifecycle observer, then drive one full BeginPlay/EndPlay turn.
	const ETransportResult ConfigureResult = TransportHost.Configure(ENetworkMode::DedicatedServer, Config);
	const EObjectResult RegisterResult =
		Host.RegisterClass<FTransportHostLifecycleActor>(TransportHostLifecycleActorTypeId, "TransportHostLifecycleActor");
	const TObjectPtr<UWorld> World = Host.CreateWorld();
	UWorld* const WorldInstance = World.Get();
	const TObjectCreationResult<FTransportHostLifecycleActor> ActorCreation =
		Host.CreateObject<FTransportHostLifecycleActor>(TransportHostLifecycleActorTypeId, TransportHost, StateDuringBeginPlay, StateDuringEndPlay);
	const EEngineResult ActorRegistration =
		WorldInstance == nullptr ? EEngineResult::InvalidReference : WorldInstance->RegisterActor(TObjectPtr<AActor>{ActorCreation.Object});
	const ETransportHostState StateBeforeBeginPlay = TransportHost.GetState();
	const ERuntimeResult BeginResult = Host.BeginPlay(123);
	const ETransportHostState StateAfterBeginPlay = TransportHost.GetState();
	const ERuntimeResult EndResult = Host.EndPlay();
	const ETransportHostState StateAfterEndPlay = TransportHost.GetState();

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, ConfigureResult, "The server host must accept its configuration before engine BeginPlay");
	MW_EXPECT_EQ(Test, EObjectResult::Success, RegisterResult, "The engine must register the lifecycle observer actor type");
	MW_EXPECT_TRUE(Test, WorldInstance != nullptr, "The engine must root a world before registering the observer actor");
	MW_EXPECT_EQ(Test, EObjectResult::Success, ActorCreation.Result, "The engine must create the lifecycle observer actor");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ActorRegistration, "The world must register the lifecycle observer actor");
	MW_EXPECT_EQ(Test, ETransportHostState::Idle, StateBeforeBeginPlay, "A configured host must stay idle before the engine lifecycle begins");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "Engine BeginPlay must complete at the supplied canonical time");
	MW_EXPECT_EQ(Test, ETransportHostState::Listening, StateDuringBeginPlay, "The host must listen before any world actor receives BeginPlay");
	MW_EXPECT_EQ(Test, ETransportHostState::Listening, StateAfterBeginPlay, "The bound host must remain active after world BeginPlay completes");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, EndResult, "Engine EndPlay must complete after ending the rooted world");
	MW_EXPECT_EQ(Test, ETransportHostState::Listening, StateDuringEndPlay, "The host must remain active while world actors receive EndPlay");
	MW_EXPECT_EQ(Test, ETransportHostState::Idle, StateAfterEndPlay, "The engine must stop the host after world EndPlay completes");
}

/**
 * Scenario: Root a world, drive two monotonically advancing ticks, then a rolled-back tick that is rejected.
 * Expected: Each accepted tick runs the inbound slot before the outbound slot; the rejected tick runs neither.
 */
MW_TEST_CASE(EngineHostTickDrivesBoundSystemPreAdvanceThenPostAdvance)
{
	// Arrange
	FFrameCallRecord Record{};
	FRecordingPlaySystem Frame{Record};
	FHost Host{FGarbageCollectionBudget{1, 4, 8}, Frame};

	// Act: root a world and drive two monotonically advancing ticks.
	MW_EXPECT_TRUE(Test, Host.CreateWorld().Get() != nullptr, "CreateWorld roots the world before the frame-driven ticks");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "BeginPlay reports success at the canonical baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(10), "The first tick reports success");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(20), "The second tick reports success");

	// Assert: each accepted tick drives one dispatch and one flush, in that order.
	MW_EXPECT_EQ(Test, 2, Record.DispatchCount, "Each accepted tick drives exactly one inbound dispatch");
	MW_EXPECT_EQ(Test, 2, Record.FlushCount, "Each accepted tick drives exactly one outbound flush");
	MW_EXPECT_TRUE(Test, Record.DispatchOrder < Record.FlushOrder, "The inbound dispatch runs before the outbound flush within a tick");

	// Act: a rolled-back tick must be rejected before any frame slot runs.
	MW_EXPECT_EQ(Test, ERuntimeResult::NonMonotonicTime, Host.Tick(15), "A rolled-back tick is rejected before any frame slot runs");
	// Assert: the rejected tick drives no additional dispatch or flush.
	MW_EXPECT_EQ(Test, 2, Record.DispatchCount, "A rejected tick drives no inbound dispatch");
	MW_EXPECT_EQ(Test, 2, Record.FlushCount, "A rejected tick drives no outbound flush");
}

/**
 * Scenario: Two TEngine instances over one loopback register a spawn handler, configure both hosts, root worlds, and exchange a client message driven
 * only through the canonical Tick frame order. Expected: The message spawns exactly one actor on the server world without ever calling the pumps
 * directly.
 */
MW_TEST_CASE(EngineHostClientMessageSpawnsActorOnServerWorld)
{
	// Arrange: one loopback network with two ports; the server drives port 0, the client port 1.
	THostLoopback<2, 8, 64> Network;
	TTransportHost<2, 64> ServerTransport(Network.Port(0));
	TTransportHost<1, 64> ClientTransport(Network.Port(1));
	THostPlaySystem<TTransportHost<2, 64>> ServerFrame{ServerTransport};
	THostPlaySystem<TTransportHost<1, 64>> ClientFrame{ClientTransport};

	// The test owns the spawn observables so they outlive the host whose store holds the actor.
	int HandlerInvocationCount = 0;
	int SpawnedBeginCount = 0;
	FHost ServerHost{FGarbageCollectionBudget{1, 4, 8}, ServerFrame};
	FHost ClientHost{FGarbageCollectionBudget{1, 4, 8}, ClientFrame};
	FServerSpawnContext Context{ServerHost, HandlerInvocationCount, SpawnedBeginCount};

	// Act: the server registers the actor it will spawn and both hosts root a world.
	MW_EXPECT_EQ(
		Test,
		EObjectResult::Success,
		ServerHost.RegisterClass<FTransportSpawnedActor>(TransportSpawnedActorTypeId, "TransportSpawnedActor"),
		"The server registers the actor type it will spawn on demand");
	MW_EXPECT_TRUE(Test, ServerHost.CreateWorld().Get() != nullptr, "The server roots its world");
	MW_EXPECT_TRUE(Test, ClientHost.CreateWorld().Get() != nullptr, "The client roots its world");

	// Act: install the spawn handler and configure both hosts before any lifecycle turn.
	TTransportHost<2, 64>::FMessageHandlerBinding Binding;
	Binding.Bind(
		[&Context](const FPeerId, const std::uint8_t, TSpan<const std::uint8_t>) noexcept
		{
			++Context.HandlerInvocationCount;
			const TObjectCreationResult<FTransportSpawnedActor> Creation =
				Context.Host.CreateObject<FTransportSpawnedActor>(TransportSpawnedActorTypeId, Context.SpawnedBeginCount);
			if (Creation.Result == EObjectResult::Success)
			{
				(void)Context.Host.GetWorld().SpawnActor(TObjectPtr<AActor>{Creation.Object});
			}
		});
	FDelegateHandle Handle{};
	(void)ServerTransport.AddMessageHandler(std::move(Binding), Handle);

	FTransportHostConfig ClientConfig = MakeConfig();
	ClientConfig.ServerAddress = MakeLoopbackAddress(0);
	(void)ServerTransport.Configure(ENetworkMode::DedicatedServer, MakeConfig());
	(void)ClientTransport.Configure(ENetworkMode::Client, ClientConfig);

	// Act: begin both worlds so the engine drives the bound hosts through their live frame slots.
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ServerHost.BeginPlay(0), "The server world begins play at the baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ClientHost.BeginPlay(0), "The client world begins play at the baseline");

	// Act: drive both hosts frame by frame; the handshake rides the live frame slots, not direct pumps.
	TimePointMilliseconds Now = 0;
	for (int Frame = 0; Frame < 4 && ClientTransport.GetState() != ETransportHostState::Connected; ++Frame)
	{
		Now += 10;
		(void)ClientHost.Tick(Now);
		(void)ServerHost.Tick(Now);
	}
	// Assert: the client connected through the engine's live frame slots.
	MW_EXPECT_EQ(Test, ETransportHostState::Connected, ClientTransport.GetState(), "The client connects through the engine's live frame slots");

	const int BeginCountBeforeMessage = SpawnedBeginCount;
	const std::uint32_t ServerObjectsBeforeMessage = ServerHost.GetObjectStore().Stats().OccupiedSlots;

	// Act: the client queues a one-byte application message over the established connection.
	const std::uint8_t Payload[1] = {0x42};
	const ETransportResult SendResult = ClientTransport.SendTo(ClientTransport.GetServerPeer(), AppChannel, TSpan<const std::uint8_t>(Payload, 1));

	// Act: one client tick flushes the message; one server tick dispatches it and applies the spawn.
	Now += 10;
	(void)ClientHost.Tick(Now);
	Now += 10;
	(void)ServerHost.Tick(Now);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, SendResult, "The connected client queues the application message");
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
