#include "TestSupport.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessageWriter.h>
#include <MicroWorld/Messaging/MessagingSystem.h>
#include <MicroWorld/Messaging/TypedMessageCodec.h>
#include <MicroWorld/Networking/ConnectAccepted.h>
#include <MicroWorld/Networking/ConnectRejected.h>
#include <MicroWorld/Networking/ConnectRequest.h>
#include <MicroWorld/Networking/Disconnect.h>
#include <MicroWorld/Networking/Heartbeat.h>
#include <MicroWorld/Networking/NetworkSystem.h>
#include <MicroWorld/Networking/RoutedMessage.h>
#include <MicroWorld/Transport/HostLoopback.h>

#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::Core::MakeLoopbackAddress;
using MicroWorld::Core::TSpan;
using MicroWorld::Messaging::DecodeTypedMessage;
using MicroWorld::Messaging::EMessagingResult;
using MicroWorld::Messaging::FMessage;
using MicroWorld::Messaging::FMessageWriter;
using MicroWorld::Messaging::FMessagingLinkId;
using MicroWorld::Messaging::FMessagingRoute;
using MicroWorld::Messaging::FMessagingSystem;
using MicroWorld::Messaging::MessageAcknowledgementNameId;
using MicroWorld::Networking::EConnectionRejectReason;
using MicroWorld::Networking::EConnectionState;
using MicroWorld::Networking::EDisconnectReason;
using MicroWorld::Networking::ENetworkResult;
using MicroWorld::Networking::ENetworkRole;
using MicroWorld::Networking::FConnectAccepted;
using MicroWorld::Networking::FConnectRejected;
using MicroWorld::Networking::FConnectRequest;
using MicroWorld::Networking::FDisconnect;
using MicroWorld::Networking::FHeartbeat;
using MicroWorld::Networking::FNetworkSystem;
using MicroWorld::Networking::FPeerId;
using MicroWorld::Networking::FRoutedMessage;
using MicroWorld::Transport::THostLoopback;

/**
 * Motivation: Exercises every public schema through the same public typed-codec boundary used by Messaging.
 * Responsibilities: Encode one bounded message into local storage and decode it before that storage expires.
 */
template<typename MessageType>
EMessagingResult RoundTripMessage(const MessageType& InMessage, MessageType& OutMessage) noexcept
{
	std::uint8_t Payload[FMessagingSystem::MaxMessageBytes]{};
	FMessageWriter Writer(TSpan<std::uint8_t>(Payload, sizeof(Payload)));
	if (EncodeMessagePayload(InMessage, Writer) != EMessagingResult::Success)
	{
		return EMessagingResult::Invalid;
	}
	FMessage WireMessage;
	WireMessage.SetMessageNameId(GetMessageNameId(InMessage));
	WireMessage.SetPayload(Writer.WrittenBytes());
	return DecodeTypedMessage(WireMessage, OutMessage);
}

/**
 * Motivation: Proves the public accept schema has an exact interoperable typed codec.
 * Responsibilities: Decode attempt and generation-checked peer fields from the declared little-endian wire layout.
 */
MW_TEST_CASE(NetworkProtocolConnectAcceptedRoundTripsExactPayload)
{
	// Arrange
	const std::uint8_t Payload[]{0x78, 0x56, 0x34, 0x12, 0x02, 0xEF, 0xBE, 0xAD, 0xDE};
	FMessage Message;
	Message.SetMessageNameId(GetMessageNameId(FConnectAccepted{}));
	Message.SetPayload(TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
	FConnectAccepted Decoded{};
	const FPeerId ExpectedPeer{2, 0xDEADBEEFu};

	// Act
	const EMessagingResult Result = DecodeTypedMessage(Message, Decoded);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, Result, "A complete accept payload should decode");
	MW_EXPECT_EQ(Test, std::uint32_t{0x12345678}, Decoded.AttemptId, "The attempt id should retain little-endian bytes");
	MW_EXPECT_EQ(Test, ExpectedPeer, Decoded.Peer, "The peer id should retain slot and generation");
}

/**
 * Motivation: Keeps malformed route envelopes from changing a caller's existing output value.
 * Responsibilities: Reject a payload whose declared byte count exceeds its supplied bytes without partial assignment.
 */
MW_TEST_CASE(NetworkProtocolRoutedMessageRejectsTruncatedPayloadWithoutMutation)
{
	// Arrange
	const std::uint8_t Payload[]{0x00, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x02, 0xAB};
	FMessage Message;
	Message.SetMessageNameId(GetMessageNameId(FRoutedMessage{}));
	Message.SetPayload(TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
	FRoutedMessage Decoded{};
	Decoded.Peer = {7, 9};
	const FPeerId PreservedPeer{7, 9};

	// Act
	const EMessagingResult Result = DecodeTypedMessage(Message, Decoded);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Invalid, Result, "A truncated routed payload should be rejected");
	MW_EXPECT_EQ(Test, PreservedPeer, Decoded.Peer, "Rejected decoding should preserve the previous output");
}

/**
 * Motivation: Keeps all Network-owned wire schemas interoperable and distinct from Messaging control traffic.
 * Responsibilities: Round-trip every schema and verify its public name cannot be treated as a reliable acknowledgement.
 */
MW_TEST_CASE(NetworkProtocolRoundTripsEverySchemaAndKeepsWireNamesDistinctFromMessagingAcknowledgements)
{
	// Arrange
	FConnectRequest Request{3, 0x10203040u};
	FConnectAccepted Accepted{0x10203040u, {2, 7}};
	FConnectRejected Rejected{0x10203040u, EConnectionRejectReason::Full};
	FHeartbeat Heartbeat{{2, 7}, 0x10203040u};
	FDisconnect Disconnect{{2, 7}, 0x10203040u, EDisconnectReason::Timeout};
	FRoutedMessage Routed{};
	Routed.Peer = {2, 7};
	Routed.ChannelNameId = "Gameplay";
	Routed.MessageNameId = "Updated";
	Routed.Payload[0] = 0xAA;
	Routed.Payload[1] = 0xBB;
	Routed.PayloadSize = 2;
	FConnectRequest DecodedRequest{};
	FConnectAccepted DecodedAccepted{};
	FConnectRejected DecodedRejected{};
	FHeartbeat DecodedHeartbeat{};
	FDisconnect DecodedDisconnect{};
	FRoutedMessage DecodedRouted{};

	// Act
	const EMessagingResult RequestResult = RoundTripMessage(Request, DecodedRequest);
	const EMessagingResult AcceptedResult = RoundTripMessage(Accepted, DecodedAccepted);
	const EMessagingResult RejectedResult = RoundTripMessage(Rejected, DecodedRejected);
	const EMessagingResult HeartbeatResult = RoundTripMessage(Heartbeat, DecodedHeartbeat);
	const EMessagingResult DisconnectResult = RoundTripMessage(Disconnect, DecodedDisconnect);
	const EMessagingResult RoutedResult = RoundTripMessage(Routed, DecodedRouted);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, RequestResult, "Connect requests must round-trip");
	MW_EXPECT_EQ(Test, Request.AttemptId, DecodedRequest.AttemptId, "Connect requests must preserve attempts");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, AcceptedResult, "Connect accepts must round-trip");
	MW_EXPECT_EQ(Test, Accepted.Peer, DecodedAccepted.Peer, "Connect accepts must preserve peer ids");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, RejectedResult, "Connect rejections must round-trip");
	MW_EXPECT_EQ(Test, Rejected.Reason, DecodedRejected.Reason, "Connect rejections must preserve the reason");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, HeartbeatResult, "Heartbeats must round-trip");
	MW_EXPECT_EQ(Test, Heartbeat.Peer, DecodedHeartbeat.Peer, "Heartbeats must preserve peer ids");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, DisconnectResult, "Disconnects must round-trip");
	MW_EXPECT_EQ(Test, Disconnect.Reason, DecodedDisconnect.Reason, "Disconnects must preserve the reason");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, RoutedResult, "Routed messages must round-trip");
	MW_EXPECT_EQ(Test, Routed.PayloadSize, DecodedRouted.PayloadSize, "Routed messages must preserve payload length");
	MW_EXPECT_EQ(Test, Routed.Payload[1], DecodedRouted.Payload[1], "Routed messages must preserve payload bytes");
	MW_EXPECT_TRUE(
		Test,
		GetMessageNameId(Request) != MessageAcknowledgementNameId && GetMessageNameId(Accepted) != MessageAcknowledgementNameId
			&& GetMessageNameId(Rejected) != MessageAcknowledgementNameId && GetMessageNameId(Heartbeat) != MessageAcknowledgementNameId
			&& GetMessageNameId(Disconnect) != MessageAcknowledgementNameId && GetMessageNameId(Routed) != MessageAcknowledgementNameId,
		"Network protocol names must not collide with Messaging acknowledgements");
}

/**
 * Motivation: Keeps malformed peers from entering live session policy through a partially decoded protocol object.
 * Responsibilities: Reject truncated, overlong, unknown-enum, and oversize payloads without changing caller state.
 */
MW_TEST_CASE(NetworkProtocolRejectsMalformedPayloadsWithoutMutatingOutputs)
{
	// Arrange
	const std::uint8_t TruncatedRequestPayload[]{1, 2, 3, 4};
	const std::uint8_t OverlongRequestPayload[]{1, 2, 3, 4, 5, 6};
	const std::uint8_t UnknownRejectPayload[]{1, 0, 0, 0, 0xFF};
	const std::uint8_t UnknownDisconnectPayload[]{0, 1, 0, 0, 0, 2, 0, 0, 0, 0xFF};
	FMessage Message;
	FConnectRequest DecodedRequest{9, 10};
	FConnectRejected DecodedRejected{11, EConnectionRejectReason::Full};
	FDisconnect DecodedDisconnect{{3, 4}, 5, EDisconnectReason::Requested};
	const FPeerId PreservedDisconnectPeer{3, 4};
	FRoutedMessage OversizeRouted{};
	OversizeRouted.Peer = {0, 1};
	OversizeRouted.ChannelNameId = "Gameplay";
	OversizeRouted.MessageNameId = "Updated";
	OversizeRouted.PayloadSize = static_cast<std::uint8_t>(FRoutedMessage::MaxPayloadBytes + 1);
	std::uint8_t EncodeBuffer[FMessagingSystem::MaxMessageBytes]{};
	FMessageWriter Writer(TSpan<std::uint8_t>(EncodeBuffer, sizeof(EncodeBuffer)));

	// Act
	Message.SetMessageNameId(GetMessageNameId(FConnectRequest{}));
	Message.SetPayload(TSpan<const std::uint8_t>(TruncatedRequestPayload, sizeof(TruncatedRequestPayload)));
	const EMessagingResult TruncatedResult = DecodeTypedMessage(Message, DecodedRequest);
	Message.SetPayload(TSpan<const std::uint8_t>(OverlongRequestPayload, sizeof(OverlongRequestPayload)));
	const EMessagingResult OverlongResult = DecodeTypedMessage(Message, DecodedRequest);
	Message.SetMessageNameId(GetMessageNameId(FConnectRejected{}));
	Message.SetPayload(TSpan<const std::uint8_t>(UnknownRejectPayload, sizeof(UnknownRejectPayload)));
	const EMessagingResult UnknownRejectResult = DecodeTypedMessage(Message, DecodedRejected);
	Message.SetMessageNameId(GetMessageNameId(FDisconnect{}));
	Message.SetPayload(TSpan<const std::uint8_t>(UnknownDisconnectPayload, sizeof(UnknownDisconnectPayload)));
	const EMessagingResult UnknownDisconnectResult = DecodeTypedMessage(Message, DecodedDisconnect);
	const EMessagingResult OversizeResult = EncodeMessagePayload(OversizeRouted, Writer);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Invalid, TruncatedResult, "A truncated connect request must be rejected");
	MW_EXPECT_EQ(Test, EMessagingResult::Invalid, OverlongResult, "A connect request with trailing bytes must be rejected");
	MW_EXPECT_EQ(Test, std::uint8_t{9}, DecodedRequest.ProtocolVersion, "Rejected request decoding must preserve the protocol version");
	MW_EXPECT_EQ(Test, std::uint32_t{10}, DecodedRequest.AttemptId, "Rejected request decoding must preserve the attempt");
	MW_EXPECT_EQ(Test, EMessagingResult::Invalid, UnknownRejectResult, "An unknown rejection enum must be rejected");
	MW_EXPECT_EQ(Test, EConnectionRejectReason::Full, DecodedRejected.Reason, "Rejected enum decoding must preserve the reason");
	MW_EXPECT_EQ(Test, EMessagingResult::Invalid, UnknownDisconnectResult, "An unknown disconnect enum must be rejected");
	MW_EXPECT_EQ(Test, PreservedDisconnectPeer, DecodedDisconnect.Peer, "Rejected disconnect decoding must preserve the peer");
	MW_EXPECT_EQ(Test, EDisconnectReason::Requested, DecodedDisconnect.Reason, "Rejected disconnect decoding must preserve the reason");
	MW_EXPECT_EQ(Test, EMessagingResult::Invalid, OversizeResult, "An oversize routed payload must not encode");
	MW_EXPECT_EQ(Test, std::size_t{0}, Writer.Consumed(), "An oversize routed payload must not partially encode");
}

/**
 * Motivation: Ensures delayed handshake responses cannot complete or cancel a newer client attempt.
 * Responsibilities: Inject stale accept and reject frames through an existing loopback route, then admit only the current attempt.
 */
MW_TEST_CASE(NetworkSystemIgnoresStaleAcceptAndRejectFrames)
{
	// Arrange
	constexpr std::uint8_t ServerPort = 0;
	constexpr std::uint8_t ClientPort = 1;
	THostLoopback<2, 4, 128> Loopback;
	FMessagingSystem ServerMessaging;
	FMessagingSystem ClientMessaging;
	FMessagingLinkId ServerLink{};
	FMessagingLinkId ClientLink{};
	FNetworkSystem Client(ClientMessaging, {ENetworkRole::Client});
	(void)ServerMessaging.RegisterLink(Loopback.Port(ServerPort), ServerLink);
	(void)ClientMessaging.RegisterLink(Loopback.Port(ClientPort), ClientLink);
	(void)ServerMessaging.CreateChannel({FNetworkSystem::BestEffortWireChannelNameId, false, nullptr, {}});
	const ENetworkResult InitializeResult = Client.Initialize();
	const FMessagingRoute ServerRoute{ClientLink, MakeLoopbackAddress(ServerPort)};
	const FMessagingRoute ClientRoute{ServerLink, MakeLoopbackAddress(ClientPort)};
	const FPeerId ExpectedPeer{0, 1};
	const ENetworkResult FirstConnect = Client.ConnectToServer(ServerRoute, 0);
	Loopback.Drain(ServerPort);
	const ENetworkResult SecondConnect = Client.ConnectToServer(ServerRoute, 1);
	Loopback.Drain(ServerPort);

	// Act
	const EMessagingResult StaleAcceptResult =
		ServerMessaging.SendTypedMessageToRemoteChannel(FConnectAccepted{1, {0, 1}}, FNetworkSystem::BestEffortWireChannelNameId, ClientRoute);
	ClientMessaging.PreAdvance(1);
	const EConnectionState StateAfterStaleAccept = Client.GetConnectionState();
	const FPeerId PeerAfterStaleAccept = Client.GetServerPeer();
	const EMessagingResult StaleRejectResult = ServerMessaging.SendTypedMessageToRemoteChannel(
		FConnectRejected{1, EConnectionRejectReason::ProtocolMismatch}, FNetworkSystem::BestEffortWireChannelNameId, ClientRoute);
	ClientMessaging.PreAdvance(1);
	const EConnectionState StateAfterStaleReject = Client.GetConnectionState();
	const FPeerId PeerAfterStaleReject = Client.GetServerPeer();
	const EMessagingResult CurrentAcceptResult =
		ServerMessaging.SendTypedMessageToRemoteChannel(FConnectAccepted{2, {0, 1}}, FNetworkSystem::BestEffortWireChannelNameId, ClientRoute);
	ClientMessaging.PreAdvance(1);

	// Assert
	MW_EXPECT_EQ(Test, ENetworkResult::Success, InitializeResult, "The client must initialize its protocol channels");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, FirstConnect, "The first connect attempt should be sent");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, SecondConnect, "The second connect attempt should replace the first");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, StaleAcceptResult, "The fixture must inject the stale acceptance");
	MW_EXPECT_EQ(Test, EConnectionState::Connecting, StateAfterStaleAccept, "A stale acceptance must not complete the newer attempt");
	MW_EXPECT_TRUE(Test, !PeerAfterStaleAccept.IsValid(), "A stale acceptance must not expose a server peer");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, StaleRejectResult, "The fixture must inject the stale rejection");
	MW_EXPECT_EQ(Test, EConnectionState::Connecting, StateAfterStaleReject, "A stale rejection must not cancel the newer attempt");
	MW_EXPECT_TRUE(Test, !PeerAfterStaleReject.IsValid(), "A stale rejection must not expose a server peer");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CurrentAcceptResult, "The fixture must inject the current acceptance");
	MW_EXPECT_EQ(Test, EConnectionState::Connected, Client.GetConnectionState(), "Only the current attempt may complete the client session");
	MW_EXPECT_EQ(Test, ExpectedPeer, Client.GetServerPeer(), "The current acceptance must supply the live peer id");
}

} // namespace
