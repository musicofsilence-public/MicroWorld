#include "TransportAllocationCounters.h"
#include "TestSupport.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Transport/HostLoopback.h>
#include <MicroWorld/Transport/DeviceAddress.h>
#include <MicroWorld/Transport/TransportHost.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Core/Time.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{

using MicroWorld::Core::FDelegateHandle;
using MicroWorld::Core::FReceiveResult;
using MicroWorld::Core::ITransportDevice;
using MicroWorld::Core::TimePointMilliseconds;
using MicroWorld::Core::TSpan;
using MicroWorld::Tests::GlobalAllocationCount;
using MicroWorld::Transport::ENetworkMode;
using MicroWorld::Transport::ETransportHostState;
using MicroWorld::Transport::ETransportResult;
using MicroWorld::Transport::FPeerId;
using MicroWorld::Transport::FTransportHostConfig;
using MicroWorld::Transport::THostLoopback;
using MicroWorld::Transport::TTransportHost;
using MicroWorld::Transport::Address::FDeviceAddress;
using MicroWorld::Transport::Address::MakeLoopbackAddress;

/** Motivation: Heartbeat interval (ms) the deterministic host config stamps so timed cases advance in fixed steps. */
constexpr TimePointMilliseconds HeartbeatIntervalMs = 100;
/** Motivation: Peer timeout window (ms) after which a silent peer is evicted. */
constexpr TimePointMilliseconds PeerTimeoutMs = 500;
/** Motivation: Last heartbeat instant before the timeout window the keep-alive case reaches. */
constexpr TimePointMilliseconds KeepAliveWindowEndMs = 800;
/** Motivation: Instant past the timeout window where a silent peer is evicted. */
constexpr TimePointMilliseconds EvictionTimeoutMs = 1000;
/** Motivation: Instant at which the generation-bump case evicts the first client. */
constexpr TimePointMilliseconds GenerationBumpEvictMs = 2000;
/** Motivation: Instant past the client-side timeout window where the server is treated as gone. */
constexpr TimePointMilliseconds ClientServerTimeoutMs = 2000;
/** Motivation: Instant at which the repeated-Hello case re-greets after the first admission. */
constexpr TimePointMilliseconds RepeatedHelloResendMs = 100;
/** Motivation: Send timestamp (ms) for the single-session and broadcast cases. */
constexpr TimePointMilliseconds SessionPumpMs = 100;

/** Motivation: Loopback template parameter: server port index. */
constexpr std::uint8_t ServerPortIndex = 0;
/** Motivation: Loopback template parameter: first client port index. */
constexpr std::uint8_t FirstClientPortIndex = 1;
/** Motivation: Loopback template parameter: second client port index. */
constexpr std::uint8_t SecondClientPortIndex = 2;
/** Motivation: Loopback template parameter: third client port index. */
constexpr std::uint8_t ThirdClientPortIndex = 3;
/** Motivation: FFloodDevice sender port index stamped into OutFrom. */
constexpr std::uint8_t FloodSenderPortIndex = 9;
/** Motivation: Channel the host-to-host send cases address. */
constexpr std::uint8_t SendChannel = 1;
/** Motivation: Channel the broadcast cases address. */
constexpr std::uint8_t BroadcastChannel = 1;
/** Motivation: Channel the SendTo single-target case addresses. */
constexpr std::uint8_t SendToChannel = 2;
/** Motivation: Channel the local-peer dispatch case addresses. */
constexpr std::uint8_t LocalDispatchChannel = 3;
/** Motivation: Protocol version the matched-protocol handshake cases use. */
constexpr std::uint8_t MatchedProtocolVersion = 1;
/** Motivation: Protocol version the mismatched-Hello case uses on the client side. */
constexpr std::uint8_t MismatchedProtocolVersion = 2;
/** Motivation: Welcome protocol version the allocation case sets. */
constexpr std::uint8_t WelcomeProtocolVersion = 1;
/** Motivation: Welcome peer index the allocation case sets. */
constexpr std::uint8_t WelcomePeerIndex = 2;
/** Motivation: Welcome peer generation the allocation case sets. */
constexpr std::uint8_t WelcomePeerGeneration = 3;
/** Motivation: Loopback mailbox depth every host config case uses. */
constexpr std::size_t LoopbackMailboxDepth = 8;
/** Motivation: Loopback per-packet byte capacity every host config case uses. */
constexpr std::size_t LoopbackPacketBytes = 64;
/** Motivation: Per-host packet byte capacity every TTransportHost instantiation uses. */
constexpr std::size_t HostPacketBytes = 64;
/** Motivation: Number of remote peers the rejection-when-full case admits (matches MaxPeers). */
constexpr std::size_t FullPeerCount = 2;
/** Motivation: Peer count reported by the single-peer cases after a successful handshake. */
constexpr std::size_t AdmittedPeerCount = 1;
/** Motivation: Peer count reported by the no-peer cases. */
constexpr std::size_t EmptyPeerCount = 0;
/** Motivation: Peer count reported by the broadcast case after both clients connect. */
constexpr std::size_t BroadcastPeerCount = 2;
/** Motivation: Number of receives one bounded pump is permitted to call under flood. */
constexpr std::size_t BoundedPumpReceiveCount = 7;
/** Motivation: Byte value the FFloodDevice writes into every header byte of its empty control frame. */
constexpr std::uint8_t EmptyControlFrameByte = 0;
/** Motivation: Byte count the FFloodDevice reports as received per call. */
constexpr std::size_t FloodReceivedByteCount = 4;

/** Motivation: Sentinel payload byte the generation-bump case sends to a stale and a fresh peer id. */
constexpr std::uint8_t GenerationBumpPayloadByte = 0x42;
/** Motivation: Broadcast payload byte the broadcast case delivers to every connected peer. */
constexpr std::uint8_t BroadcastPayloadByte = 0x5A;
/** Motivation: Single-byte payload the SendTo single-target case delivers. */
constexpr std::uint8_t SendToPayloadByte = 0x33;
/** Motivation: Single-byte payload the local-peer dispatch case delivers synchronously. */
constexpr std::uint8_t LocalDispatchPayloadByte = 0x77;
/** Motivation: First byte of the three-byte session payload the allocation case broadcasts. */
constexpr std::uint8_t SessionPayloadFirstByte = 0x01;
/** Motivation: Second byte of the three-byte session payload the allocation case broadcasts. */
constexpr std::uint8_t SessionPayloadSecondByte = 0x02;
/** Motivation: Third byte of the three-byte session payload the allocation case broadcasts. */
constexpr std::uint8_t SessionPayloadThirdByte = 0x03;
/** Motivation: Single-byte payload the standalone send-attempt case threads through SendTo/Broadcast. */
constexpr std::uint8_t StandalonePayloadByte = 0x01;
/** Motivation: Single-byte payload the dedicated-server local-dispatch case rejects. */
constexpr std::uint8_t DedicatedServerLocalPayloadByte = 0x11;

/** Motivation: Hand-assembled control frame whose payload byte names an undefined control type (0x09): channel 0,
 *   zero flags, declared payload length 1, and the single payload byte 0x09.
 */
constexpr std::uint8_t UnknownControlTypeFrame[5] = {0x00, 0x00, 0x01, 0x00, 0x09};

/**
 * Motivation: Records the last message a handler observed so a test can assert delivery.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FHandlerCapture
{
	/** Motivation: Number of messages the handler has observed; zero means it never ran. */
	std::size_t Count{0};

	/** Motivation: Sender identity from the most recent dispatch. */
	FPeerId From{};

	/** Motivation: Channel from the most recent dispatch. */
	std::uint8_t Channel{0};

	/** Motivation: First payload byte from the most recent dispatch, or zero for an empty payload. */
	std::uint8_t FirstByte{0};
};

/**
 * Motivation: Builds a fast-heartbeat host config with a short timeout window for deterministic tests.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
FTransportHostConfig MakeHostConfig(const std::uint8_t InProtocolVersion) noexcept
{
	FTransportHostConfig Config{};
	Config.HeartbeatIntervalMilliseconds = HeartbeatIntervalMs;
	Config.PeerTimeoutMilliseconds = PeerTimeoutMs;
	Config.ProtocolVersion = InProtocolVersion;
	return Config;
}

/**
 * Motivation: Builds a client config that greets the loopback port `InServerPort`.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
FTransportHostConfig MakeClientConfig(const std::uint8_t InProtocolVersion, const std::uint8_t InServerPort) noexcept
{
	FTransportHostConfig Config = MakeHostConfig(InProtocolVersion);
	Config.ServerAddress = MakeLoopbackAddress(InServerPort);
	return Config;
}

/**
 * Motivation: Binds one capturing handler into a host and returns its removal handle.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
template<typename HostType>
FDelegateHandle InstallCapture(HostType& InHost, FHandlerCapture& InCapture) noexcept
{
	typename HostType::FMessageHandlerBinding Binding;
	Binding.Bind(
		[&InCapture](const FPeerId InFrom, const std::uint8_t InChannel, TSpan<const std::uint8_t> InPayload) noexcept
		{
			++InCapture.Count;
			InCapture.From = InFrom;
			InCapture.Channel = InChannel;
			InCapture.FirstByte = InPayload.Size() > 0 ? InPayload[0] : std::uint8_t{0};
		});
	FDelegateHandle Handle{};
	(void)InHost.AddMessageHandler(std::move(Binding), Handle);
	return Handle;
}

/**
 * Motivation: Runs one full Hello->Welcome handshake round at `InNowMilliseconds`.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
template<typename ServerType, typename ClientType>
void RunHandshake(ServerType& InServer, ClientType& InClient, const TimePointMilliseconds InNowMilliseconds) noexcept
{
	InClient.PumpSend(InNowMilliseconds);
	InServer.PumpReceive(InNowMilliseconds);
	InServer.PumpSend(InNowMilliseconds);
	InClient.PumpReceive(InNowMilliseconds);
}

/**
 * Motivation: Drive one Hello from the client to a started server, then have the server send Welcome back.
 * Responsibilities: The server admits one peer on the Hello and the client reaches Connected once it receives the
 *   Welcome.
 */
MW_TEST_CASE(TransportHostServerAdmitsClientOnHello)
{
	// Arrange
	THostLoopback<2, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	TTransportHost<2, HostPacketBytes> Server(Loopback.Port(ServerPortIndex));
	TTransportHost<1, HostPacketBytes> Client(Loopback.Port(FirstClientPortIndex));
	(void)Server.Configure(ENetworkMode::DedicatedServer, MakeHostConfig(MatchedProtocolVersion));
	(void)Client.Configure(ENetworkMode::Client, MakeClientConfig(MatchedProtocolVersion, ServerPortIndex));
	(void)Server.Start(0);
	(void)Client.Start(0);

	// Act - client sends Hello, server receives and admits it
	Client.PumpSend(0);
	Server.PumpReceive(0);
	// Assert
	MW_EXPECT_EQ(Test, AdmittedPeerCount, Server.ActivePeerCount(), "Server admits one peer on a valid Hello");

	// Act - server sends Welcome, client receives and connects
	Server.PumpSend(0);
	Client.PumpReceive(0);
	// Assert
	MW_EXPECT_EQ(Test, ETransportHostState::Connected, Client.GetState(), "Client connects once it receives the Welcome");
}

/**
 * Motivation: Configure a client but leave it unstarted, then start it, then run the handshake.
 * Responsibilities: The client state advances Idle to Connecting on Start and reaches Connected after the handshake.
 */
MW_TEST_CASE(TransportHostClientAdvancesThroughConnectingToConnected)
{
	// Arrange
	THostLoopback<2, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	TTransportHost<2, HostPacketBytes> Server(Loopback.Port(ServerPortIndex));
	TTransportHost<1, HostPacketBytes> Client(Loopback.Port(FirstClientPortIndex));
	(void)Server.Configure(ENetworkMode::DedicatedServer, MakeHostConfig(MatchedProtocolVersion));
	(void)Client.Configure(ENetworkMode::Client, MakeClientConfig(MatchedProtocolVersion, ServerPortIndex));
	(void)Server.Start(0);

	// Assert - a configured but unstarted client is Idle
	MW_EXPECT_EQ(Test, ETransportHostState::Idle, Client.GetState(), "A configured but unstarted client is Idle");
	// Act
	(void)Client.Start(0);
	// Assert - a started client is Connecting
	MW_EXPECT_EQ(Test, ETransportHostState::Connecting, Client.GetState(), "A started client is Connecting");
	// Act
	RunHandshake(Server, Client, 0);
	// Assert
	MW_EXPECT_EQ(Test, ETransportHostState::Connected, Client.GetState(), "The client reaches Connected after the handshake");
}

/**
 * Motivation: Start three clients against a two-peer-capacity server and have each client send Hello.
 * Responsibilities: The server admits exactly its peer capacity and rejects the overflow Hello.
 */
MW_TEST_CASE(TransportHostRejectsHelloWhenPeerTableFull)
{
	// Arrange
	THostLoopback<4, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	TTransportHost<2, HostPacketBytes> Server(Loopback.Port(ServerPortIndex));
	TTransportHost<1, HostPacketBytes> FirstClient(Loopback.Port(FirstClientPortIndex));
	TTransportHost<1, HostPacketBytes> SecondClient(Loopback.Port(SecondClientPortIndex));
	TTransportHost<1, HostPacketBytes> ThirdClient(Loopback.Port(ThirdClientPortIndex));
	(void)Server.Configure(ENetworkMode::DedicatedServer, MakeHostConfig(MatchedProtocolVersion));
	(void)FirstClient.Configure(ENetworkMode::Client, MakeClientConfig(MatchedProtocolVersion, ServerPortIndex));
	(void)SecondClient.Configure(ENetworkMode::Client, MakeClientConfig(MatchedProtocolVersion, ServerPortIndex));
	(void)ThirdClient.Configure(ENetworkMode::Client, MakeClientConfig(MatchedProtocolVersion, ServerPortIndex));
	(void)Server.Start(0);
	(void)FirstClient.Start(0);
	(void)SecondClient.Start(0);
	(void)ThirdClient.Start(0);

	// Act
	FirstClient.PumpSend(0);
	SecondClient.PumpSend(0);
	ThirdClient.PumpSend(0);
	Server.PumpReceive(0);

	// Assert
	MW_EXPECT_EQ(Test, FullPeerCount, Server.ActivePeerCount(), "Server admits exactly MaxPeers clients and rejects the rest");
}

/**
 * Motivation: Complete a handshake, then drive client heartbeats every interval well past the timeout window.
 * Responsibilities: Heartbeats received within the window keep the peer alive beyond the timeout.
 */
MW_TEST_CASE(TransportHostHeartbeatKeepsPeerAlive)
{
	// Arrange
	THostLoopback<2, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	TTransportHost<2, HostPacketBytes> Server(Loopback.Port(ServerPortIndex));
	TTransportHost<1, HostPacketBytes> Client(Loopback.Port(FirstClientPortIndex));
	(void)Server.Configure(ENetworkMode::DedicatedServer, MakeHostConfig(MatchedProtocolVersion));
	(void)Client.Configure(ENetworkMode::Client, MakeClientConfig(MatchedProtocolVersion, ServerPortIndex));
	(void)Server.Start(0);
	(void)Client.Start(0);
	RunHandshake(Server, Client, 0);

	// Act - drive client heartbeats every interval well past the 500 ms timeout window.
	for (TimePointMilliseconds Now = HeartbeatIntervalMs; Now <= KeepAliveWindowEndMs; Now += HeartbeatIntervalMs)
	{
		Client.PumpSend(Now);
		Server.PumpReceive(Now);
	}

	// Assert
	MW_EXPECT_EQ(Test, AdmittedPeerCount, Server.ActivePeerCount(), "Heartbeats keep the peer alive beyond the timeout window");
}

/**
 * Motivation: Complete a handshake, then send no further client traffic while advancing the server past the
 *   timeout window.
 * Responsibilities: The peer is evicted after missing heartbeats past the timeout.
 */
MW_TEST_CASE(TransportHostEvictsPeerAfterTimeout)
{
	// Arrange
	THostLoopback<2, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	TTransportHost<2, HostPacketBytes> Server(Loopback.Port(ServerPortIndex));
	TTransportHost<1, HostPacketBytes> Client(Loopback.Port(FirstClientPortIndex));
	(void)Server.Configure(ENetworkMode::DedicatedServer, MakeHostConfig(MatchedProtocolVersion));
	(void)Client.Configure(ENetworkMode::Client, MakeClientConfig(MatchedProtocolVersion, ServerPortIndex));
	(void)Server.Start(0);
	(void)Client.Start(0);
	RunHandshake(Server, Client, 0);
	MW_EXPECT_EQ(Test, AdmittedPeerCount, Server.ActivePeerCount(), "Peer is active right after the handshake");

	// Act - no further client traffic; advance the server past the timeout window.
	Server.PumpReceive(EvictionTimeoutMs);
	// Assert
	MW_EXPECT_EQ(Test, EmptyPeerCount, Server.ActivePeerCount(), "Peer is evicted after missing heartbeats past the timeout");
}

/**
 * Motivation: Admit a first client, evict it by timeout, then admit a second client into the freed slot and send
 *   to both the stale and fresh peer ids.
 * Responsibilities: Eviction bumps the slot generation; the stale id fails to send while the freshly assigned id
 *   resolves the reused slot.
 */
MW_TEST_CASE(TransportHostBumpsGenerationSoStaleIdFailsAfterReadmission)
{
	// Arrange
	THostLoopback<3, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	TTransportHost<2, HostPacketBytes> Server(Loopback.Port(ServerPortIndex));
	TTransportHost<1, HostPacketBytes> FirstClient(Loopback.Port(FirstClientPortIndex));
	TTransportHost<1, HostPacketBytes> SecondClient(Loopback.Port(SecondClientPortIndex));
	(void)Server.Configure(ENetworkMode::DedicatedServer, MakeHostConfig(MatchedProtocolVersion));
	(void)FirstClient.Configure(ENetworkMode::Client, MakeClientConfig(MatchedProtocolVersion, ServerPortIndex));
	(void)SecondClient.Configure(ENetworkMode::Client, MakeClientConfig(MatchedProtocolVersion, ServerPortIndex));
	(void)Server.Start(0);
	(void)FirstClient.Start(0);

	RunHandshake(Server, FirstClient, 0);
	const FPeerId StaleId = FirstClient.GetAssignedPeer();

	// Act - first client goes silent and times out, freeing and bumping its slot.
	Server.PumpReceive(GenerationBumpEvictMs);
	// Assert
	MW_EXPECT_EQ(Test, EmptyPeerCount, Server.ActivePeerCount(), "First client is evicted after timeout");

	// Act - second client is admitted into the same freed slot with a bumped generation.
	(void)SecondClient.Start(GenerationBumpEvictMs);
	RunHandshake(Server, SecondClient, GenerationBumpEvictMs);
	const FPeerId FreshId = SecondClient.GetAssignedPeer();

	const std::uint8_t Payload[1] = {GenerationBumpPayloadByte};
	// Act / Assert - the stale id must fail and the fresh id must succeed on the reused slot.
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Invalid,
		Server.SendTo(StaleId, SendChannel, TSpan<const std::uint8_t>(Payload, sizeof(Payload))),
		"A stale peer id no longer matches the reused slot");
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Server.SendTo(FreshId, SendChannel, TSpan<const std::uint8_t>(Payload, sizeof(Payload))),
		"The freshly assigned peer id resolves the reused slot");
}

/**
 * Motivation: Connect two clients, broadcast a payload from the server, and pump delivery to both.
 * Responsibilities: Each connected client receives the broadcast exactly once with the broadcast payload byte.
 */
MW_TEST_CASE(TransportHostBroadcastReachesEveryConnectedPeer)
{
	// Arrange
	THostLoopback<3, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	TTransportHost<3, HostPacketBytes> Server(Loopback.Port(ServerPortIndex));
	TTransportHost<1, HostPacketBytes> FirstClient(Loopback.Port(FirstClientPortIndex));
	TTransportHost<1, HostPacketBytes> SecondClient(Loopback.Port(SecondClientPortIndex));
	(void)Server.Configure(ENetworkMode::DedicatedServer, MakeHostConfig(MatchedProtocolVersion));
	(void)FirstClient.Configure(ENetworkMode::Client, MakeClientConfig(MatchedProtocolVersion, ServerPortIndex));
	(void)SecondClient.Configure(ENetworkMode::Client, MakeClientConfig(MatchedProtocolVersion, ServerPortIndex));
	(void)Server.Start(0);
	(void)FirstClient.Start(0);
	(void)SecondClient.Start(0);

	FHandlerCapture FirstCapture{};
	FHandlerCapture SecondCapture{};
	(void)InstallCapture(FirstClient, FirstCapture);
	(void)InstallCapture(SecondClient, SecondCapture);

	RunHandshake(Server, FirstClient, 0);
	RunHandshake(Server, SecondClient, 0);
	MW_EXPECT_EQ(Test, BroadcastPeerCount, Server.ActivePeerCount(), "Both clients are connected before the broadcast");

	const std::uint8_t Payload[1] = {BroadcastPayloadByte};
	// Act
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Server.Broadcast(BroadcastChannel, TSpan<const std::uint8_t>(Payload, sizeof(Payload))),
		"Broadcast queues for every peer");
	Server.PumpSend(0);
	FirstClient.PumpReceive(0);
	SecondClient.PumpReceive(0);

	// Assert
	MW_EXPECT_EQ(Test, AdmittedPeerCount, FirstCapture.Count, "First client receives the broadcast exactly once");
	MW_EXPECT_EQ(Test, BroadcastPayloadByte, FirstCapture.FirstByte, "First client sees the broadcast payload");
	MW_EXPECT_EQ(Test, AdmittedPeerCount, SecondCapture.Count, "Second client receives the broadcast exactly once");
	MW_EXPECT_EQ(Test, BroadcastPayloadByte, SecondCapture.FirstByte, "Second client sees the broadcast payload");
}

/**
 * Motivation: Connect a client, send a payload to its peer id on a chosen channel, and pump delivery.
 * Responsibilities: The addressed peer receives exactly one message on the requested channel carrying the sent payload.
 */
MW_TEST_CASE(TransportHostSendToDeliversToTheAddressedPeer)
{
	// Arrange
	THostLoopback<2, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	TTransportHost<2, HostPacketBytes> Server(Loopback.Port(ServerPortIndex));
	TTransportHost<1, HostPacketBytes> Client(Loopback.Port(FirstClientPortIndex));
	(void)Server.Configure(ENetworkMode::DedicatedServer, MakeHostConfig(MatchedProtocolVersion));
	(void)Client.Configure(ENetworkMode::Client, MakeClientConfig(MatchedProtocolVersion, ServerPortIndex));
	(void)Server.Start(0);
	(void)Client.Start(0);

	FHandlerCapture ClientCapture{};
	(void)InstallCapture(Client, ClientCapture);
	RunHandshake(Server, Client, 0);

	const FPeerId ClientId = Client.GetAssignedPeer();
	const std::uint8_t Payload[1] = {SendToPayloadByte};
	// Act
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Server.SendTo(ClientId, SendToChannel, TSpan<const std::uint8_t>(Payload, sizeof(Payload))),
		"SendTo queues to the addressed peer");
	Server.PumpSend(0);
	Client.PumpReceive(0);

	// Assert
	MW_EXPECT_EQ(Test, AdmittedPeerCount, ClientCapture.Count, "The addressed peer receives exactly one message");
	MW_EXPECT_EQ(Test, SendToChannel, ClientCapture.Channel, "The message arrives on the requested channel");
	MW_EXPECT_EQ(Test, SendToPayloadByte, ClientCapture.FirstByte, "The message carries the sent payload");
}

/**
 * Motivation: Configure and start a listen server, then SendTo the local peer.
 * Responsibilities: The local-peer message dispatches synchronously to the handler with no device pump, attributed to
 *   the local peer id with its channel and.
 */
MW_TEST_CASE(TransportHostListenServerDispatchesToLocalPeerWithoutDevice)
{
	// Arrange
	THostLoopback<1, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	TTransportHost<2, HostPacketBytes> Server(Loopback.Port(ServerPortIndex));
	(void)Server.Configure(ENetworkMode::ListenServer, MakeHostConfig(MatchedProtocolVersion));
	(void)Server.Start(0);

	FHandlerCapture LocalCapture{};
	(void)InstallCapture(Server, LocalCapture);

	const std::uint8_t Payload[1] = {LocalDispatchPayloadByte};
	// Act / Assert
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Server.SendTo(Server.GetLocalPeer(), LocalDispatchChannel, TSpan<const std::uint8_t>(Payload, sizeof(Payload))),
		"A local-peer send reports success");
	MW_EXPECT_EQ(Test, AdmittedPeerCount, LocalCapture.Count, "The local peer receives exactly one message, synchronously without a pump");
	MW_EXPECT_EQ(Test, Server.GetLocalPeer(), LocalCapture.From, "The message is attributed to the local peer id");
	MW_EXPECT_EQ(Test, LocalDispatchChannel, LocalCapture.Channel, "The local message keeps its channel");
	MW_EXPECT_EQ(Test, LocalDispatchPayloadByte, LocalCapture.FirstByte, "The local message keeps its payload");
}

/**
 * Motivation: Complete a handshake, then leave the server silent while advancing the client past its timeout
 *   window.
 * Responsibilities: The client returns to Connecting when the server times out.
 */
MW_TEST_CASE(TransportHostClientReturnsToConnectingOnServerTimeout)
{
	// Arrange
	THostLoopback<2, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	TTransportHost<2, HostPacketBytes> Server(Loopback.Port(ServerPortIndex));
	TTransportHost<1, HostPacketBytes> Client(Loopback.Port(FirstClientPortIndex));
	(void)Server.Configure(ENetworkMode::DedicatedServer, MakeHostConfig(MatchedProtocolVersion));
	(void)Client.Configure(ENetworkMode::Client, MakeClientConfig(MatchedProtocolVersion, ServerPortIndex));
	(void)Server.Start(0);
	(void)Client.Start(0);
	RunHandshake(Server, Client, 0);
	MW_EXPECT_EQ(Test, ETransportHostState::Connected, Client.GetState(), "Client is connected after the handshake");

	// Act - the server goes silent; advance the client past its timeout window.
	Client.PumpReceive(ClientServerTimeoutMs);
	// Assert
	MW_EXPECT_EQ(Test, ETransportHostState::Connecting, Client.GetState(), "Client re-enters Connecting when the server times out");
}

/**
 * Motivation: Admit a client with a first Hello, then send a second Hello from the same address without consuming
 *   the Welcome.
 * Responsibilities: The repeated Hello re-welcomes the client and reuses the same slot instead of allocating another.
 */
MW_TEST_CASE(TransportHostRepeatedHelloReusesTheSameSlot)
{
	// Arrange
	THostLoopback<2, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	TTransportHost<2, HostPacketBytes> Server(Loopback.Port(ServerPortIndex));
	TTransportHost<1, HostPacketBytes> Client(Loopback.Port(FirstClientPortIndex));
	(void)Server.Configure(ENetworkMode::DedicatedServer, MakeHostConfig(MatchedProtocolVersion));
	(void)Client.Configure(ENetworkMode::Client, MakeClientConfig(MatchedProtocolVersion, ServerPortIndex));
	(void)Server.Start(0);
	(void)Client.Start(0);

	// Act - first Hello admits the client; the client never consumes the Welcome, so it re-greets.
	Client.PumpSend(0);
	Server.PumpReceive(0);
	// Assert
	MW_EXPECT_EQ(Test, AdmittedPeerCount, Server.ActivePeerCount(), "First Hello admits the client");

	// Act
	Client.PumpSend(RepeatedHelloResendMs);
	Server.PumpReceive(RepeatedHelloResendMs);
	// Assert
	MW_EXPECT_EQ(Test, AdmittedPeerCount, Server.ActivePeerCount(), "A repeated Hello reuses the slot instead of allocating another");
}

/**
 * Motivation: Complete a handshake, stop the client to send a Bye, then have the server process it.
 * Responsibilities: The Bye frees the peer's slot.
 */
MW_TEST_CASE(TransportHostByeEvictsPeer)
{
	// Arrange
	THostLoopback<2, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	TTransportHost<2, HostPacketBytes> Server(Loopback.Port(ServerPortIndex));
	TTransportHost<1, HostPacketBytes> Client(Loopback.Port(FirstClientPortIndex));
	(void)Server.Configure(ENetworkMode::DedicatedServer, MakeHostConfig(MatchedProtocolVersion));
	(void)Client.Configure(ENetworkMode::Client, MakeClientConfig(MatchedProtocolVersion, ServerPortIndex));
	(void)Server.Start(0);
	(void)Client.Start(0);
	RunHandshake(Server, Client, 0);
	MW_EXPECT_EQ(Test, AdmittedPeerCount, Server.ActivePeerCount(), "Peer is active before the Bye");

	// Act - Stop sends a best-effort Bye to the server, then the server processes it.
	Client.Stop();
	Server.PumpReceive(0);
	// Assert
	MW_EXPECT_EQ(Test, EmptyPeerCount, Server.ActivePeerCount(), "Server frees the peer on Bye");
}

/**
 * Motivation: Have a client with a mismatched protocol version send Hello to the server, then pump both
 *   directions.
 * Responsibilities: The server ignores the version-mismatched Hello and the rejected client never leaves Connecting.
 */
MW_TEST_CASE(TransportHostIgnoresHelloWithWrongProtocolVersion)
{
	// Arrange
	THostLoopback<2, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	TTransportHost<2, HostPacketBytes> Server(Loopback.Port(ServerPortIndex));
	TTransportHost<1, HostPacketBytes> Client(Loopback.Port(FirstClientPortIndex));
	(void)Server.Configure(ENetworkMode::DedicatedServer, MakeHostConfig(MatchedProtocolVersion));
	(void)Client.Configure(ENetworkMode::Client, MakeClientConfig(MismatchedProtocolVersion, ServerPortIndex));
	(void)Server.Start(0);
	(void)Client.Start(0);

	// Act / Assert
	Client.PumpSend(0);
	Server.PumpReceive(0);
	MW_EXPECT_EQ(Test, EmptyPeerCount, Server.ActivePeerCount(), "Server ignores a version-mismatched Hello");

	// Act / Assert
	Server.PumpSend(0);
	Client.PumpReceive(0);
	MW_EXPECT_EQ(Test, ETransportHostState::Connecting, Client.GetState(), "The rejected client never leaves Connecting");
}

/**
 * Motivation: Deliver a control frame whose payload byte names an undefined control type to a started server.
 * Responsibilities: The unknown control message is dropped and no peer is admitted.
 */
MW_TEST_CASE(TransportHostDropsUnknownControlMessage)
{
	// Arrange
	THostLoopback<2, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	TTransportHost<2, HostPacketBytes> Server(Loopback.Port(ServerPortIndex));
	(void)Server.Configure(ENetworkMode::DedicatedServer, MakeHostConfig(MatchedProtocolVersion));
	(void)Server.Start(0);

	// Act - a payload byte whose control type (0x09) names no defined control message.
	(void)Loopback.Port(FirstClientPortIndex)
		.TrySend(MakeLoopbackAddress(ServerPortIndex), TSpan<const std::uint8_t>(UnknownControlTypeFrame, sizeof(UnknownControlTypeFrame)));
	Server.PumpReceive(0);

	// Assert
	MW_EXPECT_EQ(Test, EmptyPeerCount, Server.ActivePeerCount(), "An unknown control message admits nobody");
}

/**
 * Motivation: Configure and start a standalone host, then attempt SendTo and Broadcast.
 * Responsibilities: Both sends return Unavailable and the host stays Idle after Start.
 */
MW_TEST_CASE(TransportHostStandaloneReportsUnavailableOnSend)
{
	// Arrange
	THostLoopback<1, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	TTransportHost<2, HostPacketBytes> Host(Loopback.Port(ServerPortIndex));
	(void)Host.Configure(ENetworkMode::Standalone, MakeHostConfig(MatchedProtocolVersion));
	(void)Host.Start(0);

	const std::uint8_t Payload[1] = {StandalonePayloadByte};
	// Act / Assert
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Unavailable,
		Host.SendTo(FPeerId{0, 0}, SendChannel, TSpan<const std::uint8_t>(Payload, sizeof(Payload))),
		"Standalone SendTo is Unavailable");
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Unavailable,
		Host.Broadcast(BroadcastChannel, TSpan<const std::uint8_t>(Payload, sizeof(Payload))),
		"Standalone Broadcast is Unavailable");
	MW_EXPECT_EQ(Test, ETransportHostState::Idle, Host.GetState(), "Standalone stays Idle after Start");
}

/**
 * Motivation: Capture the allocation counter after construction, then run a full handshake plus unicast and
 *   broadcast round trips.
 * Responsibilities: The full client/server session performs no observable heap allocation.
 */
MW_TEST_CASE(TransportHostSessionPerformsNoObservableAllocation)
{
	// Arrange
	THostLoopback<2, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	TTransportHost<2, HostPacketBytes> Server(Loopback.Port(ServerPortIndex));
	TTransportHost<1, HostPacketBytes> Client(Loopback.Port(FirstClientPortIndex));
	(void)Server.Configure(ENetworkMode::DedicatedServer, MakeHostConfig(MatchedProtocolVersion));
	(void)Client.Configure(ENetworkMode::Client, MakeClientConfig(MatchedProtocolVersion, ServerPortIndex));

	FHandlerCapture ClientCapture{};
	(void)InstallCapture(Client, ClientCapture);
	(void)Server.Start(0);
	(void)Client.Start(0);

	// Capture the counter only after every fixed-storage object and handler exists.
	const std::uint32_t AllocationsBefore = GlobalAllocationCount;

	// Act
	RunHandshake(Server, Client, 0);
	const std::uint8_t Payload[3] = {SessionPayloadFirstByte, SessionPayloadSecondByte, SessionPayloadThirdByte};
	(void)Client.SendTo(Client.GetServerPeer(), SendChannel, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
	Client.PumpSend(SessionPumpMs);
	Server.PumpReceive(SessionPumpMs);
	(void)Server.Broadcast(BroadcastChannel, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
	Server.PumpSend(SessionPumpMs);
	Client.PumpReceive(SessionPumpMs);

	const std::uint32_t AllocationsAfter = GlobalAllocationCount;
	// Assert
	MW_EXPECT_EQ(Test, AllocationsBefore, AllocationsAfter, "A full TTransportHost session must not allocate");
}

/**
 * Motivation: Configure and start a dedicated server, then attempt a local SendTo and a Broadcast with no peers.
 * Responsibilities: A dedicated server rejects a send to the local peer as Invalid, succeeds with Broadcast, and
 *   dispatches nothing locally.
 */
MW_TEST_CASE(TransportHostDedicatedServerHasNoLocalDispatch)
{
	// Arrange
	THostLoopback<1, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	TTransportHost<2, HostPacketBytes> Server(Loopback.Port(ServerPortIndex));
	(void)Server.Configure(ENetworkMode::DedicatedServer, MakeHostConfig(MatchedProtocolVersion));
	(void)Server.Start(0);

	FHandlerCapture Capture{};
	(void)InstallCapture(Server, Capture);

	const std::uint8_t Payload[1] = {DedicatedServerLocalPayloadByte};
	// Act / Assert
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Invalid,
		Server.SendTo(Server.GetLocalPeer(), SendChannel, TSpan<const std::uint8_t>(Payload, sizeof(Payload))),
		"A dedicated server rejects a send to the local peer");
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Server.Broadcast(BroadcastChannel, TSpan<const std::uint8_t>(Payload, sizeof(Payload))),
		"Broadcast with no peers succeeds");
	MW_EXPECT_EQ(Test, EmptyPeerCount, Capture.Count, "A dedicated server performs no local dispatch");
}

/**
 * Motivation: Always-ready device that counts receive calls, so a test can prove one pump is bounded. Every
 *   `TryReceive` delivers a minimal empty control frame and reports `Success`, so a `PumpReceive` would
 *   loop forever were it not bounded; `TrySend` is a no-op success.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FFloodDevice final : public ITransportDevice
{
public:
	/**
	 * Motivation: Accepts and discards every send.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	ETransportResult TrySend(const FDeviceAddress&, TSpan<const std::uint8_t>) noexcept override { return ETransportResult::Success; }

	/**
	 * Motivation: Delivers one empty control frame per call and counts the call.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	ETransportResult TryReceive(FDeviceAddress& OutFrom, TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept override
	{
		if (InDestination.Size() < FloodReceivedByteCount)
		{
			return ETransportResult::Unavailable;
		}
		++ReceiveCallCount;
		OutFrom = MakeLoopbackAddress(FloodSenderPortIndex);
		InDestination[0] = EmptyControlFrameByte;
		InDestination[1] = EmptyControlFrameByte;
		InDestination[2] = EmptyControlFrameByte;
		InDestination[3] = EmptyControlFrameByte;
		OutResult.BytesReceived = FloodReceivedByteCount;
		return ETransportResult::Success;
	}

	/**
	 * Motivation: Reports the fixed maximum packet size.
	 * Responsibilities: Return the stored value and touch nothing else.
	 */
	std::size_t MaxPacketBytes() const noexcept override { return LoopbackPacketBytes; }

	/**
	 * Motivation: Makes this synchronous flood fake's lack of staged bytes explicit at the required pre-advance turn.
	 * Responsibilities:
	 * Do no work because TrySend succeeds synchronously and stages nothing.
	 */
	void PreAdvance(TimePointMilliseconds) noexcept override {}

	/** Motivation: Counts how many receives one or more pumps have requested. */
	std::size_t ReceiveCallCount{0};
};

/**
 * Motivation: Pump a started server whose device never runs dry of empty control frames.
 * Responsibilities: One pump is bounded to MaxPeers plus four receives, so a flood cannot starve the frame.
 */
MW_TEST_CASE(TransportHostPumpReceiveIsBoundedUnderFlood)
{
	// Arrange
	FFloodDevice Device;
	TTransportHost<3, HostPacketBytes> Server(Device);
	(void)Server.Configure(ENetworkMode::DedicatedServer, MakeHostConfig(MatchedProtocolVersion));
	(void)Server.Start(0);

	// Act
	Server.PumpReceive(0);

	// Assert - MaxPeers (3) + 4 = 7 receives is the per-pump bound, even though the device never runs dry.
	MW_EXPECT_EQ(Test, BoundedPumpReceiveCount, Device.ReceiveCallCount, "One pump is bounded to MaxPeers + 4 receives");
}

} // namespace
