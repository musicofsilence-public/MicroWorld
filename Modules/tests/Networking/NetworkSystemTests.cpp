#include "TestSupport.h"

#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Messaging/ChannelInformation.h>
#include <MicroWorld/Messaging/MessagingSystem.h>
#include <MicroWorld/Messaging/TypedMessageCodec.h>
#include <MicroWorld/Networking/ConnectAccepted.h>
#include <MicroWorld/Networking/ConnectRejected.h>
#include <MicroWorld/Networking/ConnectRequest.h>
#include <MicroWorld/Networking/Heartbeat.h>
#include <MicroWorld/Networking/NetworkSystem.h>
#include <MicroWorld/Networking/RoutedMessage.h>
#include <MicroWorld/Transport/HostLoopback.h>

#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::Core::MakeLoopbackAddress;
using MicroWorld::Messaging::EMessagingResult;
using MicroWorld::Messaging::FMessage;
using MicroWorld::Messaging::FMessagingLinkId;
using MicroWorld::Messaging::FMessagingRoute;
using MicroWorld::Messaging::FMessagingSystem;
using MicroWorld::Messaging::FNameId;
using MicroWorld::Networking::EConnectionRejectReason;
using MicroWorld::Networking::EConnectionState;
using MicroWorld::Networking::ENetworkResult;
using MicroWorld::Networking::ENetworkRole;
using MicroWorld::Networking::FConnectAccepted;
using MicroWorld::Networking::FConnectRejected;
using MicroWorld::Networking::FConnectRequest;
using MicroWorld::Networking::FHeartbeat;
using MicroWorld::Networking::FNetworkSystem;
using MicroWorld::Networking::FNetworkSystemInformation;
using MicroWorld::Networking::FPeerId;
using MicroWorld::Networking::FRoutedMessage;
using MicroWorld::Transport::THostLoopback;

/** Motivation: Identifies the deterministic server loopback mailbox. */
constexpr std::uint8_t ServerPort = 0;
/** Motivation: Identifies the deterministic client loopback mailbox. */
constexpr std::uint8_t ClientPort = 1;
/** Motivation: Leaves room for one complete Messaging frame in each loopback mailbox. */
constexpr std::size_t PacketBytes = 128;
/** Motivation: Lets a handshake exchange its request and acceptance without fixture saturation. */
constexpr std::size_t MailboxDepth = 4;

/** Motivation: Names the local-only application channel exercised through Network's private wire channels. */
constexpr FNameId GameplayChannel{"Gameplay"};
/** Motivation: Names the application message delivered by addressed-send and route-validation tests. */
constexpr FNameId GameplayMessage{"Updated"};

/**
 * Motivation: Connects a client and server through registered Messaging routes without giving Network direct device access.
 * Responsibilities: Advance Messaging receive turns around Network's explicit immediate protocol sends.
 */
void RunHandshake(FMessagingSystem& InServerMessaging, FMessagingSystem& InClientMessaging, FNetworkSystem& InClient) noexcept
{
	InClientMessaging.PreAdvance(0);
	InServerMessaging.PreAdvance(0);
	InClientMessaging.PreAdvance(0);
	InClient.PreAdvance(0);
}

/**
 * Motivation: Publishes a valid application message without depending on a default Messaging route.
 * Responsibilities: Return a complete, bounded message suitable for Network send APIs.
 */
FMessage MakeGameplayMessage() noexcept
{
	const std::uint8_t Payload[]{9};
	FMessage Message;
	Message.SetMessageNameId(GameplayMessage);
	Message.SetPayload(MicroWorld::Core::TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
	return Message;
}

/**
 * Motivation: Brings one client/server pair to the public Connected state for session behavior tests.
 * Responsibilities: Initialize private channels, request admission, and pump the exact two Messaging receives.
 */
FPeerId ConnectPair(
	FMessagingSystem& InServerMessaging,
	FMessagingSystem& InClientMessaging,
	FNetworkSystem& InServer,
	FNetworkSystem& InClient,
	const FMessagingRoute& InServerRoute) noexcept
{
	if (InServer.Initialize() != ENetworkResult::Success || InClient.Initialize() != ENetworkResult::Success
		|| InClient.ConnectToServer(InServerRoute, 0) != ENetworkResult::Success)
	{
		return {};
	}
	RunHandshake(InServerMessaging, InClientMessaging, InClient);
	return InClient.GetServerPeer();
}

/**
 * Motivation: Verifies the planned Networking boundary admits a client through Messaging and exposes only the logical server handle.
 * Responsibilities: Assert client state and peer validity after an exact request/accept exchange.
 */
MW_TEST_CASE(NetworkSystemClientConnectsThroughMessagingRoute)
{
	// Arrange
	THostLoopback<2, MailboxDepth, PacketBytes> Loopback;
	FMessagingSystem ServerMessaging;
	FMessagingSystem ClientMessaging;
	FMessagingLinkId ServerLink{};
	FMessagingLinkId ClientLink{};
	FNetworkSystem Server(ServerMessaging, {ENetworkRole::Server});
	FNetworkSystem Client(ClientMessaging, {ENetworkRole::Client});
	(void)ServerMessaging.RegisterLink(Loopback.Port(ServerPort), ServerLink);
	(void)ClientMessaging.RegisterLink(Loopback.Port(ClientPort), ClientLink);

	// Act
	const ENetworkResult ServerInitialize = Server.Initialize();
	const ENetworkResult ClientInitialize = Client.Initialize();
	const ENetworkResult ConnectResult = Client.ConnectToServer({ClientLink, MakeLoopbackAddress(ServerPort)}, 0);
	RunHandshake(ServerMessaging, ClientMessaging, Client);

	// Assert
	MW_EXPECT_EQ(Test, ENetworkResult::Success, ServerInitialize, "The server should reserve its private Messaging channels");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, ClientInitialize, "The client should reserve its private Messaging channels");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, ConnectResult, "A registered route should accept a client connect request");
	MW_EXPECT_EQ(Test, EConnectionState::Connected, Client.GetConnectionState(), "The client should become connected after acceptance");
	MW_EXPECT_TRUE(Test, Client.GetServerPeer().IsValid(), "A connected client should expose its assigned logical server peer");
}

/**
 * Motivation: Keeps server-only fan-out from being accidentally available to clients.
 * Responsibilities: Reject the wrong-role operation before requiring a route or connected peer.
 */
MW_TEST_CASE(NetworkSystemRejectsServerBroadcastFromClientRole)
{
	// Arrange
	FMessagingSystem Messaging;
	FNetworkSystem Client(Messaging, {ENetworkRole::Client});
	MicroWorld::Messaging::FMessage Message;

	// Act
	const ENetworkResult Result = Client.Broadcast("Gameplay", Message);

	// Assert
	MW_EXPECT_EQ(Test, ENetworkResult::WrongRole, Result, "A client must not broadcast to peer sessions");
}

/**
 * Motivation: Keeps role-only calls and disconnected client sends from accidentally reaching Messaging.
 * Responsibilities: Reject each command before a route, application channel, or transport device is required.
 */
MW_TEST_CASE(NetworkSystemRejectsWrongRoleAndDisconnectedSendCommands)
{
	// Arrange
	FMessagingSystem Messaging;
	FNetworkSystem Server(Messaging, {ENetworkRole::Server});
	FNetworkSystem Client(Messaging, {ENetworkRole::Client});
	const FMessage Message = MakeGameplayMessage();

	// Act
	const ENetworkResult ServerConnectResult = Server.ConnectToServer({}, 0);
	const ENetworkResult ServerClientSendResult = Server.SendToServer(GameplayChannel, Message);
	const ENetworkResult ClientServerSendResult = Client.SendToServer(GameplayChannel, Message);
	const ENetworkResult ClientPeerDisconnectResult = Client.DisconnectPeer({0, 1});

	// Assert
	MW_EXPECT_EQ(Test, ENetworkResult::WrongRole, ServerConnectResult, "Only a client may begin a server connection attempt");
	MW_EXPECT_EQ(Test, ENetworkResult::WrongRole, ServerClientSendResult, "Only a client may send to its sole server session");
	MW_EXPECT_EQ(Test, ENetworkResult::NotConnected, ClientServerSendResult, "A client cannot send application data before admission");
	MW_EXPECT_EQ(Test, ENetworkResult::WrongRole, ClientPeerDisconnectResult, "Only a server may retire a remote peer");
}

/**
 * Motivation: Covers server fan-out's empty and one-peer public result contracts.
 * Responsibilities: Return NotConnected before admission, then send the application message once to the admitted remote subscriber.
 */
MW_TEST_CASE(NetworkSystemBroadcastReportsNoPeerThenDeliversToAnAdmittedPeer)
{
	// Arrange
	THostLoopback<2, MailboxDepth, PacketBytes> Loopback;
	FMessagingSystem ServerMessaging;
	FMessagingSystem ClientMessaging;
	FMessagingLinkId ServerLink{};
	FMessagingLinkId ClientLink{};
	FNetworkSystem Server(ServerMessaging, {ENetworkRole::Server});
	FNetworkSystem Client(ClientMessaging, {ENetworkRole::Client});
	(void)ServerMessaging.RegisterLink(Loopback.Port(ServerPort), ServerLink);
	(void)ClientMessaging.RegisterLink(Loopback.Port(ClientPort), ClientLink);
	const FMessage Message = MakeGameplayMessage();
	const ENetworkResult ServerInitialize = Server.Initialize();
	const ENetworkResult ClientInitialize = Client.Initialize();
	(void)ServerMessaging.CreateChannel({GameplayChannel, false, nullptr, {}});
	(void)ClientMessaging.CreateChannel({GameplayChannel, false, nullptr, {}});
	std::size_t ClientDeliveryCount = 0;
	FMessagingSystem::FSubscriberDelegate Subscriber;
	(void)Subscriber.Bind([&ClientDeliveryCount](const FMessage&) noexcept { ++ClientDeliveryCount; });
	(void)ClientMessaging.SubscribeToChannel(GameplayChannel, std::move(Subscriber));
	const ENetworkResult NoPeerResult = Server.Broadcast(GameplayChannel, Message);
	const ENetworkResult ConnectResult = Client.ConnectToServer({ClientLink, MakeLoopbackAddress(ServerPort)}, 0);
	RunHandshake(ServerMessaging, ClientMessaging, Client);

	// Act
	const ENetworkResult BroadcastResult = Server.Broadcast(GameplayChannel, Message);
	ClientMessaging.PreAdvance(0);

	// Assert
	MW_EXPECT_EQ(Test, ENetworkResult::Success, ServerInitialize, "The server must initialize before fan-out");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, ClientInitialize, "The client must initialize before admission");
	MW_EXPECT_EQ(Test, ENetworkResult::NotConnected, NoPeerResult, "A server with no eligible peer must not report broadcast success");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, ConnectResult, "The client should issue the admission request");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, BroadcastResult, "A server with one eligible peer should accept fan-out");
	MW_EXPECT_EQ(Test, std::size_t{1}, ClientDeliveryCount, "Fan-out should produce one remote delivery for one admitted peer");
}

/**
 * Motivation: Makes failed private-channel reservation visibly transactional at the public Messaging boundary.
 * Responsibilities: Verify a full channel table leaves no partial Network channel that blocks a later initialization.
 */
MW_TEST_CASE(NetworkSystemInitializationUnwindsWhenTheSecondPrivateChannelCannotBeCreated)
{
	// Arrange
	FMessagingSystem Messaging;
	const FNameId First{"One"};
	const FNameId Second{"Two"};
	const FNameId Third{"Three"};
	(void)Messaging.CreateChannel({First, false, nullptr, {}});
	(void)Messaging.CreateChannel({Second, false, nullptr, {}});
	(void)Messaging.CreateChannel({Third, false, nullptr, {}});
	FNetworkSystem Network(Messaging, {ENetworkRole::Server});

	// Act
	const ENetworkResult FullResult = Network.Initialize();
	(void)Messaging.DestroyChannel(Third);
	const ENetworkResult RetryResult = Network.Initialize();

	// Assert
	MW_EXPECT_EQ(Test, ENetworkResult::Full, FullResult, "A channel table with one free slot cannot reserve both Network channels");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, RetryResult, "The failed reservation must release its first private channel before a retry");
}

/**
 * Motivation: Guards session liveness and client-visible explicit close without exposing a transport endpoint.
 * Responsibilities: Retire the client peer on timeout and on a valid server disconnect.
 */
MW_TEST_CASE(NetworkSystemRetiresClientSessionsOnTimeoutAndExplicitServerDisconnect)
{
	// Arrange
	THostLoopback<2, MailboxDepth, PacketBytes> Loopback;
	FMessagingSystem ServerMessaging;
	FMessagingSystem ClientMessaging;
	FMessagingLinkId ServerLink{};
	FMessagingLinkId ClientLink{};
	FNetworkSystem Server(ServerMessaging, {ENetworkRole::Server});
	FNetworkSystem Client(ClientMessaging, {ENetworkRole::Client});
	(void)ServerMessaging.RegisterLink(Loopback.Port(ServerPort), ServerLink);
	(void)ClientMessaging.RegisterLink(Loopback.Port(ClientPort), ClientLink);
	const FPeerId ConnectedPeer = ConnectPair(ServerMessaging, ClientMessaging, Server, Client, {ClientLink, MakeLoopbackAddress(ServerPort)});

	// Act
	Client.PreAdvance(5001);
	const EConnectionState TimedOutState = Client.GetConnectionState();
	const FPeerId ReconnectedPeer = ConnectPair(ServerMessaging, ClientMessaging, Server, Client, {ClientLink, MakeLoopbackAddress(ServerPort)});
	const ENetworkResult DisconnectResult = Server.DisconnectPeer(ReconnectedPeer);
	ClientMessaging.PreAdvance(0);

	// Assert
	MW_EXPECT_TRUE(Test, ConnectedPeer.IsValid(), "The fixture must establish the initial client session");
	MW_EXPECT_EQ(Test, EConnectionState::Disconnected, TimedOutState, "Silence beyond the configured deadline must retire the client session");
	MW_EXPECT_TRUE(Test, ReconnectedPeer.IsValid(), "A later connection attempt should restore one client session");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, DisconnectResult, "The server should explicitly disconnect its current peer");
	MW_EXPECT_EQ(Test, EConnectionState::Disconnected, Client.GetConnectionState(), "A matching disconnect packet must retire the client session");
}

/**
 * Motivation: Preserves idempotent retries while ensuring a newer attempt invalidates the prior logical peer.
 * Responsibilities: Keep repeated requests on one attempt connected and reject the stale peer after a replacement attempt.
 */
MW_TEST_CASE(NetworkSystemKeepsRepeatedAttemptsIdempotentAndInvalidatesThePreviousPeerOnReconnect)
{
	// Arrange
	THostLoopback<2, MailboxDepth, PacketBytes> Loopback;
	FMessagingSystem ServerMessaging;
	FMessagingSystem ClientMessaging;
	FMessagingLinkId ServerLink{};
	FMessagingLinkId ClientLink{};
	FNetworkSystem Server(ServerMessaging, {ENetworkRole::Server});
	FNetworkSystem Client(ClientMessaging, {ENetworkRole::Client});
	(void)ServerMessaging.RegisterLink(Loopback.Port(ServerPort), ServerLink);
	(void)ClientMessaging.RegisterLink(Loopback.Port(ClientPort), ClientLink);
	const FMessagingRoute ServerRoute{ClientLink, MakeLoopbackAddress(ServerPort)};
	const ENetworkResult ServerInitialize = Server.Initialize();
	const ENetworkResult ClientInitialize = Client.Initialize();
	const ENetworkResult FirstConnectResult = Client.ConnectToServer(ServerRoute, 0);
	Client.PreAdvance(1000);
	ServerMessaging.PreAdvance(1000);
	ClientMessaging.PreAdvance(1000);
	const FPeerId FirstPeer = Client.GetServerPeer();
	const ENetworkResult ReconnectResult = Client.ConnectToServer(ServerRoute, 1001);
	ServerMessaging.PreAdvance(1001);
	ClientMessaging.PreAdvance(1001);
	const FPeerId SecondPeer = Client.GetServerPeer();
	const FMessage Message = MakeGameplayMessage();

	// Act
	const ENetworkResult StaleSendResult = Server.SendTo(FirstPeer, GameplayChannel, Message);

	// Assert
	MW_EXPECT_EQ(Test, ENetworkResult::Success, ServerInitialize, "The server must initialize before handling repeated connect requests");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, ClientInitialize, "The client must initialize before sending connect requests");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, FirstConnectResult, "The initial client attempt should be sent");
	MW_EXPECT_TRUE(Test, FirstPeer.IsValid(), "Repeated requests for one attempt must still produce one public peer id");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, ReconnectResult, "A newer client attempt should be sent through the same route");
	MW_EXPECT_TRUE(Test, SecondPeer.IsValid(), "The newer attempt must be accepted");
	MW_EXPECT_TRUE(Test, FirstPeer != SecondPeer, "A replacement attempt must not reuse the previous peer generation");
	MW_EXPECT_EQ(Test, ENetworkResult::NotConnected, StaleSendResult, "The server must reject an addressed send through the stale peer id");
}

/**
 * Motivation: Proves Network sends application data remotely and resolves the logical sender after local delivery.
 * Responsibilities: Verify an addressed server send and a reliable client reply each reach only the intended remote subscriber.
 */
MW_TEST_CASE(NetworkSystemRoutesAddressedBestEffortAndReliableApplicationMessages)
{
	// Arrange
	THostLoopback<2, MailboxDepth, PacketBytes> Loopback;
	FMessagingSystem ServerMessaging;
	FMessagingSystem ClientMessaging;
	FMessagingLinkId ServerLink{};
	FMessagingLinkId ClientLink{};
	FNetworkSystem Server(ServerMessaging, {ENetworkRole::Server});
	FNetworkSystem Client(ClientMessaging, {ENetworkRole::Client});
	(void)ServerMessaging.RegisterLink(Loopback.Port(ServerPort), ServerLink);
	(void)ClientMessaging.RegisterLink(Loopback.Port(ClientPort), ClientLink);
	const FPeerId Peer = ConnectPair(ServerMessaging, ClientMessaging, Server, Client, {ClientLink, MakeLoopbackAddress(ServerPort)});
	(void)ServerMessaging.CreateChannel({GameplayChannel, false, nullptr, {}});
	(void)ClientMessaging.CreateChannel({GameplayChannel, true, nullptr, {}});
	std::size_t ServerDeliveryCount = 0;
	std::size_t ClientDeliveryCount = 0;
	FPeerId ServerResolvedSender{};
	FPeerId ClientResolvedSender{};
	FMessagingSystem::FSubscriberDelegate ServerSubscriber;
	FMessagingSystem::FSubscriberDelegate ClientSubscriber;
	(void)ServerSubscriber.Bind(
		[&](const FMessage& InMessage) noexcept
		{
			++ServerDeliveryCount;
			ServerResolvedSender = Server.ResolveSenderPeer(InMessage);
		});
	(void)ClientSubscriber.Bind(
		[&](const FMessage& InMessage) noexcept
		{
			++ClientDeliveryCount;
			ClientResolvedSender = Client.ResolveSenderPeer(InMessage);
		});
	(void)ServerMessaging.SubscribeToChannel(GameplayChannel, std::move(ServerSubscriber));
	(void)ClientMessaging.SubscribeToChannel(GameplayChannel, std::move(ClientSubscriber));
	const FMessage Message = MakeGameplayMessage();

	// Act
	const ENetworkResult AddressedResult = Server.SendTo(Peer, GameplayChannel, Message);
	ClientMessaging.PreAdvance(0);
	const ENetworkResult ReliableResult = Client.SendToServer(GameplayChannel, Message);
	ServerMessaging.PreAdvance(0);
	ClientMessaging.PreAdvance(0);
	ServerMessaging.PreAdvance(0);

	// Assert
	MW_EXPECT_EQ(Test, ENetworkResult::Success, AddressedResult, "A server should send one addressed application message to its live peer");
	MW_EXPECT_EQ(Test, std::size_t{1}, ClientDeliveryCount, "The addressed message must be delivered remotely without a local server echo");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, ReliableResult, "A reliable application channel should select the reliable private wire channel");
	MW_EXPECT_EQ(Test, std::size_t{1}, ServerDeliveryCount, "The reliable client reply must reach the server subscriber once");
	MW_EXPECT_EQ(Test, Peer, ClientResolvedSender, "The addressed server message must retain the validated client-side server peer");
	MW_EXPECT_EQ(Test, Peer, ServerResolvedSender, "The reliable client reply must retain the validated server-side client peer");
}

/**
 * Motivation: Rejects protocol traffic whose route or logical peer cannot name the live session.
 * Responsibilities: Keep a valid server peer alive while wrong-peer routed data and unknown-route heartbeat traffic are ignored.
 */
MW_TEST_CASE(NetworkSystemDropsRouteMismatchedAndUnknownSenderTraffic)
{
	// Arrange
	THostLoopback<3, MailboxDepth, PacketBytes> Loopback;
	FMessagingSystem ServerMessaging;
	FMessagingSystem ClientMessaging;
	FMessagingSystem AttackerMessaging;
	FMessagingLinkId ServerLink{};
	FMessagingLinkId ClientLink{};
	FMessagingLinkId AttackerLink{};
	FNetworkSystem Server(ServerMessaging, {ENetworkRole::Server});
	FNetworkSystem Client(ClientMessaging, {ENetworkRole::Client});
	(void)ServerMessaging.RegisterLink(Loopback.Port(ServerPort), ServerLink);
	(void)ClientMessaging.RegisterLink(Loopback.Port(ClientPort), ClientLink);
	(void)AttackerMessaging.RegisterLink(Loopback.Port(2), AttackerLink);
	const FPeerId Peer = ConnectPair(ServerMessaging, ClientMessaging, Server, Client, {ClientLink, MakeLoopbackAddress(ServerPort)});
	(void)ServerMessaging.CreateChannel({GameplayChannel, false, nullptr, {}});
	std::size_t ServerDeliveryCount = 0;
	FMessagingSystem::FSubscriberDelegate Subscriber;
	(void)Subscriber.Bind([&](const FMessage&) noexcept { ++ServerDeliveryCount; });
	(void)ServerMessaging.SubscribeToChannel(GameplayChannel, std::move(Subscriber));
	(void)AttackerMessaging.CreateChannel({FNetworkSystem::BestEffortWireChannelNameId, false, nullptr, {}});
	FRoutedMessage WrongPeerMessage{};
	WrongPeerMessage.Peer = {Peer.Index, Peer.Generation + 1};
	WrongPeerMessage.ChannelNameId = GameplayChannel;
	WrongPeerMessage.MessageNameId = GameplayMessage;

	// Act
	const EMessagingResult WrongPeerResult = ClientMessaging.SendTypedMessageToRemoteChannel(
		WrongPeerMessage, FNetworkSystem::BestEffortWireChannelNameId, {ClientLink, MakeLoopbackAddress(ServerPort)});
	const EMessagingResult UnknownHeartbeatResult = AttackerMessaging.SendTypedMessageToRemoteChannel(
		FHeartbeat{Peer, 1}, FNetworkSystem::BestEffortWireChannelNameId, {AttackerLink, MakeLoopbackAddress(ServerPort)});
	ServerMessaging.PreAdvance(0);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, WrongPeerResult, "The fixture should inject a syntactically valid wrong-peer routed frame");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, UnknownHeartbeatResult, "The fixture should inject a syntactically valid unknown-route heartbeat");
	MW_EXPECT_EQ(Test, std::size_t{0}, ServerDeliveryCount, "Route or peer mismatches must not reach the application channel");
	MW_EXPECT_EQ(
		Test, ENetworkResult::Success, Server.SendTo(Peer, GameplayChannel, MakeGameplayMessage()), "Ignored traffic must not retire the valid peer");
}

/**
 * Motivation: Proves server admission stops at the fixed peer registry rather than accepting an unbounded fifth session.
 * Responsibilities: Admit four distinct client routes, then return the documented full-registry rejection to the fifth client.
 */
MW_TEST_CASE(NetworkSystemRejectsFifthClientWhenAllPeerSlotsAreAdmitted)
{
	// Arrange
	THostLoopback<6, MailboxDepth, PacketBytes> Loopback;
	FMessagingSystem ServerMessaging;
	FMessagingSystem FirstClientMessaging;
	FMessagingSystem SecondClientMessaging;
	FMessagingSystem ThirdClientMessaging;
	FMessagingSystem FourthClientMessaging;
	FMessagingSystem FifthClientMessaging;
	FMessagingLinkId ServerLink{};
	FMessagingLinkId FirstClientLink{};
	FMessagingLinkId SecondClientLink{};
	FMessagingLinkId ThirdClientLink{};
	FMessagingLinkId FourthClientLink{};
	FMessagingLinkId FifthClientLink{};
	FNetworkSystem Server(ServerMessaging, {ENetworkRole::Server});
	FNetworkSystem FirstClient(FirstClientMessaging, {ENetworkRole::Client});
	FNetworkSystem SecondClient(SecondClientMessaging, {ENetworkRole::Client});
	FNetworkSystem ThirdClient(ThirdClientMessaging, {ENetworkRole::Client});
	FNetworkSystem FourthClient(FourthClientMessaging, {ENetworkRole::Client});
	FNetworkSystem FifthClient(FifthClientMessaging, {ENetworkRole::Client});
	(void)ServerMessaging.RegisterLink(Loopback.Port(ServerPort), ServerLink);
	(void)FirstClientMessaging.RegisterLink(Loopback.Port(1), FirstClientLink);
	(void)SecondClientMessaging.RegisterLink(Loopback.Port(2), SecondClientLink);
	(void)ThirdClientMessaging.RegisterLink(Loopback.Port(3), ThirdClientLink);
	(void)FourthClientMessaging.RegisterLink(Loopback.Port(4), FourthClientLink);
	(void)FifthClientMessaging.RegisterLink(Loopback.Port(5), FifthClientLink);
	const ENetworkResult ServerInitialize = Server.Initialize();
	const ENetworkResult FirstInitialize = FirstClient.Initialize();
	const ENetworkResult SecondInitialize = SecondClient.Initialize();
	const ENetworkResult ThirdInitialize = ThirdClient.Initialize();
	const ENetworkResult FourthInitialize = FourthClient.Initialize();
	const ENetworkResult FifthInitialize = FifthClient.Initialize();

	// Act
	const ENetworkResult FirstConnect = FirstClient.ConnectToServer({FirstClientLink, MakeLoopbackAddress(ServerPort)}, 0);
	ServerMessaging.PreAdvance(0);
	FirstClientMessaging.PreAdvance(0);
	const ENetworkResult SecondConnect = SecondClient.ConnectToServer({SecondClientLink, MakeLoopbackAddress(ServerPort)}, 0);
	ServerMessaging.PreAdvance(0);
	SecondClientMessaging.PreAdvance(0);
	const ENetworkResult ThirdConnect = ThirdClient.ConnectToServer({ThirdClientLink, MakeLoopbackAddress(ServerPort)}, 0);
	ServerMessaging.PreAdvance(0);
	ThirdClientMessaging.PreAdvance(0);
	const ENetworkResult FourthConnect = FourthClient.ConnectToServer({FourthClientLink, MakeLoopbackAddress(ServerPort)}, 0);
	ServerMessaging.PreAdvance(0);
	FourthClientMessaging.PreAdvance(0);
	const ENetworkResult FifthConnect = FifthClient.ConnectToServer({FifthClientLink, MakeLoopbackAddress(ServerPort)}, 0);
	ServerMessaging.PreAdvance(0);
	FifthClientMessaging.PreAdvance(0);

	// Assert
	MW_EXPECT_EQ(Test, ENetworkResult::Success, ServerInitialize, "The server must reserve its protocol channels");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, FirstInitialize, "The first client must initialize");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, SecondInitialize, "The second client must initialize");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, ThirdInitialize, "The third client must initialize");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, FourthInitialize, "The fourth client must initialize");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, FifthInitialize, "The fifth client must initialize");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, FirstConnect, "The first client should request admission");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, SecondConnect, "The second client should request admission");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, ThirdConnect, "The third client should request admission");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, FourthConnect, "The fourth client should request admission");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, FifthConnect, "The fifth client should receive a protocol response");
	MW_EXPECT_EQ(Test, EConnectionState::Connected, FirstClient.GetConnectionState(), "The first client should occupy one server slot");
	MW_EXPECT_EQ(Test, EConnectionState::Connected, SecondClient.GetConnectionState(), "The second client should occupy one server slot");
	MW_EXPECT_EQ(Test, EConnectionState::Connected, ThirdClient.GetConnectionState(), "The third client should occupy one server slot");
	MW_EXPECT_EQ(Test, EConnectionState::Connected, FourthClient.GetConnectionState(), "The fourth client should occupy one server slot");
	MW_EXPECT_EQ(
		Test,
		EConnectionState::Connecting,
		FifthClient.GetConnectionState(),
		"A full rejection must preserve the initial attempt so the client can retry on its heartbeat");
}

/**
 * Motivation: Applies the product's one-route admission policy without reducing Network's reusable four-slot storage.
 * Responsibilities: Admit the configured first route and keep a distinct rejected route connecting for its heartbeat retry.
 */
MW_TEST_CASE(NetworkSystemRejectsSecondDistinctRouteAtConfiguredAdmissionLimit)
{
	// Arrange
	THostLoopback<3, MailboxDepth, PacketBytes> Loopback;
	FMessagingSystem ServerMessaging;
	FMessagingSystem FirstClientMessaging;
	FMessagingSystem SecondClientMessaging;
	FMessagingLinkId ServerLink{};
	FMessagingLinkId FirstClientLink{};
	FMessagingLinkId SecondClientLink{};
	FNetworkSystemInformation ServerInformation{ENetworkRole::Server};
	ServerInformation.MaximumAdmittedServerPeers = 1;
	ServerInformation.HeartbeatIntervalMilliseconds = 10;
	ServerInformation.PeerTimeoutMilliseconds = 100;
	FNetworkSystemInformation ClientInformation{ENetworkRole::Client};
	ClientInformation.HeartbeatIntervalMilliseconds = ServerInformation.HeartbeatIntervalMilliseconds;
	ClientInformation.PeerTimeoutMilliseconds = ServerInformation.PeerTimeoutMilliseconds;
	FNetworkSystem Server(ServerMessaging, ServerInformation);
	FNetworkSystem FirstClient(FirstClientMessaging, ClientInformation);
	FNetworkSystem SecondClient(SecondClientMessaging, ClientInformation);
	(void)ServerMessaging.RegisterLink(Loopback.Port(ServerPort), ServerLink);
	(void)FirstClientMessaging.RegisterLink(Loopback.Port(1), FirstClientLink);
	(void)SecondClientMessaging.RegisterLink(Loopback.Port(2), SecondClientLink);
	const ENetworkResult ServerInitialize = Server.Initialize();
	const ENetworkResult FirstInitialize = FirstClient.Initialize();
	const ENetworkResult SecondInitialize = SecondClient.Initialize();
	std::size_t PeerConnectedCount = 0;
	MicroWorld::Core::TDelegate<void(FPeerId), FNetworkSystem::EventCallableBytes> PeerConnectedObserver;
	const MicroWorld::Core::EDelegateResult BindPeerConnectedObserverResult =
		PeerConnectedObserver.Bind([&PeerConnectedCount](const FPeerId) noexcept { ++PeerConnectedCount; });
	MicroWorld::Core::FDelegateHandle PeerConnectedHandle{};
	const MicroWorld::Core::EDelegateResult AddPeerConnectedObserverResult =
		Server.OnPeerConnected().Add(std::move(PeerConnectedObserver), PeerConnectedHandle);
	std::size_t FullRejectionCount = 0;
	EConnectionRejectReason LastRejectionReason = EConnectionRejectReason::ProtocolMismatch;
	FMessagingSystem::FSubscriberDelegate RejectionObserver;
	const MicroWorld::Core::EDelegateResult BindRejectionObserverResult = RejectionObserver.Bind(
		[&FullRejectionCount, &LastRejectionReason](const FMessage& InMessage) noexcept
		{
			FConnectRejected Rejected{};
			if (MicroWorld::Messaging::DecodeTypedMessage(InMessage, Rejected) == EMessagingResult::Success)
			{
				++FullRejectionCount;
				LastRejectionReason = Rejected.Reason;
			}
		});
	const EMessagingResult SubscribeRejectionObserverResult = SecondClientMessaging.SubscribeToChannel(
		FNetworkSystem::BestEffortWireChannelNameId, MicroWorld::Networking::GetMessageNameId(FConnectRejected{}), std::move(RejectionObserver));

	// Act
	const ENetworkResult FirstConnect = FirstClient.ConnectToServer({FirstClientLink, MakeLoopbackAddress(ServerPort)}, 0);
	ServerMessaging.PreAdvance(0);
	FirstClientMessaging.PreAdvance(0);
	const ENetworkResult SecondConnect = SecondClient.ConnectToServer({SecondClientLink, MakeLoopbackAddress(ServerPort)}, 0);
	ServerMessaging.PreAdvance(0);
	SecondClientMessaging.PreAdvance(0);
	SecondClient.PreAdvance(10);
	ServerMessaging.PreAdvance(10);
	SecondClientMessaging.PreAdvance(10);
	const EMessagingResult DuplicateFirstRequest = FirstClientMessaging.SendTypedMessageToRemoteChannel(
		FConnectRequest{ClientInformation.ProtocolVersion, 1},
		FNetworkSystem::BestEffortWireChannelNameId,
		{FirstClientLink, MakeLoopbackAddress(ServerPort)});
	ServerMessaging.PreAdvance(11);
	FirstClientMessaging.PreAdvance(11);

	// Assert
	MW_EXPECT_EQ(Test, ENetworkResult::Success, ServerInitialize, "The limited server should initialize normally");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, FirstInitialize, "The first client should initialize normally");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, SecondInitialize, "The second client should initialize normally");
	MW_EXPECT_EQ(
		Test,
		MicroWorld::Core::EDelegateResult::Success,
		BindPeerConnectedObserverResult,
		"The server admission observer should bind before either route connects");
	MW_EXPECT_EQ(
		Test, MicroWorld::Core::EDelegateResult::Success, AddPeerConnectedObserverResult, "The server should register its admission observer");
	MW_EXPECT_EQ(
		Test,
		MicroWorld::Core::EDelegateResult::Success,
		BindRejectionObserverResult,
		"The rejection observer should bind before the limited route connects");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeRejectionObserverResult, "The limited client should observe its private full response");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, FirstConnect, "The configured first route should request admission");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, SecondConnect, "The rejected route should still issue its initial request");
	MW_EXPECT_EQ(Test, EConnectionState::Connected, FirstClient.GetConnectionState(), "The first distinct route should become active");
	MW_EXPECT_EQ(
		Test,
		EConnectionState::Connecting,
		SecondClient.GetConnectionState(),
		"A full rejection must preserve the connecting state for cadence retries");
	MW_EXPECT_TRUE(Test, Server.HasActivePeer(), "The server should report its one admitted route as active");
	MW_EXPECT_EQ(Test, std::size_t{2}, FullRejectionCount, "The full response must reach the limited client again after its cadence retry request");
	MW_EXPECT_EQ(Test, EConnectionRejectReason::Full, LastRejectionReason, "The limited client must observe Full rather than an unrelated rejection");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, DuplicateFirstRequest, "A duplicate request on the admitted route should reach the server");
	MW_EXPECT_EQ(Test, std::size_t{1}, PeerConnectedCount, "The first route must be admitted exactly once despite a same-route duplicate request");
}

/**
 * Motivation: Guards the bounded fan-out policy when one eligible peer applies backpressure.
 * Responsibilities: Send once to all four live peers, report Partial for the blocked peer, and continue with later peers.
 */
MW_TEST_CASE(NetworkSystemFansOutToFourPeersAndContinuesAfterOnePeerIsBackpressured)
{
	// Arrange
	THostLoopback<5, 1, PacketBytes> Loopback;
	FMessagingSystem ServerMessaging;
	FMessagingSystem FirstClientMessaging;
	FMessagingSystem SecondClientMessaging;
	FMessagingSystem ThirdClientMessaging;
	FMessagingSystem FourthClientMessaging;
	FMessagingLinkId ServerLink{};
	FMessagingLinkId FirstClientLink{};
	FMessagingLinkId SecondClientLink{};
	FMessagingLinkId ThirdClientLink{};
	FMessagingLinkId FourthClientLink{};
	FNetworkSystem Server(ServerMessaging, {ENetworkRole::Server});
	FNetworkSystem FirstClient(FirstClientMessaging, {ENetworkRole::Client});
	FNetworkSystem SecondClient(SecondClientMessaging, {ENetworkRole::Client});
	FNetworkSystem ThirdClient(ThirdClientMessaging, {ENetworkRole::Client});
	FNetworkSystem FourthClient(FourthClientMessaging, {ENetworkRole::Client});
	(void)ServerMessaging.RegisterLink(Loopback.Port(ServerPort), ServerLink);
	(void)FirstClientMessaging.RegisterLink(Loopback.Port(1), FirstClientLink);
	(void)SecondClientMessaging.RegisterLink(Loopback.Port(2), SecondClientLink);
	(void)ThirdClientMessaging.RegisterLink(Loopback.Port(3), ThirdClientLink);
	(void)FourthClientMessaging.RegisterLink(Loopback.Port(4), FourthClientLink);
	(void)Server.Initialize();
	(void)FirstClient.Initialize();
	(void)SecondClient.Initialize();
	(void)ThirdClient.Initialize();
	(void)FourthClient.Initialize();
	(void)FirstClient.ConnectToServer({FirstClientLink, MakeLoopbackAddress(ServerPort)}, 0);
	ServerMessaging.PreAdvance(0);
	FirstClientMessaging.PreAdvance(0);
	(void)SecondClient.ConnectToServer({SecondClientLink, MakeLoopbackAddress(ServerPort)}, 0);
	ServerMessaging.PreAdvance(0);
	SecondClientMessaging.PreAdvance(0);
	(void)ThirdClient.ConnectToServer({ThirdClientLink, MakeLoopbackAddress(ServerPort)}, 0);
	ServerMessaging.PreAdvance(0);
	ThirdClientMessaging.PreAdvance(0);
	(void)FourthClient.ConnectToServer({FourthClientLink, MakeLoopbackAddress(ServerPort)}, 0);
	ServerMessaging.PreAdvance(0);
	FourthClientMessaging.PreAdvance(0);
	(void)ServerMessaging.CreateChannel({GameplayChannel, false, nullptr, {}});
	(void)FirstClientMessaging.CreateChannel({GameplayChannel, false, nullptr, {}});
	(void)SecondClientMessaging.CreateChannel({GameplayChannel, false, nullptr, {}});
	(void)ThirdClientMessaging.CreateChannel({GameplayChannel, false, nullptr, {}});
	(void)FourthClientMessaging.CreateChannel({GameplayChannel, false, nullptr, {}});
	std::size_t FirstDeliveryCount = 0;
	std::size_t SecondDeliveryCount = 0;
	std::size_t ThirdDeliveryCount = 0;
	std::size_t FourthDeliveryCount = 0;
	FMessagingSystem::FSubscriberDelegate FirstSubscriber;
	FMessagingSystem::FSubscriberDelegate SecondSubscriber;
	FMessagingSystem::FSubscriberDelegate ThirdSubscriber;
	FMessagingSystem::FSubscriberDelegate FourthSubscriber;
	(void)FirstSubscriber.Bind([&FirstDeliveryCount](const FMessage&) noexcept { ++FirstDeliveryCount; });
	(void)SecondSubscriber.Bind([&SecondDeliveryCount](const FMessage&) noexcept { ++SecondDeliveryCount; });
	(void)ThirdSubscriber.Bind([&ThirdDeliveryCount](const FMessage&) noexcept { ++ThirdDeliveryCount; });
	(void)FourthSubscriber.Bind([&FourthDeliveryCount](const FMessage&) noexcept { ++FourthDeliveryCount; });
	(void)FirstClientMessaging.SubscribeToChannel(GameplayChannel, std::move(FirstSubscriber));
	(void)SecondClientMessaging.SubscribeToChannel(GameplayChannel, std::move(SecondSubscriber));
	(void)ThirdClientMessaging.SubscribeToChannel(GameplayChannel, std::move(ThirdSubscriber));
	(void)FourthClientMessaging.SubscribeToChannel(GameplayChannel, std::move(FourthSubscriber));
	const FMessage Message = MakeGameplayMessage();

	// Act
	const ENetworkResult FirstBroadcast = Server.Broadcast(GameplayChannel, Message);
	SecondClientMessaging.PreAdvance(0);
	ThirdClientMessaging.PreAdvance(0);
	FourthClientMessaging.PreAdvance(0);
	const ENetworkResult PartialBroadcast = Server.Broadcast(GameplayChannel, Message);
	FirstClientMessaging.PreAdvance(0);
	SecondClientMessaging.PreAdvance(0);
	ThirdClientMessaging.PreAdvance(0);
	FourthClientMessaging.PreAdvance(0);

	// Assert
	MW_EXPECT_EQ(Test, ENetworkResult::Success, FirstBroadcast, "The initial fan-out should send once to every live peer");
	MW_EXPECT_EQ(Test, ENetworkResult::Partial, PartialBroadcast, "One full mailbox should make fan-out partially accepted");
	MW_EXPECT_EQ(Test, std::size_t{1}, FirstDeliveryCount, "The backpressured first peer should retain only its first fan-out");
	MW_EXPECT_EQ(Test, std::size_t{2}, SecondDeliveryCount, "The second peer must receive the send after the first peer fails");
	MW_EXPECT_EQ(Test, std::size_t{2}, ThirdDeliveryCount, "The third peer must receive the send after the first peer fails");
	MW_EXPECT_EQ(Test, std::size_t{2}, FourthDeliveryCount, "The fourth peer must receive the send after the first peer fails");
}

/**
 * Motivation: Ensures shutting down Network releases its private reliable retries so the same instance can reserve its channels again.
 * Responsibilities: Leave a reliable routed frame pending, shut down, reinitialize, and reject sends while shut down.
 */
MW_TEST_CASE(NetworkSystemShutdownCancelsPrivateReliableFramesAndGuardsReinitialization)
{
	// Arrange
	THostLoopback<2, MailboxDepth, PacketBytes> Loopback;
	FMessagingSystem ServerMessaging;
	FMessagingSystem ClientMessaging;
	FMessagingLinkId ServerLink{};
	FMessagingLinkId ClientLink{};
	FNetworkSystem Server(ServerMessaging, {ENetworkRole::Server});
	FNetworkSystem Client(ClientMessaging, {ENetworkRole::Client});
	(void)ServerMessaging.RegisterLink(Loopback.Port(ServerPort), ServerLink);
	(void)ClientMessaging.RegisterLink(Loopback.Port(ClientPort), ClientLink);
	const FPeerId Peer = ConnectPair(ServerMessaging, ClientMessaging, Server, Client, {ClientLink, MakeLoopbackAddress(ServerPort)});
	const bool bPeerIsValid = Peer.IsValid();
	(void)ServerMessaging.CreateChannel({GameplayChannel, true, nullptr, {}});
	(void)ClientMessaging.CreateChannel({GameplayChannel, true, nullptr, {}});

	// Act
	const ENetworkResult PendingSendResult = Client.SendToServer(GameplayChannel, MakeGameplayMessage());
	Client.Shutdown();
	const ENetworkResult GuardedSendResult = Client.SendToServer(GameplayChannel, MakeGameplayMessage());
	const ENetworkResult ReinitializeResult = Client.Initialize();

	// Assert
	MW_EXPECT_TRUE(Test, bPeerIsValid, "The fixture must establish a client session before creating a private reliable frame");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, PendingSendResult, "A reliable routed send should reserve a private reliable frame");
	MW_EXPECT_EQ(Test, ENetworkResult::NotConnected, GuardedSendResult, "A shut down client must not send through its released private channels");
	MW_EXPECT_EQ(
		Test, ENetworkResult::Success, ReinitializeResult, "Shutdown must cancel pending private reliable frames before channel destruction");
}

/**
 * Motivation: Keeps the routed payload limit safe for both private wire channel reliability policies.
 * Responsibilities: Accept the exact reliable-safe payload and reject one additional byte before it can reach the client.
 */
MW_TEST_CASE(NetworkSystemAcceptsExactReliableSafePayloadAndRejectsOneAdditionalByte)
{
	// Arrange
	THostLoopback<2, MailboxDepth, PacketBytes> Loopback;
	FMessagingSystem ServerMessaging;
	FMessagingSystem ClientMessaging;
	FMessagingLinkId ServerLink{};
	FMessagingLinkId ClientLink{};
	FNetworkSystem Server(ServerMessaging, {ENetworkRole::Server});
	FNetworkSystem Client(ClientMessaging, {ENetworkRole::Client});
	(void)ServerMessaging.RegisterLink(Loopback.Port(ServerPort), ServerLink);
	(void)ClientMessaging.RegisterLink(Loopback.Port(ClientPort), ClientLink);
	const FPeerId Peer = ConnectPair(ServerMessaging, ClientMessaging, Server, Client, {ClientLink, MakeLoopbackAddress(ServerPort)});
	(void)ServerMessaging.CreateChannel({GameplayChannel, true, nullptr, {}});
	(void)ClientMessaging.CreateChannel({GameplayChannel, true, nullptr, {}});
	std::uint8_t ExactBytes[FNetworkSystem::MaxRoutedMessageBytes]{};
	std::uint8_t OversizeBytes[FNetworkSystem::MaxRoutedMessageBytes + 1]{};
	FMessage ExactMessage;
	ExactMessage.SetMessageNameId(GameplayMessage);
	ExactMessage.SetPayload(MicroWorld::Core::TSpan<const std::uint8_t>(ExactBytes, sizeof(ExactBytes)));
	FMessage OversizeMessage;
	OversizeMessage.SetMessageNameId(GameplayMessage);
	OversizeMessage.SetPayload(MicroWorld::Core::TSpan<const std::uint8_t>(OversizeBytes, sizeof(OversizeBytes)));

	// Act
	const ENetworkResult ExactResult = Server.SendTo(Peer, GameplayChannel, ExactMessage);
	const ENetworkResult OversizeResult = Server.SendTo(Peer, GameplayChannel, OversizeMessage);

	// Assert
	MW_EXPECT_TRUE(Test, Peer.IsValid(), "The fixture must establish a server peer before addressed sends");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, ExactResult, "The derived reliable-safe payload must fit the private reliable wire frame");
	MW_EXPECT_EQ(Test, ENetworkResult::Invalid, OversizeResult, "One byte beyond the derived payload limit must be rejected before a remote send");
}

/**
 * Motivation: Prevents delayed attempts from disconnecting a current route while preserving idempotence and deliberate reconnect replacement.
 * Responsibilities: Keep the live peer for lower and equal attempts, then invalidate it when a greater attempt arrives on the same route.
 */
MW_TEST_CASE(NetworkSystemServerOrdersLowerEqualAndGreaterConnectAttempts)
{
	// Arrange
	THostLoopback<2, MailboxDepth, PacketBytes> Loopback;
	FMessagingSystem ServerMessaging;
	FMessagingSystem ClientMessaging;
	FMessagingLinkId ServerLink{};
	FMessagingLinkId ClientLink{};
	FNetworkSystem Server(ServerMessaging, {ENetworkRole::Server});
	FNetworkSystem Client(ClientMessaging, {ENetworkRole::Client});
	(void)ServerMessaging.RegisterLink(Loopback.Port(ServerPort), ServerLink);
	(void)ClientMessaging.RegisterLink(Loopback.Port(ClientPort), ClientLink);
	std::size_t PeerConnectedCount = 0;
	FPeerId MostRecentConnectedPeer{};
	MicroWorld::Core::TDelegate<void(FPeerId), FNetworkSystem::EventCallableBytes> PeerConnectedObserver;
	const MicroWorld::Core::EDelegateResult BindPeerConnectedObserverResult = PeerConnectedObserver.Bind(
		[&PeerConnectedCount, &MostRecentConnectedPeer](const FPeerId InPeer) noexcept
		{
			++PeerConnectedCount;
			MostRecentConnectedPeer = InPeer;
		});
	MicroWorld::Core::FDelegateHandle PeerConnectedHandle{};
	const MicroWorld::Core::EDelegateResult AddPeerConnectedObserverResult =
		Server.OnPeerConnected().Add(std::move(PeerConnectedObserver), PeerConnectedHandle);
	const FPeerId FirstPeer = ConnectPair(ServerMessaging, ClientMessaging, Server, Client, {ClientLink, MakeLoopbackAddress(ServerPort)});
	const bool bFirstPeerIsValid = FirstPeer.IsValid();
	(void)ServerMessaging.CreateChannel({GameplayChannel, false, nullptr, {}});
	const FMessage Message = MakeGameplayMessage();

	// Act
	const EMessagingResult LowerRequest = ClientMessaging.SendTypedMessageToRemoteChannel(
		FConnectRequest{1, 0}, FNetworkSystem::BestEffortWireChannelNameId, {ClientLink, MakeLoopbackAddress(ServerPort)});
	ServerMessaging.PreAdvance(1);
	const ENetworkResult LowerAttemptSend = Server.SendTo(FirstPeer, GameplayChannel, Message);
	const EMessagingResult EqualRequest = ClientMessaging.SendTypedMessageToRemoteChannel(
		FConnectRequest{1, 1}, FNetworkSystem::BestEffortWireChannelNameId, {ClientLink, MakeLoopbackAddress(ServerPort)});
	ServerMessaging.PreAdvance(2);
	const ENetworkResult EqualAttemptSend = Server.SendTo(FirstPeer, GameplayChannel, Message);
	const EMessagingResult GreaterRequest = ClientMessaging.SendTypedMessageToRemoteChannel(
		FConnectRequest{1, 2}, FNetworkSystem::BestEffortWireChannelNameId, {ClientLink, MakeLoopbackAddress(ServerPort)});
	ServerMessaging.PreAdvance(3);
	const ENetworkResult GreaterAttemptSend = Server.SendTo(FirstPeer, GameplayChannel, Message);
	// Drain earlier addressed sends so the replacement-peer send observes admission rather than loopback backpressure.
	ClientMessaging.PreAdvance(3);
	const ENetworkResult ReplacementAttemptSend = Server.SendTo(MostRecentConnectedPeer, GameplayChannel, Message);

	// Assert
	MW_EXPECT_EQ(
		Test, MicroWorld::Core::EDelegateResult::Success, BindPeerConnectedObserverResult, "The peer observer must bind before admission begins");
	MW_EXPECT_EQ(
		Test, MicroWorld::Core::EDelegateResult::Success, AddPeerConnectedObserverResult, "The server must accept the peer admission observer");
	MW_EXPECT_TRUE(Test, bFirstPeerIsValid, "The fixture must admit the initial attempt");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, LowerRequest, "The fixture must deliver the stale request to the server");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, LowerAttemptSend, "A lower attempt must leave the current peer live");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, EqualRequest, "The fixture must deliver the duplicate request to the server");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, EqualAttemptSend, "An equal attempt must preserve the idempotent peer generation");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, GreaterRequest, "The fixture must deliver the replacement request to the server");
	MW_EXPECT_EQ(Test, ENetworkResult::NotConnected, GreaterAttemptSend, "A greater attempt must retire the previous peer generation");
	MW_EXPECT_EQ(Test, std::size_t{2}, PeerConnectedCount, "A greater attempt must publish exactly one replacement peer admission");
	MW_EXPECT_TRUE(Test, MostRecentConnectedPeer.IsValid(), "A greater attempt must admit a replacement peer");
	const bool bReplacementPeerDiffersFromFirstPeer = MostRecentConnectedPeer != FirstPeer;
	MW_EXPECT_TRUE(Test, bReplacementPeerDiffersFromFirstPeer, "A replacement attempt must advance the peer generation");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, ReplacementAttemptSend, "The replacement peer must accept addressed server sends");
}

/**
 * Motivation: Treats valid routed server application traffic as proof that the remote server is alive.
 * Responsibilities: Refresh client liveness only after route and peer validation, preventing a timeout after valid routed delivery.
 */
MW_TEST_CASE(NetworkSystemRoutedServerTrafficRefreshesClientLiveness)
{
	// Arrange
	THostLoopback<2, MailboxDepth, PacketBytes> Loopback;
	FMessagingSystem ServerMessaging;
	FMessagingSystem ClientMessaging;
	FMessagingLinkId ServerLink{};
	FMessagingLinkId ClientLink{};
	FNetworkSystem Server(ServerMessaging, {ENetworkRole::Server});
	FNetworkSystem Client(ClientMessaging, {ENetworkRole::Client});
	(void)ServerMessaging.RegisterLink(Loopback.Port(ServerPort), ServerLink);
	(void)ClientMessaging.RegisterLink(Loopback.Port(ClientPort), ClientLink);
	const FPeerId Peer = ConnectPair(ServerMessaging, ClientMessaging, Server, Client, {ClientLink, MakeLoopbackAddress(ServerPort)});
	(void)ServerMessaging.CreateChannel({GameplayChannel, false, nullptr, {}});
	(void)ClientMessaging.CreateChannel({GameplayChannel, false, nullptr, {}});
	Server.PostAdvance(4999);

	// Act
	const ENetworkResult SendResult = Server.SendTo(Peer, GameplayChannel, MakeGameplayMessage());
	Client.PostAdvance(4999);
	ClientMessaging.PreAdvance(4999);
	Client.PreAdvance(5001);

	// Assert
	MW_EXPECT_EQ(Test, ENetworkResult::Success, SendResult, "The fixture must deliver a valid routed server application message");
	MW_EXPECT_EQ(
		Test,
		EConnectionState::Connected,
		Client.GetConnectionState(),
		"Validated routed server traffic must refresh client liveness before the timeout check");
}

} // namespace
