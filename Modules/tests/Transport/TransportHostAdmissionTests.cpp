#include "CoreAllocationCounters.h"
#include "TestSupport.h"
#include "TransportHostTestHelpers.h"

namespace
{

using namespace ::MicroWorld::Tests;

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

} // namespace
