#include "TestSupport.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/Message.h>
#include <MicroWorld/Engine/MessageChannelBinding.h>
#include <MicroWorld/Engine/MessageRouter.h>
#include <MicroWorld/Engine/NetworkFrame.h>
#include <MicroWorld/Net/HostLoopback.h>
#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetHost.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/ObjectPtr.h>
#include <MicroWorld/Time.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{
using MicroWorld::BroadcastActorId;
using MicroWorld::EChannelSendTarget;
using MicroWorld::EEngineResult;
using MicroWorld::EMessageResult;
using MicroWorld::ENetHostState;
using MicroWorld::ENetMode;
using MicroWorld::ENetResult;
using MicroWorld::ERuntimeResult;
using MicroWorld::FGarbageCollectionBudget;
using MicroWorld::FMessageActorId;
using MicroWorld::FMessageChannelId;
using MicroWorld::FMessageHandlerBinding;
using MicroWorld::FMessageHandlerHandle;
using MicroWorld::FMessageTypeId;
using MicroWorld::FMessageView;
using MicroWorld::FNetHostConfig;
using MicroWorld::IEncodedMessageSink;
using MicroWorld::LocalChannelId;
using MicroWorld::MakeLoopbackAddress;
using MicroWorld::TEngineHost;
using MicroWorld::THostLoopback;
using MicroWorld::TimePointMilliseconds;
using MicroWorld::TMessageChannelBinding;
using MicroWorld::TMessageRouter;
using MicroWorld::TNetHost;
using MicroWorld::TNetHostFrame;
using MicroWorld::TNetworkFrameSet;
using MicroWorld::TSpan;

/** Asserts a messaging operation returned Success without discarding the result. */
#define MW_EXPECT_SUCCESS(TestContext, Result, Message) MW_EXPECT_EQ(TestContext, EMessageResult::Success, Result, Message)

/** Wall-clock step every simulated frame advances by; must be nonzero so Tick never rejects it as non-monotonic. */
constexpr TimePointMilliseconds FrameStepMilliseconds = 10;

/** Upper bound on frames spent waiting for the client Hello/Welcome handshake before a test gives up. */
constexpr int MaxHandshakeFrames = 8;

/** The router-facing channel id every case binds; kept numerically distinct from AppWireChannelByte so the two id spaces read as separate concepts.
 */
constexpr FMessageChannelId AppChannelId = 1;

/** The TNetHost wire-level channel byte the bindings under test read and write. */
constexpr std::uint8_t AppWireChannelByte = 5;

/** A second wire-level channel byte on the same hosts, used only to prove a binding ignores traffic addressed to some other channel. */
constexpr std::uint8_t ForeignWireChannelByte = 9;

/** Message type shared by every case; its value is arbitrary and only needs to be nonzero. */
constexpr FMessageTypeId TestMessageType = 42;

/** Actor id a targeted send names as its listener, matched against a handler registered under the same id.
 *  Named distinctly from TMessageRouter's own ListenerActorId parameter so this file-scope constant never shadows it. */
constexpr FMessageActorId TestListenerActorId = 7;

/** Actor id recorded as every test message's sender.
 *  Named distinctly from TMessageRouter's own SenderActorId parameter so this file-scope constant never shadows it. */
constexpr FMessageActorId TestSenderActorId = 3;

/** Capacities shared by every router instance in this suite; each case constructs its own fresh router, sized with generous headroom. */
constexpr std::size_t HandlerCapacity = 2;
constexpr std::size_t OutboundQueueCapacity = 4;
constexpr std::size_t MessageByteCapacity = 32;
constexpr std::size_t ChannelCapacity = 1;

/** The network host type every case wires a channel binding to. */
using FNet = TNetHost<2, 64>;

/** Adapts FNet to the engine's per-frame network slot, matching EngineNetHostTests.cpp's wiring. */
using FNetFrame = TNetHostFrame<FNet>;

/** The channel binding under test, duck-typed on FNet. */
using FBinding = TMessageChannelBinding<FNet>;

/** Engine host profile sized for a bare rooted world; these cases never spawn actors. */
using FHost = TEngineHost<6, 8, 256, 16, 1, 2, 4, 64>;

/** Per-side D3 composition root: holds one side's net frame and message router behind the one INetworkFrame slot TEngineHost drives. */
using FFrameSet = TNetworkFrameSet<2>;

/** Router profile shared by every case; its capacities are generous headroom, never the behavior under test. */
using FTestRouter = TMessageRouter<HandlerCapacity, OutboundQueueCapacity, MessageByteCapacity, ChannelCapacity>;

/** Builds the shared fast-heartbeat, short-timeout config every case's hosts use for deterministic frames. */
FNetHostConfig MakeConfig() noexcept
{
	FNetHostConfig Config{};
	Config.HeartbeatIntervalMilliseconds = 100;
	Config.PeerTimeoutMilliseconds = 500;
	Config.ProtocolVersion = 1;
	return Config;
}

/** Captures one delivered message's full header, arrival channel, and payload bytes for later assertion. */
struct FDeliveredMessageRecord final
{
	/** Bounds the payload copy so this fixture stays fixed-size like the production types it observes. */
	static constexpr std::size_t MaxPayloadBytes = 8;

	/** Distinguishes "never invoked" from a delivery whose fields all happen to be zero. */
	bool bWasCalled{false};

	/** The delivered view's MessageTypeId. */
	FMessageTypeId MessageTypeId{0};

	/** The delivered view's TargetActorId. */
	FMessageActorId TargetActorId{0};

	/** The delivered view's SenderActorId. */
	FMessageActorId SenderActorId{0};

	/** The channel id the router recorded as this message's arrival channel. */
	FMessageChannelId ArrivedOnChannelId{LocalChannelId};

	/** Number of valid bytes at the front of PayloadBytes. */
	std::size_t PayloadLength{0};

	/** Copy of the delivered payload, truncated to MaxPayloadBytes. */
	std::uint8_t PayloadBytes[MaxPayloadBytes]{};

	/** Records one delivered view's header, arrival channel, and payload for a later assertion. */
	void Record(const FMessageView& View) noexcept
	{
		bWasCalled = true;
		MessageTypeId = View.Header.MessageTypeId;
		TargetActorId = View.Header.TargetActorId;
		SenderActorId = View.Header.SenderActorId;
		ArrivedOnChannelId = View.ArrivedOnChannelId;
		PayloadLength = View.Payload.Size();
		for (std::size_t Index = 0; Index < View.Payload.Size() && Index < MaxPayloadBytes; ++Index)
		{
			PayloadBytes[Index] = View.Payload.Data()[Index];
		}
	}
};

/** Binds a nothrow inline handler that records every delivered view into Recorder. */
FMessageHandlerBinding MakeRecordingHandler(FDeliveredMessageRecord& Recorder) noexcept
{
	FMessageHandlerBinding Delegate;
	(void)Delegate.Bind([&Recorder](const FMessageView& View) noexcept { Recorder.Record(View); });
	return Delegate;
}

/**
 * A minimal IEncodedMessageSink test double that can be toggled to reject every inbound message, isolating
 * TMessageChannelBinding::DroppedInboundCount from a real router's own queue mechanics.
 */
class FToggleableSink final : public IEncodedMessageSink
{
public:
	/** Selects whether the next ReceiveEncodedMessage calls accept or reject their message. */
	void SetRejectInbound(const bool bReject) noexcept { bRejectInbound = bReject; }

	/** Reports how many ReceiveEncodedMessage calls this stub has observed, accepted or not. */
	std::size_t ReceivedCallCount() const noexcept { return CallCount; }

	/** Records the call and reports CapacityExceeded while rejecting, Success otherwise. */
	EMessageResult ReceiveEncodedMessage(const FMessageChannelId ArrivedOnChannelId, const TSpan<const std::uint8_t> Encoded) noexcept override
	{
		(void)ArrivedOnChannelId;
		(void)Encoded;
		++CallCount;
		return bRejectInbound ? EMessageResult::CapacityExceeded : EMessageResult::Success;
	}

private:
	/** Selects this stub's next scripted outcome. */
	bool bRejectInbound{false};

	/** Total ReceiveEncodedMessage calls observed, accepted or not. */
	std::size_t CallCount{0};
};

/**
 * Runs one side's frame-driven pump for one tick. Each side's TEngineHost is bound to an
 * FFrameSet holding that side's net frame and message router (net added first, router added
 * last per the D3 recipe), so this single Tick call already dispatches the net frame then the
 * router (inbound) and flushes the router then the net frame (outbound) in the right order.
 */
void PumpSide(FHost& Host, const TimePointMilliseconds NowMilliseconds) noexcept
{
	(void)Host.Tick(NowMilliseconds);
}

/** Drives both sides through PumpSide until the client's NetHost reports Connected or the frame budget runs out, mirroring EngineNetHostTests.cpp's
 * handshake loop. */
TimePointMilliseconds ConnectClientToServer(FHost& ClientHost, FNet& ClientNet, FHost& ServerHost, TimePointMilliseconds NowMilliseconds) noexcept
{
	for (int Frame = 0; Frame < MaxHandshakeFrames && ClientNet.GetState() != ENetHostState::Connected; ++Frame)
	{
		NowMilliseconds += FrameStepMilliseconds;
		PumpSide(ClientHost, NowMilliseconds);
		PumpSide(ServerHost, NowMilliseconds);
	}
	return NowMilliseconds;
}

/** Client-to-server targeted delivery: SendMessageToActor on the bound channel reaches only the server's matching handler. */
MW_TEST_CASE(EngineMessageChannel_ClientToServerTargetedSendReachesServerHandler)
{
	THostLoopback<2, 8, 64> Network;
	FNet ServerNet(Network.Port(0));
	FNet ClientNet(Network.Port(1));
	FNetFrame ServerFrame{ServerNet};
	FNetFrame ClientFrame{ClientNet};
	FTestRouter ClientRouter;
	FTestRouter ServerRouter;
	FFrameSet ServerSet;
	MW_EXPECT_EQ(Test, EEngineResult::Success, ServerSet.Add(ServerFrame), "The server's frame set must accept its net frame first (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ServerSet.Add(ServerRouter), "The server's frame set must accept its router last (D3 order)");
	FFrameSet ClientSet;
	MW_EXPECT_EQ(Test, EEngineResult::Success, ClientSet.Add(ClientFrame), "The client's frame set must accept its net frame first (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ClientSet.Add(ClientRouter), "The client's frame set must accept its router last (D3 order)");
	FHost ServerHost{FGarbageCollectionBudget{1, 4, 8}, ServerSet};
	FHost ClientHost{FGarbageCollectionBudget{1, 4, 8}, ClientSet};
	FBinding ClientBinding(ClientNet, AppWireChannelByte, AppChannelId, EChannelSendTarget::Server, ClientRouter);
	FBinding ServerBinding(ServerNet, AppWireChannelByte, AppChannelId, EChannelSendTarget::AllPeers, ServerRouter);

	MW_EXPECT_TRUE(Test, ClientBinding.IsAttached(), "The client binding must register its inbound handler");
	MW_EXPECT_TRUE(Test, ServerBinding.IsAttached(), "The server binding must register its inbound handler");
	MW_EXPECT_SUCCESS(Test, ClientRouter.AddChannel(ClientBinding), "The client router must accept its wired channel");
	MW_EXPECT_SUCCESS(Test, ServerRouter.AddChannel(ServerBinding), "The server router must accept its wired channel");

	FDeliveredMessageRecord ServerRecord;
	FMessageHandlerHandle ServerHandle{};
	MW_EXPECT_SUCCESS(
		Test,
		ServerRouter.AddMessageHandler(TestMessageType, TestListenerActorId, MakeRecordingHandler(ServerRecord), ServerHandle),
		"The server must register its listener before the client sends");

	MW_EXPECT_TRUE(Test, ServerHost.CreateWorld().Get() != nullptr, "The server roots its world before ticking");
	MW_EXPECT_TRUE(Test, ClientHost.CreateWorld().Get() != nullptr, "The client roots its world before ticking");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ServerHost.BeginPlay(0), "The server world begins play at the baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ClientHost.BeginPlay(0), "The client world begins play at the baseline");

	FNetHostConfig ClientConfig = MakeConfig();
	ClientConfig.ServerAddress = MakeLoopbackAddress(0);
	(void)ServerNet.Configure(ENetMode::DedicatedServer, MakeConfig());
	(void)ClientNet.Configure(ENetMode::Client, ClientConfig);
	(void)ServerNet.Start(0);
	(void)ClientNet.Start(0);

	const TimePointMilliseconds ConnectedAt = ConnectClientToServer(ClientHost, ClientNet, ServerHost, 0);
	MW_EXPECT_EQ(Test, ENetHostState::Connected, ClientNet.GetState(), "The client must connect through the frame-set-driven pump order");

	const std::array<std::uint8_t, 1> Payload{0x11};
	MW_EXPECT_SUCCESS(
		Test,
		ClientRouter.SendMessageToActor(
			AppChannelId, TestMessageType, TestListenerActorId, TestSenderActorId, TSpan<const std::uint8_t>(Payload.data(), 1)),
		"A connected client must queue a targeted send on its wired channel");

	const TimePointMilliseconds DeliveredAt = ConnectedAt + FrameStepMilliseconds;
	PumpSide(ClientHost, DeliveredAt);
	PumpSide(ServerHost, DeliveredAt);

	MW_EXPECT_TRUE(Test, ServerRecord.bWasCalled, "The server handler must receive the client's targeted message");
	MW_EXPECT_EQ(Test, TestMessageType, ServerRecord.MessageTypeId, "The delivered view must carry the original message type");
	MW_EXPECT_EQ(Test, TestListenerActorId, ServerRecord.TargetActorId, "The delivered view must carry the original target actor");
	MW_EXPECT_EQ(Test, TestSenderActorId, ServerRecord.SenderActorId, "The delivered view must carry the original sender actor");
	MW_EXPECT_EQ(Test, AppChannelId, ServerRecord.ArrivedOnChannelId, "The delivered view must report the channel it arrived on");
	MW_EXPECT_EQ(Test, std::size_t{1}, ServerRecord.PayloadLength, "The delivered view must carry the original payload length");
	MW_EXPECT_EQ(Test, std::uint8_t{0x11}, ServerRecord.PayloadBytes[0], "The delivered view must carry the original payload byte");
}

/** Server-to-client broadcast delivery: BroadcastMessage on the bound channel reaches the client's handler. */
MW_TEST_CASE(EngineMessageChannel_ServerBroadcastReachesClientHandler)
{
	THostLoopback<2, 8, 64> Network;
	FNet ServerNet(Network.Port(0));
	FNet ClientNet(Network.Port(1));
	FNetFrame ServerFrame{ServerNet};
	FNetFrame ClientFrame{ClientNet};
	FTestRouter ClientRouter;
	FTestRouter ServerRouter;
	FFrameSet ServerSet;
	MW_EXPECT_EQ(Test, EEngineResult::Success, ServerSet.Add(ServerFrame), "The server's frame set must accept its net frame first (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ServerSet.Add(ServerRouter), "The server's frame set must accept its router last (D3 order)");
	FFrameSet ClientSet;
	MW_EXPECT_EQ(Test, EEngineResult::Success, ClientSet.Add(ClientFrame), "The client's frame set must accept its net frame first (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ClientSet.Add(ClientRouter), "The client's frame set must accept its router last (D3 order)");
	FHost ServerHost{FGarbageCollectionBudget{1, 4, 8}, ServerSet};
	FHost ClientHost{FGarbageCollectionBudget{1, 4, 8}, ClientSet};
	FBinding ClientBinding(ClientNet, AppWireChannelByte, AppChannelId, EChannelSendTarget::Server, ClientRouter);
	FBinding ServerBinding(ServerNet, AppWireChannelByte, AppChannelId, EChannelSendTarget::AllPeers, ServerRouter);

	MW_EXPECT_TRUE(Test, ClientBinding.IsAttached(), "The client binding must register its inbound handler");
	MW_EXPECT_TRUE(Test, ServerBinding.IsAttached(), "The server binding must register its inbound handler");
	MW_EXPECT_SUCCESS(Test, ClientRouter.AddChannel(ClientBinding), "The client router must accept its wired channel");
	MW_EXPECT_SUCCESS(Test, ServerRouter.AddChannel(ServerBinding), "The server router must accept its wired channel");

	FDeliveredMessageRecord ClientRecord;
	FMessageHandlerHandle ClientHandle{};
	MW_EXPECT_SUCCESS(
		Test,
		ClientRouter.AddMessageHandler(TestMessageType, BroadcastActorId, MakeRecordingHandler(ClientRecord), ClientHandle),
		"The client must register a broadcast listener before the server sends");

	MW_EXPECT_TRUE(Test, ServerHost.CreateWorld().Get() != nullptr, "The server roots its world before ticking");
	MW_EXPECT_TRUE(Test, ClientHost.CreateWorld().Get() != nullptr, "The client roots its world before ticking");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ServerHost.BeginPlay(0), "The server world begins play at the baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ClientHost.BeginPlay(0), "The client world begins play at the baseline");

	FNetHostConfig ClientConfig = MakeConfig();
	ClientConfig.ServerAddress = MakeLoopbackAddress(0);
	(void)ServerNet.Configure(ENetMode::DedicatedServer, MakeConfig());
	(void)ClientNet.Configure(ENetMode::Client, ClientConfig);
	(void)ServerNet.Start(0);
	(void)ClientNet.Start(0);

	const TimePointMilliseconds ConnectedAt = ConnectClientToServer(ClientHost, ClientNet, ServerHost, 0);
	MW_EXPECT_EQ(Test, ENetHostState::Connected, ClientNet.GetState(), "The client must connect through the frame-set-driven pump order");

	const std::array<std::uint8_t, 1> Payload{0x22};
	MW_EXPECT_SUCCESS(
		Test,
		ServerRouter.BroadcastMessage(AppChannelId, TestMessageType, TestSenderActorId, TSpan<const std::uint8_t>(Payload.data(), 1)),
		"A server with one active peer must queue a broadcast on its wired channel");

	const TimePointMilliseconds DeliveredAt = ConnectedAt + FrameStepMilliseconds;
	PumpSide(ServerHost, DeliveredAt);
	PumpSide(ClientHost, DeliveredAt);

	MW_EXPECT_TRUE(Test, ClientRecord.bWasCalled, "The client handler must receive the server's broadcast message");
	MW_EXPECT_EQ(Test, TestMessageType, ClientRecord.MessageTypeId, "The delivered view must carry the original message type");
	MW_EXPECT_EQ(Test, BroadcastActorId, ClientRecord.TargetActorId, "A broadcast's delivered view must target every subscriber");
	MW_EXPECT_EQ(Test, TestSenderActorId, ClientRecord.SenderActorId, "The delivered view must carry the original sender actor");
	MW_EXPECT_EQ(Test, AppChannelId, ClientRecord.ArrivedOnChannelId, "The delivered view must report the channel it arrived on");
	MW_EXPECT_EQ(Test, std::uint8_t{0x22}, ClientRecord.PayloadBytes[0], "The delivered view must carry the original payload byte");
}

/** A message sent on a different wire-channel byte than the binding's own must never reach that binding's sink. */
MW_TEST_CASE(EngineMessageChannel_ForeignWireChannelNeverReachesBoundSink)
{
	THostLoopback<2, 8, 64> Network;
	FNet ServerNet(Network.Port(0));
	FNet ClientNet(Network.Port(1));
	FNetFrame ServerFrame{ServerNet};
	FNetFrame ClientFrame{ClientNet};
	FTestRouter ServerRouter;
	FFrameSet ServerSet;
	MW_EXPECT_EQ(Test, EEngineResult::Success, ServerSet.Add(ServerFrame), "The server's frame set must accept its net frame first (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ServerSet.Add(ServerRouter), "The server's frame set must accept its router last (D3 order)");
	FHost ServerHost{FGarbageCollectionBudget{1, 4, 8}, ServerSet};
	// The client in this case has no router at all (it sends raw wire bytes directly below), so it
	// keeps the bare net frame instead of a frame set.
	FHost ClientHost{FGarbageCollectionBudget{1, 4, 8}, ClientFrame};
	FBinding ServerBinding(ServerNet, AppWireChannelByte, AppChannelId, EChannelSendTarget::AllPeers, ServerRouter);

	MW_EXPECT_TRUE(Test, ServerBinding.IsAttached(), "The server binding must register its inbound handler");
	MW_EXPECT_SUCCESS(Test, ServerRouter.AddChannel(ServerBinding), "The server router must accept its wired channel");

	FDeliveredMessageRecord ServerRecord;
	FMessageHandlerHandle ServerHandle{};
	MW_EXPECT_SUCCESS(
		Test,
		ServerRouter.AddMessageHandler(TestMessageType, BroadcastActorId, MakeRecordingHandler(ServerRecord), ServerHandle),
		"The server must register a listener that a foreign-channel message must never reach");

	MW_EXPECT_TRUE(Test, ServerHost.CreateWorld().Get() != nullptr, "The server roots its world before ticking");
	MW_EXPECT_TRUE(Test, ClientHost.CreateWorld().Get() != nullptr, "The client roots its world before ticking");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ServerHost.BeginPlay(0), "The server world begins play at the baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ClientHost.BeginPlay(0), "The client world begins play at the baseline");

	FNetHostConfig ClientConfig = MakeConfig();
	ClientConfig.ServerAddress = MakeLoopbackAddress(0);
	(void)ServerNet.Configure(ENetMode::DedicatedServer, MakeConfig());
	(void)ClientNet.Configure(ENetMode::Client, ClientConfig);
	(void)ServerNet.Start(0);
	(void)ClientNet.Start(0);

	// The client sends raw wire bytes directly (bypassing any router), so only the server binding's
	// own channel filter is exercised; the client side needs no router pumped alongside its tick.
	TimePointMilliseconds Now = 0;
	for (int Frame = 0; Frame < MaxHandshakeFrames && ClientNet.GetState() != ENetHostState::Connected; ++Frame)
	{
		Now += FrameStepMilliseconds;
		(void)ClientHost.Tick(Now);
		PumpSide(ServerHost, Now);
	}
	MW_EXPECT_EQ(Test, ENetHostState::Connected, ClientNet.GetState(), "The client must connect before sending the foreign-channel message");

	const std::array<std::uint8_t, 1> Payload{0x33};
	const ENetResult SendResult = ClientNet.SendTo(ClientNet.GetServerPeer(), ForeignWireChannelByte, TSpan<const std::uint8_t>(Payload.data(), 1));
	MW_EXPECT_EQ(Test, ENetResult::Success, SendResult, "A connected client can queue raw bytes on any non-zero wire channel");

	Now += FrameStepMilliseconds;
	(void)ClientHost.Tick(Now);
	PumpSide(ServerHost, Now);

	MW_EXPECT_TRUE(Test, !ServerRecord.bWasCalled, "A message on a foreign wire channel must never reach this binding's sink");
	MW_EXPECT_EQ(Test, std::size_t{0}, ServerRouter.QueuedInboundCount(), "The router's inbound queue must stay empty for a filtered message");
	MW_EXPECT_EQ(
		Test,
		std::uint32_t{0},
		ServerBinding.DroppedInboundCount(),
		"The channel filter runs before the sink is consulted, so a foreign-channel message is never counted as dropped");
}

/** Sending before any server peer is connected reports Unavailable and the router retains the message; it flows once the client connects. */
MW_TEST_CASE(EngineMessageChannel_SendBeforeConnectReportsUnavailableThenDeliversAfterConnect)
{
	THostLoopback<2, 8, 64> Network;
	FNet ServerNet(Network.Port(0));
	FNet ClientNet(Network.Port(1));
	FNetFrame ServerFrame{ServerNet};
	FNetFrame ClientFrame{ClientNet};
	FTestRouter ClientRouter;
	FTestRouter ServerRouter;
	FFrameSet ServerSet;
	MW_EXPECT_EQ(Test, EEngineResult::Success, ServerSet.Add(ServerFrame), "The server's frame set must accept its net frame first (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ServerSet.Add(ServerRouter), "The server's frame set must accept its router last (D3 order)");
	FFrameSet ClientSet;
	MW_EXPECT_EQ(Test, EEngineResult::Success, ClientSet.Add(ClientFrame), "The client's frame set must accept its net frame first (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ClientSet.Add(ClientRouter), "The client's frame set must accept its router last (D3 order)");
	FHost ServerHost{FGarbageCollectionBudget{1, 4, 8}, ServerSet};
	FHost ClientHost{FGarbageCollectionBudget{1, 4, 8}, ClientSet};
	FBinding ClientBinding(ClientNet, AppWireChannelByte, AppChannelId, EChannelSendTarget::Server, ClientRouter);
	FBinding ServerBinding(ServerNet, AppWireChannelByte, AppChannelId, EChannelSendTarget::AllPeers, ServerRouter);

	MW_EXPECT_TRUE(Test, ClientBinding.IsAttached(), "The client binding must register its inbound handler");
	MW_EXPECT_TRUE(Test, ServerBinding.IsAttached(), "The server binding must register its inbound handler");
	MW_EXPECT_SUCCESS(Test, ClientRouter.AddChannel(ClientBinding), "The client router must accept its wired channel");
	MW_EXPECT_SUCCESS(Test, ServerRouter.AddChannel(ServerBinding), "The server router must accept its wired channel");

	FDeliveredMessageRecord ServerRecord;
	FMessageHandlerHandle ServerHandle{};
	MW_EXPECT_SUCCESS(
		Test,
		ServerRouter.AddMessageHandler(TestMessageType, BroadcastActorId, MakeRecordingHandler(ServerRecord), ServerHandle),
		"The server must register its listener before the client sends");

	MW_EXPECT_TRUE(Test, ServerHost.CreateWorld().Get() != nullptr, "The server roots its world before ticking");
	MW_EXPECT_TRUE(Test, ClientHost.CreateWorld().Get() != nullptr, "The client roots its world before ticking");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ServerHost.BeginPlay(0), "The server world begins play at the baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ClientHost.BeginPlay(0), "The client world begins play at the baseline");

	FNetHostConfig ClientConfig = MakeConfig();
	ClientConfig.ServerAddress = MakeLoopbackAddress(0);
	(void)ServerNet.Configure(ENetMode::DedicatedServer, MakeConfig());
	(void)ClientNet.Configure(ENetMode::Client, ClientConfig);
	(void)ServerNet.Start(0);
	(void)ClientNet.Start(0);

	const std::array<std::uint8_t, 1> Payload{0x44};
	MW_EXPECT_SUCCESS(
		Test,
		ClientRouter.BroadcastMessage(AppChannelId, TestMessageType, TestSenderActorId, TSpan<const std::uint8_t>(Payload.data(), 1)),
		"Queuing succeeds before connect: the router's own outbound queue is independent of the transport");

	TimePointMilliseconds Now = FrameStepMilliseconds;
	PumpSide(ClientHost, Now);
	MW_EXPECT_EQ(
		Test,
		std::size_t{1},
		ClientRouter.QueuedOutboundCount(),
		"Unavailable (no server peer yet) must retain the queued message instead of dropping it");
	MW_EXPECT_TRUE(Test, !ServerRecord.bWasCalled, "Nothing can have arrived before the client even connects");

	// Under the old manual pump order the router flushed before that same frame's engine tick, so the
	// connecting frame that flipped the peer to Connected still saw the pre-flip state; a further pump
	// was needed after ConnectClientToServer returned. The frame set instead flushes the router right
	// after the tick's own dispatch step (both inside one Host.Tick), so the very connecting frame whose
	// dispatch admits the client also flushes and delivers the retained message within that same
	// ConnectClientToServer iteration - one frame earlier than before.
	Now = ConnectClientToServer(ClientHost, ClientNet, ServerHost, Now);
	MW_EXPECT_EQ(Test, ENetHostState::Connected, ClientNet.GetState(), "The client must connect through the frame-set-driven pump order");
	MW_EXPECT_EQ(
		Test,
		std::size_t{0},
		ClientRouter.QueuedOutboundCount(),
		"The set's flush runs right after the same tick's dispatch flips the peer to Connected, so the retained message is already sent "
		"by the time the handshake loop reports Connected");
	MW_EXPECT_TRUE(
		Test,
		ServerRecord.bWasCalled,
		"That same connecting frame's server tick both receives the wire packet and dispatches it to the handler, so delivery is already "
		"complete once the handshake loop returns");
	MW_EXPECT_EQ(Test, TestMessageType, ServerRecord.MessageTypeId, "The delivered view must carry the original message type");
	MW_EXPECT_EQ(Test, std::uint8_t{0x44}, ServerRecord.PayloadBytes[0], "The delivered view must carry the original payload byte");
}

/** A rejecting sink increments TMessageChannelBinding::DroppedInboundCount while leaving the binding otherwise usable. */
MW_TEST_CASE(EngineMessageChannel_RejectingSinkIncrementsDroppedInboundCount)
{
	// A listen server's Broadcast dispatches to its own local peer synchronously (TNetHost::SendToLocalPeer),
	// so this case never crosses the loopback network and needs no engine tick or pumping at all.
	THostLoopback<1, 4, 64> Network;
	FNet ListenServerHost(Network.Port(0));
	MW_EXPECT_EQ(
		Test, ENetResult::Success, ListenServerHost.Configure(ENetMode::ListenServer, MakeConfig()), "Configuring an idle host must succeed");
	MW_EXPECT_EQ(Test, ENetResult::Success, ListenServerHost.Start(0), "Starting an idle host must succeed");

	FToggleableSink Sink;
	FBinding Binding(ListenServerHost, AppWireChannelByte, AppChannelId, EChannelSendTarget::AllPeers, Sink);
	MW_EXPECT_TRUE(Test, Binding.IsAttached(), "The binding must register its inbound handler");

	const std::array<std::uint8_t, 1> Payload{0x5A};
	const TSpan<const std::uint8_t> PayloadView(Payload.data(), Payload.size());

	Sink.SetRejectInbound(false);
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		ListenServerHost.Broadcast(AppWireChannelByte, PayloadView),
		"A listen server always accepts its own local-peer broadcast");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, Binding.DroppedInboundCount(), "An accepted sink call must not count as dropped");

	Sink.SetRejectInbound(true);
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		ListenServerHost.Broadcast(AppWireChannelByte, PayloadView),
		"The transport still succeeds; only the sink rejects");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, Binding.DroppedInboundCount(), "A rejecting sink must increment DroppedInboundCount");

	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		ListenServerHost.Broadcast(AppWireChannelByte, PayloadView),
		"A second rejected broadcast must still be accepted by the transport");
	MW_EXPECT_EQ(Test, std::uint32_t{2}, Binding.DroppedInboundCount(), "A second rejection must climb the counter again, staying consistent");
	MW_EXPECT_EQ(Test, std::size_t{3}, Sink.ReceivedCallCount(), "All three broadcasts must reach the sink after passing the channel filter");
}

} // namespace
