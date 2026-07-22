/**
 * @file Main.cpp
 * @brief Phase 6.1 two-node UDP acceptance demo.
 *
 * One host executable hosts TWO independent MicroWorld nodes — a dedicated
 * server built on a full TEngineHost and a bare TNetHost client — talking over
 * real localhost UDP. A client input event spawns an actor in the server's
 * world; the server broadcasts world state each step. The two nodes live in one
 * process and are driven in one deterministic interleaved loop so the printed
 * trace is byte-identical across runs.
 */

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Delegates/Delegate.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/NetworkFrame.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetHost.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/Object/ClassDescriptor.h>
#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/ObjectPtr.h>
#include <MicroWorld/PlatformHost/HostUdpDriver.h>
#include <MicroWorld/PlatformHost/UdpAddress.h>
#include <MicroWorld/Time.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace
{

using namespace MicroWorld;

/** Loopback octet prefix shared by every endpoint address in the demo. */
constexpr std::uint8_t OctetA = 127;
constexpr std::uint8_t OctetB = 0;
constexpr std::uint8_t OctetC = 0;
constexpr std::uint8_t OctetD = 1;

/** Fixed logical-clock advance per sub-action; the trace prints no wall time. */
constexpr TimePointMilliseconds FrameStep = 10;

/** Upper bound on the interleaved handshake loop; bounded work, no spin. */
constexpr int HandshakeIterationCap = 32;

/** Upper bound on the select() readiness wait so delivery is deterministic without a busy poll. */
constexpr DurationMilliseconds ReadinessWaitMilliseconds = 500;

/** Application channel that carries the client's spawn request to the server (channel 0 is reserved). */
constexpr std::uint8_t InputEventChannel = 1;

/** Application channel the server uses to broadcast world state to connected peers. */
constexpr std::uint8_t StateBroadcastChannel = 2;

/** Opcode the client sends as its one-byte input-event payload to request a spawn. */
constexpr std::uint8_t SpawnRequestOpcode = 0x42;

/** Channel-1 input opcode count that maps to the number of pre-allocated spawn registries. */
constexpr int MaxSpawns = 2;

/** Stable descriptor id for the actor the server spawns in response to a client input event. */
constexpr FTypeId DemoSpawnedActorTypeId{0x00080001u};

/**
 * The server engine host profile. Bounds are deliberately small, fixed, and
 * tuned so one bounded GC slice {1,4,8} completes a full mark/sweep cycle every
 * tick: MaxRoots(1) <= MaxRootOperations(1) and MaxObjects(8) <=
 * MaxSweepOperations(8). Without that invariant the store stays mid-cycle
 * (ActiveCollector set) across ticks, and a spawn arriving in that window fails
 * CreateObject under LifecycleLocked. This mirrors the proven EngineNetHostTests
 * profile; MaxActors leaves headroom above the demo's two spawns.
 */
using FServerEngine = TEngineHost<6, 8, 256, 16, 1, 4, 4, 64>;

/** Server network host bound to one UDP driver; capacity fits one client peer. */
using FServerNet = TNetHost<2, 256>;

/** Client network host bound to its own UDP driver; capacity fits one server peer. */
using FClientNet = TNetHost<1, 256>;

/**
 * A minimal actor the server spawns on demand to prove a remote input event
 * changes server world contents. It bumps an external begin counter so the
 * spawn is observable without reaching into the object store.
 */
class FDemoSpawnedActor final : public AActor
{
public:
	/** Forwards the component lease and the begin counter the actor bumps on play. */
	FDemoSpawnedActor(FActorComponentRegistryBase Components, int& InBeginCount) noexcept : AActor(std::move(Components)), BeginCount(InBeginCount) {}

	/** Keeps exact descriptor-driven destruction publicly instantiable. */
	~FDemoSpawnedActor() noexcept override = default;

protected:
	/** Records that this spawned actor began on the server world exactly once. */
	void BeginPlay() noexcept override { ++BeginCount; }

private:
	/** Receives the begin-count reference owned by the demo; not held by this actor. */
	int& BeginCount;
};

/**
 * Everything the server's channel-1 handler needs to spawn one actor into the
 * server world on each input event. The registries are pre-allocated and
 * indexed by a monotonic sequence because each FActorComponentRegistry lease is
 * one-shot (MakeView may be issued only once per registry).
 */
struct FDemoSpawnContext
{
	/** The server engine host whose world receives the spawned actor. */
	FServerEngine& Host;

	/** Caller-owned component registries, one per permitted spawn; indexed by sequence. */
	std::array<FActorComponentRegistry<0>, MaxSpawns>& SpawnedRegistries;

	/** Monotonic count of input events handled; selects the next one-shot registry. */
	int& SpawnSequence;

	/** Live actor count the server reports each step; bumped exactly once per accepted spawn. */
	int& WorldActorCount;
};

/**
 * The state the client's channel-2 handler decodes from each server broadcast.
 * Held by reference so the main loop prints decoded payload values rather than
 * loop indices, keeping the trace invariant across runs.
 */
struct FDemoStateCapture
{
	/** Most recent logical state tick decoded from a broadcast payload. */
	int LastTick{0};

	/** Most recent world actor count decoded from a broadcast payload. */
	int LastActors{0};
};

/**
 * Builds the shared heartbeat/timeout config both hosts use. The intervals are
 * deliberately long relative to the demo's logical time so no spontaneous
 * heartbeat datagram fires and the only wire traffic is the two explicit input
 * events and the three broadcasts.
 */
FNetHostConfig MakeDemoConfig() noexcept
{
	FNetHostConfig Config{};
	Config.HeartbeatIntervalMilliseconds = 10000;
	Config.PeerTimeoutMilliseconds = 60000;
	Config.ProtocolVersion = 1;
	return Config;
}

/**
 * Drives the Hello/Welcome handshake over real localhost UDP, advancing the
 * logical clock a fixed step per iteration. The server advances only through
 * its engine Tick (its frame runs PumpReceive at step 1 and PumpSend at step
 * 7); the bare client uses explicit pumps.
 */
bool RunHandshake(
	FHostUdpDriver& ServerDriver, FHostUdpDriver& ClientDriver, FServerEngine& ServerHost, FClientNet& Client, TimePointMilliseconds& Now) noexcept
{
	for (int Iteration = 0; Iteration < HandshakeIterationCap; ++Iteration)
	{
		Now += FrameStep;
		(void)Client.PumpSend(Now);
		if (ServerDriver.PollReadable(ReadinessWaitMilliseconds))
		{
			(void)ServerHost.Tick(Now);
		}
		if (ClientDriver.PollReadable(ReadinessWaitMilliseconds))
		{
			(void)Client.PumpReceive(Now);
		}
		if (Client.GetState() == ENetHostState::Connected)
		{
			return true;
		}
	}
	return false;
}

/**
 * Decodes the two-byte state payload the server broadcasts each step. Returns
 * false on any malformed payload so the caller can fail closed rather than print
 * an underdetermined count.
 */
bool DecodeStatePayload(const TSpan<const std::uint8_t> Payload, int& OutTick, int& OutActors) noexcept
{
	if (Payload.Size() < 2)
	{
		return false;
	}
	OutTick = static_cast<int>(Payload[0]);
	OutActors = static_cast<int>(Payload[1]);
	return true;
}

} // namespace

/**
 * Composes the server engine host and bare client net host over real localhost
 * UDP, drives them through one deterministic interleaved loop, and prints a
 * byte-identical trace across runs. Returns 0 on success and 1 on any failure.
 */
int main()
{
	using namespace MicroWorld;

	FHostUdpDriver ServerDriver(0);
	FHostUdpDriver ClientDriver(0);
	if (!ServerDriver.IsOpen() || !ClientDriver.IsOpen())
	{
		return 1;
	}

	FServerNet ServerNet(ServerDriver);
	FClientNet ClientNet(ClientDriver);
	TNetHostFrame<FServerNet> ServerFrame{ServerNet};

	std::array<FActorComponentRegistry<0>, MaxSpawns> SpawnedRegistries{};
	int SpawnSequence = 0;
	int SpawnedBeginCount = 0;
	int WorldActorCount = 0;

	FServerEngine ServerHost{FGarbageCollectionBudget{1, 4, 8}, ServerFrame};
	FDemoSpawnContext SpawnContext{ServerHost, SpawnedRegistries, SpawnSequence, WorldActorCount};
	FDemoStateCapture StateCapture{};

	if (ServerHost.RegisterClass<FDemoSpawnedActor>(DemoSpawnedActorTypeId, "DemoSpawnedActor") != EObjectResult::Success)
	{
		return 1;
	}
	if (ServerHost.CreateWorld().Get() == nullptr)
	{
		return 1;
	}

	FServerNet::FMessageHandlerBinding SpawnBinding;
	SpawnBinding.Bind(
		[&SpawnContext, &SpawnedBeginCount](const FPeerId, const std::uint8_t, TSpan<const std::uint8_t>) noexcept
		{
			if (SpawnContext.SpawnSequence >= MaxSpawns)
			{
				return;
			}
			const int Slot = SpawnContext.SpawnSequence;
			++SpawnContext.SpawnSequence;
			const TObjectCreationResult<FDemoSpawnedActor> Creation = SpawnContext.Host.CreateObject<FDemoSpawnedActor>(
				DemoSpawnedActorTypeId, SpawnContext.SpawnedRegistries[static_cast<std::size_t>(Slot)].MakeView(), SpawnedBeginCount);
			if (Creation.Result != EObjectResult::Success)
			{
				return;
			}
			if (SpawnContext.Host.GetWorld().SpawnActor(TObjectPtr<AActor>{Creation.Object}) != EEngineResult::Success)
			{
				return;
			}
			++SpawnContext.WorldActorCount;
			std::printf(
				"[server] received spawn request from peer -> spawned actor %d (world actor count = %d)\n",
				SpawnContext.WorldActorCount,
				SpawnContext.WorldActorCount);
		});
	FDelegateHandle SpawnHandle{};
	if (ServerNet.AddMessageHandler(std::move(SpawnBinding), SpawnHandle) != EDelegateResult::Success)
	{
		return 1;
	}

	FClientNet::FMessageHandlerBinding StateBinding;
	StateBinding.Bind(
		[&StateCapture](const FPeerId, const std::uint8_t, TSpan<const std::uint8_t> Payload) noexcept
		{
			int DecodedTick = 0;
			int DecodedActors = 0;
			if (!DecodeStatePayload(Payload, DecodedTick, DecodedActors))
			{
				return;
			}
			StateCapture.LastTick = DecodedTick;
			StateCapture.LastActors = DecodedActors;
			std::printf("[client] received state: tick=%d actors=%d\n", StateCapture.LastTick, StateCapture.LastActors);
		});
	FDelegateHandle StateHandle{};
	if (ClientNet.AddMessageHandler(std::move(StateBinding), StateHandle) != EDelegateResult::Success)
	{
		return 1;
	}

	FNetHostConfig ClientConfig = MakeDemoConfig();
	ClientConfig.ServerAddress = MakeUdpAddress(OctetA, OctetB, OctetC, OctetD, ServerDriver.BoundPort());
	if (ServerNet.Configure(ENetMode::DedicatedServer, MakeDemoConfig()) != ENetResult::Success)
	{
		return 1;
	}
	if (ClientNet.Configure(ENetMode::Client, ClientConfig) != ENetResult::Success)
	{
		return 1;
	}

	if (ServerNet.Start(0) != ENetResult::Success)
	{
		return 1;
	}
	if (ClientNet.Start(0) != ENetResult::Success)
	{
		return 1;
	}

	std::printf("[server] listening\n");
	std::printf("[client] connecting to server\n");

	if (ServerHost.BeginPlay(0) != ERuntimeResult::Success)
	{
		return 1;
	}

	TimePointMilliseconds Now = 0;
	if (!RunHandshake(ServerDriver, ClientDriver, ServerHost, ClientNet, Now))
	{
		return 1;
	}
	std::printf("[client] connected\n");

	constexpr int StateStepCount = 3;
	for (int StateTick = 1; StateTick <= StateStepCount; ++StateTick)
	{
		const bool bRequestsSpawn = (StateTick == 1) || (StateTick == 3);
		if (bRequestsSpawn)
		{
			const std::uint8_t Payload[1] = {SpawnRequestOpcode};
			if (ClientNet.SendTo(ClientNet.GetServerPeer(), InputEventChannel, TSpan<const std::uint8_t>(Payload, 1)) != ENetResult::Success)
			{
				return 1;
			}
			std::printf("[client] sending spawn request (input event)\n");
		}

		Now += FrameStep;
		(void)ClientNet.PumpSend(Now);

		if (bRequestsSpawn)
		{
			(void)ServerDriver.PollReadable(ReadinessWaitMilliseconds);
		}
		Now += FrameStep;
		if (ServerHost.Tick(Now) != ERuntimeResult::Success)
		{
			return 1;
		}

		const std::uint8_t StatePayload[2] = {static_cast<std::uint8_t>(StateTick), static_cast<std::uint8_t>(WorldActorCount)};
		if (ServerNet.Broadcast(StateBroadcastChannel, TSpan<const std::uint8_t>(StatePayload, 2)) != ENetResult::Success)
		{
			return 1;
		}
		std::printf("[server] heartbeat broadcast: state tick=%d actors=%d\n", StateTick, WorldActorCount);

		Now += FrameStep;
		if (ServerHost.Tick(Now) != ERuntimeResult::Success)
		{
			return 1;
		}

		(void)ClientDriver.PollReadable(ReadinessWaitMilliseconds);
		Now += FrameStep;
		(void)ClientNet.PumpReceive(Now);
	}

	std::printf("[demo] complete\n");
	return 0;
}
