#include "TestSupport.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessageChannelBinding.h>
#include <MicroWorld/Messaging/MessageRouter.h>
#include <MicroWorld/Engine/EngineSystem.h>
#include <MicroWorld/Messaging/ReliableChannel.h>
#include <MicroWorld/Transport/HostLoopback.h>
#include <MicroWorld/Transport/DeviceAddress.h>
#include <MicroWorld/Transport/TransportHost.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Transport/PacketDropDevice.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Core/Time.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{
using MicroWorld::DurationMilliseconds;
using MicroWorld::EEngineResult;
using MicroWorld::ERuntimeResult;
using MicroWorld::FDefaultEngineTraits;
using MicroWorld::FGarbageCollectionBudget;
using MicroWorld::TEngine;
using MicroWorld::THostPlaySystem;
using MicroWorld::TimePointMilliseconds;
using MicroWorld::TPlaySystemSet;
using MicroWorld::TSpan;
using MicroWorld::Messaging::BroadcastActorId;
using MicroWorld::Messaging::EChannelSendTarget;
using MicroWorld::Messaging::EMessageResult;
using MicroWorld::Messaging::FMessageActorId;
using MicroWorld::Messaging::FMessageChannelId;
using MicroWorld::Messaging::FMessageHandlerBinding;
using MicroWorld::Messaging::FMessageHandlerHandle;
using MicroWorld::Messaging::FMessageTypeId;
using MicroWorld::Messaging::FMessageView;
using MicroWorld::Messaging::FReliableChannelConfig;
using MicroWorld::Messaging::IEncodedMessageSink;
using MicroWorld::Messaging::LocalChannelId;
using MicroWorld::Messaging::ReliableHeaderBytes;
using MicroWorld::Messaging::TMessageChannelBinding;
using MicroWorld::Messaging::TMessageRouter;
using MicroWorld::Messaging::TReliableChannel;
using MicroWorld::Transport::ENetworkMode;
using MicroWorld::Transport::ETransportHostState;
using MicroWorld::Transport::ETransportResult;
using MicroWorld::Transport::FPacketDropDevice;
using MicroWorld::Transport::FTransportHostConfig;
using MicroWorld::Transport::THostLoopback;
using MicroWorld::Transport::TTransportHost;
using MicroWorld::Transport::Address::MakeLoopbackAddress;

/** Asserts a messaging operation returned Success without discarding the result. */
#define MW_EXPECT_SUCCESS(TestContext, Result, Message) MW_EXPECT_EQ(TestContext, EMessageResult::Success, Result, Message)

/** Wall-clock step every simulated frame advances by; must be nonzero so Tick never rejects it as non-monotonic. */
constexpr TimePointMilliseconds FrameStepMilliseconds = 10;

/** Upper bound on frames spent waiting for the client Hello/Welcome handshake before a test gives up. */
constexpr int MaxHandshakeFrames = 8;

/** The router-facing channel id every case binds; kept numerically distinct from AppWireChannelByte so the two id spaces read as separate concepts.
 */
constexpr FMessageChannelId AppChannelId = 1;

/** The TTransportHost wire-level channel byte the bindings under test read and write. */
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

/** GC budget every host world is rooted with; generous headroom since these cases never spawn actors, so the values are not under test. */
constexpr std::uint32_t TestRootOperations = 1;
constexpr std::uint32_t TestMarkOperations = 4;
constexpr std::uint32_t TestSweepOperations = 8;

/** The network host type every case wires a channel binding to. */
using FTransport = TTransportHost<2, 64>;

/** Adapts FTransport to the engine's per-frame network slot, matching EngineHostTests.cpp's wiring. */
using FHostPlay = THostPlaySystem<FTransport>;

/** The channel binding under test, duck-typed on FTransport. */
using FBinding = TMessageChannelBinding<FTransport>;

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

/** Per-side D3 composition root: holds one side's host play system and message router behind the one IPlaySystem slot TEngine drives. */
using FFrameSet = TPlaySystemSet<2>;

/** Per-side D3 composition root for the multi-channel cases: two host play systems (telemetry, command) plus the one router that binds both. */
using FMultiChannelFrameSet = TPlaySystemSet<3>;

/** Router profile shared by every case; its capacities are generous headroom, never the behavior under test. */
using FTestRouter = TMessageRouter<HandlerCapacity, OutboundQueueCapacity, MessageByteCapacity, ChannelCapacity>;

/** Channel capacity for the multi-channel cases: exactly Telemetry + Command, the roadmap 4.2 scenario under test. */
constexpr std::size_t MultiChannelCapacity = 2;

/** One router driving two wired channels (roadmap 4.2): otherwise identical profile to FTestRouter, sized only for its extra channel slot. */
using FMultiChannelRouter = TMessageRouter<HandlerCapacity, OutboundQueueCapacity, MessageByteCapacity, MultiChannelCapacity>;

/** Builds the shared fast-heartbeat, short-timeout config every case's hosts use for deterministic frames. */
FTransportHostConfig MakeConfig() noexcept
{
	FTransportHostConfig Config{};
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
 * FFrameSet holding that side's host play system and message router (transport added first, router added
 * last per the D3 recipe), so this single Tick call already dispatches the host play system then the
 * router (inbound) and flushes the router then the host play system (outbound) in the right order.
 */
void PumpSide(FHost& InHost, const TimePointMilliseconds InNowMilliseconds) noexcept
{
	(void)InHost.Tick(InNowMilliseconds);
}

/** Drives both sides through PumpSide until the client's TransportHost reports Connected or the frame budget runs out, mirroring
 * EngineHostTests.cpp's handshake loop. */
TimePointMilliseconds ConnectClientToServer(
	FHost& InClientHost, FTransport& InClientTransport, FHost& InServerHost, TimePointMilliseconds InNowMilliseconds) noexcept
{
	bool bClientConnected = InClientTransport.GetState() == ETransportHostState::Connected;
	for (int Frame = 0; Frame < MaxHandshakeFrames && !bClientConnected; ++Frame)
	{
		InNowMilliseconds += FrameStepMilliseconds;
		PumpSide(InClientHost, InNowMilliseconds);
		PumpSide(InServerHost, InNowMilliseconds);
		bClientConnected = InClientTransport.GetState() == ETransportHostState::Connected;
	}
	return InNowMilliseconds;
}

/**
 * Extends ConnectClientToServer to two independent wires (roadmap 4.2's telemetry + command networks):
 * waits for BOTH client transports to report Connected, since each side's one Host.Tick already pumps both
 * of that side's host play systems through its frame set.
 */
TimePointMilliseconds ConnectClientToServerOverTwoWires(
	FHost& InClientHost,
	FTransport& InClientTransportA,
	FTransport& InClientTransportB,
	FHost& InServerHost,
	TimePointMilliseconds InNowMilliseconds) noexcept
{
	bool bBothClientsConnected =
		InClientTransportA.GetState() == ETransportHostState::Connected && InClientTransportB.GetState() == ETransportHostState::Connected;
	for (int Frame = 0; Frame < MaxHandshakeFrames && !bBothClientsConnected; ++Frame)
	{
		InNowMilliseconds += FrameStepMilliseconds;
		PumpSide(InClientHost, InNowMilliseconds);
		PumpSide(InServerHost, InNowMilliseconds);
		bBothClientsConnected =
			InClientTransportA.GetState() == ETransportHostState::Connected && InClientTransportB.GetState() == ETransportHostState::Connected;
	}
	return InNowMilliseconds;
}

/**
 * Scenario: Connect a client and server through a bound channel, then issue a targeted send from the client.
 * Expected: The send enqueues successfully and reaches only the server's matching handler, carrying the original header fields and payload byte.
 */
MW_TEST_CASE(EngineMessageChannel_ClientToServerTargetedSendReachesServerHandler)
{
	// Arrange
	THostLoopback<2, 8, 64> Network;
	FTransport ServerTransport(Network.Port(0));
	FTransport ClientTransport(Network.Port(1));
	FHostPlay ServerFrame{ServerTransport};
	FHostPlay ClientFrame{ClientTransport};
	FTestRouter ClientRouter;
	FTestRouter ServerRouter;
	FFrameSet ServerSet;
	MW_EXPECT_EQ(
		Test, EEngineResult::Success, ServerSet.Add(ServerFrame), "The server's frame set must accept its host play system first (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ServerSet.Add(ServerRouter), "The server's frame set must accept its router last (D3 order)");
	FFrameSet ClientSet;
	MW_EXPECT_EQ(
		Test, EEngineResult::Success, ClientSet.Add(ClientFrame), "The client's frame set must accept its host play system first (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ClientSet.Add(ClientRouter), "The client's frame set must accept its router last (D3 order)");
	FHost ServerHost{FGarbageCollectionBudget{TestRootOperations, TestMarkOperations, TestSweepOperations}, ServerSet};
	FHost ClientHost{FGarbageCollectionBudget{TestRootOperations, TestMarkOperations, TestSweepOperations}, ClientSet};
	FBinding ClientBinding(ClientTransport, AppWireChannelByte, AppChannelId, EChannelSendTarget::Server, ClientRouter);
	FBinding ServerBinding(ServerTransport, AppWireChannelByte, AppChannelId, EChannelSendTarget::AllPeers, ServerRouter);

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

	FTransportHostConfig ClientConfig = MakeConfig();
	ClientConfig.ServerAddress = MakeLoopbackAddress(0);
	(void)ServerTransport.Configure(ENetworkMode::DedicatedServer, MakeConfig());
	(void)ClientTransport.Configure(ENetworkMode::Client, ClientConfig);
	(void)ServerTransport.Start(0);
	(void)ClientTransport.Start(0);

	const TimePointMilliseconds ConnectedAt = ConnectClientToServer(ClientHost, ClientTransport, ServerHost, 0);
	MW_EXPECT_EQ(Test, ETransportHostState::Connected, ClientTransport.GetState(), "The client must connect through the frame-set-driven pump order");

	// Act
	const std::array<std::uint8_t, 1> Payload{0x11};
	const EMessageResult SendResult = ClientRouter.SendMessageToActor(
		AppChannelId, TestMessageType, TestListenerActorId, TestSenderActorId, TSpan<const std::uint8_t>(Payload.data(), 1));

	const TimePointMilliseconds DeliveredAt = ConnectedAt + FrameStepMilliseconds;
	PumpSide(ClientHost, DeliveredAt);
	PumpSide(ServerHost, DeliveredAt);

	// Assert
	MW_EXPECT_SUCCESS(Test, SendResult, "A connected client must queue a targeted send on its wired channel");
	MW_EXPECT_TRUE(Test, ServerRecord.bWasCalled, "The server handler must receive the client's targeted message");
	MW_EXPECT_EQ(Test, TestMessageType, ServerRecord.MessageTypeId, "The delivered view must carry the original message type");
	MW_EXPECT_EQ(Test, TestListenerActorId, ServerRecord.TargetActorId, "The delivered view must carry the original target actor");
	MW_EXPECT_EQ(Test, TestSenderActorId, ServerRecord.SenderActorId, "The delivered view must carry the original sender actor");
	MW_EXPECT_EQ(Test, AppChannelId, ServerRecord.ArrivedOnChannelId, "The delivered view must report the channel it arrived on");
	MW_EXPECT_EQ(Test, std::size_t{1}, ServerRecord.PayloadLength, "The delivered view must carry the original payload length");
	MW_EXPECT_EQ(Test, std::uint8_t{0x11}, ServerRecord.PayloadBytes[0], "The delivered view must carry the original payload byte");
}

/**
 * Scenario: Connect a client and server through a bound channel with a client broadcast listener, then issue a broadcast from the server.
 * Expected: The broadcast enqueues successfully and reaches the client handler, targeting every subscriber and carrying the original header fields
 * and payload byte.
 */
MW_TEST_CASE(EngineMessageChannel_ServerBroadcastReachesClientHandler)
{
	// Arrange
	THostLoopback<2, 8, 64> Network;
	FTransport ServerTransport(Network.Port(0));
	FTransport ClientTransport(Network.Port(1));
	FHostPlay ServerFrame{ServerTransport};
	FHostPlay ClientFrame{ClientTransport};
	FTestRouter ClientRouter;
	FTestRouter ServerRouter;
	FFrameSet ServerSet;
	MW_EXPECT_EQ(
		Test, EEngineResult::Success, ServerSet.Add(ServerFrame), "The server's frame set must accept its host play system first (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ServerSet.Add(ServerRouter), "The server's frame set must accept its router last (D3 order)");
	FFrameSet ClientSet;
	MW_EXPECT_EQ(
		Test, EEngineResult::Success, ClientSet.Add(ClientFrame), "The client's frame set must accept its host play system first (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ClientSet.Add(ClientRouter), "The client's frame set must accept its router last (D3 order)");
	FHost ServerHost{FGarbageCollectionBudget{TestRootOperations, TestMarkOperations, TestSweepOperations}, ServerSet};
	FHost ClientHost{FGarbageCollectionBudget{TestRootOperations, TestMarkOperations, TestSweepOperations}, ClientSet};
	FBinding ClientBinding(ClientTransport, AppWireChannelByte, AppChannelId, EChannelSendTarget::Server, ClientRouter);
	FBinding ServerBinding(ServerTransport, AppWireChannelByte, AppChannelId, EChannelSendTarget::AllPeers, ServerRouter);

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

	FTransportHostConfig ClientConfig = MakeConfig();
	ClientConfig.ServerAddress = MakeLoopbackAddress(0);
	(void)ServerTransport.Configure(ENetworkMode::DedicatedServer, MakeConfig());
	(void)ClientTransport.Configure(ENetworkMode::Client, ClientConfig);
	(void)ServerTransport.Start(0);
	(void)ClientTransport.Start(0);

	const TimePointMilliseconds ConnectedAt = ConnectClientToServer(ClientHost, ClientTransport, ServerHost, 0);
	MW_EXPECT_EQ(Test, ETransportHostState::Connected, ClientTransport.GetState(), "The client must connect through the frame-set-driven pump order");

	// Act
	const std::array<std::uint8_t, 1> Payload{0x22};
	const EMessageResult SendResult =
		ServerRouter.BroadcastMessage(AppChannelId, TestMessageType, TestSenderActorId, TSpan<const std::uint8_t>(Payload.data(), 1));

	const TimePointMilliseconds DeliveredAt = ConnectedAt + FrameStepMilliseconds;
	PumpSide(ServerHost, DeliveredAt);
	PumpSide(ClientHost, DeliveredAt);

	// Assert
	MW_EXPECT_SUCCESS(Test, SendResult, "A server with one active peer must queue a broadcast on its wired channel");
	MW_EXPECT_TRUE(Test, ClientRecord.bWasCalled, "The client handler must receive the server's broadcast message");
	MW_EXPECT_EQ(Test, TestMessageType, ClientRecord.MessageTypeId, "The delivered view must carry the original message type");
	MW_EXPECT_EQ(Test, BroadcastActorId, ClientRecord.TargetActorId, "A broadcast's delivered view must target every subscriber");
	MW_EXPECT_EQ(Test, TestSenderActorId, ClientRecord.SenderActorId, "The delivered view must carry the original sender actor");
	MW_EXPECT_EQ(Test, AppChannelId, ClientRecord.ArrivedOnChannelId, "The delivered view must report the channel it arrived on");
	MW_EXPECT_EQ(Test, std::uint8_t{0x22}, ClientRecord.PayloadBytes[0], "The delivered view must carry the original payload byte");
}

/**
 * Scenario: Connect a client and server through one bound channel, then send raw bytes on a different wire-channel byte.
 * Expected: The foreign-channel message never reaches the binding's sink; the router's inbound queue stays empty and DroppedInboundCount stays zero.
 */
MW_TEST_CASE(EngineMessageChannel_ForeignWireChannelNeverReachesBoundSink)
{
	// Arrange
	THostLoopback<2, 8, 64> Network;
	FTransport ServerTransport(Network.Port(0));
	FTransport ClientTransport(Network.Port(1));
	FHostPlay ServerFrame{ServerTransport};
	FHostPlay ClientFrame{ClientTransport};
	FTestRouter ServerRouter;
	FFrameSet ServerSet;
	MW_EXPECT_EQ(
		Test, EEngineResult::Success, ServerSet.Add(ServerFrame), "The server's frame set must accept its host play system first (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ServerSet.Add(ServerRouter), "The server's frame set must accept its router last (D3 order)");
	FHost ServerHost{FGarbageCollectionBudget{TestRootOperations, TestMarkOperations, TestSweepOperations}, ServerSet};
	// The client in this case has no router at all (it sends raw wire bytes directly below), so it
	// keeps the bare host play system instead of a frame set.
	FHost ClientHost{FGarbageCollectionBudget{TestRootOperations, TestMarkOperations, TestSweepOperations}, ClientFrame};
	FBinding ServerBinding(ServerTransport, AppWireChannelByte, AppChannelId, EChannelSendTarget::AllPeers, ServerRouter);

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

	FTransportHostConfig ClientConfig = MakeConfig();
	ClientConfig.ServerAddress = MakeLoopbackAddress(0);
	(void)ServerTransport.Configure(ENetworkMode::DedicatedServer, MakeConfig());
	(void)ClientTransport.Configure(ENetworkMode::Client, ClientConfig);
	(void)ServerTransport.Start(0);
	(void)ClientTransport.Start(0);

	// The client sends raw wire bytes directly (bypassing any router), so only the server binding's
	// own channel filter is exercised; the client side needs no router pumped alongside its tick.
	TimePointMilliseconds Now = 0;
	bool bClientConnected = ClientTransport.GetState() == ETransportHostState::Connected;
	for (int Frame = 0; Frame < MaxHandshakeFrames && !bClientConnected; ++Frame)
	{
		Now += FrameStepMilliseconds;
		(void)ClientHost.Tick(Now);
		PumpSide(ServerHost, Now);
		bClientConnected = ClientTransport.GetState() == ETransportHostState::Connected;
	}
	MW_EXPECT_EQ(
		Test, ETransportHostState::Connected, ClientTransport.GetState(), "The client must connect before sending the foreign-channel message");

	// Act
	const std::array<std::uint8_t, 1> Payload{0x33};
	const ETransportResult SendResult =
		ClientTransport.SendTo(ClientTransport.GetServerPeer(), ForeignWireChannelByte, TSpan<const std::uint8_t>(Payload.data(), 1));
	MW_EXPECT_EQ(Test, ETransportResult::Success, SendResult, "A connected client can queue raw bytes on any non-zero wire channel");

	Now += FrameStepMilliseconds;
	(void)ClientHost.Tick(Now);
	PumpSide(ServerHost, Now);

	// Assert
	MW_EXPECT_TRUE(Test, !ServerRecord.bWasCalled, "A message on a foreign wire channel must never reach this binding's sink");
	MW_EXPECT_EQ(Test, std::size_t{0}, ServerRouter.QueuedInboundCount(), "The router's inbound queue must stay empty for a filtered message");
	MW_EXPECT_EQ(
		Test,
		std::uint32_t{0},
		ServerBinding.DroppedInboundCount(),
		"The channel filter runs before the sink is consulted, so a foreign-channel message is never counted as dropped");
}

/**
 * Scenario: Queue a broadcast before the client connects and pump once, then connect through the frame-set-driven handshake.
 * Expected: Queuing succeeds and the unavailable transport retains the queued message; the retained message is sent and delivered within the same
 * connecting frame once the peer flips to Connected.
 */
MW_TEST_CASE(EngineMessageChannel_SendBeforeConnectReportsUnavailableThenDeliversAfterConnect)
{
	// Arrange
	THostLoopback<2, 8, 64> Network;
	FTransport ServerTransport(Network.Port(0));
	FTransport ClientTransport(Network.Port(1));
	FHostPlay ServerFrame{ServerTransport};
	FHostPlay ClientFrame{ClientTransport};
	FTestRouter ClientRouter;
	FTestRouter ServerRouter;
	FFrameSet ServerSet;
	MW_EXPECT_EQ(
		Test, EEngineResult::Success, ServerSet.Add(ServerFrame), "The server's frame set must accept its host play system first (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ServerSet.Add(ServerRouter), "The server's frame set must accept its router last (D3 order)");
	FFrameSet ClientSet;
	MW_EXPECT_EQ(
		Test, EEngineResult::Success, ClientSet.Add(ClientFrame), "The client's frame set must accept its host play system first (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ClientSet.Add(ClientRouter), "The client's frame set must accept its router last (D3 order)");
	FHost ServerHost{FGarbageCollectionBudget{TestRootOperations, TestMarkOperations, TestSweepOperations}, ServerSet};
	FHost ClientHost{FGarbageCollectionBudget{TestRootOperations, TestMarkOperations, TestSweepOperations}, ClientSet};
	FBinding ClientBinding(ClientTransport, AppWireChannelByte, AppChannelId, EChannelSendTarget::Server, ClientRouter);
	FBinding ServerBinding(ServerTransport, AppWireChannelByte, AppChannelId, EChannelSendTarget::AllPeers, ServerRouter);

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

	FTransportHostConfig ClientConfig = MakeConfig();
	ClientConfig.ServerAddress = MakeLoopbackAddress(0);
	(void)ServerTransport.Configure(ENetworkMode::DedicatedServer, MakeConfig());
	(void)ClientTransport.Configure(ENetworkMode::Client, ClientConfig);
	(void)ServerTransport.Start(0);
	(void)ClientTransport.Start(0);

	// Act: queue the message while no peer is connected, then pump once.
	const std::array<std::uint8_t, 1> Payload{0x44};
	const EMessageResult SendResult =
		ClientRouter.BroadcastMessage(AppChannelId, TestMessageType, TestSenderActorId, TSpan<const std::uint8_t>(Payload.data(), 1));

	TimePointMilliseconds Now = FrameStepMilliseconds;
	PumpSide(ClientHost, Now);

	// Assert: queuing succeeds (the router's own outbound queue is transport-independent) and the unavailable transport retains the message.
	MW_EXPECT_SUCCESS(Test, SendResult, "Queuing succeeds before connect: the router's own outbound queue is independent of the transport");
	MW_EXPECT_EQ(
		Test,
		std::size_t{1},
		ClientRouter.QueuedOutboundCount(),
		"Unavailable (no server peer yet) must retain the queued message instead of dropping it");
	MW_EXPECT_TRUE(Test, !ServerRecord.bWasCalled, "Nothing can have arrived before the client even connects");

	// Act: connect (the frame set flushes within the connecting frame's own tick).
	// Under the old manual pump order the router flushed before that same frame's engine tick, so the
	// connecting frame that flipped the peer to Connected still saw the pre-flip state; a further pump
	// was needed after ConnectClientToServer returned. The frame set instead flushes the router right
	// after the tick's own dispatch step (both inside one Host.Tick), so the very connecting frame whose
	// dispatch admits the client also flushes and delivers the retained message within that same
	// ConnectClientToServer iteration - one frame earlier than before.
	Now = ConnectClientToServer(ClientHost, ClientTransport, ServerHost, Now);

	// Assert
	MW_EXPECT_EQ(Test, ETransportHostState::Connected, ClientTransport.GetState(), "The client must connect through the frame-set-driven pump order");
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

/**
 * Scenario: Wire a binding to a toggleable sink on a listen server, then broadcast with the sink accepting and then rejecting twice.
 * Expected: An accepted sink call does not count as dropped; each rejected broadcast still succeeds at the transport and increments
 * DroppedInboundCount; every broadcast reaches the sink after passing the channel filter.
 */
MW_TEST_CASE(EngineMessageChannel_RejectingSinkIncrementsDroppedInboundCount)
{
	// Arrange: a listen server's Broadcast dispatches to its own local peer synchronously (TTransportHost::SendToLocalPeer),
	// so this case never crosses the loopback network and needs no engine tick or pumping at all.
	THostLoopback<1, 4, 64> Network;
	FTransport ListenServerHost(Network.Port(0));
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		ListenServerHost.Configure(ENetworkMode::ListenServer, MakeConfig()),
		"Configuring an idle host must succeed");
	MW_EXPECT_EQ(Test, ETransportResult::Success, ListenServerHost.Start(0), "Starting an idle host must succeed");

	FToggleableSink Sink;
	FBinding Binding(ListenServerHost, AppWireChannelByte, AppChannelId, EChannelSendTarget::AllPeers, Sink);
	MW_EXPECT_TRUE(Test, Binding.IsAttached(), "The binding must register its inbound handler");

	const std::array<std::uint8_t, 1> Payload{0x5A};
	const TSpan<const std::uint8_t> PayloadView(Payload.data(), Payload.size());

	// Act
	Sink.SetRejectInbound(false);
	(void)ListenServerHost.Broadcast(AppWireChannelByte, PayloadView);
	// Assert
	MW_EXPECT_EQ(Test, std::uint32_t{0}, Binding.DroppedInboundCount(), "An accepted sink call must not count as dropped");

	// Act
	Sink.SetRejectInbound(true);
	const ETransportResult FirstRejectResult = ListenServerHost.Broadcast(AppWireChannelByte, PayloadView);
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, FirstRejectResult, "The transport still succeeds; only the sink rejects");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, Binding.DroppedInboundCount(), "A rejecting sink must increment DroppedInboundCount");

	// Act
	const ETransportResult SecondRejectResult = ListenServerHost.Broadcast(AppWireChannelByte, PayloadView);
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, SecondRejectResult, "A second rejected broadcast must still be accepted by the transport");
	MW_EXPECT_EQ(Test, std::uint32_t{2}, Binding.DroppedInboundCount(), "A second rejection must climb the counter again, staying consistent");
	MW_EXPECT_EQ(Test, std::size_t{3}, Sink.ReceivedCallCount(), "All three broadcasts must reach the sink after passing the channel filter");
}

/**
 * Scenario: Connect two wires (telemetry and command) behind one router per side, then broadcast a distinct payload on each channel and pump one
 * frame per side. Expected: Both sends enqueue successfully; each message arrives tagged with its own channel id within one post-send frame and never
 * bleeds into the other channel's record.
 */
MW_TEST_CASE(EngineMessageChannel_MultiChannelIsolationDeliversBothInOneFrame)
{
	// Arrange
	THostLoopback<2, 8, 64> TelemetryNetwork;
	THostLoopback<2, 8, 64> CommandNetwork;
	FTransport TelemetryServerTransport(TelemetryNetwork.Port(0));
	FTransport TelemetryClientTransport(TelemetryNetwork.Port(1));
	FTransport CommandServerTransport(CommandNetwork.Port(0));
	FTransport CommandClientTransport(CommandNetwork.Port(1));
	FHostPlay TelemetryServerFrame{TelemetryServerTransport};
	FHostPlay TelemetryClientFrame{TelemetryClientTransport};
	FHostPlay CommandServerFrame{CommandServerTransport};
	FHostPlay CommandClientFrame{CommandClientTransport};
	FMultiChannelRouter ClientRouter;
	FMultiChannelRouter ServerRouter;

	FMultiChannelFrameSet ServerSet;
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		ServerSet.Add(TelemetryServerFrame),
		"The server's frame set must accept the telemetry host play system first (D3 order)");
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		ServerSet.Add(CommandServerFrame),
		"The server's frame set must accept the command host play system second (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ServerSet.Add(ServerRouter), "The server's frame set must accept its router last (D3 order)");
	FMultiChannelFrameSet ClientSet;
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		ClientSet.Add(TelemetryClientFrame),
		"The client's frame set must accept the telemetry host play system first (D3 order)");
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		ClientSet.Add(CommandClientFrame),
		"The client's frame set must accept the command host play system second (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ClientSet.Add(ClientRouter), "The client's frame set must accept its router last (D3 order)");
	FHost ServerHost{FGarbageCollectionBudget{TestRootOperations, TestMarkOperations, TestSweepOperations}, ServerSet};
	FHost ClientHost{FGarbageCollectionBudget{TestRootOperations, TestMarkOperations, TestSweepOperations}, ClientSet};

	FBinding TelemetryClientBinding(TelemetryClientTransport, TelemetryWireChannelByte, TelemetryChannelId, EChannelSendTarget::Server, ClientRouter);
	FBinding TelemetryServerBinding(
		TelemetryServerTransport, TelemetryWireChannelByte, TelemetryChannelId, EChannelSendTarget::AllPeers, ServerRouter);
	FBinding CommandClientBinding(CommandClientTransport, CommandWireChannelByte, CommandChannelId, EChannelSendTarget::Server, ClientRouter);
	FBinding CommandServerBinding(CommandServerTransport, CommandWireChannelByte, CommandChannelId, EChannelSendTarget::AllPeers, ServerRouter);

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

	FTransportHostConfig TelemetryClientConfig = MakeConfig();
	TelemetryClientConfig.ServerAddress = MakeLoopbackAddress(0);
	FTransportHostConfig CommandClientConfig = MakeConfig();
	CommandClientConfig.ServerAddress = MakeLoopbackAddress(0);
	(void)TelemetryServerTransport.Configure(ENetworkMode::DedicatedServer, MakeConfig());
	(void)TelemetryClientTransport.Configure(ENetworkMode::Client, TelemetryClientConfig);
	(void)CommandServerTransport.Configure(ENetworkMode::DedicatedServer, MakeConfig());
	(void)CommandClientTransport.Configure(ENetworkMode::Client, CommandClientConfig);
	(void)TelemetryServerTransport.Start(0);
	(void)TelemetryClientTransport.Start(0);
	(void)CommandServerTransport.Start(0);
	(void)CommandClientTransport.Start(0);

	const TimePointMilliseconds ConnectedAt =
		ConnectClientToServerOverTwoWires(ClientHost, TelemetryClientTransport, CommandClientTransport, ServerHost, 0);
	MW_EXPECT_EQ(
		Test,
		ETransportHostState::Connected,
		TelemetryClientTransport.GetState(),
		"The telemetry wire must connect through the frame-set-driven pump order");
	MW_EXPECT_EQ(
		Test,
		ETransportHostState::Connected,
		CommandClientTransport.GetState(),
		"The command wire must connect through the frame-set-driven pump order");

	// Act
	const std::array<std::uint8_t, 1> TelemetryPayload{0xAA};
	const std::array<std::uint8_t, 1> CommandPayload{0xBB};
	const EMessageResult TelemetrySendResult =
		ClientRouter.BroadcastMessage(TelemetryChannelId, TestMessageType, TestSenderActorId, TSpan<const std::uint8_t>(TelemetryPayload.data(), 1));
	const EMessageResult CommandSendResult =
		ClientRouter.BroadcastMessage(CommandChannelId, TestMessageType, TestSenderActorId, TSpan<const std::uint8_t>(CommandPayload.data(), 1));

	const TimePointMilliseconds DeliveredAt = ConnectedAt + FrameStepMilliseconds;
	PumpSide(ClientHost, DeliveredAt);
	PumpSide(ServerHost, DeliveredAt);

	// Assert
	MW_EXPECT_SUCCESS(Test, TelemetrySendResult, "A connected client must queue a broadcast on its telemetry channel");
	MW_EXPECT_SUCCESS(Test, CommandSendResult, "A connected client must queue a broadcast on its command channel");
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
 * Scenario: Connect telemetry and command wires, prime the client's command FIFO and the server's command mailbox to saturation, then queue a
 * stalled-channel message followed by a healthy-channel message and pump the client once. Expected: Both sends enqueue successfully; the stalled
 * channel at the head of the router's shared outbound queue retains both itself and the healthy message queued behind it.
 */
MW_TEST_CASE(EngineMessageChannel_StalledChannelRetainsRouterHead)
{
	// Arrange
	THostLoopback<2, 8, 64> TelemetryNetwork;
	THostLoopback<2, 8, 64> CommandNetwork;
	FTransport TelemetryServerTransport(TelemetryNetwork.Port(0));
	FTransport TelemetryClientTransport(TelemetryNetwork.Port(1));
	FTransport CommandServerTransport(CommandNetwork.Port(0));
	FTransport CommandClientTransport(CommandNetwork.Port(1));
	FHostPlay TelemetryServerFrame{TelemetryServerTransport};
	FHostPlay TelemetryClientFrame{TelemetryClientTransport};
	FHostPlay CommandServerFrame{CommandServerTransport};
	FHostPlay CommandClientFrame{CommandClientTransport};
	FMultiChannelRouter ClientRouter;
	FMultiChannelRouter ServerRouter;

	FMultiChannelFrameSet ServerSet;
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		ServerSet.Add(TelemetryServerFrame),
		"The server's frame set must accept the telemetry host play system first (D3 order)");
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		ServerSet.Add(CommandServerFrame),
		"The server's frame set must accept the command host play system second (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ServerSet.Add(ServerRouter), "The server's frame set must accept its router last (D3 order)");
	FMultiChannelFrameSet ClientSet;
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		ClientSet.Add(TelemetryClientFrame),
		"The client's frame set must accept the telemetry host play system first (D3 order)");
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		ClientSet.Add(CommandClientFrame),
		"The client's frame set must accept the command host play system second (D3 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ClientSet.Add(ClientRouter), "The client's frame set must accept its router last (D3 order)");
	FHost ServerHost{FGarbageCollectionBudget{TestRootOperations, TestMarkOperations, TestSweepOperations}, ServerSet};
	FHost ClientHost{FGarbageCollectionBudget{TestRootOperations, TestMarkOperations, TestSweepOperations}, ClientSet};

	FBinding TelemetryClientBinding(TelemetryClientTransport, TelemetryWireChannelByte, TelemetryChannelId, EChannelSendTarget::Server, ClientRouter);
	FBinding TelemetryServerBinding(
		TelemetryServerTransport, TelemetryWireChannelByte, TelemetryChannelId, EChannelSendTarget::AllPeers, ServerRouter);
	FBinding CommandClientBinding(CommandClientTransport, CommandWireChannelByte, CommandChannelId, EChannelSendTarget::Server, ClientRouter);
	FBinding CommandServerBinding(CommandServerTransport, CommandWireChannelByte, CommandChannelId, EChannelSendTarget::AllPeers, ServerRouter);

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

	FTransportHostConfig TelemetryClientConfig = MakeConfig();
	TelemetryClientConfig.ServerAddress = MakeLoopbackAddress(0);
	FTransportHostConfig CommandClientConfig = MakeConfig();
	CommandClientConfig.ServerAddress = MakeLoopbackAddress(0);
	(void)TelemetryServerTransport.Configure(ENetworkMode::DedicatedServer, MakeConfig());
	(void)TelemetryClientTransport.Configure(ENetworkMode::Client, TelemetryClientConfig);
	(void)CommandServerTransport.Configure(ENetworkMode::DedicatedServer, MakeConfig());
	(void)CommandClientTransport.Configure(ENetworkMode::Client, CommandClientConfig);
	(void)TelemetryServerTransport.Start(0);
	(void)TelemetryClientTransport.Start(0);
	(void)CommandServerTransport.Start(0);
	(void)CommandClientTransport.Start(0);

	const TimePointMilliseconds ConnectedAt =
		ConnectClientToServerOverTwoWires(ClientHost, TelemetryClientTransport, CommandClientTransport, ServerHost, 0);
	MW_EXPECT_EQ(
		Test,
		ETransportHostState::Connected,
		CommandClientTransport.GetState(),
		"The command wire must connect before its outbound FIFO can be primed");

	// auto: this test names no Transport peer-id type, matching TMessageChannelBinding::TrySendEncodedMessage's own convention.
	const auto CommandServerPeer = CommandClientTransport.GetServerPeer();
	MW_EXPECT_TRUE(Test, CommandServerPeer.IsValid(), "The client must resolve its server peer on the command wire before priming");

	// (1) Prime the client's own command-wire TTransportHost outbound FIFO to exactly its capacity via raw
	// SendTo calls that bypass the router entirely; no Tick runs between these calls, so nothing drains
	// and no heartbeat timer advances.
	const std::array<std::uint8_t, 1> FifoPrimerPayload{0xF0};
	for (std::size_t Index = 0; Index < FTransport::SendQueueDepth; ++Index)
	{
		MW_EXPECT_EQ(
			Test,
			ETransportResult::Success,
			CommandClientTransport.SendTo(CommandServerPeer, CommandWireChannelByte, TSpan<const std::uint8_t>(FifoPrimerPayload.data(), 1)),
			"Priming the client's own outbound FIFO must succeed while it still has a free slot");
	}

	// (2) Fill the server's command mailbox directly (bypassing both routers and both TTransportHosts), one
	// raw packet at a time, until one more than MailboxCapacityValue() reports Full.
	const std::array<std::uint8_t, 1> MailboxFillerPayload{0xF1};
	for (std::size_t Index = 0; Index < CommandNetwork.MailboxCapacityValue(); ++Index)
	{
		MW_EXPECT_EQ(
			Test,
			ETransportResult::Success,
			CommandNetwork.Port(1).TrySend(MakeLoopbackAddress(0), TSpan<const std::uint8_t>(MailboxFillerPayload.data(), 1)),
			"Each raw packet up to the server's command mailbox capacity must be accepted");
	}
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Full,
		CommandNetwork.Port(1).TrySend(MakeLoopbackAddress(0), TSpan<const std::uint8_t>(MailboxFillerPayload.data(), 1)),
		"One packet beyond MailboxCapacityValue() must report Full: the server's command mailbox is now saturated");

	// Act: queue the stalled channel's message first (the router's next head), then a healthy channel's
	// message right behind it.
	const std::array<std::uint8_t, 1> StalledPayload{0xC0};
	const std::array<std::uint8_t, 1> HealthyPayload{0xC1};
	const EMessageResult StalledEnqueueResult = ClientRouter.SendMessageToActor(
		CommandChannelId, TestMessageType, BroadcastActorId, TestSenderActorId, TSpan<const std::uint8_t>(StalledPayload.data(), 1));
	const EMessageResult HealthyEnqueueResult = ClientRouter.SendMessageToActor(
		TelemetryChannelId, TestMessageType, BroadcastActorId, TestSenderActorId, TSpan<const std::uint8_t>(HealthyPayload.data(), 1));

	// Assert: queuing succeeds regardless of transport state (both are buffered before the flush under test).
	MW_EXPECT_SUCCESS(
		Test, StalledEnqueueResult, "Queuing succeeds regardless of transport state: the router's own outbound queue is independent of the device");
	MW_EXPECT_SUCCESS(Test, HealthyEnqueueResult, "Queuing the healthy channel's message behind the stalled one must also succeed");
	MW_EXPECT_EQ(Test, std::size_t{2}, ClientRouter.QueuedOutboundCount(), "Both messages must be queued before the flush under test");

	// Act: only the client is pumped; the server side is irrelevant to what the client's own router head
	// does, and never receiving anything is exactly the point of a stalled wire.
	const TimePointMilliseconds FlushAt = ConnectedAt + FrameStepMilliseconds;
	PumpSide(ClientHost, FlushAt);

	// Assert
	MW_EXPECT_EQ(
		Test,
		std::size_t{2},
		ClientRouter.QueuedOutboundCount(),
		"Accepted v1 cross-channel head-of-line caveat: a stalled channel at the head of the router's one shared outbound queue retains both "
		"itself and the healthy channel's message queued behind it, because TMessageRouter::PostAdvance stops the whole tick on the first "
		"non-Success send (matching TTransportManager::AdvanceSend's retained-head discipline from Task 2.2)");
}

/**
 * Scenario: Wrap each side's wire binding in a TReliableChannel behind an every-third-send packet-drop device, then send several guaranteed messages
 * and pump until each is acknowledged. Expected: Every message is delivered to the server exactly once despite the injected drops; at least one
 * resend fires because a send was actually dropped.
 */
MW_TEST_CASE(EngineMessageChannel_ReliableChannelSurvivesPacketDropsDeliveringExactlyOnce)
{
	// Arrange
	constexpr std::uint32_t DropEveryNthSend = 3;
	constexpr std::size_t MessagesToSend = 6;
	constexpr DurationMilliseconds ReliableRetryIntervalMilliseconds = 50;
	constexpr std::uint8_t ReliableMaxSendAttempts = 5;
	constexpr int MaxFramesPerMessage = ReliableMaxSendAttempts + 3;
	constexpr std::size_t ReliableSlotBytes = MessageByteCapacity + ReliableHeaderBytes;
	using FReliableChannel = TReliableChannel<4, ReliableSlotBytes>;
	using FClientFrameSet = TPlaySystemSet<3>;
	using FServerFrameSet = TPlaySystemSet<2>;

	THostLoopback<2, 8, 64> Network;
	FPacketDropDevice ClientDropDevice(Network.Port(1), DropEveryNthSend);
	FTransport ServerTransport(Network.Port(0));
	FTransport ClientTransport(ClientDropDevice);
	FHostPlay ServerFrame{ServerTransport};
	FHostPlay ClientFrame{ClientTransport};

	FTestRouter ClientRouter;
	FRecordingReliableForwardSink ServerForwardSink;

	FReliableChannelConfig ReliableConfig{};
	ReliableConfig.RetryIntervalMilliseconds = ReliableRetryIntervalMilliseconds;
	ReliableConfig.MaxSendAttempts = ReliableMaxSendAttempts;
	FReliableChannel ClientReliable(ClientRouter, ReliableConfig);
	FReliableChannel ServerReliable(ServerForwardSink, ReliableConfig);

	FBinding ClientBinding(ClientTransport, AppWireChannelByte, AppChannelId, EChannelSendTarget::Server, ClientReliable);
	FBinding ServerBinding(ServerTransport, AppWireChannelByte, AppChannelId, EChannelSendTarget::AllPeers, ServerReliable);
	ClientReliable.SetInnerChannel(ClientBinding);
	ServerReliable.SetInnerChannel(ServerBinding);

	MW_EXPECT_TRUE(Test, ClientBinding.IsAttached(), "The client binding must register its inbound handler");
	MW_EXPECT_TRUE(Test, ServerBinding.IsAttached(), "The server binding must register its inbound handler");
	MW_EXPECT_SUCCESS(Test, ClientRouter.AddChannel(ClientReliable), "The client router must accept its guaranteed channel");

	FClientFrameSet ClientSet;
	MW_EXPECT_EQ(
		Test, EEngineResult::Success, ClientSet.Add(ClientFrame), "The client's frame set must accept its host play system first (4.4 order)");
	MW_EXPECT_EQ(
		Test, EEngineResult::Success, ClientSet.Add(ClientReliable), "The client's frame set must accept its reliable channel second (4.4 order)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ClientSet.Add(ClientRouter), "The client's frame set must accept its router last (4.4 order)");
	FServerFrameSet ServerSet;
	MW_EXPECT_EQ(
		Test, EEngineResult::Success, ServerSet.Add(ServerFrame), "The server's frame set must accept its host play system first (4.4 order)");
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		ServerSet.Add(ServerReliable),
		"The server's frame set must accept its reliable channel second; this side needs no router");

	FHost ServerHost{FGarbageCollectionBudget{TestRootOperations, TestMarkOperations, TestSweepOperations}, ServerSet};
	FHost ClientHost{FGarbageCollectionBudget{TestRootOperations, TestMarkOperations, TestSweepOperations}, ClientSet};
	MW_EXPECT_TRUE(Test, ServerHost.CreateWorld().Get() != nullptr, "The server roots its world before ticking");
	MW_EXPECT_TRUE(Test, ClientHost.CreateWorld().Get() != nullptr, "The client roots its world before ticking");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ServerHost.BeginPlay(0), "The server world begins play at the baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ClientHost.BeginPlay(0), "The client world begins play at the baseline");

	// A heartbeat/timeout window far longer than this case's whole run keeps every raw send call
	// attributable to the client's one-time Hello plus the reliable channel's own Data/retry traffic,
	// so the deterministic every-3rd-call drop is guaranteed to land on a message send at some point.
	FTransportHostConfig SharedConfig = MakeConfig();
	SharedConfig.HeartbeatIntervalMilliseconds = 1'000'000;
	SharedConfig.PeerTimeoutMilliseconds = 2'000'000;
	FTransportHostConfig ClientConfig = SharedConfig;
	ClientConfig.ServerAddress = MakeLoopbackAddress(0);
	(void)ServerTransport.Configure(ENetworkMode::DedicatedServer, SharedConfig);
	(void)ClientTransport.Configure(ENetworkMode::Client, ClientConfig);
	(void)ServerTransport.Start(0);
	(void)ClientTransport.Start(0);

	TimePointMilliseconds Now = ConnectClientToServer(ClientHost, ClientTransport, ServerHost, 0);
	MW_EXPECT_EQ(Test, ETransportHostState::Connected, ClientTransport.GetState(), "The client must connect before the drop-injected sends begin");

	// Act: send each guaranteed message and pump until it is fully acknowledged, recovering from every injected drop.
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

	// Assert
	MW_EXPECT_EQ(
		Test,
		MessagesToSend,
		ServerForwardSink.ForwardedCount(),
		"Every message the client sent must be delivered to the server exactly once despite the injected drops");
	MW_EXPECT_TRUE(
		Test, ClientReliable.ResentCount() > 0, "At least one message must have been resent because FPacketDropDevice actually dropped a send");
}

} // namespace
