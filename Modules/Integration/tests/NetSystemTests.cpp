#include "TestSupport.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Delegates/Delegate.h>
#include <MicroWorld/EngineSystem.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Integration/NetSystem.h>
#include <MicroWorld/Net/HostLoopback.h>
#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetDriver.h>
#include <MicroWorld/Net/NetHost.h>
#include <MicroWorld/Net/NetResult.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace MicroWorld::Tests
{

namespace
{

	/** Provides two deterministic ports so systems exchange packets without a platform transport. */
	using FLoopback = MicroWorld::THostLoopback<2, 8, 256>;

	/** Uses the default two-driver and four-channel profile for normal composition tests. */
	using FSystem = MicroWorld::TNetSystem<>;

	/** Makes driver exhaustion observable with one fixed slot. */
	struct FOneDriverTraits : MicroWorld::FDefaultNetSystemTraits
	{
		static constexpr std::size_t MaxNetDrivers = 1;
	};

	/** Makes channel exhaustion observable with one router and system channel slot. */
	struct FOneChannelTraits : MicroWorld::FDefaultNetSystemTraits
	{
		static constexpr std::size_t MaxRouterChannels = 1;
		static constexpr std::size_t MaxChannels = 1;
	};

	/** Builds a host configuration accepted by both loopback roles. */
	MicroWorld::FNetHostConfig MakeConfig() noexcept
	{
		return MicroWorld::FNetHostConfig{};
	}

	/** Supplies a shared monotonic order source so each driver's first pump is directly observable. */
	class FDriverPumpSequence final
	{
	public:
		/** Returns a unique increasing stamp for one driver operation. */
		std::uint32_t Next() noexcept { return ++Counter; }

	private:
		/** Tracks the order across every driver that shares this test-owned sequence. */
		std::uint32_t Counter{0};
	};

	/** Records the first inbound and outbound pump each fake driver receives. */
	struct FDriverPumpRecord
	{
		/** Counts transport receive calls so the first call identifies driver pump order. */
		std::size_t ReceiveCount{0};

		/** Counts transport send calls so the first call identifies driver pump order. */
		std::size_t SendCount{0};

		/** Holds the first receive stamp from the shared test sequence. */
		std::uint32_t FirstReceiveOrder{0};

		/** Holds the first send stamp from the shared test sequence. */
		std::uint32_t FirstSendOrder{0};
	};

	/** Provides a deterministic client transport whose observable operations reveal TNetSystem's driver pump order. */
	class FRecordingDriver final : public MicroWorld::INetDriver
	{
	public:
		/** Binds the driver to caller-owned operation observability for this isolated test. */
		FRecordingDriver(FDriverPumpRecord& InRecord, FDriverPumpSequence& InSequence) noexcept : Record(InRecord), Sequence(InSequence) {}

		/** Records each outbound transport attempt and accepts it without a real network. */
		MicroWorld::ENetResult TrySend(const MicroWorld::FNetAddress&, MicroWorld::TSpan<const std::uint8_t>) noexcept override
		{
			++Record.SendCount;
			if (Record.FirstSendOrder == 0)
			{
				Record.FirstSendOrder = Sequence.Next();
			}
			return MicroWorld::ENetResult::Success;
		}

		/** Records each inbound transport attempt and reports the deterministic empty state. */
		MicroWorld::ENetResult TryReceive(MicroWorld::FNetAddress&, MicroWorld::TSpan<std::uint8_t>, MicroWorld::FNetReceiveResult&) noexcept override
		{
			++Record.ReceiveCount;
			if (Record.FirstReceiveOrder == 0)
			{
				Record.FirstReceiveOrder = Sequence.Next();
			}
			return MicroWorld::ENetResult::Unavailable;
		}

		/** Matches the integration profile's packet budget so host configuration remains valid. */
		std::size_t MaxPacketBytes() const noexcept override { return 256; }

	private:
		/** Receives this driver's counts and first-operation stamps; never owned here. */
		FDriverPumpRecord& Record;

		/** Orders operations across the two fake drivers; never owned here. */
		FDriverPumpSequence& Sequence;
	};

} // namespace

/** Proves two configured drivers receive distinct usable identities from one system. */
MW_TEST_CASE(NetSystem_AddNetDriverAcceptsTwoDrivers)
{
	FLoopback Loopback;
	FSystem System;
	const MicroWorld::FNetHostConfig Config = MakeConfig();

	const MicroWorld::FNetDriverHandle FirstDriver = System.AddNetDriver(Loopback.Port(0), MicroWorld::ENetMode::Standalone, Config);
	const MicroWorld::FNetDriverHandle SecondDriver = System.AddNetDriver(Loopback.Port(1), MicroWorld::ENetMode::Standalone, Config);
	const bool bFirstDriverValid = FirstDriver.IsValid();
	const bool bSecondDriverValid = SecondDriver.IsValid();
	const bool bDistinctSlots = FirstDriver.Index != SecondDriver.Index;

	MW_EXPECT_TRUE(Test, bFirstDriverValid, "The first configured driver must receive a valid handle");
	MW_EXPECT_TRUE(Test, bSecondDriverValid, "The second configured driver must receive a valid handle");
	MW_EXPECT_TRUE(Test, bDistinctSlots, "Two configured drivers must receive distinct slot identities");
}

/** Proves both reliability modes compose on one driver without exposing their internal wrappers. */
MW_TEST_CASE(NetSystem_AddChannelAcceptsBestEffortAndGuaranteedOnOneDriver)
{
	FLoopback Loopback;
	FSystem System;
	const MicroWorld::FNetHostConfig Config = MakeConfig();
	const MicroWorld::FNetDriverHandle Driver = System.AddNetDriver(Loopback.Port(0), MicroWorld::ENetMode::Standalone, Config);

	const MicroWorld::FChannelHandle BestEffort =
		System.AddChannel(Driver, MicroWorld::FMessageChannelId{1}, MicroWorld::EChannelReliability::BestEffort);
	const MicroWorld::FChannelHandle Guaranteed =
		System.AddChannel(Driver, MicroWorld::FMessageChannelId{2}, MicroWorld::EChannelReliability::Guaranteed);
	const bool bDriverValid = Driver.IsValid();
	const bool bBestEffortValid = BestEffort.IsValid();
	const bool bGuaranteedValid = Guaranteed.IsValid();

	MW_EXPECT_TRUE(Test, bDriverValid, "Channel setup requires a valid configured driver handle");
	MW_EXPECT_TRUE(Test, bBestEffortValid, "A best-effort channel must receive a valid handle");
	MW_EXPECT_TRUE(Test, bGuaranteedValid, "A guaranteed channel must receive a valid handle");
}

/** Proves a forged driver generation cannot add a channel and cannot affect the current driver slot. */
MW_TEST_CASE(NetSystem_AddChannelRejectsForgedDriverGeneration)
{
	FLoopback Loopback;
	FSystem System;
	const MicroWorld::FNetHostConfig Config = MakeConfig();
	const MicroWorld::FNetDriverHandle Driver = System.AddNetDriver(Loopback.Port(0), MicroWorld::ENetMode::Standalone, Config);
	const MicroWorld::FNetDriverHandle StaleDriver{Driver.Index, static_cast<std::uint8_t>(Driver.Generation + 1)};

	const MicroWorld::FChannelHandle RejectedChannel =
		System.AddChannel(StaleDriver, MicroWorld::FMessageChannelId{1}, MicroWorld::EChannelReliability::BestEffort);
	const MicroWorld::FChannelHandle CurrentChannel =
		System.AddChannel(Driver, MicroWorld::FMessageChannelId{1}, MicroWorld::EChannelReliability::BestEffort);
	const bool bDriverValid = Driver.IsValid();
	const bool bRejectedChannelValid = RejectedChannel.IsValid();
	const bool bCurrentChannelValid = CurrentChannel.IsValid();

	MW_EXPECT_TRUE(Test, bDriverValid, "The original driver handle must be valid before forging a stale one");
	MW_EXPECT_TRUE(Test, !bRejectedChannelValid, "A mismatched driver generation must reject channel creation");
	MW_EXPECT_TRUE(Test, bCurrentChannelValid, "A stale-handle rejection must leave the current driver usable");
}

/** Proves a full fixed driver table rejects the next composition request. */
MW_TEST_CASE(NetSystem_AddNetDriverRejectsCapacityExhaustion)
{
	FLoopback Loopback;
	MicroWorld::TNetSystem<FOneDriverTraits> System;
	const MicroWorld::FNetHostConfig Config = MakeConfig();

	const MicroWorld::FNetDriverHandle AcceptedDriver = System.AddNetDriver(Loopback.Port(0), MicroWorld::ENetMode::Standalone, Config);
	const MicroWorld::FNetDriverHandle RejectedDriver = System.AddNetDriver(Loopback.Port(1), MicroWorld::ENetMode::Standalone, Config);
	const bool bAcceptedDriverValid = AcceptedDriver.IsValid();
	const bool bRejectedDriverValid = RejectedDriver.IsValid();

	MW_EXPECT_TRUE(Test, bAcceptedDriverValid, "The only available driver slot must accept its first driver");
	MW_EXPECT_TRUE(Test, !bRejectedDriverValid, "A driver beyond fixed capacity must return an invalid handle");
}

/** Proves a full fixed channel table rejects the next channel without disturbing its accepted predecessor. */
MW_TEST_CASE(NetSystem_AddChannelRejectsCapacityExhaustion)
{
	FLoopback Loopback;
	MicroWorld::TNetSystem<FOneChannelTraits> System;
	const MicroWorld::FNetHostConfig Config = MakeConfig();
	const MicroWorld::FNetDriverHandle Driver = System.AddNetDriver(Loopback.Port(0), MicroWorld::ENetMode::Standalone, Config);

	const MicroWorld::FChannelHandle AcceptedChannel =
		System.AddChannel(Driver, MicroWorld::FMessageChannelId{1}, MicroWorld::EChannelReliability::BestEffort);
	const MicroWorld::FChannelHandle RejectedChannel =
		System.AddChannel(Driver, MicroWorld::FMessageChannelId{2}, MicroWorld::EChannelReliability::BestEffort);
	const bool bDriverValid = Driver.IsValid();
	const bool bAcceptedChannelValid = AcceptedChannel.IsValid();
	const bool bRejectedChannelValid = RejectedChannel.IsValid();

	MW_EXPECT_TRUE(Test, bDriverValid, "Channel capacity setup requires one valid driver");
	MW_EXPECT_TRUE(Test, bAcceptedChannelValid, "The only available channel slot must accept its first channel");
	MW_EXPECT_TRUE(Test, !bRejectedChannelValid, "A channel beyond fixed capacity must return an invalid handle");
}

/** Proves BeginPlay freezes composition, starts configured hosts, and leaves no eager packet before that lifecycle turn. */
MW_TEST_CASE(NetSystem_BeginPlayFinalizesCompositionAndDefersHostStart)
{
	FLoopback Loopback;
	FSystem System;
	MicroWorld::FNetHostConfig ClientConfig = MakeConfig();
	ClientConfig.ServerAddress = MicroWorld::MakeLoopbackAddress(1);
	const MicroWorld::FNetDriverHandle Driver = System.AddNetDriver(Loopback.Port(0), MicroWorld::ENetMode::Client, ClientConfig);
	const MicroWorld::FChannelHandle InitialChannel =
		System.AddChannel(Driver, MicroWorld::FMessageChannelId{1}, MicroWorld::EChannelReliability::BestEffort);

	System.PreAdvance(10);
	System.PostAdvance(10);
	const bool bNoPacketBeforeBeginPlay = Loopback.IsEmpty(1);

	System.BeginPlay(20);
	const MicroWorld::FChannelHandle LateChannel =
		System.AddChannel(Driver, MicroWorld::FMessageChannelId{2}, MicroWorld::EChannelReliability::BestEffort);
	const MicroWorld::FNetDriverHandle LateDriver = System.AddNetDriver(Loopback.Port(1), MicroWorld::ENetMode::Standalone, MakeConfig());
	System.PostAdvance(20);
	const bool bPacketQueuedAfterBeginPlay = !Loopback.IsEmpty(1);
	const bool bDriverValid = Driver.IsValid();
	const bool bInitialChannelValid = InitialChannel.IsValid();
	const bool bLateChannelValid = LateChannel.IsValid();
	const bool bLateDriverValid = LateDriver.IsValid();

	MW_EXPECT_TRUE(Test, bDriverValid, "The client driver must configure before the lifecycle starts");
	MW_EXPECT_TRUE(Test, bInitialChannelValid, "The initial channel must configure before composition freezes");
	MW_EXPECT_TRUE(Test, bNoPacketBeforeBeginPlay, "A configured host must not emit packets before BeginPlay starts it");
	MW_EXPECT_TRUE(Test, !bLateChannelValid, "BeginPlay must freeze later channel composition for the completed network system");
	MW_EXPECT_TRUE(Test, !bLateDriverValid, "BeginPlay must freeze later driver composition for the completed network system");
	MW_EXPECT_TRUE(Test, bPacketQueuedAfterBeginPlay, "BeginPlay must start the client host before its next outbound pump");
}

/** Proves the Core lifecycle interface pumps owned drivers in the published forward and reverse orders. */
MW_TEST_CASE(NetSystem_CoreLifecyclePumpsDriversInForwardAndReverseOrder)
{
	FDriverPumpSequence Sequence;
	FDriverPumpRecord FirstRecord{};
	FDriverPumpRecord SecondRecord{};
	FRecordingDriver FirstDriver{FirstRecord, Sequence};
	FRecordingDriver SecondDriver{SecondRecord, Sequence};
	FSystem System;
	MicroWorld::FNetHostConfig Config = MakeConfig();
	Config.ServerAddress = MicroWorld::MakeLoopbackAddress(0);

	const MicroWorld::FNetDriverHandle FirstHandle = System.AddNetDriver(FirstDriver, MicroWorld::ENetMode::Client, Config);
	const MicroWorld::FNetDriverHandle SecondHandle = System.AddNetDriver(SecondDriver, MicroWorld::ENetMode::Client, Config);
	MicroWorld::IEngineSystem& Lifecycle = System;

	Lifecycle.BeginPlay(0);
	Lifecycle.PreAdvance(10);
	Lifecycle.PostAdvance(10);

	const bool bFirstHandleValid = FirstHandle.IsValid();
	const bool bSecondHandleValid = SecondHandle.IsValid();
	const bool bFirstDriverReceived = FirstRecord.ReceiveCount > 0;
	const bool bSecondDriverReceived = SecondRecord.ReceiveCount > 0;
	const bool bFirstDriverSent = FirstRecord.SendCount > 0;
	const bool bSecondDriverSent = SecondRecord.SendCount > 0;
	const bool bReceiveOrderIsForward = FirstRecord.FirstReceiveOrder < SecondRecord.FirstReceiveOrder;
	const bool bSendOrderIsReverse = SecondRecord.FirstSendOrder < FirstRecord.FirstSendOrder;

	MW_EXPECT_TRUE(Test, bFirstHandleValid, "The first recording driver must compose before lifecycle pumping");
	MW_EXPECT_TRUE(Test, bSecondHandleValid, "The second recording driver must compose before lifecycle pumping");
	MW_EXPECT_TRUE(Test, bFirstDriverReceived, "PreAdvance must pump the first live driver");
	MW_EXPECT_TRUE(Test, bSecondDriverReceived, "PreAdvance must pump the second live driver");
	MW_EXPECT_TRUE(Test, bFirstDriverSent, "PostAdvance must pump the first live driver");
	MW_EXPECT_TRUE(Test, bSecondDriverSent, "PostAdvance must pump the second live driver");
	MW_EXPECT_TRUE(Test, bReceiveOrderIsForward, "PreAdvance must pump the first-added driver before the second");
	MW_EXPECT_TRUE(Test, bSendOrderIsReverse, "PostAdvance must pump the second-added driver before the first");
}

/** Proves pre-play frame turns leave the router inert until BeginPlay closes and opens the composition. */
MW_TEST_CASE(NetSystem_PreBeginPlayPumpsLeaveQueuedLocalRouterMessageUndelivered)
{
	FLoopback Loopback;
	FSystem System;
	int DeliveryCount = 0;
	MicroWorld::FMessageHandlerBinding Handler;
	Handler.Bind([&DeliveryCount](const MicroWorld::FMessageView&) noexcept { ++DeliveryCount; });
	MicroWorld::FMessageHandlerHandle HandlerHandle{};
	const MicroWorld::EMessageResult HandlerResult =
		System.GetRouter().AddMessageHandler(MicroWorld::FMessageTypeId{1}, MicroWorld::BroadcastActorId, std::move(Handler), HandlerHandle);
	const std::uint8_t Payload[1] = {0x31};
	const MicroWorld::EMessageResult QueueResult = System.GetRouter().BroadcastMessage(
		MicroWorld::LocalChannelId, MicroWorld::FMessageTypeId{1}, MicroWorld::BroadcastActorId, MicroWorld::TSpan<const std::uint8_t>(Payload, 1));

	System.PreAdvance(10);
	System.PostAdvance(10);
	const int DeliveriesBeforeBeginPlay = DeliveryCount;

	System.BeginPlay(20);
	System.PostAdvance(20);
	System.PreAdvance(30);
	const int DeliveriesAfterBeginPlay = DeliveryCount;
	const bool bHandlerHandleValid = HandlerHandle.IsValid();
	const bool bLoopbackStayedUnused = Loopback.IsEmpty(0) && Loopback.IsEmpty(1);

	MW_EXPECT_EQ(
		Test, MicroWorld::EMessageResult::Success, HandlerResult, "The router must register the local delivery handler before lifecycle pumping");
	MW_EXPECT_TRUE(Test, bHandlerHandleValid, "Successful local handler registration must publish a valid handle");
	MW_EXPECT_EQ(Test, MicroWorld::EMessageResult::Success, QueueResult, "The router must queue the local message before BeginPlay");
	MW_EXPECT_EQ(Test, 0, DeliveriesBeforeBeginPlay, "Pre-BeginPlay frame turns must not dispatch queued router messages");
	MW_EXPECT_EQ(Test, 1, DeliveriesAfterBeginPlay, "The first post-BeginPlay frame must dispatch the queued local router message");
	MW_EXPECT_TRUE(Test, bLoopbackStayedUnused, "A router-only lifecycle test must not emit transport packets");
}

/** Proves EndPlay stops a started client so later frame turns cannot emit another connection hello. */
MW_TEST_CASE(NetSystem_EndPlayStopsClientBeforeFuturePostAdvance)
{
	FDriverPumpSequence Sequence;
	FDriverPumpRecord Record{};
	FRecordingDriver Driver{Record, Sequence};
	FSystem System;
	MicroWorld::FNetHostConfig Config = MakeConfig();
	Config.ServerAddress = MicroWorld::MakeLoopbackAddress(0);
	const MicroWorld::FNetDriverHandle DriverHandle = System.AddNetDriver(Driver, MicroWorld::ENetMode::Client, Config);
	MicroWorld::IEngineSystem& Lifecycle = System;

	Lifecycle.BeginPlay(0);
	Lifecycle.PostAdvance(10);
	const std::size_t SendsAfterBeginPlay = Record.SendCount;
	Lifecycle.EndPlay();
	Lifecycle.PostAdvance(20);
	const std::size_t SendsAfterEndPlay = Record.SendCount;
	const bool bDriverHandleValid = DriverHandle.IsValid();

	MW_EXPECT_TRUE(Test, bDriverHandleValid, "The client driver must compose before its lifecycle turns");
	MW_EXPECT_EQ(Test, std::size_t{1}, SendsAfterBeginPlay, "A started client must emit its initial hello during PostAdvance");
	MW_EXPECT_EQ(Test, SendsAfterBeginPlay, SendsAfterEndPlay, "EndPlay must stop the client before a later PostAdvance can emit another hello");
}

/** Proves PostAdvance sends routed output before the remote system's PreAdvance delivers it to a router handler. */
MW_TEST_CASE(NetSystem_PreAdvanceAndPostAdvancePumpRoutedMessageInOrder)
{
	FLoopback Loopback;
	FSystem ServerSystem;
	FSystem ClientSystem;
	const MicroWorld::FNetHostConfig ServerConfig = MakeConfig();
	MicroWorld::FNetHostConfig ClientConfig = MakeConfig();
	ClientConfig.ServerAddress = MicroWorld::MakeLoopbackAddress(0);
	const MicroWorld::FNetDriverHandle ServerDriver =
		ServerSystem.AddNetDriver(Loopback.Port(0), MicroWorld::ENetMode::DedicatedServer, ServerConfig);
	const MicroWorld::FNetDriverHandle ClientDriver = ClientSystem.AddNetDriver(Loopback.Port(1), MicroWorld::ENetMode::Client, ClientConfig);
	const MicroWorld::FChannelHandle ServerChannel =
		ServerSystem.AddChannel(ServerDriver, MicroWorld::FMessageChannelId{1}, MicroWorld::EChannelReliability::BestEffort);
	const MicroWorld::FChannelHandle ClientChannel =
		ClientSystem.AddChannel(ClientDriver, MicroWorld::FMessageChannelId{1}, MicroWorld::EChannelReliability::BestEffort);
	int DeliveryCount = 0;
	MicroWorld::FMessageHandlerBinding Handler;
	Handler.Bind([&DeliveryCount](const MicroWorld::FMessageView&) noexcept { ++DeliveryCount; });
	MicroWorld::FMessageHandlerHandle HandlerHandle{};
	const MicroWorld::EMessageResult HandlerResult =
		ServerSystem.GetRouter().AddMessageHandler(MicroWorld::FMessageTypeId{1}, MicroWorld::BroadcastActorId, std::move(Handler), HandlerHandle);

	ServerSystem.BeginPlay(0);
	ClientSystem.BeginPlay(0);
	ClientSystem.PreAdvance(10);
	ClientSystem.PostAdvance(10);
	ServerSystem.PreAdvance(10);
	ServerSystem.PostAdvance(10);
	ClientSystem.PreAdvance(20);
	ClientSystem.PostAdvance(20);

	const std::uint8_t Payload[1] = {0x7A};
	const MicroWorld::EMessageResult SendResult = ClientSystem.GetRouter().BroadcastMessage(
		MicroWorld::FMessageChannelId{1},
		MicroWorld::FMessageTypeId{1},
		MicroWorld::FMessageActorId{1},
		MicroWorld::TSpan<const std::uint8_t>(Payload, 1));
	ClientSystem.PreAdvance(30);
	ClientSystem.PostAdvance(30);
	const int DeliveriesAfterClientPostAdvance = DeliveryCount;
	ServerSystem.PreAdvance(30);
	const int DeliveriesAfterServerPreAdvance = DeliveryCount;
	const bool bServerDriverValid = ServerDriver.IsValid();
	const bool bClientDriverValid = ClientDriver.IsValid();
	const bool bServerChannelValid = ServerChannel.IsValid();
	const bool bClientChannelValid = ClientChannel.IsValid();
	const bool bHandlerHandleValid = HandlerHandle.IsValid();

	MW_EXPECT_TRUE(Test, bServerDriverValid, "The server driver must configure for the pump-order scenario");
	MW_EXPECT_TRUE(Test, bClientDriverValid, "The client driver must configure for the pump-order scenario");
	MW_EXPECT_TRUE(Test, bServerChannelValid, "The server message channel must configure before BeginPlay");
	MW_EXPECT_TRUE(Test, bClientChannelValid, "The client message channel must configure before BeginPlay");
	MW_EXPECT_EQ(Test, MicroWorld::EMessageResult::Success, HandlerResult, "The server router must accept its delivery handler");
	MW_EXPECT_TRUE(Test, bHandlerHandleValid, "A successful router handler registration must publish a valid handle");
	MW_EXPECT_EQ(Test, MicroWorld::EMessageResult::Success, SendResult, "The connected client router must queue the outbound message");
	MW_EXPECT_EQ(Test, 0, DeliveriesAfterClientPostAdvance, "Client PostAdvance must send before the remote router can deliver the message");
	MW_EXPECT_EQ(Test, 1, DeliveriesAfterServerPreAdvance, "Server PreAdvance must receive and deliver the routed message exactly once");
}

} // namespace MicroWorld::Tests
