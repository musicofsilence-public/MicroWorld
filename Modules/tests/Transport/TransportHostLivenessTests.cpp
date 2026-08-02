#include "TestSupport.h"
#include "TransportHostTestHelpers.h"

namespace
{

using namespace ::MicroWorld::Tests;

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

} // namespace
