#include "NetAllocationCounters.h"
#include "TestSupport.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Net/HostLoopback.h>
#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetHost.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/Time.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{

using MicroWorld::ENetHostState;
using MicroWorld::ENetMode;
using MicroWorld::ENetResult;
using MicroWorld::FDelegateHandle;
using MicroWorld::FHostLoopback;
using MicroWorld::FNetAddress;
using MicroWorld::FNetHostConfig;
using MicroWorld::FNetReceiveResult;
using MicroWorld::FPeerId;
using MicroWorld::INetDriver;
using MicroWorld::MakeLoopbackAddress;
using MicroWorld::TimePointMilliseconds;
using MicroWorld::TNetHost;
using MicroWorld::TSpan;
using MicroWorld::Tests::GlobalAllocationCount;

/** Records the last message a handler observed so a test can assert delivery. */
struct FHandlerCapture
{
	/** Number of messages the handler has observed; zero means it never ran. */
	std::size_t Count{0};

	/** Sender identity from the most recent dispatch. */
	FPeerId From{};

	/** Channel from the most recent dispatch. */
	std::uint8_t Channel{0};

	/** First payload byte from the most recent dispatch, or zero for an empty payload. */
	std::uint8_t FirstByte{0};
};

/** Builds a fast-heartbeat host config with a short timeout window for deterministic tests. */
FNetHostConfig MakeHostConfig(const std::uint8_t ProtocolVersion) noexcept
{
	FNetHostConfig Config{};
	Config.HeartbeatIntervalMilliseconds = 100;
	Config.PeerTimeoutMilliseconds = 500;
	Config.ProtocolVersion = ProtocolVersion;
	return Config;
}

/** Builds a client config that greets the loopback port `ServerPort`. */
FNetHostConfig MakeClientConfig(const std::uint8_t ProtocolVersion, const std::uint8_t ServerPort) noexcept
{
	FNetHostConfig Config = MakeHostConfig(ProtocolVersion);
	Config.ServerAddress = MakeLoopbackAddress(ServerPort);
	return Config;
}

/** Binds one capturing handler into a host and returns its removal handle. */
template<typename HostType>
FDelegateHandle InstallCapture(HostType& Host, FHandlerCapture& Capture) noexcept
{
	typename HostType::FMessageHandlerBinding Binding;
	Binding.Bind(
		[&Capture](const FPeerId From, const std::uint8_t Channel, TSpan<const std::uint8_t> Payload) noexcept
		{
			++Capture.Count;
			Capture.From = From;
			Capture.Channel = Channel;
			Capture.FirstByte = Payload.Size() > 0 ? Payload[0] : std::uint8_t{0};
		});
	FDelegateHandle Handle{};
	(void)Host.AddMessageHandler(std::move(Binding), Handle);
	return Handle;
}

/** Runs one full Hello->Welcome handshake round at `NowMilliseconds`. */
template<typename ServerType, typename ClientType>
void RunHandshake(ServerType& Server, ClientType& Client, const TimePointMilliseconds NowMilliseconds) noexcept
{
	Client.PumpSend(NowMilliseconds);
	Server.PumpReceive(NowMilliseconds);
	Server.PumpSend(NowMilliseconds);
	Client.PumpReceive(NowMilliseconds);
}

/** Proves a server admits one client and issues a Welcome that connects it. */
MW_TEST_CASE(NetHostServerAdmitsClientOnHello)
{
	FHostLoopback<2, 8, 64> Loopback;
	TNetHost<2, 64> Server(Loopback.Port(0));
	TNetHost<1, 64> Client(Loopback.Port(1));
	(void)Server.Configure(ENetMode::DedicatedServer, MakeHostConfig(1));
	(void)Client.Configure(ENetMode::Client, MakeClientConfig(1, 0));
	(void)Server.Start(0);
	(void)Client.Start(0);

	Client.PumpSend(0);
	Server.PumpReceive(0);
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Server.ActivePeerCount(), "Server admits one peer on a valid Hello");

	Server.PumpSend(0);
	Client.PumpReceive(0);
	MW_EXPECT_EQ(Test, ENetHostState::Connected, Client.GetState(), "Client connects once it receives the Welcome");
}

/** Proves the client state machine advances Idle -> Connecting -> Connected. */
MW_TEST_CASE(NetHostClientAdvancesThroughConnectingToConnected)
{
	FHostLoopback<2, 8, 64> Loopback;
	TNetHost<2, 64> Server(Loopback.Port(0));
	TNetHost<1, 64> Client(Loopback.Port(1));
	(void)Server.Configure(ENetMode::DedicatedServer, MakeHostConfig(1));
	(void)Client.Configure(ENetMode::Client, MakeClientConfig(1, 0));
	(void)Server.Start(0);

	MW_EXPECT_EQ(Test, ENetHostState::Idle, Client.GetState(), "A configured but unstarted client is Idle");
	(void)Client.Start(0);
	MW_EXPECT_EQ(Test, ENetHostState::Connecting, Client.GetState(), "A started client is Connecting");
	RunHandshake(Server, Client, 0);
	MW_EXPECT_EQ(Test, ENetHostState::Connected, Client.GetState(), "The client reaches Connected after the handshake");
}

/** Proves a server admits only up to its peer capacity and rejects the overflow Hello. */
MW_TEST_CASE(NetHostRejectsHelloWhenPeerTableFull)
{
	FHostLoopback<4, 8, 64> Loopback;
	TNetHost<2, 64> Server(Loopback.Port(0));
	TNetHost<1, 64> FirstClient(Loopback.Port(1));
	TNetHost<1, 64> SecondClient(Loopback.Port(2));
	TNetHost<1, 64> ThirdClient(Loopback.Port(3));
	(void)Server.Configure(ENetMode::DedicatedServer, MakeHostConfig(1));
	(void)FirstClient.Configure(ENetMode::Client, MakeClientConfig(1, 0));
	(void)SecondClient.Configure(ENetMode::Client, MakeClientConfig(1, 0));
	(void)ThirdClient.Configure(ENetMode::Client, MakeClientConfig(1, 0));
	(void)Server.Start(0);
	(void)FirstClient.Start(0);
	(void)SecondClient.Start(0);
	(void)ThirdClient.Start(0);

	FirstClient.PumpSend(0);
	SecondClient.PumpSend(0);
	ThirdClient.PumpSend(0);
	Server.PumpReceive(0);

	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Server.ActivePeerCount(), "Server admits exactly MaxPeers clients and rejects the rest");
}

/** Proves heartbeats received within the window keep a peer alive past the timeout. */
MW_TEST_CASE(NetHostHeartbeatKeepsPeerAlive)
{
	FHostLoopback<2, 8, 64> Loopback;
	TNetHost<2, 64> Server(Loopback.Port(0));
	TNetHost<1, 64> Client(Loopback.Port(1));
	(void)Server.Configure(ENetMode::DedicatedServer, MakeHostConfig(1));
	(void)Client.Configure(ENetMode::Client, MakeClientConfig(1, 0));
	(void)Server.Start(0);
	(void)Client.Start(0);
	RunHandshake(Server, Client, 0);

	// Drive client heartbeats every interval well past the 500 ms timeout window.
	for (TimePointMilliseconds Now = 100; Now <= 800; Now += 100)
	{
		Client.PumpSend(Now);
		Server.PumpReceive(Now);
	}

	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Server.ActivePeerCount(), "Heartbeats keep the peer alive beyond the timeout window");
}

/** Proves a peer that misses heartbeats past the timeout is evicted. */
MW_TEST_CASE(NetHostEvictsPeerAfterTimeout)
{
	FHostLoopback<2, 8, 64> Loopback;
	TNetHost<2, 64> Server(Loopback.Port(0));
	TNetHost<1, 64> Client(Loopback.Port(1));
	(void)Server.Configure(ENetMode::DedicatedServer, MakeHostConfig(1));
	(void)Client.Configure(ENetMode::Client, MakeClientConfig(1, 0));
	(void)Server.Start(0);
	(void)Client.Start(0);
	RunHandshake(Server, Client, 0);
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Server.ActivePeerCount(), "Peer is active right after the handshake");

	// No further client traffic; advance the server past the timeout window.
	Server.PumpReceive(1000);
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Server.ActivePeerCount(), "Peer is evicted after missing heartbeats past the timeout");
}

/** Proves eviction bumps the slot generation so a stale peer id fails after re-admission. */
MW_TEST_CASE(NetHostBumpsGenerationSoStaleIdFailsAfterReadmission)
{
	FHostLoopback<3, 8, 64> Loopback;
	TNetHost<2, 64> Server(Loopback.Port(0));
	TNetHost<1, 64> FirstClient(Loopback.Port(1));
	TNetHost<1, 64> SecondClient(Loopback.Port(2));
	(void)Server.Configure(ENetMode::DedicatedServer, MakeHostConfig(1));
	(void)FirstClient.Configure(ENetMode::Client, MakeClientConfig(1, 0));
	(void)SecondClient.Configure(ENetMode::Client, MakeClientConfig(1, 0));
	(void)Server.Start(0);
	(void)FirstClient.Start(0);

	RunHandshake(Server, FirstClient, 0);
	const FPeerId StaleId = FirstClient.GetAssignedPeer();

	// First client goes silent and times out, freeing and bumping its slot.
	Server.PumpReceive(2000);
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Server.ActivePeerCount(), "First client is evicted after timeout");

	// Second client is admitted into the same freed slot with a bumped generation.
	(void)SecondClient.Start(2000);
	RunHandshake(Server, SecondClient, 2000);
	const FPeerId FreshId = SecondClient.GetAssignedPeer();

	const std::array<std::uint8_t, 1> Payload{0x42};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Invalid,
		Server.SendTo(StaleId, 1, TSpan<const std::uint8_t>(Payload.data(), Payload.size())),
		"A stale peer id no longer matches the reused slot");
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Server.SendTo(FreshId, 1, TSpan<const std::uint8_t>(Payload.data(), Payload.size())),
		"The freshly assigned peer id resolves the reused slot");
}

/** Proves a broadcast reaches every connected remote peer. */
MW_TEST_CASE(NetHostBroadcastReachesEveryConnectedPeer)
{
	FHostLoopback<3, 8, 64> Loopback;
	TNetHost<3, 64> Server(Loopback.Port(0));
	TNetHost<1, 64> FirstClient(Loopback.Port(1));
	TNetHost<1, 64> SecondClient(Loopback.Port(2));
	(void)Server.Configure(ENetMode::DedicatedServer, MakeHostConfig(1));
	(void)FirstClient.Configure(ENetMode::Client, MakeClientConfig(1, 0));
	(void)SecondClient.Configure(ENetMode::Client, MakeClientConfig(1, 0));
	(void)Server.Start(0);
	(void)FirstClient.Start(0);
	(void)SecondClient.Start(0);

	FHandlerCapture FirstCapture{};
	FHandlerCapture SecondCapture{};
	(void)InstallCapture(FirstClient, FirstCapture);
	(void)InstallCapture(SecondClient, SecondCapture);

	RunHandshake(Server, FirstClient, 0);
	RunHandshake(Server, SecondClient, 0);
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Server.ActivePeerCount(), "Both clients are connected before the broadcast");

	const std::array<std::uint8_t, 1> Payload{0x5A};
	MW_EXPECT_EQ(
		Test, ENetResult::Success, Server.Broadcast(1, TSpan<const std::uint8_t>(Payload.data(), Payload.size())), "Broadcast queues for every peer");
	Server.PumpSend(0);
	FirstClient.PumpReceive(0);
	SecondClient.PumpReceive(0);

	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), FirstCapture.Count, "First client receives the broadcast exactly once");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x5A), FirstCapture.FirstByte, "First client sees the broadcast payload");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), SecondCapture.Count, "Second client receives the broadcast exactly once");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x5A), SecondCapture.FirstByte, "Second client sees the broadcast payload");
}

/** Proves SendTo delivers only to the addressed peer on the given channel. */
MW_TEST_CASE(NetHostSendToDeliversToTheAddressedPeer)
{
	FHostLoopback<2, 8, 64> Loopback;
	TNetHost<2, 64> Server(Loopback.Port(0));
	TNetHost<1, 64> Client(Loopback.Port(1));
	(void)Server.Configure(ENetMode::DedicatedServer, MakeHostConfig(1));
	(void)Client.Configure(ENetMode::Client, MakeClientConfig(1, 0));
	(void)Server.Start(0);
	(void)Client.Start(0);

	FHandlerCapture ClientCapture{};
	(void)InstallCapture(Client, ClientCapture);
	RunHandshake(Server, Client, 0);

	const FPeerId ClientId = Client.GetAssignedPeer();
	const std::array<std::uint8_t, 1> Payload{0x33};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Server.SendTo(ClientId, 2, TSpan<const std::uint8_t>(Payload.data(), Payload.size())),
		"SendTo queues to the addressed peer");
	Server.PumpSend(0);
	Client.PumpReceive(0);

	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), ClientCapture.Count, "The addressed peer receives exactly one message");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(2), ClientCapture.Channel, "The message arrives on the requested channel");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x33), ClientCapture.FirstByte, "The message carries the sent payload");
}

/** Proves a listen server dispatches a local-peer message straight to the handler with no driver. */
MW_TEST_CASE(NetHostListenServerDispatchesToLocalPeerWithoutDriver)
{
	FHostLoopback<1, 8, 64> Loopback;
	TNetHost<2, 64> Server(Loopback.Port(0));
	(void)Server.Configure(ENetMode::ListenServer, MakeHostConfig(1));
	(void)Server.Start(0);

	FHandlerCapture LocalCapture{};
	(void)InstallCapture(Server, LocalCapture);

	const std::array<std::uint8_t, 1> Payload{0x77};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Server.SendTo(Server.GetLocalPeer(), 3, TSpan<const std::uint8_t>(Payload.data(), Payload.size())),
		"A local-peer send reports success");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), LocalCapture.Count, "The local peer receives exactly one message, synchronously without a pump");
	MW_EXPECT_EQ(Test, Server.GetLocalPeer(), LocalCapture.From, "The message is attributed to the local peer id");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(3), LocalCapture.Channel, "The local message keeps its channel");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x77), LocalCapture.FirstByte, "The local message keeps its payload");
}

/** Proves a client returns to Connecting when its server stops answering. */
MW_TEST_CASE(NetHostClientReturnsToConnectingOnServerTimeout)
{
	FHostLoopback<2, 8, 64> Loopback;
	TNetHost<2, 64> Server(Loopback.Port(0));
	TNetHost<1, 64> Client(Loopback.Port(1));
	(void)Server.Configure(ENetMode::DedicatedServer, MakeHostConfig(1));
	(void)Client.Configure(ENetMode::Client, MakeClientConfig(1, 0));
	(void)Server.Start(0);
	(void)Client.Start(0);
	RunHandshake(Server, Client, 0);
	MW_EXPECT_EQ(Test, ENetHostState::Connected, Client.GetState(), "Client is connected after the handshake");

	// The server goes silent; advance the client past its timeout window.
	Client.PumpReceive(2000);
	MW_EXPECT_EQ(Test, ENetHostState::Connecting, Client.GetState(), "Client re-enters Connecting when the server times out");
}

/** Proves a repeated Hello from an admitted address re-welcomes without allocating a second slot. */
MW_TEST_CASE(NetHostRepeatedHelloReusesTheSameSlot)
{
	FHostLoopback<2, 8, 64> Loopback;
	TNetHost<2, 64> Server(Loopback.Port(0));
	TNetHost<1, 64> Client(Loopback.Port(1));
	(void)Server.Configure(ENetMode::DedicatedServer, MakeHostConfig(1));
	(void)Client.Configure(ENetMode::Client, MakeClientConfig(1, 0));
	(void)Server.Start(0);
	(void)Client.Start(0);

	// First Hello admits the client; the client never consumes the Welcome, so it re-greets.
	Client.PumpSend(0);
	Server.PumpReceive(0);
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Server.ActivePeerCount(), "First Hello admits the client");

	Client.PumpSend(100);
	Server.PumpReceive(100);
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Server.ActivePeerCount(), "A repeated Hello reuses the slot instead of allocating another");
}

/** Proves a Bye received from a peer frees its slot. */
MW_TEST_CASE(NetHostByeEvictsPeer)
{
	FHostLoopback<2, 8, 64> Loopback;
	TNetHost<2, 64> Server(Loopback.Port(0));
	TNetHost<1, 64> Client(Loopback.Port(1));
	(void)Server.Configure(ENetMode::DedicatedServer, MakeHostConfig(1));
	(void)Client.Configure(ENetMode::Client, MakeClientConfig(1, 0));
	(void)Server.Start(0);
	(void)Client.Start(0);
	RunHandshake(Server, Client, 0);
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Server.ActivePeerCount(), "Peer is active before the Bye");

	// Stop sends a best-effort Bye to the server, then the server processes it.
	Client.Stop();
	Server.PumpReceive(0);
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Server.ActivePeerCount(), "Server frees the peer on Bye");
}

/** Proves a server ignores a Hello whose protocol version does not match. */
MW_TEST_CASE(NetHostIgnoresHelloWithWrongProtocolVersion)
{
	FHostLoopback<2, 8, 64> Loopback;
	TNetHost<2, 64> Server(Loopback.Port(0));
	TNetHost<1, 64> Client(Loopback.Port(1));
	(void)Server.Configure(ENetMode::DedicatedServer, MakeHostConfig(1));
	(void)Client.Configure(ENetMode::Client, MakeClientConfig(2, 0));
	(void)Server.Start(0);
	(void)Client.Start(0);

	Client.PumpSend(0);
	Server.PumpReceive(0);
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Server.ActivePeerCount(), "Server ignores a version-mismatched Hello");

	Server.PumpSend(0);
	Client.PumpReceive(0);
	MW_EXPECT_EQ(Test, ENetHostState::Connecting, Client.GetState(), "The rejected client never leaves Connecting");
}

/** Proves an unknown control type is dropped without admitting a peer. */
MW_TEST_CASE(NetHostDropsUnknownControlMessage)
{
	FHostLoopback<2, 8, 64> Loopback;
	TNetHost<2, 64> Server(Loopback.Port(0));
	(void)Server.Configure(ENetMode::DedicatedServer, MakeHostConfig(1));
	(void)Server.Start(0);

	// Channel 0, zero flags, one payload byte whose control type (0x09) is undefined.
	const std::array<std::uint8_t, 5> Frame{0x00, 0x00, 0x01, 0x00, 0x09};
	(void)Loopback.Port(1).TrySend(MakeLoopbackAddress(0), TSpan<const std::uint8_t>(Frame.data(), Frame.size()));
	Server.PumpReceive(0);

	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Server.ActivePeerCount(), "An unknown control message admits nobody");
}

/** Proves a standalone host originates no traffic and reports Unavailable on send. */
MW_TEST_CASE(NetHostStandaloneReportsUnavailableOnSend)
{
	FHostLoopback<1, 8, 64> Loopback;
	TNetHost<2, 64> Host(Loopback.Port(0));
	(void)Host.Configure(ENetMode::Standalone, MakeHostConfig(1));
	(void)Host.Start(0);

	const std::array<std::uint8_t, 1> Payload{0x01};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Unavailable,
		Host.SendTo(FPeerId{0, 0}, 1, TSpan<const std::uint8_t>(Payload.data(), Payload.size())),
		"Standalone SendTo is Unavailable");
	MW_EXPECT_EQ(
		Test,
		ENetResult::Unavailable,
		Host.Broadcast(1, TSpan<const std::uint8_t>(Payload.data(), Payload.size())),
		"Standalone Broadcast is Unavailable");
	MW_EXPECT_EQ(Test, ENetHostState::Idle, Host.GetState(), "Standalone stays Idle after Start");
}

/** Proves a full client/server session performs no observable heap allocation. */
MW_TEST_CASE(NetHostSessionPerformsNoObservableAllocation)
{
	FHostLoopback<2, 8, 64> Loopback;
	TNetHost<2, 64> Server(Loopback.Port(0));
	TNetHost<1, 64> Client(Loopback.Port(1));
	(void)Server.Configure(ENetMode::DedicatedServer, MakeHostConfig(1));
	(void)Client.Configure(ENetMode::Client, MakeClientConfig(1, 0));

	FHandlerCapture ClientCapture{};
	(void)InstallCapture(Client, ClientCapture);
	(void)Server.Start(0);
	(void)Client.Start(0);

	// Capture the counter only after every fixed-storage object and handler exists.
	const std::uint32_t AllocationsBefore = GlobalAllocationCount;

	RunHandshake(Server, Client, 0);
	const std::array<std::uint8_t, 3> Payload{0x01, 0x02, 0x03};
	(void)Client.SendTo(Client.GetServerPeer(), 1, TSpan<const std::uint8_t>(Payload.data(), Payload.size()));
	Client.PumpSend(100);
	Server.PumpReceive(100);
	(void)Server.Broadcast(1, TSpan<const std::uint8_t>(Payload.data(), Payload.size()));
	Server.PumpSend(100);
	Client.PumpReceive(100);

	const std::uint32_t AllocationsAfter = GlobalAllocationCount;
	MW_EXPECT_EQ(Test, AllocationsBefore, AllocationsAfter, "A full TNetHost session must not allocate");
}

/** Proves a dedicated server has no local peer: a local send is rejected and dispatches nothing. */
MW_TEST_CASE(NetHostDedicatedServerHasNoLocalDispatch)
{
	FHostLoopback<1, 8, 64> Loopback;
	TNetHost<2, 64> Server(Loopback.Port(0));
	(void)Server.Configure(ENetMode::DedicatedServer, MakeHostConfig(1));
	(void)Server.Start(0);

	FHandlerCapture Capture{};
	(void)InstallCapture(Server, Capture);

	const std::array<std::uint8_t, 1> Payload{0x11};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Invalid,
		Server.SendTo(Server.GetLocalPeer(), 1, TSpan<const std::uint8_t>(Payload.data(), Payload.size())),
		"A dedicated server rejects a send to the local peer");
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Server.Broadcast(1, TSpan<const std::uint8_t>(Payload.data(), Payload.size())),
		"Broadcast with no peers succeeds");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Capture.Count, "A dedicated server performs no local dispatch");
}

/**
 * Always-ready driver that counts receive calls, so a test can prove one pump is bounded.
 *
 * Every `TryReceive` delivers a minimal empty control frame and reports `Success`, so a
 * `PumpReceive` would loop forever were it not bounded; `TrySend` is a no-op success.
 */
class FFloodDriver final : public INetDriver
{
public:
	/** Accepts and discards every send. */
	ENetResult TrySend(const FNetAddress&, TSpan<const std::uint8_t>) noexcept override { return ENetResult::Success; }

	/** Delivers one empty control frame per call and counts the call. */
	ENetResult TryReceive(FNetAddress& OutFrom, TSpan<std::uint8_t> Destination, FNetReceiveResult& OutResult) noexcept override
	{
		if (Destination.Size() < 4)
		{
			return ENetResult::Unavailable;
		}
		++ReceiveCallCount;
		OutFrom = MakeLoopbackAddress(9);
		Destination[0] = 0;
		Destination[1] = 0;
		Destination[2] = 0;
		Destination[3] = 0;
		OutResult.BytesReceived = 4;
		return ENetResult::Success;
	}

	/** Reports the fixed maximum packet size. */
	std::size_t MaxPacketBytes() const noexcept override { return 64; }

	/** Counts how many receives one or more pumps have requested. */
	std::size_t ReceiveCallCount{0};
};

/** Proves one PumpReceive processes at most MaxPeers + 4 receives, so a flood cannot starve the frame. */
MW_TEST_CASE(NetHostPumpReceiveIsBoundedUnderFlood)
{
	FFloodDriver Driver;
	TNetHost<3, 64> Server(Driver);
	(void)Server.Configure(ENetMode::DedicatedServer, MakeHostConfig(1));
	(void)Server.Start(0);

	Server.PumpReceive(0);

	// MaxPeers (3) + 4 = 7 receives is the per-pump bound, even though the driver never runs dry.
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(7), Driver.ReceiveCallCount, "One pump is bounded to MaxPeers + 4 receives");
}

} // namespace
