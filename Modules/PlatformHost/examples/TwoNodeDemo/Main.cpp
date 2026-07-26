/**
 * @file Main.cpp
 * @brief Phase 6.1 two-node UDP acceptance demo.
 *
 * One host executable hosts TWO independent MicroWorld nodes — a dedicated
 * server built on a full TEngine and a bare TNetHost client — talking over
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
#include <MicroWorld/Engine/EngineSystem.h>
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

/** Loopback IPv4 octets shared by every endpoint address in the demo. */
constexpr std::uint8_t LoopbackIpv4Octets[4] = {127, 0, 0, 1};

/** Fixed logical-clock advance per sub-action; the trace prints no wall time. */
constexpr TimePointMilliseconds LogicalClockStepMilliseconds = 10;

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
/** Server engine traits: carries the exact capacities FServerEngine sized before the traits refactor. */
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

/** Server network host bound to one UDP driver; capacity fits one client peer. */
using FServerNet = TNetHost<2 /*MaxPeers*/, 256 /*MaxPacketBytes*/>;

/** Client network host bound to its own UDP driver; capacity fits one server peer. */
using FClientNet = TNetHost<1 /*MaxPeers*/, 256 /*MaxPacketBytes*/>;

/**
 * A minimal actor the server spawns on demand to prove a remote input event
 * changes server world contents. It bumps an external begin counter so the
 * spawn is observable without reaching into the object store.
 */
class FDemoSpawnedActor final : public AActor
{
public:
	/** Binds the begin counter the actor bumps on play. */
	FDemoSpawnedActor(int& InBeginCount) noexcept : AActor(), BeginCount(InBeginCount) {}

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
 * server world on each input event. A monotonic sequence bounds accepted
 * requests and gives each accepted request a stable position.
 */
struct FDemoSpawnContext
{
	/** The server engine host whose world receives the spawned actor. */
	FServerEngine& Host;

	/** Monotonic count of input events handled; enforces the bounded spawn limit. */
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
	FHostUdpDriver& ServerDriver,
	FHostUdpDriver& ClientDriver,
	FServerEngine& ServerHost,
	FClientNet& Client,
	TimePointMilliseconds& LogicalClockMilliseconds) noexcept
{
	for (int Iteration = 0; Iteration < HandshakeIterationCap; ++Iteration)
	{
		LogicalClockMilliseconds += LogicalClockStepMilliseconds;
		(void)Client.PumpSend(LogicalClockMilliseconds);
		if (ServerDriver.PollReadable(ReadinessWaitMilliseconds))
		{
			(void)ServerHost.Tick(LogicalClockMilliseconds);
		}
		if (ClientDriver.PollReadable(ReadinessWaitMilliseconds))
		{
			(void)Client.PumpReceive(LogicalClockMilliseconds);
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

/**
 * Checks whether both loopback UDP sockets bound successfully. FHostUdpDriver
 * opens its socket in its constructor and cannot be moved, so this is a query
 * over already-constructed drivers, not a command that opens them.
 */
bool BothLoopbackDriversOpen(const FHostUdpDriver& ServerDriver, const FHostUdpDriver& ClientDriver) noexcept
{
	return ServerDriver.IsOpen() && ClientDriver.IsOpen();
}

/**
 * Registers the demo's one spawnable actor class and creates the server's
 * world. Returns false on the first failing step so main can abort before any
 * handler or session work depends on a half-built engine host.
 */
bool RegisterDemoWorld(FServerEngine& ServerHost) noexcept
{
	if (ServerHost.RegisterClass<FDemoSpawnedActor>(DemoSpawnedActorTypeId, "DemoSpawnedActor") != EObjectResult::Success)
	{
		return false;
	}
	if (ServerHost.CreateWorld().Get() == nullptr)
	{
		return false;
	}
	return true;
}

/**
 * Spawns one server-world actor in response to a client input event. Does
 * nothing once MaxSpawns requests have already been handled, keeping the
 * demonstration's world usage bounded.
 */
void HandleClientSpawnRequest(FDemoSpawnContext& SpawnContext, int& SpawnedBeginCount) noexcept
{
	if (SpawnContext.SpawnSequence >= MaxSpawns)
	{
		return;
	}
	++SpawnContext.SpawnSequence;
	const TObjectCreationResult<FDemoSpawnedActor> Creation =
		SpawnContext.Host.CreateObject<FDemoSpawnedActor>(DemoSpawnedActorTypeId, SpawnedBeginCount);
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
}

/**
 * Builds the server's channel-1 spawn-request handler binding and registers it
 * with the server net host. The bound lambda only forwards to
 * HandleClientSpawnRequest, keeping the capture list separate from the handler
 * logic it invokes.
 */
bool InstallServerSpawnHandler(FServerNet& ServerNet, FDemoSpawnContext& SpawnContext, int& SpawnedBeginCount, FDelegateHandle& OutHandle) noexcept
{
	FServerNet::FMessageHandlerBinding Binding;
	Binding.Bind([&SpawnContext, &SpawnedBeginCount](const FPeerId, const std::uint8_t, TSpan<const std::uint8_t>) noexcept
				 { HandleClientSpawnRequest(SpawnContext, SpawnedBeginCount); });
	return ServerNet.AddMessageHandler(std::move(Binding), OutHandle) == EDelegateResult::Success;
}

/**
 * Decodes a server state broadcast payload into the client's capture state and
 * prints the received-state trace line. Does nothing on a malformed payload so
 * a corrupt broadcast cannot overwrite the client's last-known state.
 */
void HandleServerStateBroadcast(FDemoStateCapture& StateCapture, TSpan<const std::uint8_t> Payload) noexcept
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
}

/**
 * Builds the client's channel-2 state-broadcast handler binding and registers
 * it with the client net host. The bound lambda only forwards to
 * HandleServerStateBroadcast, keeping the capture list separate from the
 * handler logic it invokes.
 */
bool InstallClientStateHandler(FClientNet& ClientNet, FDemoStateCapture& StateCapture, FDelegateHandle& OutHandle) noexcept
{
	FClientNet::FMessageHandlerBinding Binding;
	Binding.Bind([&StateCapture](const FPeerId, const std::uint8_t, TSpan<const std::uint8_t> Payload) noexcept
				 { HandleServerStateBroadcast(StateCapture, Payload); });
	return ClientNet.AddMessageHandler(std::move(Binding), OutHandle) == EDelegateResult::Success;
}

/**
 * Builds the client's server address from the server driver's bound loopback
 * port, configures both net hosts, starts both, and prints the two startup
 * trace lines. Returns false on the first failing step so main can abort
 * before BeginPlay runs against a half-started session.
 */
bool ConfigureAndStartHosts(FServerNet& ServerNet, FClientNet& ClientNet, const FHostUdpDriver& ServerDriver) noexcept
{
	FNetHostConfig ClientConfig = MakeDemoConfig();
	ClientConfig.ServerAddress =
		MakeUdpAddress(LoopbackIpv4Octets[0], LoopbackIpv4Octets[1], LoopbackIpv4Octets[2], LoopbackIpv4Octets[3], ServerDriver.BoundPort());
	if (ServerNet.Configure(ENetMode::DedicatedServer, MakeDemoConfig()) != ENetResult::Success)
	{
		return false;
	}
	if (ClientNet.Configure(ENetMode::Client, ClientConfig) != ENetResult::Success)
	{
		return false;
	}
	if (ServerNet.Start(0) != ENetResult::Success)
	{
		return false;
	}
	if (ClientNet.Start(0) != ENetResult::Success)
	{
		return false;
	}
	std::printf("[server] listening\n");
	std::printf("[client] connecting to server\n");
	return true;
}

/**
 * Reports whether the given state tick should also send a client spawn
 * request. The demo issues exactly two spawn requests (bounded by MaxSpawns ==
 * 2 pre-allocated per-actor registries), on the first and last of the three
 * state ticks, so the middle tick demonstrates a broadcast with no new spawn.
 */
bool IsSpawnRequestDue(int StateTick) noexcept
{
	return (StateTick == 1) || (StateTick == 3);
}

/**
 * Sends the client's one-byte spawn-request opcode when due, then always
 * pumps the client's send queue and advances the logical clock. The pump runs
 * every tick, not only when a request was sent, so the logical clock and wire
 * state stay in lockstep regardless of whether this tick issued a request.
 */
bool SendSpawnRequestIfDue(FClientNet& ClientNet, bool bSpawnRequestDue, TimePointMilliseconds& LogicalClockMilliseconds) noexcept
{
	if (bSpawnRequestDue)
	{
		const std::uint8_t Payload[1] = {SpawnRequestOpcode};
		if (ClientNet.SendTo(ClientNet.GetServerPeer(), InputEventChannel, TSpan<const std::uint8_t>(Payload, 1)) != ENetResult::Success)
		{
			return false;
		}
		std::printf("[client] sending spawn request (input event)\n");
	}
	LogicalClockMilliseconds += LogicalClockStepMilliseconds;
	(void)ClientNet.PumpSend(LogicalClockMilliseconds);
	return true;
}

/**
 * Polls the server socket for the spawn-request datagram when one is due,
 * then advances the logical clock and ticks the server engine. This tick's
 * PumpReceive step delivers the input event, which fires the spawn handler.
 */
bool AdvanceServerFrame(
	FServerEngine& ServerHost, FHostUdpDriver& ServerDriver, bool bSpawnRequestDue, TimePointMilliseconds& LogicalClockMilliseconds) noexcept
{
	if (bSpawnRequestDue)
	{
		(void)ServerDriver.PollReadable(ReadinessWaitMilliseconds);
	}
	LogicalClockMilliseconds += LogicalClockStepMilliseconds;
	return ServerHost.Tick(LogicalClockMilliseconds) == ERuntimeResult::Success;
}

/**
 * Broadcasts the current tick and world actor count to connected peers,
 * prints the heartbeat trace line, then advances the logical clock and ticks
 * the server engine again. This second tick's PumpSend step flushes the
 * broadcast onto the wire.
 */
bool BroadcastServerState(
	FServerNet& ServerNet, FServerEngine& ServerHost, int StateTick, int WorldActorCount, TimePointMilliseconds& LogicalClockMilliseconds) noexcept
{
	const std::uint8_t StatePayload[2] = {static_cast<std::uint8_t>(StateTick), static_cast<std::uint8_t>(WorldActorCount)};
	if (ServerNet.Broadcast(StateBroadcastChannel, TSpan<const std::uint8_t>(StatePayload, 2)) != ENetResult::Success)
	{
		return false;
	}
	std::printf("[server] heartbeat broadcast: state tick=%d actors=%d\n", StateTick, WorldActorCount);
	LogicalClockMilliseconds += LogicalClockStepMilliseconds;
	return ServerHost.Tick(LogicalClockMilliseconds) == ERuntimeResult::Success;
}

/**
 * Polls the client socket for the broadcast datagram, advances the logical
 * clock, then pumps the client's receive queue. The pump delivers the
 * broadcast to the client's state handler, which prints the received-state
 * trace line.
 */
void DeliverToClient(FClientNet& ClientNet, FHostUdpDriver& ClientDriver, TimePointMilliseconds& LogicalClockMilliseconds) noexcept
{
	(void)ClientDriver.PollReadable(ReadinessWaitMilliseconds);
	LogicalClockMilliseconds += LogicalClockStepMilliseconds;
	(void)ClientNet.PumpReceive(LogicalClockMilliseconds);
}

/**
 * Drives the three-tick state-broadcast loop: each tick optionally sends a
 * client spawn request, advances the server frame, broadcasts server state,
 * and delivers that state to the client, all under one shared logical clock.
 * Returns false on the first step that reports a hard failure.
 */
bool RunStateBroadcastLoop(
	FClientNet& ClientNet,
	FServerNet& ServerNet,
	FServerEngine& ServerHost,
	FHostUdpDriver& ServerDriver,
	FHostUdpDriver& ClientDriver,
	int& WorldActorCount,
	TimePointMilliseconds& LogicalClockMilliseconds) noexcept
{
	constexpr int StateBroadcastStepCount = 3;
	for (int StateTick = 1; StateTick <= StateBroadcastStepCount; ++StateTick)
	{
		const bool bSpawnRequestDue = IsSpawnRequestDue(StateTick);
		if (!SendSpawnRequestIfDue(ClientNet, bSpawnRequestDue, LogicalClockMilliseconds))
		{
			return false;
		}
		if (!AdvanceServerFrame(ServerHost, ServerDriver, bSpawnRequestDue, LogicalClockMilliseconds))
		{
			return false;
		}
		if (!BroadcastServerState(ServerNet, ServerHost, StateTick, WorldActorCount, LogicalClockMilliseconds))
		{
			return false;
		}
		DeliverToClient(ClientNet, ClientDriver, LogicalClockMilliseconds);
	}
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
	FServerNet ServerNet(ServerDriver);
	FClientNet ClientNet(ClientDriver);
	TNetHostSystem<FServerNet> ServerFrame{ServerNet};

	int SpawnSequence = 0;
	int SpawnedBeginCount = 0;
	int WorldActorCount = 0;

	FServerEngine ServerHost{FGarbageCollectionBudget{1, 4, 8}, ServerFrame};
	FDemoSpawnContext SpawnContext{ServerHost, SpawnSequence, WorldActorCount};
	FDemoStateCapture StateCapture{};
	FDelegateHandle SpawnHandle{};
	FDelegateHandle StateHandle{};
	TimePointMilliseconds LogicalClockMilliseconds = 0;

	if (!BothLoopbackDriversOpen(ServerDriver, ClientDriver))
	{
		return 1;
	}
	if (!RegisterDemoWorld(ServerHost))
	{
		return 1;
	}
	if (!InstallServerSpawnHandler(ServerNet, SpawnContext, SpawnedBeginCount, SpawnHandle))
	{
		return 1;
	}
	if (!InstallClientStateHandler(ClientNet, StateCapture, StateHandle))
	{
		return 1;
	}
	if (!ConfigureAndStartHosts(ServerNet, ClientNet, ServerDriver))
	{
		return 1;
	}

	if (ServerHost.BeginPlay(0) != ERuntimeResult::Success)
	{
		return 1;
	}
	if (!RunHandshake(ServerDriver, ClientDriver, ServerHost, ClientNet, LogicalClockMilliseconds))
	{
		return 1;
	}
	std::printf("[client] connected\n");

	if (!RunStateBroadcastLoop(ClientNet, ServerNet, ServerHost, ServerDriver, ClientDriver, WorldActorCount, LogicalClockMilliseconds))
	{
		return 1;
	}

	std::printf("[demo] complete\n");
	return 0;
}
