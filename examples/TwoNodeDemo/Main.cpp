/**
 * Motivation: One host executable hosts TWO independent MicroWorld nodes -- a dedicated server built on
 *   a full TEngine and a bare TTransportHost client -- talking over real localhost UDP. A client input
 *   event spawns an actor in the server's world; the server broadcasts world state each step. The two
 *   nodes are driven in one deterministic interleaved loop so the printed trace is byte-identical.
 */

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Delegates/Delegate.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/EngineSystem.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Transport/TransportHost.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Platform/Host/HostWifiDevice.h>
#include <MicroWorld/Platform/Host/UdpAddress.h>
#include <MicroWorld/Core/Time.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace
{

using namespace MicroWorld::Core;
using namespace MicroWorld::Engine;
using namespace MicroWorld::Transport;
using MicroWorld::Platform::Host::FHostWifiDevice;
using MicroWorld::Transport::MakeUdpAddress;

/** Motivation: Loopback IPv4 octets shared by every endpoint address in the demo. */
constexpr std::uint8_t LoopbackIpv4Octets[4] = {127, 0, 0, 1};

/** Motivation: Fixed logical-clock advance per sub-action; the trace prints no wall time. */
constexpr TimePointMilliseconds LogicalClockStepMilliseconds = 10;

/** Motivation: Upper bound on the interleaved handshake loop; bounded work, no spin. */
constexpr int HandshakeIterationCap = 32;

/** Motivation: Upper bound on the select() readiness wait so delivery is deterministic without a busy poll. */
constexpr DurationMilliseconds ReadinessWaitMilliseconds = 500;

/** Motivation: Application channel that carries the client's spawn request to the server (channel 0 is reserved). */
constexpr std::uint8_t InputEventChannel = 1;

/** Motivation: Application channel the server uses to broadcast world state to connected peers. */
constexpr std::uint8_t StateBroadcastChannel = 2;

/** Motivation: Opcode the client sends as its one-byte input-event payload to request a spawn. */
constexpr std::uint8_t SpawnRequestOpcode = 0x42;

/** Motivation: Channel-1 input opcode count that maps to the number of pre-allocated spawn registries. */
constexpr int MaxSpawns = 2;

/** Motivation: Stable descriptor id for the actor the server spawns in response to a client input event. */
constexpr FTypeId DemoSpawnedActorTypeId{0x00080001u};

/**
 * Motivation: Carries the server engine profile -- bounds deliberately small and tuned so one bounded GC
 *   slice {1,4,8} completes a full mark/sweep cycle every tick, so a spawn arriving mid-cycle never fails
 *   CreateObject under LifecycleLocked (the proven EngineHostTests profile).
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

/** Motivation: Server network host bound to one UDP device; capacity fits one client peer. */
using FServerTransport = TTransportHost<2 /*MaxPeers*/, 256 /*MaxPacketBytes*/>;

/** Motivation: Client network host bound to its own UDP device; capacity fits one server peer. */
using FClientTransport = TTransportHost<1 /*MaxPeers*/, 256 /*MaxPacketBytes*/>;

/**
 * Motivation: A minimal actor the server spawns on demand to prove a remote input event changes server
 *   world contents, bumping an external begin counter so the spawn is observable without reaching into
 *   the object store.
 * Responsibilities: Bump one begin counter when play begins, and stay descriptor-destroyable.
 * Example:
 *   auto Creation = ServerHost.CreateObject<FDemoSpawnedActor>(DemoSpawnedActorTypeId, BeginCount);
 *   ServerHost.GetWorld().SpawnActor(TObjectPtr<AActor>{Creation.Object});
 */
class FDemoSpawnedActor final : public AActor
{
public:
	/**
	 * Motivation: Binds the begin counter the actor bumps on play, so the demo can observe spawns.
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
	/** Motivation: Receives the begin-count reference owned by the demo; not held by this actor. */
	int& BeginCount;
};

/**
 * Motivation: Bundles everything the server's channel-1 handler needs to spawn one actor per input event,
 *   so the handler can run inside a no-capture lambda by naming this context directly.
 * Responsibilities: Hold the engine plus the spawn sequence and world actor count it mutates.
 * Example:
 *   FDemoSpawnContext SpawnContext{ServerHost, SpawnSequence, WorldActorCount};
 */
struct FDemoSpawnContext
{
	/** Motivation: The server engine whose world receives the spawned actor. */
	FServerEngine& Host;

	/** Motivation: Monotonic count of input events handled; enforces the bounded spawn limit. */
	int& SpawnSequence;

	/** Motivation: Live actor count the server reports each step; bumped exactly once per accepted spawn. */
	int& WorldActorCount;
};

/**
 * Motivation: Holds the state the client's channel-2 handler decodes from each server broadcast, so the
 *   main loop prints decoded payload values rather than loop indices and the trace stays invariant.
 * Responsibilities: Hold the last decoded tick and actor count.
 * Example:
 *   FDemoStateCapture StateCapture;
 *   HandleServerStateBroadcast(StateCapture, Payload);
 */
struct FDemoStateCapture
{
	/** Motivation: Most recent logical state tick decoded from a broadcast payload. */
	int LastTick{0};

	/** Motivation: Most recent world actor count decoded from a broadcast payload. */
	int LastActors{0};
};

/**
 * Motivation: Lets both hosts build the shared heartbeat/timeout config from one source, with intervals
 *   long enough that no spontaneous heartbeat fires, so the only wire traffic is the explicit exchanges.
 * Responsibilities: Return a config carrying the long heartbeat, long timeout, and protocol version.
 */
FTransportHostConfig MakeDemoConfig() noexcept
{
	FTransportHostConfig Config{};
	Config.HeartbeatIntervalMilliseconds = 10000;
	Config.PeerTimeoutMilliseconds = 60000;
	Config.ProtocolVersion = 1;
	return Config;
}

/**
 * Motivation: Drives the Hello/Welcome handshake over real localhost UDP, advancing the logical clock a
 *   fixed step per iteration, so the two nodes connect before the demo loop runs.
 * Responsibilities: Pump client and server a bounded number of times until connected, then report success.
 */
bool RunHandshake(
	FHostWifiDevice& ServerDevice,
	FHostWifiDevice& ClientDevice,
	FServerEngine& ServerHost,
	FClientTransport& Client,
	TimePointMilliseconds& LogicalClockMilliseconds) noexcept
{
	for (int Iteration = 0; Iteration < HandshakeIterationCap; ++Iteration)
	{
		LogicalClockMilliseconds += LogicalClockStepMilliseconds;
		(void)Client.PumpSend(LogicalClockMilliseconds);
		if (ServerDevice.PollReadable(ReadinessWaitMilliseconds))
		{
			(void)ServerHost.Tick(LogicalClockMilliseconds);
		}
		if (ClientDevice.PollReadable(ReadinessWaitMilliseconds))
		{
			(void)Client.PumpReceive(LogicalClockMilliseconds);
		}
		if (Client.GetState() == ETransportHostState::Connected)
		{
			return true;
		}
	}
	return false;
}

/**
 * Motivation: Lets the caller decode the two-byte state payload the server broadcasts each step, so a
 *   malformed payload can fail closed rather than print an underdetermined count.
 * Responsibilities: Validate the size and write the tick and actor count on success.
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
 * Motivation: Lets main check whether both loopback UDP sockets bound successfully, as a pure query over
 *   already-constructed devices (FHostWifiDevice opens its socket in its constructor).
 * Responsibilities: Report whether both devices are open and nothing else.
 */
bool BothLoopbackDevicesOpen(const FHostWifiDevice& ServerDevice, const FHostWifiDevice& ClientDevice) noexcept
{
	return ServerDevice.IsOpen() && ClientDevice.IsOpen();
}

/**
 * Motivation: Lets main register the demo's one spawnable actor class and create the server's world, so
 *   later steps run against a fully-built engine.
 * Responsibilities: Register the class and create the world, returning false on the first failing step.
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
 * Motivation: Spawns one server-world actor in response to a client input event, keeping the demo's
 *   world usage bounded.
 * Responsibilities: Honor the spawn limit, create the actor, spawn it into the world, and bump the count.
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
 * Motivation: Lets the server register its channel-1 spawn-request handler in one place, so the bound
 *   lambda stays a thin forwarder into HandleClientSpawnRequest.
 * Responsibilities: Build the binding, register it, and report whether registration succeeded.
 */
bool InstallServerSpawnHandler(
	FServerTransport& ServerTransport, FDemoSpawnContext& SpawnContext, int& SpawnedBeginCount, FDelegateHandle& OutHandle) noexcept
{
	FServerTransport::FMessageHandlerBinding Binding;
	Binding.Bind([&SpawnContext, &SpawnedBeginCount](const FPeerId, const std::uint8_t, TSpan<const std::uint8_t>) noexcept
				 { HandleClientSpawnRequest(SpawnContext, SpawnedBeginCount); });
	return ServerTransport.AddMessageHandler(std::move(Binding), OutHandle) == EDelegateResult::Success;
}

/**
 * Motivation: Decodes a server state broadcast payload into the client's capture state and prints the
 *   received-state trace line, ignoring malformed payloads so they cannot overwrite last-known state.
 * Responsibilities: Decode, store, and print one broadcast; do nothing on a malformed payload.
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
 * Motivation: Lets the client register its channel-2 state-broadcast handler in one place, so the bound
 *   lambda stays a thin forwarder into HandleServerStateBroadcast.
 * Responsibilities: Build the binding, register it, and report whether registration succeeded.
 */
bool InstallClientStateHandler(FClientTransport& ClientTransport, FDemoStateCapture& StateCapture, FDelegateHandle& OutHandle) noexcept
{
	FClientTransport::FMessageHandlerBinding Binding;
	Binding.Bind([&StateCapture](const FPeerId, const std::uint8_t, TSpan<const std::uint8_t> Payload) noexcept
				 { HandleServerStateBroadcast(StateCapture, Payload); });
	return ClientTransport.AddMessageHandler(std::move(Binding), OutHandle) == EDelegateResult::Success;
}

/**
 * Motivation: Lets main configure and start both transport hosts in one ordered step, so BeginPlay never
 *   runs against a half-started session.
 * Responsibilities: Build the client server address, configure and start both hosts, and print the startup lines.
 */
bool ConfigureAndStartHosts(FServerTransport& ServerTransport, FClientTransport& ClientTransport, const FHostWifiDevice& ServerDevice) noexcept
{
	FTransportHostConfig ClientConfig = MakeDemoConfig();
	ClientConfig.ServerAddress =
		MakeUdpAddress(LoopbackIpv4Octets[0], LoopbackIpv4Octets[1], LoopbackIpv4Octets[2], LoopbackIpv4Octets[3], ServerDevice.BoundPort());
	if (ServerTransport.Configure(ENetworkMode::DedicatedServer, MakeDemoConfig()) != ETransportResult::Success)
	{
		return false;
	}
	if (ClientTransport.Configure(ENetworkMode::Client, ClientConfig) != ETransportResult::Success)
	{
		return false;
	}
	if (ServerTransport.Start(0) != ETransportResult::Success)
	{
		return false;
	}
	if (ClientTransport.Start(0) != ETransportResult::Success)
	{
		return false;
	}
	std::printf("[server] listening\n");
	std::printf("[client] connecting to server\n");
	return true;
}

/**
 * Motivation: Reports whether a given state tick should also send a client spawn request, so the demo
 *   issues exactly two requests (first and last tick) and leaves the middle tick as a no-spawn broadcast.
 * Responsibilities: Return true only on the ticks that should issue a spawn request.
 */
bool IsSpawnRequestDue(int StateTick) noexcept
{
	return (StateTick == 1) || (StateTick == 3);
}

/**
 * Motivation: Sends the client's one-byte spawn-request opcode when due, then always pumps the client's
 *   send queue and advances the logical clock, so clock and wire state stay in lockstep every tick.
 * Responsibilities: Send the request when due, advance the clock, and pump the send queue each call.
 */
bool SendSpawnRequestIfDue(FClientTransport& ClientTransport, bool bSpawnRequestDue, TimePointMilliseconds& LogicalClockMilliseconds) noexcept
{
	if (bSpawnRequestDue)
	{
		const std::uint8_t Payload[1] = {SpawnRequestOpcode};
		if (ClientTransport.SendTo(ClientTransport.GetServerPeer(), InputEventChannel, TSpan<const std::uint8_t>(Payload, 1))
			!= ETransportResult::Success)
		{
			return false;
		}
		std::printf("[client] sending spawn request (input event)\n");
	}
	LogicalClockMilliseconds += LogicalClockStepMilliseconds;
	(void)ClientTransport.PumpSend(LogicalClockMilliseconds);
	return true;
}

/**
 * Motivation: Advances one server frame, polling the socket for a due spawn-request datagram and ticking
 *   the engine so this tick's PumpReceive step delivers the input event to the spawn handler.
 * Responsibilities: Poll when due, advance the clock, and tick the server engine.
 */
bool AdvanceServerFrame(
	FServerEngine& ServerHost, FHostWifiDevice& ServerDevice, bool bSpawnRequestDue, TimePointMilliseconds& LogicalClockMilliseconds) noexcept
{
	if (bSpawnRequestDue)
	{
		(void)ServerDevice.PollReadable(ReadinessWaitMilliseconds);
	}
	LogicalClockMilliseconds += LogicalClockStepMilliseconds;
	return ServerHost.Tick(LogicalClockMilliseconds) == ERuntimeResult::Success;
}

/**
 * Motivation: Broadcasts the current tick and world actor count to peers, then ticks the engine again so
 *   this second tick's PumpSend step flushes the broadcast onto the wire.
 * Responsibilities: Broadcast the state payload, print the heartbeat line, advance the clock, and tick.
 */
bool BroadcastServerState(
	FServerTransport& ServerTransport,
	FServerEngine& ServerHost,
	int StateTick,
	int WorldActorCount,
	TimePointMilliseconds& LogicalClockMilliseconds) noexcept
{
	const std::uint8_t StatePayload[2] = {static_cast<std::uint8_t>(StateTick), static_cast<std::uint8_t>(WorldActorCount)};
	if (ServerTransport.Broadcast(StateBroadcastChannel, TSpan<const std::uint8_t>(StatePayload, 2)) != ETransportResult::Success)
	{
		return false;
	}
	std::printf("[server] heartbeat broadcast: state tick=%d actors=%d\n", StateTick, WorldActorCount);
	LogicalClockMilliseconds += LogicalClockStepMilliseconds;
	return ServerHost.Tick(LogicalClockMilliseconds) == ERuntimeResult::Success;
}

/**
 * Motivation: Delivers one broadcast to the client, polling its socket, advancing the clock, and pumping
 *   the receive queue so the state handler prints the received-state trace line.
 * Responsibilities: Poll the client socket, advance the clock, and pump the receive queue.
 */
void DeliverToClient(FClientTransport& ClientTransport, FHostWifiDevice& ClientDevice, TimePointMilliseconds& LogicalClockMilliseconds) noexcept
{
	(void)ClientDevice.PollReadable(ReadinessWaitMilliseconds);
	LogicalClockMilliseconds += LogicalClockStepMilliseconds;
	(void)ClientTransport.PumpReceive(LogicalClockMilliseconds);
}

/**
 * Motivation: Drives the three-tick state-broadcast loop under one shared logical clock, so the demo's
 *   spawn-and-broadcast exchange runs deterministically each tick.
 * Responsibilities: For each tick, send a spawn request if due, advance the server frame, broadcast
 *   state, and deliver it to the client; return false on the first hard failure.
 */
bool RunStateBroadcastLoop(
	FClientTransport& ClientTransport,
	FServerTransport& ServerTransport,
	FServerEngine& ServerHost,
	FHostWifiDevice& ServerDevice,
	FHostWifiDevice& ClientDevice,
	int& WorldActorCount,
	TimePointMilliseconds& LogicalClockMilliseconds) noexcept
{
	constexpr int StateBroadcastStepCount = 3;
	for (int StateTick = 1; StateTick <= StateBroadcastStepCount; ++StateTick)
	{
		const bool bSpawnRequestDue = IsSpawnRequestDue(StateTick);
		if (!SendSpawnRequestIfDue(ClientTransport, bSpawnRequestDue, LogicalClockMilliseconds))
		{
			return false;
		}
		if (!AdvanceServerFrame(ServerHost, ServerDevice, bSpawnRequestDue, LogicalClockMilliseconds))
		{
			return false;
		}
		if (!BroadcastServerState(ServerTransport, ServerHost, StateTick, WorldActorCount, LogicalClockMilliseconds))
		{
			return false;
		}
		DeliverToClient(ClientTransport, ClientDevice, LogicalClockMilliseconds);
	}
	return true;
}

} // namespace

/**
 * Motivation: Application entry point for the TwoNodeDemo, so the single entry point owns the one place that
 *   composes the server engine and bare client over real localhost UDP and drives a byte-identical trace.
 * Responsibilities: Construct and wire both nodes, run the handshake and the state-broadcast loop, and
 *   return 0 on success or 1 on any failure.
 */
int main()
{
	using namespace MicroWorld::Core;
	using namespace MicroWorld::Engine;
	using MicroWorld::Platform::Host::FHostWifiDevice;
	using MicroWorld::Transport::MakeUdpAddress;

	FHostWifiDevice ServerDevice(0);
	FHostWifiDevice ClientDevice(0);
	FServerTransport ServerTransport(ServerDevice);
	FClientTransport ClientTransport(ClientDevice);
	THostPlaySystem<FServerTransport> ServerFrame{ServerTransport};

	int SpawnSequence = 0;
	int SpawnedBeginCount = 0;
	int WorldActorCount = 0;

	FServerEngine ServerHost{FGarbageCollectionBudget{1, 4, 8}, ServerFrame};
	FDemoSpawnContext SpawnContext{ServerHost, SpawnSequence, WorldActorCount};
	FDemoStateCapture StateCapture{};
	FDelegateHandle SpawnHandle{};
	FDelegateHandle StateHandle{};
	TimePointMilliseconds LogicalClockMilliseconds = 0;

	if (!BothLoopbackDevicesOpen(ServerDevice, ClientDevice))
	{
		return 1;
	}
	if (!RegisterDemoWorld(ServerHost))
	{
		return 1;
	}
	if (!InstallServerSpawnHandler(ServerTransport, SpawnContext, SpawnedBeginCount, SpawnHandle))
	{
		return 1;
	}
	if (!InstallClientStateHandler(ClientTransport, StateCapture, StateHandle))
	{
		return 1;
	}
	if (!ConfigureAndStartHosts(ServerTransport, ClientTransport, ServerDevice))
	{
		return 1;
	}

	if (ServerHost.BeginPlay(0) != ERuntimeResult::Success)
	{
		return 1;
	}
	if (!RunHandshake(ServerDevice, ClientDevice, ServerHost, ClientTransport, LogicalClockMilliseconds))
	{
		return 1;
	}
	std::printf("[client] connected\n");

	if (!RunStateBroadcastLoop(ClientTransport, ServerTransport, ServerHost, ServerDevice, ClientDevice, WorldActorCount, LogicalClockMilliseconds))
	{
		return 1;
	}

	std::printf("[demo] complete\n");
	return 0;
}
