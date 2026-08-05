#include "TestSupport.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Messaging/ChannelInformation.h>
#include <MicroWorld/Messaging/MessagingSystem.h>
#include <MicroWorld/Networking/NetworkSystem.h>
#include <MicroWorld/Platform/Host/HostWifiDevice.h>
#include <MicroWorld/Platform/Host/UdpAddress.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::Core::TimePointMilliseconds;
using MicroWorld::Messaging::EMessagingResult;
using MicroWorld::Messaging::FMessage;
using MicroWorld::Messaging::FMessagingLinkId;
using MicroWorld::Messaging::FMessagingRoute;
using MicroWorld::Messaging::FMessagingSystem;
using MicroWorld::Messaging::FNameId;
using MicroWorld::Networking::EConnectionState;
using MicroWorld::Networking::ENetworkResult;
using MicroWorld::Networking::ENetworkRole;
using MicroWorld::Networking::FNetworkSystem;
using MicroWorld::Networking::FPeerId;
using MicroWorld::Platform::Host::FHostWifiDevice;
using MicroWorld::Transport::MakeUdpAddress;

/** Motivation: Host loopback octets identify the real UDP server endpoint in this test. */
constexpr std::uint8_t LoopbackOctets[4] = {127, 0, 0, 1};

/** Motivation: Names the local-only channel that carries application traffic through Network. */
constexpr FNameId GameplayChannel{"Gameplay"};

/** Motivation: Names the application payload sent through the client/server Network session. */
constexpr FNameId GameplayMessage{"Update"};

/** Motivation: Bounds real socket polling while admission datagrams cross localhost. */
constexpr int PollTimeoutMilliseconds = 100;

/** Motivation: Bounds the asynchronous UDP handshake without making the test wait indefinitely. */
constexpr int HandshakeIterationCap = 20;

/** Motivation: Supplies a short bounded application payload for addressed and broadcast sends. */
const std::array<std::uint8_t, 4> ApplicationPayload = {0x10, 0x20, 0x30, 0x40};

/**
 * Motivation: Records the visible Network application delivery facts needed by the real socket test.
 * Responsibilities: Count deliveries and preserve the validated logical sender, never retaining caller-owned payload storage.
 * Example: FDeliveryCapture Capture{};
 */
struct FDeliveryCapture final
{
	/** Motivation: Counts completed local application deliveries. */
	std::size_t Count{0};

	/** Motivation: Preserves the resolved logical source from the most recent delivery. */
	FPeerId Sender{};

	/** Motivation: Preserves the first payload byte for content verification. */
	std::uint8_t FirstPayloadByte{0};
};

/**
 * Motivation: Creates one application message whose storage outlives every synchronous Network send in this test.
 * Responsibilities: Return a named bounded view over ApplicationPayload without allocating.
 */
FMessage MakeGameplayMessage() noexcept
{
	FMessage Message;
	Message.SetMessageNameId(GameplayMessage);
	Message.SetPayload(MicroWorld::Core::TSpan<const std::uint8_t>(ApplicationPayload.data(), ApplicationPayload.size()));
	return Message;
}

/**
 * Motivation: Pumps device input and Network liveness in the order used by a direct Messaging composition.
 * Responsibilities: Advance each device once, then let Messaging decode and Network emit any due protocol traffic.
 */
void AdvancePair(
	FHostWifiDevice& InServerDevice,
	FHostWifiDevice& InClientDevice,
	FMessagingSystem& InServerMessaging,
	FMessagingSystem& InClientMessaging,
	FNetworkSystem& InServerNetwork,
	FNetworkSystem& InClientNetwork,
	const TimePointMilliseconds InNow) noexcept
{
	InServerDevice.PreAdvance(InNow);
	InClientDevice.PreAdvance(InNow);
	InServerMessaging.PreAdvance(InNow);
	InClientMessaging.PreAdvance(InNow);
	InServerNetwork.PreAdvance(InNow);
	InClientNetwork.PreAdvance(InNow);
	InClientNetwork.PostAdvance(InNow);
	InServerNetwork.PostAdvance(InNow);
	InClientMessaging.PostAdvance(InNow);
	InServerMessaging.PostAdvance(InNow);
	InClientDevice.PostAdvance(InNow);
	InServerDevice.PostAdvance(InNow);
}

/**
 * Motivation: Waits for a real localhost UDP admission exchange without assuming packet delivery timing.
 * Responsibilities: Pump both direct compositions until the client is connected or the bounded poll budget is exhausted.
 */
void PumpHandshake(
	FHostWifiDevice& InServerDevice,
	FHostWifiDevice& InClientDevice,
	FMessagingSystem& InServerMessaging,
	FMessagingSystem& InClientMessaging,
	FNetworkSystem& InServerNetwork,
	FNetworkSystem& InClientNetwork) noexcept
{
	for (int Iteration = 0; Iteration < HandshakeIterationCap; ++Iteration)
	{
		const TimePointMilliseconds Now{static_cast<std::uint64_t>(Iteration)};
		AdvancePair(InServerDevice, InClientDevice, InServerMessaging, InClientMessaging, InServerNetwork, InClientNetwork, Now);
		if (InClientNetwork.GetConnectionState() == EConnectionState::Connected)
		{
			return;
		}
		(void)InServerDevice.PollReadable(PollTimeoutMilliseconds);
		(void)InClientDevice.PollReadable(PollTimeoutMilliseconds);
	}
}

} // namespace

/**
 * Motivation: Scenario: Compose real host UDP devices through Messaging and Networking instead of a Transport session facade.
 * Responsibilities: Expected: Client admission, client-to-server delivery with logical source resolution, and server broadcast all cross localhost
 * UDP.
 */
MW_TEST_CASE(HostNetworkMessagingCompletesApplicationFlowAcrossRealUdp)
{
	// Arrange
	FHostWifiDevice ServerDevice(0);
	FHostWifiDevice ClientDevice(0);
	MW_EXPECT_TRUE(Test, ServerDevice.IsOpen(), "The server UDP device opened");
	MW_EXPECT_TRUE(Test, ClientDevice.IsOpen(), "The client UDP device opened");

	FMessagingSystem ServerMessaging;
	FMessagingSystem ClientMessaging;
	FMessagingLinkId ServerLink{};
	FMessagingLinkId ClientLink{};
	MW_EXPECT_EQ(
		Test, EMessagingResult::Success, ServerMessaging.RegisterLink(ServerDevice, ServerLink), "The server device registers with Messaging");
	MW_EXPECT_EQ(
		Test, EMessagingResult::Success, ClientMessaging.RegisterLink(ClientDevice, ClientLink), "The client device registers with Messaging");

	FNetworkSystem ServerNetwork(ServerMessaging, {ENetworkRole::Server});
	FNetworkSystem ClientNetwork(ClientMessaging, {ENetworkRole::Client});
	MW_EXPECT_EQ(Test, ENetworkResult::Success, ServerNetwork.Initialize(), "The server reserves Network wire channels");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, ClientNetwork.Initialize(), "The client reserves Network wire channels");
	MW_EXPECT_EQ(
		Test,
		EMessagingResult::Success,
		ServerMessaging.CreateChannel({GameplayChannel, false, nullptr, {}}),
		"The server creates a local gameplay channel");
	MW_EXPECT_EQ(
		Test,
		EMessagingResult::Success,
		ClientMessaging.CreateChannel({GameplayChannel, false, nullptr, {}}),
		"The client creates a local gameplay channel");

	FDeliveryCapture ServerCapture{};
	FDeliveryCapture ClientCapture{};
	FMessagingSystem::FSubscriberDelegate ServerSubscriber;
	FMessagingSystem::FSubscriberDelegate ClientSubscriber;
	(void)ServerSubscriber.Bind(
		[&ServerCapture, &ServerNetwork](const FMessage& InMessage) noexcept
		{
			++ServerCapture.Count;
			ServerCapture.Sender = ServerNetwork.ResolveSenderPeer(InMessage);
			ServerCapture.FirstPayloadByte = InMessage.GetPayload().IsEmpty() ? 0 : InMessage.GetPayload()[0];
		});
	(void)ClientSubscriber.Bind(
		[&ClientCapture, &ClientNetwork](const FMessage& InMessage) noexcept
		{
			++ClientCapture.Count;
			ClientCapture.Sender = ClientNetwork.ResolveSenderPeer(InMessage);
			ClientCapture.FirstPayloadByte = InMessage.GetPayload().IsEmpty() ? 0 : InMessage.GetPayload()[0];
		});
	MW_EXPECT_EQ(
		Test,
		EMessagingResult::Success,
		ServerMessaging.SubscribeToChannel(GameplayChannel, std::move(ServerSubscriber)),
		"The server subscribes to application delivery");
	MW_EXPECT_EQ(
		Test,
		EMessagingResult::Success,
		ClientMessaging.SubscribeToChannel(GameplayChannel, std::move(ClientSubscriber)),
		"The client subscribes to application delivery");

	const FMessagingRoute ServerRoute{
		ClientLink, MakeUdpAddress(LoopbackOctets[0], LoopbackOctets[1], LoopbackOctets[2], LoopbackOctets[3], ServerDevice.BoundPort())};
	const ENetworkResult ConnectResult = ClientNetwork.ConnectToServer(ServerRoute, 0);
	PumpHandshake(ServerDevice, ClientDevice, ServerMessaging, ClientMessaging, ServerNetwork, ClientNetwork);
	const FPeerId ServerPeer = ClientNetwork.GetServerPeer();
	const FMessage Message = MakeGameplayMessage();

	// Act
	const ENetworkResult ClientSendResult = ClientNetwork.SendToServer(GameplayChannel, Message);
	AdvancePair(ServerDevice, ClientDevice, ServerMessaging, ClientMessaging, ServerNetwork, ClientNetwork, 100);
	const ENetworkResult BroadcastResult = ServerNetwork.Broadcast(GameplayChannel, Message);
	AdvancePair(ServerDevice, ClientDevice, ServerMessaging, ClientMessaging, ServerNetwork, ClientNetwork, 101);

	// Assert
	MW_EXPECT_EQ(Test, ENetworkResult::Success, ConnectResult, "The client accepts the registered server route");
	MW_EXPECT_EQ(Test, EConnectionState::Connected, ClientNetwork.GetConnectionState(), "The client reaches Connected over real UDP");
	MW_EXPECT_TRUE(Test, ServerPeer.IsValid(), "The client exposes its logical server peer after admission");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, ClientSendResult, "The client sends application traffic only through Network");
	MW_EXPECT_EQ(Test, std::size_t{1}, ServerCapture.Count, "The server receives one remote application message");
	MW_EXPECT_TRUE(Test, ServerCapture.Sender.IsValid(), "The server resolves the validated sender peer without an endpoint address");
	MW_EXPECT_EQ(Test, ApplicationPayload[0], ServerCapture.FirstPayloadByte, "The server receives the client payload");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, BroadcastResult, "The server broadcasts once through each eligible Network peer");
	MW_EXPECT_EQ(Test, std::size_t{1}, ClientCapture.Count, "The client receives the server broadcast once");
	MW_EXPECT_TRUE(Test, ClientCapture.Sender.IsValid(), "The client resolves the server as a logical peer");
	MW_EXPECT_EQ(Test, ApplicationPayload[0], ClientCapture.FirstPayloadByte, "The client receives the broadcast payload");
}
