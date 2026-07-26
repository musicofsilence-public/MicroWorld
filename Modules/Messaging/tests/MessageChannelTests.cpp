#include "TestSupport.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessageChannelBinding.h>
#include <MicroWorld/Messaging/MessageRouter.h>
#include <MicroWorld/Engine/EngineSystem.h>
#include <MicroWorld/Messaging/ReliableChannel.h>
#include <MicroWorld/Net/HostLoopback.h>
#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetHost.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/Net/PacketDropDriver.h>
#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/ObjectPtr.h>
#include <MicroWorld/Time.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{
using MicroWorld::BroadcastActorId;
using MicroWorld::DurationMilliseconds;
using MicroWorld::EChannelSendTarget;
using MicroWorld::EEngineResult;
using MicroWorld::EMessageResult;
using MicroWorld::ENetHostState;
using MicroWorld::ENetMode;
using MicroWorld::ENetResult;
using MicroWorld::ERuntimeResult;
using MicroWorld::FDefaultEngineTraits;
using MicroWorld::FGarbageCollectionBudget;
using MicroWorld::FMessageActorId;
using MicroWorld::FMessageChannelId;
using MicroWorld::FMessageHandlerBinding;
using MicroWorld::FMessageHandlerHandle;
using MicroWorld::FMessageTypeId;
using MicroWorld::FMessageView;
using MicroWorld::FNetHostConfig;
using MicroWorld::FPacketDropDriver;
using MicroWorld::FReliableChannelConfig;
using MicroWorld::IEncodedMessageSink;
using MicroWorld::LocalChannelId;
using MicroWorld::MakeLoopbackAddress;
using MicroWorld::ReliableHeaderBytes;
using MicroWorld::TEngine;
using MicroWorld::TEngineSystemSet;
using MicroWorld::THostLoopback;
using MicroWorld::TimePointMilliseconds;
using MicroWorld::TMessageChannelBinding;
using MicroWorld::TMessageRouter;
using MicroWorld::TNetHost;
using MicroWorld::TNetHostSystem;
using MicroWorld::TReliableChannel;
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

/** Router-facing channel id the multi-channel cases bind their telemetry wire to; equals AppChannelId since both name "the first channel". */
constexpr FMessageChannelId TelemetryChannelId = AppChannelId;

/** Router-facing channel id the multi-channel cases bind their command wire to; distinct from TelemetryChannelId so the router holds two channels. */
constexpr FMessageChannelId CommandChannelId = 2;

/** Wire-level channel byte the multi-channel cases' telemetry hosts read and write; equals AppWireChannelByte since it is that same single-channel
 * byte. */
constexpr std::uint8_t TelemetryWireChannelByte = AppWireChannelByte;

/** Wire-level channel byte the multi-channel cases' command hosts read and write; a different network to Telemetry's, so the value need not differ,
 * but a distinct one keeps captured logs unambiguous. */
constexpr std::uint8_t CommandWireChannelByte = 6;

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
using FNetFrame = TNetHostSystem<FNet>;

/** The channel binding under test, duck-typed on FNet. */
using FBinding = TMessageChannelBinding<FNet>;

/** Carries the exact capacities FHost sized before the traits refactor, so the test store is unchanged. */
struct FHostTraits : FDefaultEngineTraits
{
	static constexpr std::size_t MaxClasses = 6;
	static constexpr std::size_t MaxObjects = 8;
	static constexpr std::size_t SlotSizeBytes = 256;
	static constexpr std::size_t MaxRoots = 1;
	static constexpr std::size_t MaxActors = 2;
	static constexpr std::size_t MaxTimers = 4;
};

/** Engine profile sized for a bare rooted world; these cases never spawn actors. */
using FHost = TEngine<FHostTraits>;

/** Per-side D3 composition root: holds one side's net frame and message router behind the one IEngineSystem slot TEngine drives. */
using FFrameSet = TEngineSystemSet<2>;

/** Per-side D3 composition root for the multi-channel cases: two net frames (telemetry, command) plus the one router that binds both. */
using FMultiChannelFrameSet = TEngineSystemSet<3>;

/** Router profile shared by every case; its capacities are generous headroom, never the behavior under test. */
using FTestRouter = TMessageRouter<HandlerCapacity, OutboundQueueCapacity, MessageByteCapacity, ChannelCapacity>;

/** Channel capacity for the multi-channel cases: exactly Telemetry + Command, the roadmap 4.2 scenario under test. */
constexpr std::size_t MultiChannelCapacity = 2;

/** One router driving two wired channels (roadmap 4.2): otherwise identical profile to FTestRouter, sized only for its extra channel slot. */
using FMultiChannelRouter = TMessageRouter<HandlerCapacity, OutboundQueueCapacity, MessageByteCapacity, MultiChannelCapacity>;

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
	void Record(const FMessageView& InView) noexcept
	{
		bWasCalled = true;
		MessageTypeId = InView.Header.MessageTypeId;
		TargetActorId = InView.Header.TargetActorId;
		SenderActorId = InView.Header.SenderActorId;
		ArrivedOnChannelId = InView.ArrivedOnChannelId;
		PayloadLength = InView.Payload.Size();
		for (std::size_t Index = 0; Index < InView.Payload.Size() && Index < MaxPayloadBytes; ++Index)
		{
			PayloadBytes[Index] = InView.Payload.Data()[Index];
		}
	}
};

/** Binds a nothrow inline handler that records every delivered view into Recorder. */
FMessageHandlerBinding MakeRecordingHandler(FDeliveredMessageRecord& InRecorder) noexcept
{
	FMessageHandlerBinding Delegate;
	(void)Delegate.Bind([&InRecorder](const FMessageView& InView) noexcept { InRecorder.Record(InView); });
	return Delegate;
}

/**
 * Buckets delivered views by ArrivedOnChannelId into one of two FDeliveredMessageRecords, so a single
 * handler registered once on a multi-channel router can still prove per-channel isolation: a view that
 * arrived on TelemetryChannelId only ever writes Telemetry, one that arrived on CommandChannelId only
 * ever writes Command, so the recorded payload in either slot can only be its own channel's payload.
 */
struct FChannelKeyedMessageRecords final
{
	/** Written only by a view whose ArrivedOnChannelId equals TelemetryChannelId. */
	FDeliveredMessageRecord Telemetry;

	/** Written only by a view whose ArrivedOnChannelId equals CommandChannelId. */
	FDeliveredMessageRecord Command;

	/** Routes View into the slot matching its arrival channel; any other channel id is left unrecorded (never expected in this suite). */
	void RecordByArrivalChannel(const FMessageView& InView) noexcept
	{
		if (InView.ArrivedOnChannelId == TelemetryChannelId)
		{
			Telemetry.Record(InView);
		}
		else if (InView.ArrivedOnChannelId == CommandChannelId)
		{
			Command.Record(InView);
		}
	}
};

/** Binds a nothrow inline handler that buckets every delivered view into Recorder's channel-keyed slot. */
FMessageHandlerBinding MakeChannelKeyedRecordingHandler(FChannelKeyedMessageRecords& InRecorder) noexcept
{
	FMessageHandlerBinding Delegate;
	(void)Delegate.Bind([&InRecorder](const FMessageView& InView) noexcept { InRecorder.RecordByArrivalChannel(InView); });
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
	void SetRejectInbound(const bool bInReject) noexcept { bRejectInbound = bInReject; }

	/** Reports how many ReceiveEncodedMessage calls this stub has observed, accepted or not. */
	std::size_t ReceivedCallCount() const noexcept { return CallCount; }

	/** Records the call and reports CapacityExceeded while rejecting, Success otherwise. */
	EMessageResult ReceiveEncodedMessage(const FMessageChannelId InArrivedOnChannelId, const TSpan<const std::uint8_t> InEncoded) noexcept override
	{
		(void)InArrivedOnChannelId;
		(void)InEncoded;
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
 * Minimal application-layer stand-in for the reliable-channel integration case's server side:
 * counts every payload a TReliableChannel forwards once its own ack/duplicate handling has run,
 * so delivered-count can be compared against sender count without a full router and handler.
 */
class FRecordingReliableForwardSink final : public IEncodedMessageSink
{
public:
	/** Reports how many payloads have been forwarded so far. */
	std::size_t ForwardedCount() const noexcept { return CallCount; }

	/** Records the call and always accepts. */
	EMessageResult ReceiveEncodedMessage(const FMessageChannelId InArrivedOnChannelId, const TSpan<const std::uint8_t> InEncoded) noexcept override
	{
		(void)InArrivedOnChannelId;
		(void)InEncoded;
		++CallCount;
		return EMessageResult::Success;
	}

private:
	/** Total ReceiveEncodedMessage calls observed so far. */
	std::size_t CallCount{0};
};

/**
 * Runs one side's frame-driven pump for one tick. Each side's TEngine is bound to an
 * FFrameSet holding that side's net frame and message router (net added first, router added
 * last per the D3 recipe), so this single Tick call already dispatches the net frame then the
 * router (inbound) and flushes the router then the net frame (outbound) in the right order.
 */
void PumpSide(FHost& InHost, const TimePointMilliseconds InNowMilliseconds) noexcept
{
	(void)InHost.Tick(InNowMilliseconds);
}

/** Drives both sides through PumpSide until the client's NetHost reports Connected or the frame budget runs out, mirroring EngineNetHostTests.cpp's
 * handshake loop. */
TimePointMilliseconds ConnectClientToServer(
	FHost& InClientHost, FNet& InClientNet, FHost& InServerHost, TimePointMilliseconds InNowMilliseconds) noexcept
{
	for (int Frame = 0; Frame < MaxHandshakeFrames && InClientNet.GetState() != ENetHostState::Connected; ++Frame)
	{
		InNowMilliseconds += FrameStepMilliseconds;
		PumpSide(InClientHost, InNowMilliseconds);
		PumpSide(InServerHost, InNowMilliseconds);
	}
	return InNowMilliseconds;
}

/**
 * Extends ConnectClientToServer to two independent wires (roadmap 4.2's telemetry + command networks):
 * waits for BOTH client nets to report Connected, since each side's one Host.Tick already pumps both
 * of that side's net frames through its frame set.
 */
TimePointMilliseconds ConnectClientToServerOverTwoWires(
	FHost& InClientHost, FNet& InClientNetA, FNet& InClientNetB, FHost& InServerHost, TimePointMilliseconds InNowMilliseconds) noexcept
{
	for (int Frame = 0;
		 Frame < MaxHandshakeFrames && (InClientNetA.GetState() != ENetHostState::Connected || InClientNetB.GetState() != ENetHostState::Connected);
		 ++Frame)
	{
		InNowMilliseconds += FrameStepMilliseconds;
		PumpSide(InClientHost, InNowMilliseconds);
		PumpSide(InServerHost, InNowMilliseconds);
	}
	return InNowMilliseconds;
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

/**
 * Roadmap 4.2: one router per side drives two independent wires (telemetry + command) behind one
 * TEngineSystemSet<3>. A message sent on each channel must arrive tagged with that channel's own id
 * and never bleed into the other's record (proven below by each record only ever holding its own
 * distinct payload byte), and both must arrive within one post-send frame per side.
 */
MW_TEST_CASE(EngineMessageChannel_MultiChannelIsolationDeliversBothInOneFrame)
{
	THostLoopback<2, 8, 64> TelemetryNetwork;
	THostLoopback<2, 8, 64> CommandNetwork;
	FNet TelemetryServerNet(TelemetryNetwork.Port(0));
	FNet TelemetryClientNet(TelemetryNetwork.Port(1));
	FNet CommandServerNet(CommandNetwork.Port(0));
	FNet CommandClientNet(CommandNetwork.Port(1));
	FNetFrame TelemetryServerFrame{TelemetryServerNet};
	FNetFrame TelemetryClientFrame{TelemetryClientNet};
	FNetFrame CommandServerFrame{CommandServerNet};
	FNetFrame CommandClientFrame{CommandClientNet};
	FMultiChannelRouter ClientRouter;
	FMultiChannelRouter ServerRouter;

	FMultiChannelFrameSet ServerSet;
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		ServerSet.Add(TelemetryServerFrame),
		"The server's frame set must accept the telemetry net frame first (D3 order)");
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		ServerSet.Add(CommandServerFrame),
		"The server's frame set must accept the command net frame second (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ServerSet.Add(ServerRouter), "The server's frame set must accept its router last (D3 order)");
	FMultiChannelFrameSet ClientSet;
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		ClientSet.Add(TelemetryClientFrame),
		"The client's frame set must accept the telemetry net frame first (D3 order)");
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		ClientSet.Add(CommandClientFrame),
		"The client's frame set must accept the command net frame second (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ClientSet.Add(ClientRouter), "The client's frame set must accept its router last (D3 order)");
	FHost ServerHost{FGarbageCollectionBudget{1, 4, 8}, ServerSet};
	FHost ClientHost{FGarbageCollectionBudget{1, 4, 8}, ClientSet};

	FBinding TelemetryClientBinding(TelemetryClientNet, TelemetryWireChannelByte, TelemetryChannelId, EChannelSendTarget::Server, ClientRouter);
	FBinding TelemetryServerBinding(TelemetryServerNet, TelemetryWireChannelByte, TelemetryChannelId, EChannelSendTarget::AllPeers, ServerRouter);
	FBinding CommandClientBinding(CommandClientNet, CommandWireChannelByte, CommandChannelId, EChannelSendTarget::Server, ClientRouter);
	FBinding CommandServerBinding(CommandServerNet, CommandWireChannelByte, CommandChannelId, EChannelSendTarget::AllPeers, ServerRouter);

	MW_EXPECT_TRUE(Test, TelemetryClientBinding.IsAttached(), "The telemetry client binding must register its inbound handler");
	MW_EXPECT_TRUE(Test, TelemetryServerBinding.IsAttached(), "The telemetry server binding must register its inbound handler");
	MW_EXPECT_TRUE(Test, CommandClientBinding.IsAttached(), "The command client binding must register its inbound handler");
	MW_EXPECT_TRUE(Test, CommandServerBinding.IsAttached(), "The command server binding must register its inbound handler");
	MW_EXPECT_SUCCESS(Test, ClientRouter.AddChannel(TelemetryClientBinding), "The client router must accept its telemetry channel");
	MW_EXPECT_SUCCESS(
		Test, ClientRouter.AddChannel(CommandClientBinding), "The client router must accept its command channel as a second, distinct channel id");
	MW_EXPECT_SUCCESS(Test, ServerRouter.AddChannel(TelemetryServerBinding), "The server router must accept its telemetry channel");
	MW_EXPECT_SUCCESS(
		Test, ServerRouter.AddChannel(CommandServerBinding), "The server router must accept its command channel as a second, distinct channel id");

	FChannelKeyedMessageRecords ServerRecords;
	FMessageHandlerHandle ServerHandle{};
	MW_EXPECT_SUCCESS(
		Test,
		ServerRouter.AddMessageHandler(TestMessageType, BroadcastActorId, MakeChannelKeyedRecordingHandler(ServerRecords), ServerHandle),
		"The server must register one handler shared by both channels before the client sends, bucketing by arrival channel");

	MW_EXPECT_TRUE(Test, ServerHost.CreateWorld().Get() != nullptr, "The server roots its world before ticking");
	MW_EXPECT_TRUE(Test, ClientHost.CreateWorld().Get() != nullptr, "The client roots its world before ticking");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ServerHost.BeginPlay(0), "The server world begins play at the baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ClientHost.BeginPlay(0), "The client world begins play at the baseline");

	FNetHostConfig TelemetryClientConfig = MakeConfig();
	TelemetryClientConfig.ServerAddress = MakeLoopbackAddress(0);
	FNetHostConfig CommandClientConfig = MakeConfig();
	CommandClientConfig.ServerAddress = MakeLoopbackAddress(0);
	(void)TelemetryServerNet.Configure(ENetMode::DedicatedServer, MakeConfig());
	(void)TelemetryClientNet.Configure(ENetMode::Client, TelemetryClientConfig);
	(void)CommandServerNet.Configure(ENetMode::DedicatedServer, MakeConfig());
	(void)CommandClientNet.Configure(ENetMode::Client, CommandClientConfig);
	(void)TelemetryServerNet.Start(0);
	(void)TelemetryClientNet.Start(0);
	(void)CommandServerNet.Start(0);
	(void)CommandClientNet.Start(0);

	const TimePointMilliseconds ConnectedAt = ConnectClientToServerOverTwoWires(ClientHost, TelemetryClientNet, CommandClientNet, ServerHost, 0);
	MW_EXPECT_EQ(
		Test, ENetHostState::Connected, TelemetryClientNet.GetState(), "The telemetry wire must connect through the frame-set-driven pump order");
	MW_EXPECT_EQ(
		Test, ENetHostState::Connected, CommandClientNet.GetState(), "The command wire must connect through the frame-set-driven pump order");

	const std::array<std::uint8_t, 1> TelemetryPayload{0xAA};
	const std::array<std::uint8_t, 1> CommandPayload{0xBB};
	MW_EXPECT_SUCCESS(
		Test,
		ClientRouter.BroadcastMessage(TelemetryChannelId, TestMessageType, TestSenderActorId, TSpan<const std::uint8_t>(TelemetryPayload.data(), 1)),
		"A connected client must queue a broadcast on its telemetry channel");
	MW_EXPECT_SUCCESS(
		Test,
		ClientRouter.BroadcastMessage(CommandChannelId, TestMessageType, TestSenderActorId, TSpan<const std::uint8_t>(CommandPayload.data(), 1)),
		"A connected client must queue a broadcast on its command channel");

	const TimePointMilliseconds DeliveredAt = ConnectedAt + FrameStepMilliseconds;
	PumpSide(ClientHost, DeliveredAt);
	PumpSide(ServerHost, DeliveredAt);

	MW_EXPECT_TRUE(Test, ServerRecords.Telemetry.bWasCalled, "One post-send frame per side must deliver the telemetry message");
	MW_EXPECT_TRUE(Test, ServerRecords.Command.bWasCalled, "That same one post-send frame per side must also deliver the command message");
	MW_EXPECT_EQ(
		Test, TelemetryChannelId, ServerRecords.Telemetry.ArrivedOnChannelId, "The telemetry message must arrive tagged with its own channel id");
	MW_EXPECT_EQ(Test, CommandChannelId, ServerRecords.Command.ArrivedOnChannelId, "The command message must arrive tagged with its own channel id");
	MW_EXPECT_EQ(
		Test,
		std::uint8_t{0xAA},
		ServerRecords.Telemetry.PayloadBytes[0],
		"The record keyed by channel 1 must hold only the telemetry payload, never the command payload: proves no cross-channel bleed");
	MW_EXPECT_EQ(
		Test,
		std::uint8_t{0xBB},
		ServerRecords.Command.PayloadBytes[0],
		"The record keyed by channel 2 must hold only the command payload, never the telemetry payload: proves no cross-channel bleed");
}

/**
 * Roadmap 4.2's accepted v1 caveat: TMessageRouter has ONE shared outbound queue (see its PostAdvance),
 * so a stalled channel at the head retains that head and blocks every later entry for the whole tick,
 * even one queued for an otherwise healthy channel - matching TNetManager::AdvanceSend's retained-head
 * discipline from Task 2.2. To drive the stall deterministically in one flush: TNetHost::SendTo only
 * queues into its own fixed-size outbound FIFO (TNetManager) and never touches the driver at queue
 * time (see NetHost.h/NetManager.h), so filling the loopback mailbox alone cannot make a single SendTo
 * call observe Full. This case therefore (1) fills the client's own command-wire TNetHost FIFO to
 * capacity via direct SendTo calls bypassing the router, so the very next SendTo the router's flush
 * issues is guaranteed to see Full, and (2) additionally fills the server's command mailbox directly
 * via THostLoopback (per MailboxCapacityValue()), so the stall also reflects a genuinely unreachable
 * peer rather than only a local queue.
 */
MW_TEST_CASE(EngineMessageChannel_StalledChannelRetainsRouterHead)
{
	THostLoopback<2, 8, 64> TelemetryNetwork;
	THostLoopback<2, 8, 64> CommandNetwork;
	FNet TelemetryServerNet(TelemetryNetwork.Port(0));
	FNet TelemetryClientNet(TelemetryNetwork.Port(1));
	FNet CommandServerNet(CommandNetwork.Port(0));
	FNet CommandClientNet(CommandNetwork.Port(1));
	FNetFrame TelemetryServerFrame{TelemetryServerNet};
	FNetFrame TelemetryClientFrame{TelemetryClientNet};
	FNetFrame CommandServerFrame{CommandServerNet};
	FNetFrame CommandClientFrame{CommandClientNet};
	FMultiChannelRouter ClientRouter;
	FMultiChannelRouter ServerRouter;

	FMultiChannelFrameSet ServerSet;
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		ServerSet.Add(TelemetryServerFrame),
		"The server's frame set must accept the telemetry net frame first (D3 order)");
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		ServerSet.Add(CommandServerFrame),
		"The server's frame set must accept the command net frame second (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ServerSet.Add(ServerRouter), "The server's frame set must accept its router last (D3 order)");
	FMultiChannelFrameSet ClientSet;
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		ClientSet.Add(TelemetryClientFrame),
		"The client's frame set must accept the telemetry net frame first (D3 order)");
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		ClientSet.Add(CommandClientFrame),
		"The client's frame set must accept the command net frame second (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ClientSet.Add(ClientRouter), "The client's frame set must accept its router last (D3 order)");
	FHost ServerHost{FGarbageCollectionBudget{1, 4, 8}, ServerSet};
	FHost ClientHost{FGarbageCollectionBudget{1, 4, 8}, ClientSet};

	FBinding TelemetryClientBinding(TelemetryClientNet, TelemetryWireChannelByte, TelemetryChannelId, EChannelSendTarget::Server, ClientRouter);
	FBinding TelemetryServerBinding(TelemetryServerNet, TelemetryWireChannelByte, TelemetryChannelId, EChannelSendTarget::AllPeers, ServerRouter);
	FBinding CommandClientBinding(CommandClientNet, CommandWireChannelByte, CommandChannelId, EChannelSendTarget::Server, ClientRouter);
	FBinding CommandServerBinding(CommandServerNet, CommandWireChannelByte, CommandChannelId, EChannelSendTarget::AllPeers, ServerRouter);

	MW_EXPECT_TRUE(Test, TelemetryClientBinding.IsAttached(), "The telemetry client binding must register its inbound handler");
	MW_EXPECT_TRUE(Test, TelemetryServerBinding.IsAttached(), "The telemetry server binding must register its inbound handler");
	MW_EXPECT_TRUE(Test, CommandClientBinding.IsAttached(), "The command client binding must register its inbound handler");
	MW_EXPECT_TRUE(Test, CommandServerBinding.IsAttached(), "The command server binding must register its inbound handler");
	MW_EXPECT_SUCCESS(Test, ClientRouter.AddChannel(TelemetryClientBinding), "The client router must accept its telemetry channel");
	MW_EXPECT_SUCCESS(
		Test, ClientRouter.AddChannel(CommandClientBinding), "The client router must accept its command channel as a second, distinct channel id");
	MW_EXPECT_SUCCESS(Test, ServerRouter.AddChannel(TelemetryServerBinding), "The server router must accept its telemetry channel");
	MW_EXPECT_SUCCESS(
		Test, ServerRouter.AddChannel(CommandServerBinding), "The server router must accept its command channel as a second, distinct channel id");

	MW_EXPECT_TRUE(Test, ServerHost.CreateWorld().Get() != nullptr, "The server roots its world before ticking");
	MW_EXPECT_TRUE(Test, ClientHost.CreateWorld().Get() != nullptr, "The client roots its world before ticking");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ServerHost.BeginPlay(0), "The server world begins play at the baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ClientHost.BeginPlay(0), "The client world begins play at the baseline");

	FNetHostConfig TelemetryClientConfig = MakeConfig();
	TelemetryClientConfig.ServerAddress = MakeLoopbackAddress(0);
	FNetHostConfig CommandClientConfig = MakeConfig();
	CommandClientConfig.ServerAddress = MakeLoopbackAddress(0);
	(void)TelemetryServerNet.Configure(ENetMode::DedicatedServer, MakeConfig());
	(void)TelemetryClientNet.Configure(ENetMode::Client, TelemetryClientConfig);
	(void)CommandServerNet.Configure(ENetMode::DedicatedServer, MakeConfig());
	(void)CommandClientNet.Configure(ENetMode::Client, CommandClientConfig);
	(void)TelemetryServerNet.Start(0);
	(void)TelemetryClientNet.Start(0);
	(void)CommandServerNet.Start(0);
	(void)CommandClientNet.Start(0);

	const TimePointMilliseconds ConnectedAt = ConnectClientToServerOverTwoWires(ClientHost, TelemetryClientNet, CommandClientNet, ServerHost, 0);
	MW_EXPECT_EQ(Test, ENetHostState::Connected, CommandClientNet.GetState(), "The command wire must connect before its outbound FIFO can be primed");

	// auto: this test names no Net peer-id type, matching TMessageChannelBinding::TrySendEncodedMessage's own convention.
	const auto CommandServerPeer = CommandClientNet.GetServerPeer();
	MW_EXPECT_TRUE(Test, CommandServerPeer.IsValid(), "The client must resolve its server peer on the command wire before priming");

	// (1) Prime the client's own command-wire TNetHost outbound FIFO to exactly its capacity via raw
	// SendTo calls that bypass the router entirely; no Tick runs between these calls, so nothing drains
	// and no heartbeat timer advances.
	const std::array<std::uint8_t, 1> FifoPrimerPayload{0xF0};
	for (std::size_t Index = 0; Index < FNet::SendQueueDepth; ++Index)
	{
		MW_EXPECT_EQ(
			Test,
			ENetResult::Success,
			CommandClientNet.SendTo(CommandServerPeer, CommandWireChannelByte, TSpan<const std::uint8_t>(FifoPrimerPayload.data(), 1)),
			"Priming the client's own outbound FIFO must succeed while it still has a free slot");
	}

	// (2) Fill the server's command mailbox directly (bypassing both routers and both TNetHosts), one
	// raw packet at a time, until one more than MailboxCapacityValue() reports Full.
	const std::array<std::uint8_t, 1> MailboxFillerPayload{0xF1};
	for (std::size_t Index = 0; Index < CommandNetwork.MailboxCapacityValue(); ++Index)
	{
		MW_EXPECT_EQ(
			Test,
			ENetResult::Success,
			CommandNetwork.Port(1).TrySend(MakeLoopbackAddress(0), TSpan<const std::uint8_t>(MailboxFillerPayload.data(), 1)),
			"Each raw packet up to the server's command mailbox capacity must be accepted");
	}
	MW_EXPECT_EQ(
		Test,
		ENetResult::Full,
		CommandNetwork.Port(1).TrySend(MakeLoopbackAddress(0), TSpan<const std::uint8_t>(MailboxFillerPayload.data(), 1)),
		"One packet beyond MailboxCapacityValue() must report Full: the server's command mailbox is now saturated");

	// Queue the stalled channel's message first (the router's next head), then a healthy channel's
	// message right behind it.
	const std::array<std::uint8_t, 1> StalledPayload{0xC0};
	const std::array<std::uint8_t, 1> HealthyPayload{0xC1};
	MW_EXPECT_SUCCESS(
		Test,
		ClientRouter.SendMessageToActor(
			CommandChannelId, TestMessageType, BroadcastActorId, TestSenderActorId, TSpan<const std::uint8_t>(StalledPayload.data(), 1)),
		"Queuing succeeds regardless of transport state: the router's own outbound queue is independent of the driver");
	MW_EXPECT_SUCCESS(
		Test,
		ClientRouter.SendMessageToActor(
			TelemetryChannelId, TestMessageType, BroadcastActorId, TestSenderActorId, TSpan<const std::uint8_t>(HealthyPayload.data(), 1)),
		"Queuing the healthy channel's message behind the stalled one must also succeed");
	MW_EXPECT_EQ(Test, std::size_t{2}, ClientRouter.QueuedOutboundCount(), "Both messages must be queued before the flush under test");

	// Only the client is pumped: the server side is irrelevant to what the client's own router head
	// does, and never receiving anything is exactly the point of a stalled wire.
	const TimePointMilliseconds FlushAt = ConnectedAt + FrameStepMilliseconds;
	PumpSide(ClientHost, FlushAt);

	MW_EXPECT_EQ(
		Test,
		std::size_t{2},
		ClientRouter.QueuedOutboundCount(),
		"Accepted v1 cross-channel head-of-line caveat: a stalled channel at the head of the router's one shared outbound queue retains both "
		"itself and the healthy channel's message queued behind it, because TMessageRouter::PostAdvance stops the whole tick on the first "
		"non-Success send (matching TNetManager::AdvanceSend's retained-head discipline from Task 2.2)");
}

/**
 * Roadmap 5.2 integration case: the client wraps its wire binding in a TReliableChannel and its own
 * driver in FPacketDropDriver{3} (Task 5.1's loss injector); the server also wraps its binding in a
 * TReliableChannel (so acks and inbound both flow through the reliable wire format) whose forward
 * sink is a plain recording stub rather than a full router+handler - the simpler wiring the brief
 * allows, since counting deliveries needs no message-type dispatch. Every message the client sends
 * must still reach the server exactly once despite the injected drops, and at least one resend must
 * have fired, proving the retry/ack/dedup logic recovers from real loss end to end.
 */
MW_TEST_CASE(EngineMessageChannel_ReliableChannelSurvivesPacketDropsDeliveringExactlyOnce)
{
	constexpr std::uint32_t DropEveryNthSend = 3;
	constexpr std::size_t MessagesToSend = 6;
	constexpr DurationMilliseconds ReliableRetryIntervalMilliseconds = 50;
	constexpr std::uint8_t ReliableMaxSendAttempts = 5;
	constexpr int MaxFramesPerMessage = ReliableMaxSendAttempts + 3;
	constexpr std::size_t ReliableSlotBytes = MessageByteCapacity + ReliableHeaderBytes;
	using FReliableChannel = TReliableChannel<4, ReliableSlotBytes>;
	using FClientFrameSet = TEngineSystemSet<3>;
	using FServerFrameSet = TEngineSystemSet<2>;

	THostLoopback<2, 8, 64> Network;
	FPacketDropDriver ClientDropDriver(Network.Port(1), DropEveryNthSend);
	FNet ServerNet(Network.Port(0));
	FNet ClientNet(ClientDropDriver);
	FNetFrame ServerFrame{ServerNet};
	FNetFrame ClientFrame{ClientNet};

	FTestRouter ClientRouter;
	FRecordingReliableForwardSink ServerForwardSink;

	FReliableChannelConfig ReliableConfig{};
	ReliableConfig.RetryIntervalMilliseconds = ReliableRetryIntervalMilliseconds;
	ReliableConfig.MaxSendAttempts = ReliableMaxSendAttempts;
	FReliableChannel ClientReliable(ClientRouter, ReliableConfig);
	FReliableChannel ServerReliable(ServerForwardSink, ReliableConfig);

	FBinding ClientBinding(ClientNet, AppWireChannelByte, AppChannelId, EChannelSendTarget::Server, ClientReliable);
	FBinding ServerBinding(ServerNet, AppWireChannelByte, AppChannelId, EChannelSendTarget::AllPeers, ServerReliable);
	ClientReliable.SetInnerChannel(ClientBinding);
	ServerReliable.SetInnerChannel(ServerBinding);

	MW_EXPECT_TRUE(Test, ClientBinding.IsAttached(), "The client binding must register its inbound handler");
	MW_EXPECT_TRUE(Test, ServerBinding.IsAttached(), "The server binding must register its inbound handler");
	MW_EXPECT_SUCCESS(Test, ClientRouter.AddChannel(ClientReliable), "The client router must accept its guaranteed channel");

	FClientFrameSet ClientSet;
	MW_EXPECT_EQ(Test, EEngineResult::Success, ClientSet.Add(ClientFrame), "The client's frame set must accept its net frame first (4.4 order)");
	MW_EXPECT_EQ(
		Test, EEngineResult::Success, ClientSet.Add(ClientReliable), "The client's frame set must accept its reliable channel second (4.4 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ClientSet.Add(ClientRouter), "The client's frame set must accept its router last (4.4 order)");
	FServerFrameSet ServerSet;
	MW_EXPECT_EQ(Test, EEngineResult::Success, ServerSet.Add(ServerFrame), "The server's frame set must accept its net frame first (4.4 order)");
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		ServerSet.Add(ServerReliable),
		"The server's frame set must accept its reliable channel second; this side needs no router");

	FHost ServerHost{FGarbageCollectionBudget{1, 4, 8}, ServerSet};
	FHost ClientHost{FGarbageCollectionBudget{1, 4, 8}, ClientSet};
	MW_EXPECT_TRUE(Test, ServerHost.CreateWorld().Get() != nullptr, "The server roots its world before ticking");
	MW_EXPECT_TRUE(Test, ClientHost.CreateWorld().Get() != nullptr, "The client roots its world before ticking");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ServerHost.BeginPlay(0), "The server world begins play at the baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ClientHost.BeginPlay(0), "The client world begins play at the baseline");

	// A heartbeat/timeout window far longer than this case's whole run keeps every raw send call
	// attributable to the client's one-time Hello plus the reliable channel's own Data/retry traffic,
	// so the deterministic every-3rd-call drop is guaranteed to land on a message send at some point.
	FNetHostConfig SharedConfig = MakeConfig();
	SharedConfig.HeartbeatIntervalMilliseconds = 1'000'000;
	SharedConfig.PeerTimeoutMilliseconds = 2'000'000;
	FNetHostConfig ClientConfig = SharedConfig;
	ClientConfig.ServerAddress = MakeLoopbackAddress(0);
	(void)ServerNet.Configure(ENetMode::DedicatedServer, SharedConfig);
	(void)ClientNet.Configure(ENetMode::Client, ClientConfig);
	(void)ServerNet.Start(0);
	(void)ClientNet.Start(0);

	TimePointMilliseconds Now = ConnectClientToServer(ClientHost, ClientNet, ServerHost, 0);
	MW_EXPECT_EQ(Test, ENetHostState::Connected, ClientNet.GetState(), "The client must connect before the drop-injected sends begin");

	for (std::size_t MessageIndex = 0; MessageIndex < MessagesToSend; ++MessageIndex)
	{
		const std::array<std::uint8_t, 1> Payload{static_cast<std::uint8_t>(MessageIndex)};
		MW_EXPECT_SUCCESS(
			Test,
			ClientRouter.SendMessageToActor(
				AppChannelId, TestMessageType, TestListenerActorId, TestSenderActorId, TSpan<const std::uint8_t>(Payload.data(), 1)),
			"Queuing one guaranteed message must succeed");

		for (int Frame = 0; Frame < MaxFramesPerMessage; ++Frame)
		{
			Now += ReliableRetryIntervalMilliseconds;
			PumpSide(ClientHost, Now);
			PumpSide(ServerHost, Now);
			if (ClientReliable.PendingCount() == 0)
			{
				break;
			}
		}
		MW_EXPECT_EQ(Test, std::size_t{0}, ClientReliable.PendingCount(), "Every message must be fully acknowledged before the next one is queued");
	}

	MW_EXPECT_EQ(
		Test,
		MessagesToSend,
		ServerForwardSink.ForwardedCount(),
		"Every message the client sent must be delivered to the server exactly once despite the injected drops");
	MW_EXPECT_TRUE(
		Test, ClientReliable.ResentCount() > 0, "At least one message must have been resent because FPacketDropDriver actually dropped a send");
}

} // namespace
