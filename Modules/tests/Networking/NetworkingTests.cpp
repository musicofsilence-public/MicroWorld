#include "TestSupport.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Delegates/Delegate.h>
#include <MicroWorld/Core/PlaySystem.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Networking/Networking.h>
#include <MicroWorld/Transport/HostLoopback.h>
#include <MicroWorld/Transport/DeviceAddress.h>
#include <MicroWorld/Transport/Device.h>
#include <MicroWorld/Transport/TransportHost.h>
#include <MicroWorld/Transport/TransportResult.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace MicroWorld::Tests
{

namespace
{

	/** Motivation: Provides two deterministic ports so systems exchange packets without a platform transport. */
	using FLoopback = MicroWorld::Transport::THostLoopback<2, 8, 256>;

	/** Motivation: Uses the default two-device and four-channel profile for normal composition tests. */
	using FSystem = MicroWorld::Networking::TNetworking<>;

	/**
	 * Motivation: Makes device exhaustion observable with one fixed slot.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 * Example:
	 *   // Construct and exercise the type in one behavior test.
	 */
	struct FOneDeviceTraits : MicroWorld::Networking::FDefaultNetworkingTraits
	{
		static constexpr std::size_t MaxDevices = 1;
	};

	/**
	 * Motivation: Makes channel exhaustion observable with one router and system channel slot.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 * Example:
	 *   // Construct and exercise the type in one behavior test.
	 */
	struct FOneChannelTraits : MicroWorld::Networking::FDefaultNetworkingTraits
	{
		static constexpr std::size_t MaxRouterChannels = 1;
		static constexpr std::size_t MaxChannels = 1;
	};

	/** Motivation: Message type id both router tests register and broadcast so the handler and payload stay paired. */
	constexpr MicroWorld::Messaging::FMessageTypeId SampleMessageTypeId{1};

	/** Motivation: Routed actor id the cross-system broadcast targets so sender and receiver address one logical actor. */
	constexpr MicroWorld::Messaging::FMessageActorId SampleActorId{1};

	/** Motivation: Channel id the cross-system test composes and broadcasts on so the routed message stays on one channel. */
	constexpr MicroWorld::Messaging::FMessageChannelId SampleChannelId{1};

	/** Motivation: Single-byte payload the local-router test broadcasts so a delivery is observable without transport framing. */
	constexpr std::uint8_t LocalRouterMessagePayloadByte{0x31};

	/** Motivation: Single-byte payload the cross-system test sends so a routed delivery is observable on the remote router. */
	constexpr std::uint8_t CrossSystemPayloadByte{0x7A};

	/**
	 * Motivation: Builds a host configuration accepted by both loopback roles.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	MicroWorld::Transport::FTransportHostConfig MakeConfig() noexcept
	{
		return MicroWorld::Transport::FTransportHostConfig{};
	}

	/**
	 * Motivation: Supplies a shared monotonic order source so each device's first pump is directly observable.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 * Example:
	 *   // Construct and exercise the type in one behavior test.
	 */
	class FDevicePumpSequence final
	{
	public:
		/**
		 * Motivation: Returns a unique increasing stamp for one device operation.
		 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
		 */
		std::uint32_t Next() noexcept { return ++Counter; }

	private:
		/** Motivation: Tracks the order across every device that shares this test-owned sequence. */
		std::uint32_t Counter{0};
	};

	/**
	 * Motivation: Records the first inbound, outbound, and physical-progress pump each fake device receives.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 * Example:
	 *   // Construct and exercise the type in one behavior test.
	 */
	struct FDevicePumpRecord
	{
		/** Motivation: Counts transport receive calls so the first call identifies device pump order. */
		std::size_t ReceiveCount{0};

		/** Motivation: Counts transport send calls so the first call identifies device pump order. */
		std::size_t SendCount{0};

		/** Motivation: Counts bounded transmit-progress calls independently of logical packet acceptance. */
		std::size_t AdvanceCount{0};

		/** Motivation: Holds the first receive stamp from the shared test sequence. */
		std::uint32_t FirstReceiveOrder{0};

		/** Motivation: Holds the first send stamp from the shared test sequence. */
		std::uint32_t FirstSendOrder{0};

		/** Motivation: Holds the first transmit-progress stamp from the shared test sequence. */
		std::uint32_t FirstAdvanceOrder{0};
	};

	/**
	 * Motivation: Provides a deterministic client transport whose observable operations reveal TNetworking's device
	 *   pump order.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 * Example:
	 *   // Construct and exercise the type in one behavior test.
	 */
	class FRecordingDevice final : public MicroWorld::Transport::Device::IDevice
	{
	public:
		/**
		 * Motivation: Binds the device to caller-owned observability and a deterministic logical-send outcome.
		 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
		 */
		FRecordingDevice(
			FDevicePumpRecord& InRecord,
			FDevicePumpSequence& InSequence,
			const MicroWorld::Transport::ETransportResult InSendResult = MicroWorld::Transport::ETransportResult::Success) noexcept
			: Record(InRecord), Sequence(InSequence), SendResult(InSendResult)
		{
		}

		/**
		 * Motivation: Records each outbound transport attempt and accepts it without a real network.
		 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
		 */
		MicroWorld::Transport::ETransportResult TrySend(
			const MicroWorld::Transport::Address::FDeviceAddress&, MicroWorld::Core::TSpan<const std::uint8_t>) noexcept override
		{
			++Record.SendCount;
			if (Record.FirstSendOrder == 0)
			{
				Record.FirstSendOrder = Sequence.Next();
			}
			return SendResult;
		}

		/**
		 * Motivation: Records each inbound transport attempt and reports the deterministic empty state.
		 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
		 */
		MicroWorld::Transport::ETransportResult TryReceive(
			MicroWorld::Transport::Address::FDeviceAddress&,
			MicroWorld::Core::TSpan<std::uint8_t>,
			MicroWorld::Transport::Device::FReceiveResult&) noexcept override
		{
			++Record.ReceiveCount;
			if (Record.FirstReceiveOrder == 0)
			{
				Record.FirstReceiveOrder = Sequence.Next();
			}
			return MicroWorld::Transport::ETransportResult::Unavailable;
		}

		/**
		 * Motivation: Records one bounded physical-transmit advancement after the host's logical outbound drain.
		 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
		 */
		void AdvanceTransmit() noexcept override
		{
			++Record.AdvanceCount;
			if (Record.FirstAdvanceOrder == 0)
			{
				Record.FirstAdvanceOrder = Sequence.Next();
			}
		}

		/**
		 * Motivation: Host configuration remains valid.
		 * Responsibilities: Matches the integration profile's packet budget.
		 */
		std::size_t MaxPacketBytes() const noexcept override { return 256; }

	private:
		/** Motivation: Receives this device's counts and first-operation stamps; never owned here. */
		FDevicePumpRecord& Record;

		/** Motivation: Orders operations across the two fake devices; never owned here. */
		FDevicePumpSequence& Sequence;

		/** Motivation: Makes full-device lifecycle progress observable without a real transport. */
		MicroWorld::Transport::ETransportResult SendResult;
	};

} // namespace

/**
 * Motivation: Configure two devices on one system over a loopback with two ports.
 * Responsibilities: Each device receives a valid handle with a distinct slot identity.
 */
MW_TEST_CASE(Networking_AddDeviceAcceptsTwoDevices)
{
	// Arrange
	FLoopback Loopback;
	FSystem System;
	const MicroWorld::Transport::FTransportHostConfig Config = MakeConfig();

	// Act
	const MicroWorld::Networking::FDeviceHandle FirstDevice =
		System.AddDevice(Loopback.Port(0), MicroWorld::Transport::ENetworkMode::Standalone, Config);
	const MicroWorld::Networking::FDeviceHandle SecondDevice =
		System.AddDevice(Loopback.Port(1), MicroWorld::Transport::ENetworkMode::Standalone, Config);
	const bool bFirstDeviceValid = FirstDevice.IsValid();
	const bool bSecondDeviceValid = SecondDevice.IsValid();
	const bool bDistinctSlots = FirstDevice.Index != SecondDevice.Index;

	// Assert
	MW_EXPECT_TRUE(Test, bFirstDeviceValid, "The first configured device must receive a valid handle");
	MW_EXPECT_TRUE(Test, bSecondDeviceValid, "The second configured device must receive a valid handle");
	MW_EXPECT_TRUE(Test, bDistinctSlots, "Two configured devices must receive distinct slot identities");
}

/**
 * Motivation: Add a best-effort and a guaranteed channel on one configured device.
 * Responsibilities: Each reliability mode receives a valid channel handle without exposing internal wrappers.
 */
MW_TEST_CASE(Networking_AddChannelAcceptsBestEffortAndGuaranteedOnOneDevice)
{
	// Arrange
	FLoopback Loopback;
	FSystem System;
	const MicroWorld::Transport::FTransportHostConfig Config = MakeConfig();
	const MicroWorld::Networking::FDeviceHandle Device = System.AddDevice(Loopback.Port(0), MicroWorld::Transport::ENetworkMode::Standalone, Config);

	// Act
	const MicroWorld::Networking::FChannelHandle BestEffort =
		System.AddChannel(Device, MicroWorld::Messaging::FMessageChannelId{1}, MicroWorld::Networking::EChannelReliability::BestEffort);
	const MicroWorld::Networking::FChannelHandle Guaranteed =
		System.AddChannel(Device, MicroWorld::Messaging::FMessageChannelId{2}, MicroWorld::Networking::EChannelReliability::Guaranteed);
	const bool bDeviceValid = Device.IsValid();
	const bool bBestEffortValid = BestEffort.IsValid();
	const bool bGuaranteedValid = Guaranteed.IsValid();

	// Assert
	MW_EXPECT_TRUE(Test, bDeviceValid, "Channel setup requires a valid configured device handle");
	MW_EXPECT_TRUE(Test, bBestEffortValid, "A best-effort channel must receive a valid handle");
	MW_EXPECT_TRUE(Test, bGuaranteedValid, "A guaranteed channel must receive a valid handle");
}

/**
 * Motivation: Forge a device handle with a mismatched generation and attempt to add a channel, then add a channel
 *   on the current device.
 * Responsibilities: The forged-generation request is rejected and leaves the current device slot usable.
 */
MW_TEST_CASE(Networking_AddChannelRejectsForgedDeviceGeneration)
{
	// Arrange
	FLoopback Loopback;
	FSystem System;
	const MicroWorld::Transport::FTransportHostConfig Config = MakeConfig();
	const MicroWorld::Networking::FDeviceHandle Device = System.AddDevice(Loopback.Port(0), MicroWorld::Transport::ENetworkMode::Standalone, Config);
	const MicroWorld::Networking::FDeviceHandle StaleDevice{Device.Index, static_cast<std::uint8_t>(Device.Generation + 1)};

	// Act
	const MicroWorld::Networking::FChannelHandle RejectedChannel =
		System.AddChannel(StaleDevice, MicroWorld::Messaging::FMessageChannelId{1}, MicroWorld::Networking::EChannelReliability::BestEffort);
	const MicroWorld::Networking::FChannelHandle CurrentChannel =
		System.AddChannel(Device, MicroWorld::Messaging::FMessageChannelId{1}, MicroWorld::Networking::EChannelReliability::BestEffort);
	const bool bDeviceValid = Device.IsValid();
	const bool bRejectedChannelValid = RejectedChannel.IsValid();
	const bool bCurrentChannelValid = CurrentChannel.IsValid();

	// Assert
	MW_EXPECT_TRUE(Test, bDeviceValid, "The original device handle must be valid before forging a stale one");
	MW_EXPECT_TRUE(Test, !bRejectedChannelValid, "A mismatched device generation must reject channel creation");
	MW_EXPECT_TRUE(Test, bCurrentChannelValid, "A stale-handle rejection must leave the current device usable");
}

/**
 * Motivation: Fill a one-device system and attempt to add a second device.
 * Responsibilities: The device beyond fixed capacity is rejected with an invalid handle.
 */
MW_TEST_CASE(Networking_AddDeviceRejectsCapacityExhaustion)
{
	// Arrange
	FLoopback Loopback;
	MicroWorld::Networking::TNetworking<FOneDeviceTraits> System;
	const MicroWorld::Transport::FTransportHostConfig Config = MakeConfig();

	// Act
	const MicroWorld::Networking::FDeviceHandle AcceptedDevice =
		System.AddDevice(Loopback.Port(0), MicroWorld::Transport::ENetworkMode::Standalone, Config);
	const MicroWorld::Networking::FDeviceHandle RejectedDevice =
		System.AddDevice(Loopback.Port(1), MicroWorld::Transport::ENetworkMode::Standalone, Config);
	const bool bAcceptedDeviceValid = AcceptedDevice.IsValid();
	const bool bRejectedDeviceValid = RejectedDevice.IsValid();

	// Assert
	MW_EXPECT_TRUE(Test, bAcceptedDeviceValid, "The only available device slot must accept its first device");
	MW_EXPECT_TRUE(Test, !bRejectedDeviceValid, "A device beyond fixed capacity must return an invalid handle");
}

/**
 * Motivation: Fill a one-channel device and attempt to add a second channel.
 * Responsibilities: The channel beyond fixed capacity is rejected with an invalid handle and does not disturb the
 *   accepted predecessor.
 */
MW_TEST_CASE(Networking_AddChannelRejectsCapacityExhaustion)
{
	// Arrange
	FLoopback Loopback;
	MicroWorld::Networking::TNetworking<FOneChannelTraits> System;
	const MicroWorld::Transport::FTransportHostConfig Config = MakeConfig();
	const MicroWorld::Networking::FDeviceHandle Device = System.AddDevice(Loopback.Port(0), MicroWorld::Transport::ENetworkMode::Standalone, Config);

	// Act
	const MicroWorld::Networking::FChannelHandle AcceptedChannel =
		System.AddChannel(Device, MicroWorld::Messaging::FMessageChannelId{1}, MicroWorld::Networking::EChannelReliability::BestEffort);
	const MicroWorld::Networking::FChannelHandle RejectedChannel =
		System.AddChannel(Device, MicroWorld::Messaging::FMessageChannelId{2}, MicroWorld::Networking::EChannelReliability::BestEffort);
	const bool bDeviceValid = Device.IsValid();
	const bool bAcceptedChannelValid = AcceptedChannel.IsValid();
	const bool bRejectedChannelValid = RejectedChannel.IsValid();

	// Assert
	MW_EXPECT_TRUE(Test, bDeviceValid, "Channel capacity setup requires one valid device");
	MW_EXPECT_TRUE(Test, bAcceptedChannelValid, "The only available channel slot must accept its first channel");
	MW_EXPECT_TRUE(Test, !bRejectedChannelValid, "A channel beyond fixed capacity must return an invalid handle");
}

/**
 * Motivation: Scenario: Configure a client device and channel, pump before BeginPlay, then close composition with
 *   BeginPlay, attempt late composition, and pump once more.
 * Responsibilities: Expected: No packet crosses the transport before BeginPlay; afterward composition is frozen and the
 *   host starts to emit packets.
 */
MW_TEST_CASE(Networking_BeginPlayFinalizesCompositionAndDefersHostStart)
{
	// Arrange
	FLoopback Loopback;
	FSystem System;
	MicroWorld::Transport::FTransportHostConfig ClientConfig = MakeConfig();
	ClientConfig.ServerAddress = MicroWorld::Transport::Address::MakeLoopbackAddress(1);
	const MicroWorld::Networking::FDeviceHandle Device =
		System.AddDevice(Loopback.Port(0), MicroWorld::Transport::ENetworkMode::Client, ClientConfig);
	const MicroWorld::Networking::FChannelHandle InitialChannel =
		System.AddChannel(Device, MicroWorld::Messaging::FMessageChannelId{1}, MicroWorld::Networking::EChannelReliability::BestEffort);

	// Act: pump before BeginPlay and confirm no packet has crossed the transport yet.
	System.PreAdvance(10);
	System.PostAdvance(10);
	const bool bNoPacketBeforeBeginPlay = Loopback.IsEmpty(1);

	// Act: close composition with BeginPlay, attempt late composition, and pump once more.
	System.BeginPlay(20);
	const MicroWorld::Networking::FChannelHandle LateChannel =
		System.AddChannel(Device, MicroWorld::Messaging::FMessageChannelId{2}, MicroWorld::Networking::EChannelReliability::BestEffort);
	const MicroWorld::Networking::FDeviceHandle LateDevice =
		System.AddDevice(Loopback.Port(1), MicroWorld::Transport::ENetworkMode::Standalone, MakeConfig());
	System.PostAdvance(20);
	const bool bPacketQueuedAfterBeginPlay = !Loopback.IsEmpty(1);
	const bool bDeviceValid = Device.IsValid();
	const bool bInitialChannelValid = InitialChannel.IsValid();
	const bool bLateChannelValid = LateChannel.IsValid();
	const bool bLateDeviceValid = LateDevice.IsValid();

	// Assert
	MW_EXPECT_TRUE(Test, bDeviceValid, "The client device must configure before the lifecycle starts");
	MW_EXPECT_TRUE(Test, bInitialChannelValid, "The initial channel must configure before composition freezes");
	MW_EXPECT_TRUE(Test, bNoPacketBeforeBeginPlay, "A configured host must not emit packets before BeginPlay starts it");
	MW_EXPECT_TRUE(Test, !bLateChannelValid, "BeginPlay must freeze later channel composition for the completed network system");
	MW_EXPECT_TRUE(Test, !bLateDeviceValid, "BeginPlay must freeze later device composition for the completed network system");
	MW_EXPECT_TRUE(Test, bPacketQueuedAfterBeginPlay, "BeginPlay must start the client host before its next outbound pump");
}

/**
 * Motivation: Compose two recording devices and run one BeginPlay plus one PreAdvance/PostAdvance cycle.
 * Responsibilities: Inbound pumps run in forward add order, and outbound and physical-progress pumps run in reverse add
 *   order.
 */
MW_TEST_CASE(Networking_CoreLifecyclePumpsDevicesInForwardAndReverseOrder)
{
	// Arrange
	FDevicePumpSequence Sequence;
	FDevicePumpRecord FirstRecord{};
	FDevicePumpRecord SecondRecord{};
	FRecordingDevice FirstDevice{FirstRecord, Sequence};
	FRecordingDevice SecondDevice{SecondRecord, Sequence};
	FSystem System;
	MicroWorld::Transport::FTransportHostConfig Config = MakeConfig();
	Config.ServerAddress = MicroWorld::Transport::Address::MakeLoopbackAddress(0);

	const MicroWorld::Networking::FDeviceHandle FirstHandle = System.AddDevice(FirstDevice, MicroWorld::Transport::ENetworkMode::Client, Config);
	const MicroWorld::Networking::FDeviceHandle SecondHandle = System.AddDevice(SecondDevice, MicroWorld::Transport::ENetworkMode::Client, Config);
	MicroWorld::Core::IPlaySystem& Lifecycle = System;

	// Act: one BeginPlay plus one PreAdvance/PostAdvance cycle pumps every recording device.
	Lifecycle.BeginPlay(0);
	Lifecycle.PreAdvance(10);
	Lifecycle.PostAdvance(10);

	const bool bFirstHandleValid = FirstHandle.IsValid();
	const bool bSecondHandleValid = SecondHandle.IsValid();
	const bool bFirstDeviceReceived = FirstRecord.ReceiveCount > 0;
	const bool bSecondDeviceReceived = SecondRecord.ReceiveCount > 0;
	const bool bFirstDeviceSent = FirstRecord.SendCount > 0;
	const bool bSecondDeviceSent = SecondRecord.SendCount > 0;
	const bool bFirstDeviceAdvanced = FirstRecord.AdvanceCount == 1;
	const bool bSecondDeviceAdvanced = SecondRecord.AdvanceCount == 1;
	const bool bReceiveOrderIsForward = FirstRecord.FirstReceiveOrder < SecondRecord.FirstReceiveOrder;
	const bool bSendOrderIsReverse = SecondRecord.FirstSendOrder < FirstRecord.FirstSendOrder;
	const bool bAdvanceOrderIsReverse = SecondRecord.FirstAdvanceOrder < FirstRecord.FirstAdvanceOrder;
	const bool bSecondAdvanceFollowsSend = SecondRecord.FirstSendOrder < SecondRecord.FirstAdvanceOrder;
	const bool bFirstAdvanceFollowsSend = FirstRecord.FirstSendOrder < FirstRecord.FirstAdvanceOrder;

	// Assert
	MW_EXPECT_TRUE(Test, bFirstHandleValid, "The first recording device must compose before lifecycle pumping");
	MW_EXPECT_TRUE(Test, bSecondHandleValid, "The second recording device must compose before lifecycle pumping");
	MW_EXPECT_TRUE(Test, bFirstDeviceReceived, "PreAdvance must pump the first live device");
	MW_EXPECT_TRUE(Test, bSecondDeviceReceived, "PreAdvance must pump the second live device");
	MW_EXPECT_TRUE(Test, bFirstDeviceSent, "PostAdvance must pump the first live device");
	MW_EXPECT_TRUE(Test, bSecondDeviceSent, "PostAdvance must pump the second live device");
	MW_EXPECT_TRUE(Test, bFirstDeviceAdvanced, "PostAdvance must advance the first device exactly once after logical sends");
	MW_EXPECT_TRUE(Test, bSecondDeviceAdvanced, "PostAdvance must advance the second device exactly once after logical sends");
	MW_EXPECT_TRUE(Test, bReceiveOrderIsForward, "PreAdvance must pump the first-added device before the second");
	MW_EXPECT_TRUE(Test, bSendOrderIsReverse, "PostAdvance must pump the second-added device before the first");
	MW_EXPECT_TRUE(Test, bAdvanceOrderIsReverse, "PostAdvance must advance the second-added device before the first");
	MW_EXPECT_TRUE(Test, bSecondAdvanceFollowsSend, "The second device's physical progress must follow its logical send attempt");
	MW_EXPECT_TRUE(Test, bFirstAdvanceFollowsSend, "The first device's physical progress must follow its logical send attempt");
}

/**
 * Motivation: Compose an idle dedicated server device and a full client device, then run one BeginPlay plus one
 *   PostAdvance.
 * Responsibilities: Each non-standalone device advances transport even when it has no packet or its device is full.
 */
MW_TEST_CASE(Networking_PostAdvanceAdvancesIdleAndFullDevices)
{
	// Arrange
	FDevicePumpSequence Sequence;
	FDevicePumpRecord IdleRecord{};
	FDevicePumpRecord FullRecord{};
	FRecordingDevice IdleDevice{IdleRecord, Sequence};
	FRecordingDevice FullDevice{FullRecord, Sequence, MicroWorld::Transport::ETransportResult::Full};
	FSystem System;
	MicroWorld::Transport::FTransportHostConfig ClientConfig = MakeConfig();
	ClientConfig.ServerAddress = MicroWorld::Transport::Address::MakeLoopbackAddress(0);

	const MicroWorld::Networking::FDeviceHandle IdleHandle =
		System.AddDevice(IdleDevice, MicroWorld::Transport::ENetworkMode::DedicatedServer, MakeConfig());
	const MicroWorld::Networking::FDeviceHandle FullHandle = System.AddDevice(FullDevice, MicroWorld::Transport::ENetworkMode::Client, ClientConfig);

	// Act: one BeginPlay plus one PostAdvance exposes both the idle and full-device pump paths.
	System.BeginPlay(0);
	System.PostAdvance(10);

	const bool bIdleHandleValid = IdleHandle.IsValid();
	const bool bFullHandleValid = FullHandle.IsValid();
	const bool bIdleDeviceWasNotSent = IdleRecord.SendCount == 0;
	const bool bIdleDeviceAdvanced = IdleRecord.AdvanceCount == 1;
	const bool bFullDeviceAttemptedSend = FullRecord.SendCount == 1;
	const bool bFullDeviceAdvanced = FullRecord.AdvanceCount == 1;

	// Assert
	MW_EXPECT_TRUE(Test, bIdleHandleValid, "The idle server device must compose before lifecycle pumping");
	MW_EXPECT_TRUE(Test, bFullHandleValid, "The full client device must compose before lifecycle pumping");
	MW_EXPECT_TRUE(Test, bIdleDeviceWasNotSent, "An idle dedicated server must have no logical packet to send");
	MW_EXPECT_TRUE(Test, bIdleDeviceAdvanced, "An idle non-standalone device must still advance pending physical transmission");
	MW_EXPECT_TRUE(Test, bFullDeviceAttemptedSend, "A connecting client must attempt its queued hello even when the device is full");
	MW_EXPECT_TRUE(Test, bFullDeviceAdvanced, "A full device must still advance any previously staged physical transmission");
}

/**
 * Motivation: Register a router handler, queue a local broadcast, pump before BeginPlay, then open composition
 *   with BeginPlay and pump again.
 * Responsibilities: Pre-BeginPlay pumps leave the queued message undelivered; the first post-BeginPlay pump delivers it
 *   without emitting transport packets.
 */
MW_TEST_CASE(Networking_PreBeginPlayPumpsLeaveQueuedLocalRouterMessageUndelivered)
{
	// Arrange
	FLoopback Loopback;
	FSystem System;
	int DeliveryCount = 0;
	MicroWorld::Messaging::FMessageHandlerBinding Handler;
	Handler.Bind([&DeliveryCount](const MicroWorld::Messaging::FMessageView&) noexcept { ++DeliveryCount; });
	MicroWorld::Messaging::FMessageHandlerHandle HandlerHandle{};
	const MicroWorld::Messaging::EMessageResult HandlerResult =
		System.GetRouter().AddMessageHandler(SampleMessageTypeId, MicroWorld::Messaging::BroadcastActorId, std::move(Handler), HandlerHandle);
	const std::uint8_t Payload[1] = {LocalRouterMessagePayloadByte};
	const MicroWorld::Messaging::EMessageResult QueueResult = System.GetRouter().BroadcastMessage(
		MicroWorld::Messaging::LocalChannelId,
		SampleMessageTypeId,
		MicroWorld::Messaging::BroadcastActorId,
		MicroWorld::Core::TSpan<const std::uint8_t>(Payload, 1));

	// Act: pump before BeginPlay and confirm the queued local message has still not been dispatched.
	System.PreAdvance(10);
	System.PostAdvance(10);
	const int DeliveriesBeforeBeginPlay = DeliveryCount;

	// Act: open composition with BeginPlay and pump once so the queued message becomes eligible.
	System.BeginPlay(20);
	System.PostAdvance(20);
	System.PreAdvance(30);
	const int DeliveriesAfterBeginPlay = DeliveryCount;
	const bool bHandlerHandleValid = HandlerHandle.IsValid();
	const bool bLoopbackStayedUnused = Loopback.IsEmpty(0) && Loopback.IsEmpty(1);

	// Assert
	MW_EXPECT_EQ(
		Test,
		MicroWorld::Messaging::EMessageResult::Success,
		HandlerResult,
		"The router must register the local delivery handler before lifecycle pumping");
	MW_EXPECT_TRUE(Test, bHandlerHandleValid, "Successful local handler registration must publish a valid handle");
	MW_EXPECT_EQ(Test, MicroWorld::Messaging::EMessageResult::Success, QueueResult, "The router must queue the local message before BeginPlay");
	MW_EXPECT_EQ(Test, 0, DeliveriesBeforeBeginPlay, "Pre-BeginPlay frame turns must not dispatch queued router messages");
	MW_EXPECT_EQ(Test, 1, DeliveriesAfterBeginPlay, "The first post-BeginPlay frame must dispatch the queued local router message");
	MW_EXPECT_TRUE(Test, bLoopbackStayedUnused, "A router-only lifecycle test must not emit transport packets");
}

/**
 * Motivation: BeginPlay and one PostAdvance flush a client's initial hello, then EndPlay runs before a later
 *   PostAdvance.
 * Responsibilities: The later PostAdvance cannot emit another connection hello after EndPlay stops the client.
 */
MW_TEST_CASE(Networking_EndPlayStopsClientBeforeFuturePostAdvance)
{
	// Arrange
	FDevicePumpSequence Sequence;
	FDevicePumpRecord Record{};
	FRecordingDevice Device{Record, Sequence};
	FSystem System;
	MicroWorld::Transport::FTransportHostConfig Config = MakeConfig();
	Config.ServerAddress = MicroWorld::Transport::Address::MakeLoopbackAddress(0);
	const MicroWorld::Networking::FDeviceHandle DeviceHandle = System.AddDevice(Device, MicroWorld::Transport::ENetworkMode::Client, Config);
	MicroWorld::Core::IPlaySystem& Lifecycle = System;

	// Act: BeginPlay and one PostAdvance flush the initial connection hello.
	Lifecycle.BeginPlay(0);
	Lifecycle.PostAdvance(10);
	const std::size_t SendsAfterBeginPlay = Record.SendCount;
	// Act: EndPlay stops the host before a later PostAdvance could flush another hello.
	Lifecycle.EndPlay();
	Lifecycle.PostAdvance(20);
	const std::size_t SendsAfterEndPlay = Record.SendCount;
	const bool bDeviceHandleValid = DeviceHandle.IsValid();

	// Assert
	MW_EXPECT_TRUE(Test, bDeviceHandleValid, "The client device must compose before its lifecycle turns");
	MW_EXPECT_EQ(Test, std::size_t{1}, SendsAfterBeginPlay, "A started client must emit its initial hello during PostAdvance");
	MW_EXPECT_EQ(Test, SendsAfterBeginPlay, SendsAfterEndPlay, "EndPlay must stop the client before a later PostAdvance can emit another hello");
}

/**
 * Motivation: Scenario: Connect a client and server system, register a server router handler, broadcast from the
 *   client, and alternate PreAdvance/PostAdvance turns.
 * Responsibilities: Expected: The client's PostAdvance sends the routed message before the remote server's PreAdvance
 *   delivers it exactly once.
 */
MW_TEST_CASE(Networking_PreAdvanceAndPostAdvancePumpRoutedMessageInOrder)
{
	// Arrange
	FLoopback Loopback;
	FSystem ServerSystem;
	FSystem ClientSystem;
	const MicroWorld::Transport::FTransportHostConfig ServerConfig = MakeConfig();
	MicroWorld::Transport::FTransportHostConfig ClientConfig = MakeConfig();
	ClientConfig.ServerAddress = MicroWorld::Transport::Address::MakeLoopbackAddress(0);
	const MicroWorld::Networking::FDeviceHandle ServerDevice =
		ServerSystem.AddDevice(Loopback.Port(0), MicroWorld::Transport::ENetworkMode::DedicatedServer, ServerConfig);
	const MicroWorld::Networking::FDeviceHandle ClientDevice =
		ClientSystem.AddDevice(Loopback.Port(1), MicroWorld::Transport::ENetworkMode::Client, ClientConfig);
	const MicroWorld::Networking::FChannelHandle ServerChannel =
		ServerSystem.AddChannel(ServerDevice, MicroWorld::Messaging::FMessageChannelId{1}, MicroWorld::Networking::EChannelReliability::BestEffort);
	const MicroWorld::Networking::FChannelHandle ClientChannel =
		ClientSystem.AddChannel(ClientDevice, MicroWorld::Messaging::FMessageChannelId{1}, MicroWorld::Networking::EChannelReliability::BestEffort);
	int DeliveryCount = 0;
	MicroWorld::Messaging::FMessageHandlerBinding Handler;
	Handler.Bind([&DeliveryCount](const MicroWorld::Messaging::FMessageView&) noexcept { ++DeliveryCount; });
	MicroWorld::Messaging::FMessageHandlerHandle HandlerHandle{};
	const MicroWorld::Messaging::EMessageResult HandlerResult =
		ServerSystem.GetRouter().AddMessageHandler(SampleMessageTypeId, MicroWorld::Messaging::BroadcastActorId, std::move(Handler), HandlerHandle);

	// Act: BeginPlay plus alternating PreAdvance/PostAdvance turns connect the client to the server.
	ServerSystem.BeginPlay(0);
	ClientSystem.BeginPlay(0);
	ClientSystem.PreAdvance(10);
	ClientSystem.PostAdvance(10);
	ServerSystem.PreAdvance(10);
	ServerSystem.PostAdvance(10);
	ClientSystem.PreAdvance(20);
	ClientSystem.PostAdvance(20);

	// Act: broadcast from the client, then advance to observe send-before-delivery ordering.
	const std::uint8_t Payload[1] = {CrossSystemPayloadByte};
	const MicroWorld::Messaging::EMessageResult SendResult = ClientSystem.GetRouter().BroadcastMessage(
		SampleChannelId, SampleMessageTypeId, SampleActorId, MicroWorld::Core::TSpan<const std::uint8_t>(Payload, 1));
	ClientSystem.PreAdvance(30);
	ClientSystem.PostAdvance(30);
	const int DeliveriesAfterClientPostAdvance = DeliveryCount;
	ServerSystem.PreAdvance(30);
	const int DeliveriesAfterServerPreAdvance = DeliveryCount;
	const bool bServerDeviceValid = ServerDevice.IsValid();
	const bool bClientDeviceValid = ClientDevice.IsValid();
	const bool bServerChannelValid = ServerChannel.IsValid();
	const bool bClientChannelValid = ClientChannel.IsValid();
	const bool bHandlerHandleValid = HandlerHandle.IsValid();

	// Assert
	MW_EXPECT_TRUE(Test, bServerDeviceValid, "The server device must configure for the pump-order scenario");
	MW_EXPECT_TRUE(Test, bClientDeviceValid, "The client device must configure for the pump-order scenario");
	MW_EXPECT_TRUE(Test, bServerChannelValid, "The server message channel must configure before BeginPlay");
	MW_EXPECT_TRUE(Test, bClientChannelValid, "The client message channel must configure before BeginPlay");
	MW_EXPECT_EQ(Test, MicroWorld::Messaging::EMessageResult::Success, HandlerResult, "The server router must accept its delivery handler");
	MW_EXPECT_TRUE(Test, bHandlerHandleValid, "A successful router handler registration must publish a valid handle");
	MW_EXPECT_EQ(Test, MicroWorld::Messaging::EMessageResult::Success, SendResult, "The connected client router must queue the outbound message");
	MW_EXPECT_EQ(Test, 0, DeliveriesAfterClientPostAdvance, "Client PostAdvance must send before the remote router can deliver the message");
	MW_EXPECT_EQ(Test, 1, DeliveriesAfterServerPreAdvance, "Server PreAdvance must receive and deliver the routed message exactly once");
}

} // namespace MicroWorld::Tests
