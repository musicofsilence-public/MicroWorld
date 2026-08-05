/**
 * Motivation: Demonstrates two independent Network systems communicating through real localhost UDP.
 * Responsibilities: Compose one Engine server and one direct client, then prove connect, request, sender resolution, and broadcast.
 */

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Delegates/DelegateResult.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/DefaultEngineTraits.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/GarbageCollectionBudget.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Engine/PlaySystemSet.h>
#include <MicroWorld/Messaging/ChannelInformation.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessagingRoute.h>
#include <MicroWorld/Messaging/MessagingSystem.h>
#include <MicroWorld/Networking/ConnectionState.h>
#include <MicroWorld/Networking/NetworkSystem.h>
#include <MicroWorld/Platform/Host/HostWifiDevice.h>
#include <MicroWorld/Transport/Wifi/UdpAddressCodec.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace
{
using namespace MicroWorld::Core;
using namespace MicroWorld::Engine;
using namespace MicroWorld::Messaging;
using namespace MicroWorld::Networking;
using MicroWorld::Platform::Host::FHostWifiDevice;
using MicroWorld::Transport::MakeUdpAddress;

/** Motivation: Selects the local loopback interface shared by both real UDP sockets. */
constexpr std::uint8_t LoopbackIpv4Octets[4] = {127, 0, 0, 1};
/** Motivation: Bounds handshake work when an operating-system socket becomes unavailable. */
constexpr int HandshakeIterationCap = 32;
/** Motivation: Advances every explicit test turn with deterministic caller-supplied time. */
constexpr TimePointMilliseconds LogicalClockStepMilliseconds = 10;
/** Motivation: Identifies the local-only client request channel. */
constexpr FNameId InputEventChannel = MakeNameId("TwoNodeInput");
/** Motivation: Identifies the local-only server state channel. */
constexpr FNameId StateChannel = MakeNameId("TwoNodeState");
/** Motivation: Filters spawn request messages. */
constexpr FNameId SpawnMessageName = MakeNameId("TwoNodeSpawn");
/** Motivation: Filters state message updates. */
constexpr FNameId StateMessageName = MakeNameId("TwoNodeStateUpdate");
/** Motivation: Identifies the accepted client request payload. */
constexpr std::uint8_t SpawnRequestOpcode = 0x42;
/** Motivation: Bounds the server world to the two visible demo spawns. */
constexpr int MaxSpawns = 2;
/** Motivation: Names the descriptor used to construct the visible server actor. */
constexpr FTypeId DemoSpawnedActorTypeId{0x00080001u};

/**
 * Motivation: Bounds the demo server's Engine storage around the requested actors.
 * Responsibilities: Supply the fixed server-side Engine capacities used by this demo.
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
 * Motivation: Makes accepted remote requests observable as world actors.
 * Responsibilities: Increment the composition-owned begin counter when the actor enters play.
 * Example: ServerHost.CreateObject<FDemoSpawnedActor>(DemoSpawnedActorTypeId, BeginCount);
 */
class FDemoSpawnedActor final : public AActor
{
public:
	/**
	 * Motivation: Receives the demo-owned begin counter.
	 * Responsibilities: Retain the reference.
	 */
	explicit FDemoSpawnedActor(int& InBeginCount) noexcept : BeginCount(InBeginCount) {}
	/**
	 * Motivation: Keeps descriptor teardown available.
	 * Responsibilities: Release no owned state.
	 */
	~FDemoSpawnedActor() noexcept override = default;

protected:
	/**
	 * Motivation: Confirms the actor entered play.
	 * Responsibilities: Increment the bound counter.
	 */
	void BeginPlay() noexcept override { ++BeginCount; }

private:
	/** Motivation: Stores the composition-owned observable begin count. */
	int& BeginCount;
};

/** Motivation: Gives the server subscriber access to the composed Engine. */
FServerEngine* GServerHost = nullptr;
/** Motivation: Resolves the validated logical sender before a world mutation. */
FNetworkSystem* GServerNetwork = nullptr;
/** Motivation: Counts accepted spawn requests. */
int GSpawnCount = 0;
/** Motivation: Counts actor BeginPlay confirmations. */
int GSpawnedBeginCount = 0;
/** Motivation: Carries server state into broadcast payloads. */
int GWorldActorCount = 0;
/** Motivation: Records the client-visible actor count from the latest validated state update. */
int GClientActorCount = 0;

/**
 * Motivation: Creates one actor only for a valid Network-republished client request.
 * Responsibilities: Validate source and payload before mutating the server world.
 */
void HandleSpawnRequest(const FMessage& InMessage) noexcept
{
	if (GServerHost == nullptr || GServerNetwork == nullptr || !GServerNetwork->ResolveSenderPeer(InMessage).IsValid()
		|| InMessage.GetPayload().Size() != 1 || InMessage.GetPayload()[0] != SpawnRequestOpcode || GSpawnCount >= MaxSpawns)
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
	++GSpawnCount;
	++GWorldActorCount;
	std::printf("[server] received spawn request from peer -> spawned actor %d (world actor count = %d)\n", GWorldActorCount, GWorldActorCount);
}

/**
 * Motivation: Records state accepted by the client Network system.
 * Responsibilities: Ignore malformed payloads.
 */
void HandleStateUpdate(const FMessage& InMessage) noexcept
{
	if (InMessage.GetPayload().Size() != 2)
	{
		return;
	}
	GClientActorCount = static_cast<int>(InMessage.GetPayload()[1]);
	std::printf("[client] received state: tick=%d actors=%d\n", static_cast<int>(InMessage.GetPayload()[0]), GClientActorCount);
}

/**
 * Motivation: Drives one direct client lifecycle turn in the documented device, Messaging, Network order.
 * Responsibilities: Advance each layer exactly once.
 */
void AdvanceClient(
	FHostWifiDevice& ClientDevice, FMessagingSystem& ClientMessaging, FNetworkSystem& ClientNetwork, const TimePointMilliseconds Now) noexcept
{
	ClientDevice.PreAdvance(Now);
	ClientMessaging.PreAdvance(Now);
	ClientNetwork.PreAdvance(Now);
	ClientNetwork.PostAdvance(Now);
	ClientMessaging.PostAdvance(Now);
	ClientDevice.PostAdvance(Now);
}
} // namespace

/**
 * Motivation: Runs a real two-socket end-to-end Network session.
 * Responsibilities: Return nonzero if construction, admission, routing, or broadcast fails.
 */
int main()
{
	FHostWifiDevice ServerDevice(0);
	FHostWifiDevice ClientDevice(0);
	if (!ServerDevice.IsOpen() || !ClientDevice.IsOpen())
	{
		return 1;
	}
	TPlaySystemSet<1> ServerFrames;
	FServerEngine ServerHost{FGarbageCollectionBudget{1, 4, 8}, ServerFrames};
	if (ServerFrames.Add(ServerDevice) != EEngineResult::Success || ServerHost.CreateMessagingSystem({}) != ERuntimeResult::Success)
	{
		return 1;
	}
	FMessagingSystem* const ServerMessaging = ServerHost.GetMessagingSystem();
	FMessagingLinkId ServerLinkId{};
	if (ServerMessaging == nullptr || ServerMessaging->RegisterLink(ServerDevice, ServerLinkId) != EMessagingResult::Success
		|| ServerHost.CreateNetworkSystem({ENetworkRole::Server}) != ENetworkResult::Success
		|| ServerMessaging->CreateChannel({InputEventChannel, false, nullptr, {}}) != EMessagingResult::Success
		|| ServerMessaging->CreateChannel({StateChannel, false, nullptr, {}}) != EMessagingResult::Success)
	{
		return 1;
	}
	GServerHost = &ServerHost;
	GServerNetwork = ServerHost.GetNetworkSystem();
	FMessagingSystem::FSubscriberDelegate SpawnSubscriber;
	if (GServerNetwork == nullptr || SpawnSubscriber.Bind(HandleSpawnRequest) != EDelegateResult::Success
		|| ServerMessaging->SubscribeToChannel(InputEventChannel, SpawnMessageName, std::move(SpawnSubscriber)) != EMessagingResult::Success
		|| ServerHost.RegisterClass<FDemoSpawnedActor>(DemoSpawnedActorTypeId, "DemoSpawnedActor") != EObjectResult::Success
		|| ServerHost.CreateWorld().Get() == nullptr || ServerHost.BeginPlay(0) != ERuntimeResult::Success)
	{
		return 1;
	}

	FMessagingSystem ClientMessaging;
	FNetworkSystem ClientNetwork{ClientMessaging, {ENetworkRole::Client}};
	FMessagingLinkId ClientLinkId{};
	if (ClientMessaging.RegisterLink(ClientDevice, ClientLinkId) != EMessagingResult::Success || ClientNetwork.Initialize() != ENetworkResult::Success
		|| ClientMessaging.CreateChannel({InputEventChannel, false, nullptr, {}}) != EMessagingResult::Success
		|| ClientMessaging.CreateChannel({StateChannel, false, nullptr, {}}) != EMessagingResult::Success)
	{
		return 1;
	}
	FMessagingSystem::FSubscriberDelegate StateSubscriber;
	if (StateSubscriber.Bind(HandleStateUpdate) != EDelegateResult::Success
		|| ClientMessaging.SubscribeToChannel(StateChannel, StateMessageName, std::move(StateSubscriber)) != EMessagingResult::Success)
	{
		return 1;
	}
	ClientDevice.BeginPlay(0);
	ClientMessaging.BeginPlay(0);
	ClientNetwork.BeginPlay(0);
	const FMessagingRoute ServerRoute{
		ClientLinkId,
		MakeUdpAddress(LoopbackIpv4Octets[0], LoopbackIpv4Octets[1], LoopbackIpv4Octets[2], LoopbackIpv4Octets[3], ServerDevice.BoundPort())};
	if (ClientNetwork.ConnectToServer(ServerRoute, 0) != ENetworkResult::Success)
	{
		return 1;
	}
	std::printf("[server] listening\n");
	std::printf("[client] connecting to server\n");

	TimePointMilliseconds Now = 0;
	for (int Iteration = 0; Iteration < HandshakeIterationCap && ClientNetwork.GetConnectionState() != EConnectionState::Connected; ++Iteration)
	{
		Now += LogicalClockStepMilliseconds;
		(void)ServerHost.Tick(Now);
		AdvanceClient(ClientDevice, ClientMessaging, ClientNetwork, Now);
	}
	if (ClientNetwork.GetConnectionState() != EConnectionState::Connected)
	{
		return 1;
	}
	std::printf("[client] connected\n");

	for (int StateTick = 1; StateTick <= 3; ++StateTick)
	{
		if (StateTick == 1 || StateTick == 3)
		{
			const std::uint8_t Payload[1] = {SpawnRequestOpcode};
			FMessage Request;
			Request.SetMessageNameId(SpawnMessageName);
			Request.SetPayload(TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
			if (ClientNetwork.SendToServer(InputEventChannel, Request) != ENetworkResult::Success)
			{
				return 1;
			}
			std::printf("[client] sending spawn request (input event)\n");
		}
		Now += LogicalClockStepMilliseconds;
		(void)ServerHost.Tick(Now);
		const std::uint8_t StatePayload[2] = {static_cast<std::uint8_t>(StateTick), static_cast<std::uint8_t>(GWorldActorCount)};
		FMessage State;
		State.SetMessageNameId(StateMessageName);
		State.SetPayload(TSpan<const std::uint8_t>(StatePayload, sizeof(StatePayload)));
		if (GServerNetwork->Broadcast(StateChannel, State) != ENetworkResult::Success)
		{
			return 1;
		}
		std::printf("[server] heartbeat broadcast: state tick=%d actors=%d\n", StateTick, GWorldActorCount);
		Now += LogicalClockStepMilliseconds;
		AdvanceClient(ClientDevice, ClientMessaging, ClientNetwork, Now);
	}

	std::printf("[demo] complete\n");
	return GClientActorCount == MaxSpawns ? 0 : 1;
}
