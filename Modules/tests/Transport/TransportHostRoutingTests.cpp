#include "CoreAllocationCounters.h"
#include "TestSupport.h"
#include "TransportHostTestHelpers.h"

#include <cstddef>
#include <cstdint>

namespace
{

using namespace ::MicroWorld::Tests;

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
