#include "TestSupport.h"
#include "EngineMessagingTestHelpers.h"

#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/ReceiveResult.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Core/TickConfiguration.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineNetworkSetup.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessagingSystem.h>
#include <MicroWorld/Messaging/TypedMessageCodec.h>
#include <MicroWorld/Networking/ConnectRejected.h>
#include <MicroWorld/Networking/ConnectRequest.h>
#include <MicroWorld/Networking/ConnectAccepted.h>
#include <MicroWorld/Networking/NetworkSystem.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

using namespace ::MicroWorld::Tests;

using MicroWorld::Core::ERuntimeResult;
using MicroWorld::Core::ETransportResult;
using MicroWorld::Core::FDeviceAddress;
using MicroWorld::Core::FReceiveResult;
using MicroWorld::Core::ITransportDevice;
using MicroWorld::Core::MakeLoopbackAddress;
using MicroWorld::Core::TimePointMilliseconds;
using MicroWorld::Core::TSpan;
using MicroWorld::Engine::EActorSpawnRequestResult;
using MicroWorld::Engine::EEngineNetworkSetupResult;
using MicroWorld::Engine::FEngineNetworkSetup;
using MicroWorld::Messaging::DecodeTypedMessage;
using MicroWorld::Messaging::EMessagingResult;
using MicroWorld::Messaging::FMessage;
using MicroWorld::Messaging::FNameId;
using MicroWorld::Networking::EConnectionRejectReason;
using MicroWorld::Networking::ENetworkRole;
using MicroWorld::Networking::FConnectRejected;
using MicroWorld::Networking::FConnectRequest;

/** Motivation: Identifies the addressed server endpoint in the three-node host-only transport fixture. */
constexpr FDeviceAddress ServerAddress = MakeLoopbackAddress(1);
/** Motivation: Identifies the product client's sole addressed endpoint in the host-only transport fixture. */
constexpr FDeviceAddress ProductClientAddress = MakeLoopbackAddress(2);
/** Motivation: Identifies the second-client admission probe without claiming product support for that client. */
constexpr FDeviceAddress ProbeClientAddress = MakeLoopbackAddress(3);
/** Motivation: Paces retry and heartbeat turns quickly enough for deterministic host proof. */
constexpr MicroWorld::Core::DurationMilliseconds HeartbeatIntervalMilliseconds{10};
/** Motivation: Bounds the deliberate no-traffic interval needed to prove both live product sides retire. */
constexpr MicroWorld::Core::DurationMilliseconds PeerTimeoutMilliseconds{30};

/**
 * Motivation: Retains one complete addressed packet until its owning staged device reaches its next pre-advance turn.
 * Responsibilities: Preserve source, destination, and bounded bytes without allocating or interpreting the transport payload.
 * Example: FQueuedPacket Packet{};
 */
struct FQueuedPacket final
{
	/** Motivation: Records the addressed source required by the receiving Messaging link. */
	FDeviceAddress From{};
	/** Motivation: Records the addressed destination selected by the sending Messaging link. */
	FDeviceAddress To{};
	/** Motivation: Owns one complete packet through the staged transport handoff. */
	std::array<std::uint8_t, MicroWorld::Messaging::FMessagingSystem::MaxFrameBytes> Bytes{};
	/** Motivation: Bounds the meaningful leading packet bytes in Bytes. */
	std::size_t ByteCount{};
};

/**
 * Motivation: Makes one World's independent tick progress externally observable while network state remains false.
 * Responsibilities: Count only actor tick calls and own no runtime or networking dependency.
 * Example: FWorldTickObservation Observation{};
 */
struct FWorldTickObservation final
{
	/** Motivation: Counts completed World actor ticks across Engine frames. */
	std::size_t TickCount{};
};

/**
 * Motivation: Proves World advancement independently of the live-peer query used by the product-facing indicator path.
 * Responsibilities: Increment its caller-owned observation exactly once for every actor tick.
 * Example: World.SpawnActor<FWorldTickActor>(&Observation);
 */
class FWorldTickActor final : public MicroWorld::Engine::AActor
{
public:
	/**
	 * Motivation: Connects the actor's short lifetime to durable host-test observation storage.
	 * Responsibilities: Retain the optional observation pointer without accessing Engine networking state.
	 */
	explicit FWorldTickActor(FWorldTickObservation* const InObservation) noexcept
		: AActor(MicroWorld::Core::FTickConfiguration::EnabledEvery(0)), Observation(InObservation)
	{
	}

protected:
	/**
	 * Motivation: Records World progress even while no active network peer exists.
	 * Responsibilities: Increment the external count only when the test supplied a valid observation.
	 */
	void Tick(const MicroWorld::Core::FTickContext&) noexcept override
	{
		if (Observation != nullptr)
		{
			++Observation->TickCount;
		}
	}

private:
	/** Motivation: Observes actor ticks outside the World-owned actor lifetime. */
	FWorldTickObservation* Observation{};
};

/**
 * Motivation: Simulates a bounded addressed medium where sending and delivery are separate Engine-driven turns.
 * Responsibilities: Queue TrySend packets, transfer or drop them only from the same device's next PreAdvance, and retain
 *   narrow protocol observations needed by this integration fixture.
 * Example: FPumpGatedTransportDevice Client{ProductClientAddress};
 */
class FPumpGatedTransportDevice final : public ITransportDevice
{
public:
	/** Motivation: Bounds all fixture packet storage to a small deterministic mailbox. */
	static constexpr std::size_t MaxQueuedPackets = 8;

	/**
	 * Motivation: Gives this device one stable addressed identity before peers are attached.
	 * Responsibilities: Retain only the supplied address and begin with transfer disabled for initial-loss proof.
	 */
	explicit FPumpGatedTransportDevice(const FDeviceAddress& InAddress) noexcept : Address(InAddress) {}

	/**
	 * Motivation: Completes the fixed three-device medium after every device has a stable address.
	 * Responsibilities: Retain non-owning peer pointers that outlive this host fixture and reject no valid fixture endpoint.
	 */
	void BindPeers(FPumpGatedTransportDevice& InServer, FPumpGatedTransportDevice& InProductClient, FPumpGatedTransportDevice& InProbeClient) noexcept
	{
		Peers = {&InServer, &InProductClient, &InProbeClient};
	}

	/**
	 * Motivation: Separates packet admission from delivery so the test can model loss and exact next-turn transfer timing.
	 * Responsibilities: Enable or drop all queued transfers only when this device's subsequent PreAdvance runs.
	 */
	void SetTransfersEnabled(const bool bInTransfersEnabled) noexcept { bTransfersEnabled = bInTransfersEnabled; }

	/**
	 * Motivation: Lets assertions distinguish queued packets from a packet already delivered during a later Engine frame.
	 * Responsibilities: Return the current outbound queue depth without mutation.
	 */
	std::size_t PendingOutboundPacketCount() const noexcept { return OutboundPacketCount; }

	/**
	 * Motivation: Exposes packets not yet consumed by this Engine's Messaging receive turn.
	 * Responsibilities: Return the inbound FIFO depth without mutation.
	 */
	std::size_t PendingInboundPacketCount() const noexcept { return InboundPacketCount; }

	/**
	 * Motivation: Lets Messaging submit one complete addressed packet without allowing synchronous peer delivery.
	 * Responsibilities: Copy a routable bounded packet into the outbound queue and return Full when the fixture mailbox is full.
	 */
	ETransportResult TrySend(const FDeviceAddress& InTo, const TSpan<const std::uint8_t> InPacket) noexcept override
	{
		++TrySendCallCount;
		if (FindPeer(InTo) == nullptr || InPacket.Size() > MaxPacketBytes() || (InPacket.Size() != 0 && InPacket.Data() == nullptr))
		{
			return ETransportResult::Invalid;
		}
		if (OutboundPacketCount == MaxQueuedPackets)
		{
			return ETransportResult::Full;
		}

		FQueuedPacket& Packet = OutboundPackets[OutboundPacketCount];
		Packet.From = Address;
		Packet.To = InTo;
		Packet.ByteCount = InPacket.Size();
		for (std::size_t ByteIndex = 0; ByteIndex < Packet.ByteCount; ++ByteIndex)
		{
			Packet.Bytes[ByteIndex] = InPacket[ByteIndex];
		}
		++OutboundPacketCount;
		return ETransportResult::Success;
	}

	/**
	 * Motivation: Gives Engine-owned Messaging one packet at a time from this device's received mailbox.
	 * Responsibilities: Copy a complete queued packet and its source only on Success; otherwise leave all caller outputs unchanged.
	 */
	ETransportResult TryReceive(FDeviceAddress& OutFrom, TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept override
	{
		if (InboundPacketCount == 0)
		{
			return ETransportResult::Unavailable;
		}

		const FQueuedPacket& Packet = InboundPackets[0];
		if (Packet.ByteCount > InDestination.Size())
		{
			return ETransportResult::Full;
		}
		for (std::size_t ByteIndex = 0; ByteIndex < Packet.ByteCount; ++ByteIndex)
		{
			InDestination[ByteIndex] = Packet.Bytes[ByteIndex];
		}
		OutFrom = Packet.From;
		OutResult.BytesReceived = Packet.ByteCount;
		++SuccessfulReceiveCount;
		InspectProtocolPacket(Packet);
		RemoveFirstInboundPacket();
		return ETransportResult::Success;
	}

	/**
	 * Motivation: Lets Engine-owned Messaging preflight the maximum complete packet this host fixture accepts.
	 * Responsibilities: Return the fixed frame capacity shared by the Engine-owned Messaging system.
	 */
	std::size_t MaxPacketBytes() const noexcept override { return MicroWorld::Messaging::FMessagingSystem::MaxFrameBytes; }

	/**
	 * Motivation: Observes Engine lifecycle ownership of each fixture device.
	 * Responsibilities: Increment only the begin-play observation count.
	 */
	void BeginPlay(TimePointMilliseconds) noexcept override { ++BeginPlayCallCount; }

	/**
	 * Motivation: Establishes the earliest permitted queued-packet transfer point in each Engine frame.
	 * Responsibilities: Transfer or drop every packet queued before this turn, then record exactly one Engine-driven pre-advance turn.
	 */
	void PreAdvance(TimePointMilliseconds) noexcept override
	{
		++PreAdvanceCallCount;
		for (std::size_t PacketIndex = 0; PacketIndex < OutboundPacketCount; ++PacketIndex)
		{
			const FQueuedPacket& Packet = OutboundPackets[PacketIndex];
			FPumpGatedTransportDevice* const Peer = FindPeer(Packet.To);
			if (!bTransfersEnabled || Peer == nullptr || !Peer->QueueInbound(Packet))
			{
				++DroppedTransferCount;
			}
		}
		OutboundPacketCount = 0;
	}

	/**
	 * Motivation: Observes the matching Engine-driven post-advance turn without adding transport behavior.
	 * Responsibilities: Increment only the post-advance observation count.
	 */
	void PostAdvance(TimePointMilliseconds) noexcept override { ++PostAdvanceCallCount; }

	/**
	 * Motivation: Observes that Engine ends every configured fixture device exactly once.
	 * Responsibilities: Increment only the end-play observation count.
	 */
	void EndPlay() noexcept override { ++EndPlayCallCount; }

	/** Motivation: Counts Messaging send attempts accepted by this staged transport. */
	std::size_t TrySendCallCount{};
	/** Motivation: Counts Engine BeginPlay calls delivered to this device. */
	std::size_t BeginPlayCallCount{};
	/** Motivation: Counts Engine PreAdvance calls delivered to this device. */
	std::size_t PreAdvanceCallCount{};
	/** Motivation: Counts Engine PostAdvance calls delivered to this device. */
	std::size_t PostAdvanceCallCount{};
	/** Motivation: Counts Engine EndPlay calls delivered to this device. */
	std::size_t EndPlayCallCount{};
	/** Motivation: Counts transfers deliberately discarded while traffic is disabled or a receiving mailbox is full. */
	std::size_t DroppedTransferCount{};
	/** Motivation: Counts complete initial-request protocol messages received by this addressed device. */
	std::size_t ReceivedConnectRequestCount{};
	/** Motivation: Counts all public rejected-connection messages received by this device before their reason is classified. */
	std::size_t ReceivedConnectionRejectionCount{};
	/** Motivation: Counts exact Full connection rejections received by this addressed device. */
	std::size_t ReceivedFullRejectionCount{};
	/** Motivation: Counts packets consumed by Messaging through this device. */
	std::size_t SuccessfulReceiveCount{};
	/** Motivation: Retains connect attempt ids observed on successful receives. */
	std::array<std::uint32_t, MaxQueuedPackets> ReceivedConnectAttemptIds{};
	/** Motivation: Bounds the retained connect attempt ids. */
	std::size_t ReceivedConnectAttemptCount{};
	/** Motivation: Retains accepted peer identities observed on successful receives. */
	std::array<MicroWorld::Networking::FConnectAccepted, MaxQueuedPackets> ReceivedAcceptances{};
	/** Motivation: Bounds the retained accepted peer identities. */
	std::size_t ReceivedAcceptanceCount{};

private:
	/**
	 * Motivation: Resolves one addressed destination inside the fixed host-only three-device medium.
	 * Responsibilities: Return the matching live peer or null without allocating, mutating, or treating an empty address as routable.
	 */
	FPumpGatedTransportDevice* FindPeer(const FDeviceAddress& InAddress) noexcept
	{
		for (FPumpGatedTransportDevice* const Peer : Peers)
		{
			if (Peer != nullptr && Peer->Address == InAddress)
			{
				return Peer;
			}
		}
		return nullptr;
	}

	/**
	 * Motivation: Retains a transferred packet until the receiving Engine's Messaging pre-advance consumes it.
	 * Responsibilities: Copy one bounded packet into FIFO storage, record public protocol observations, and reject saturated input.
	 */
	bool QueueInbound(const FQueuedPacket& InPacket) noexcept
	{
		if (InboundPacketCount == MaxQueuedPackets)
		{
			return false;
		}
		InboundPackets[InboundPacketCount] = InPacket;
		++InboundPacketCount;
		return true;
	}

	/**
	 * Motivation: Leaves the fixture's public transport behavior opaque while exposing the exact Full response required by Gate 2.
	 * Responsibilities: Decode only complete messages on Networking's best-effort wire channel from the narrow device-level observation.
	 */
	void InspectProtocolPacket(const FQueuedPacket& InPacket) noexcept
	{
		constexpr std::size_t MessageFrameHeaderBytes = sizeof(FNameId) * 2;
		if (InPacket.ByteCount < MessageFrameHeaderBytes)
		{
			return;
		}

		const FNameId ChannelNameId = ReadNameIdLittleEndian(InPacket.Bytes.data());
		if (ChannelNameId != MicroWorld::Networking::FNetworkSystem::BestEffortWireChannelNameId)
		{
			return;
		}

		FMessage Message;
		Message.SetMessageNameId(ReadNameIdLittleEndian(&InPacket.Bytes[sizeof(FNameId)]));
		Message.SetPayload(TSpan<const std::uint8_t>(&InPacket.Bytes[MessageFrameHeaderBytes], InPacket.ByteCount - MessageFrameHeaderBytes));
		FConnectRequest Request{};
		if (DecodeTypedMessage(Message, Request) == EMessagingResult::Success)
		{
			++ReceivedConnectRequestCount;
			ReceivedConnectAttemptIds[ReceivedConnectAttemptCount] = Request.AttemptId;
			++ReceivedConnectAttemptCount;
			return;
		}
		MicroWorld::Networking::FConnectAccepted Accepted{};
		if (DecodeTypedMessage(Message, Accepted) == EMessagingResult::Success)
		{
			ReceivedAcceptances[ReceivedAcceptanceCount] = Accepted;
			++ReceivedAcceptanceCount;
			return;
		}
		FConnectRejected Rejection{};
		if (DecodeTypedMessage(Message, Rejection) != EMessagingResult::Success)
		{
			return;
		}
		++ReceivedConnectionRejectionCount;
		if (Rejection.Reason == EConnectionRejectReason::Full)
		{
			++ReceivedFullRejectionCount;
		}
	}

	/**
	 * Motivation: Decodes the public fixed-width message id stored in the observed Messaging frame header.
	 * Responsibilities: Read exactly sizeof(FNameId) little-endian bytes without retaining or changing the packet.
	 */
	static FNameId ReadNameIdLittleEndian(const std::uint8_t* const InBytes) noexcept
	{
		std::uint32_t Value{};
		for (std::size_t ByteIndex = 0; ByteIndex < sizeof(Value); ++ByteIndex)
		{
			Value |= static_cast<std::uint32_t>(InBytes[ByteIndex]) << (ByteIndex * 8u);
		}
		return FNameId{Value};
	}

	/**
	 * Motivation: Maintains FIFO delivery order after Engine-owned Messaging consumes the oldest inbound packet.
	 * Responsibilities: Remove exactly one already-copied packet without reallocating or modifying remaining packet payloads.
	 */
	void RemoveFirstInboundPacket() noexcept
	{
		for (std::size_t PacketIndex = 1; PacketIndex < InboundPacketCount; ++PacketIndex)
		{
			InboundPackets[PacketIndex - 1] = InboundPackets[PacketIndex];
		}
		--InboundPacketCount;
	}

	/** Motivation: Names this device's immutable transport route. */
	FDeviceAddress Address{};
	/** Motivation: Keeps the three fixture endpoints reachable without making the fake own their lifetimes. */
	std::array<FPumpGatedTransportDevice*, 3> Peers{};
	/** Motivation: Selects delivery or intentional loss on this device's next Engine pre-advance turn. */
	bool bTransfersEnabled{};
	/** Motivation: Holds accepted outbound packets until this same device's next pre-advance turn. */
	std::array<FQueuedPacket, MaxQueuedPackets> OutboundPackets{};
	/** Motivation: Bounds the active leading outbound packets. */
	std::size_t OutboundPacketCount{};
	/** Motivation: Holds packets transferred by peers until this device's Messaging pre-advance receives them. */
	std::array<FQueuedPacket, MaxQueuedPackets> InboundPackets{};
	/** Motivation: Bounds the active leading inbound packets. */
	std::size_t InboundPacketCount{};
};

/**
 * Motivation: Builds the sole Engine-owned server configuration used by the three-node proof.
 * Responsibilities: Apply the product's single-admission operational limit and deterministic test timing without exposing subsystems.
 */
FEngineNetworkSetup MakeServerSetup() noexcept
{
	FEngineNetworkSetup Setup{};
	Setup.Role = ENetworkRole::Server;
	Setup.HeartbeatInterval = HeartbeatIntervalMilliseconds;
	Setup.PeerTimeout = PeerTimeoutMilliseconds;
	Setup.MaximumAdmittedServerPeers = 1;
	return Setup;
}

/**
 * Motivation: Builds one addressed client setup through Engine's public high-level composition contract.
 * Responsibilities: Preserve the shared heartbeat and timeout values while keeping exactly one initial server route.
 */
FEngineNetworkSetup MakeClientSetup() noexcept
{
	FEngineNetworkSetup Setup{};
	Setup.Role = ENetworkRole::Client;
	Setup.InitialServerAddress = ServerAddress;
	Setup.HeartbeatInterval = HeartbeatIntervalMilliseconds;
	Setup.PeerTimeout = PeerTimeoutMilliseconds;
	return Setup;
}

/**
 * Motivation: Keeps each Engine-driven transport turn directly observable without coupling a case to its total frame count.
 * Responsibilities: Advance one Engine frame and prove that its selected device receives exactly one pre-advance and post-advance call.
 */
ERuntimeResult AdvanceEngineFrame(
	FTestContext& Test, FEngine& InEngine, FPumpGatedTransportDevice& InDevice, const TimePointMilliseconds InTime) noexcept
{
	const std::size_t PreAdvanceCallsBeforeTick = InDevice.PreAdvanceCallCount;
	const std::size_t PostAdvanceCallsBeforeTick = InDevice.PostAdvanceCallCount;
	const ERuntimeResult TickResult = InEngine.Tick(InTime);

	MW_EXPECT_EQ(
		Test,
		PreAdvanceCallsBeforeTick + std::size_t{1},
		InDevice.PreAdvanceCallCount,
		"Every Engine frame must invoke the selected device pre-advance exactly once");
	MW_EXPECT_EQ(
		Test,
		PostAdvanceCallsBeforeTick + std::size_t{1},
		InDevice.PostAdvanceCallCount,
		"Every Engine frame must invoke the selected device post-advance exactly once");
	return TickResult;
}

/**
 * Motivation: Gives each focused Gate 2 behavior case an isolated three-route Engine composition without repeating medium setup.
 * Responsibilities: Own the host-only engines and devices, configure their public networking surfaces, and route every frame through observed turns.
 * Example: FTwoNodeLoraLinkFixture Fixture{};
 */
struct FTwoNodeLoraLinkFixture final
{
	/**
	 * Motivation: Creates one isolated addressed medium before any Engine can send or receive.
	 * Responsibilities: Bind every fixed device route to the other live fixture devices.
	 */
	FTwoNodeLoraLinkFixture() noexcept : ServerDevice(ServerAddress), ProductClientDevice(ProductClientAddress), ProbeClientDevice(ProbeClientAddress)
	{
		ServerDevice.BindPeers(ServerDevice, ProductClientDevice, ProbeClientDevice);
		ProductClientDevice.BindPeers(ServerDevice, ProductClientDevice, ProbeClientDevice);
		ProbeClientDevice.BindPeers(ServerDevice, ProductClientDevice, ProbeClientDevice);
	}

	/**
	 * Motivation: Establishes the three public Engine networking compositions before their Worlds begin.
	 * Responsibilities: Configure each fixture Engine and assert successful public composition.
	 */
	void Configure(FTestContext& Test) noexcept
	{
		const EEngineNetworkSetupResult ServerSetupResult = ServerEngine.ConfigureNetworking(ServerDevice, MakeServerSetup());
		const EEngineNetworkSetupResult ProductClientSetupResult = ProductClientEngine.ConfigureNetworking(ProductClientDevice, MakeClientSetup());
		const EEngineNetworkSetupResult ProbeClientSetupResult = ProbeClientEngine.ConfigureNetworking(ProbeClientDevice, MakeClientSetup());

		MW_EXPECT_EQ(Test, EEngineNetworkSetupResult::Success, ServerSetupResult, "The server must compose through Engine");
		MW_EXPECT_EQ(Test, EEngineNetworkSetupResult::Success, ProductClientSetupResult, "The product client must compose through Engine");
		MW_EXPECT_EQ(Test, EEngineNetworkSetupResult::Success, ProbeClientSetupResult, "The host-only probe must compose through Engine");
	}

	/**
	 * Motivation: Starts all fixture Engines only after each case has created the Worlds it needs to observe.
	 * Responsibilities: Begin each fixture Engine and assert that setup emitted no transport sends.
	 */
	void Begin(FTestContext& Test) noexcept
	{
		const ERuntimeResult ServerBeginResult = ServerEngine.BeginPlay(0);
		const ERuntimeResult ProductClientBeginResult = ProductClientEngine.BeginPlay(0);
		const ERuntimeResult ProbeClientBeginResult = ProbeClientEngine.BeginPlay(0);

		MW_EXPECT_EQ(Test, ERuntimeResult::Success, ServerBeginResult, "The server World must begin");
		MW_EXPECT_EQ(Test, ERuntimeResult::Success, ProductClientBeginResult, "The product client World must begin");
		MW_EXPECT_EQ(Test, ERuntimeResult::Success, ProbeClientBeginResult, "The probe World must begin");
		MW_EXPECT_EQ(Test, std::size_t{0}, ServerDevice.TrySendCallCount, "Server setup and BeginPlay must be silent");
		MW_EXPECT_EQ(Test, std::size_t{0}, ProductClientDevice.TrySendCallCount, "Client setup and BeginPlay must be silent");
		MW_EXPECT_EQ(Test, std::size_t{0}, ProbeClientDevice.TrySendCallCount, "Probe setup and BeginPlay must be silent");
	}

	/**
	 * Motivation: Makes every server frame prove its direct Engine-to-device turn contract.
	 * Responsibilities: Advance the server Engine through the shared per-frame transport assertion.
	 */
	ERuntimeResult TickServer(FTestContext& Test, const TimePointMilliseconds InTime) noexcept
	{
		return AdvanceEngineFrame(Test, ServerEngine, ServerDevice, InTime);
	}

	/**
	 * Motivation: Makes every product-client frame prove its direct Engine-to-device turn contract.
	 * Responsibilities: Advance the product-client Engine through the shared per-frame transport assertion.
	 */
	ERuntimeResult TickProductClient(FTestContext& Test, const TimePointMilliseconds InTime) noexcept
	{
		return AdvanceEngineFrame(Test, ProductClientEngine, ProductClientDevice, InTime);
	}

	/**
	 * Motivation: Makes every host-only probe frame prove its direct Engine-to-device turn contract.
	 * Responsibilities: Advance the probe Engine through the shared per-frame transport assertion.
	 */
	ERuntimeResult TickProbeClient(FTestContext& Test, const TimePointMilliseconds InTime) noexcept
	{
		return AdvanceEngineFrame(Test, ProbeClientEngine, ProbeClientDevice, InTime);
	}

	/**
	 * Motivation: Closes the isolated Engines after each behavior proof without exposing teardown order to cases.
	 * Responsibilities: End every fixture Engine and assert successful teardown.
	 */
	void End(FTestContext& Test) noexcept
	{
		const ERuntimeResult ServerEndResult = ServerEngine.EndPlay();
		const ERuntimeResult ProductClientEndResult = ProductClientEngine.EndPlay();
		const ERuntimeResult ProbeClientEndResult = ProbeClientEngine.EndPlay();

		MW_EXPECT_EQ(Test, ERuntimeResult::Success, ServerEndResult, "The server must end cleanly");
		MW_EXPECT_EQ(Test, ERuntimeResult::Success, ProductClientEndResult, "The product client must end cleanly");
		MW_EXPECT_EQ(Test, ERuntimeResult::Success, ProbeClientEndResult, "The probe must end cleanly");
	}

	/** Motivation: Owns the server composition for one isolated behavior case. */
	FEngine ServerEngine{EngineMessagingCollectionBudget};
	/** Motivation: Owns the supported initial-client composition for one isolated behavior case. */
	FEngine ProductClientEngine{EngineMessagingCollectionBudget};
	/** Motivation: Owns the host-only second-route admission probe for one isolated behavior case. */
	FEngine ProbeClientEngine{EngineMessagingCollectionBudget};
	/** Motivation: Exposes the server transport's Engine-owned turns and protocol observations. */
	FPumpGatedTransportDevice ServerDevice;
	/** Motivation: Exposes the product client transport's Engine-owned turns and protocol observations. */
	FPumpGatedTransportDevice ProductClientDevice;
	/** Motivation: Exposes the probe transport's Engine-owned turns and protocol observations. */
	FPumpGatedTransportDevice ProbeClientDevice;
};

/**
 * Motivation: Establishes the initial route for focused admission and timeout cases through observed Engine frames.
 * Responsibilities: Drive the deterministic initial-client handshake without adding assertions unrelated to its callers.
 */
void AttachInitialClient(FTestContext& Test, FTwoNodeLoraLinkFixture& Fixture) noexcept
{
	Fixture.TickProductClient(Test, 1);
	Fixture.TickProductClient(Test, 2);
	Fixture.ProductClientDevice.SetTransfersEnabled(true);
	Fixture.TickProductClient(Test, 11);
	Fixture.TickProductClient(Test, 12);
	Fixture.ServerDevice.SetTransfersEnabled(true);
	Fixture.TickServer(Test, 12);
	Fixture.TickProductClient(Test, 21);
	Fixture.TickServer(Test, 21);
	Fixture.TickProductClient(Test, 22);
	Fixture.TickServer(Test, 22);
	Fixture.TickServer(Test, 23);
	Fixture.TickProductClient(Test, 23);
}

/**
 * Motivation: Proves staged delivery and retry attachment without unrelated admission or timeout behavior.
 * Responsibilities: Verify delayed first delivery, shared retry identity, and the accepted peer identity.
 */
MW_TEST_CASE(EngineTwoNodeLoraLinkAttachesThroughStagedRetry)
{
	FTwoNodeLoraLinkFixture Fixture{};
	Fixture.Configure(Test);
	const auto ServerWorld = Fixture.ServerEngine.CreateWorld();
	const auto ProductClientWorld = Fixture.ProductClientEngine.CreateWorld();
	const auto ProbeClientWorld = Fixture.ProbeClientEngine.CreateWorld();
	Fixture.Begin(Test);

	const ERuntimeResult FirstProductFrameResult = Fixture.TickProductClient(Test, 1);
	const std::size_t ProductSendCountAfterFirstFrame = Fixture.ProductClientDevice.TrySendCallCount;
	const std::size_t ProductPendingPacketsAfterFirstFrame = Fixture.ProductClientDevice.PendingOutboundPacketCount();
	const ERuntimeResult FirstProductDeliveryTurnResult = Fixture.TickProductClient(Test, 2);
	const std::size_t ProductDropCountAfterFirstDeliveryTurn = Fixture.ProductClientDevice.DroppedTransferCount;
	Fixture.ProductClientDevice.SetTransfersEnabled(true);
	Fixture.TickProductClient(Test, 11);
	Fixture.TickProductClient(Test, 12);
	Fixture.ServerDevice.SetTransfersEnabled(true);
	Fixture.TickServer(Test, 12);
	Fixture.TickProductClient(Test, 21);
	Fixture.TickServer(Test, 21);
	Fixture.TickProductClient(Test, 22);
	Fixture.TickServer(Test, 22);
	Fixture.TickServer(Test, 23);
	const ERuntimeResult ProductConnectedResult = Fixture.TickProductClient(Test, 23);

	MW_EXPECT_EQ(Test, ERuntimeResult::Success, FirstProductFrameResult, "The first product frame must complete");
	MW_EXPECT_EQ(Test, std::size_t{1}, ProductSendCountAfterFirstFrame, "The first client frame must queue one request");
	MW_EXPECT_EQ(Test, std::size_t{1}, ProductPendingPacketsAfterFirstFrame, "The first request must await the next device turn");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, FirstProductDeliveryTurnResult, "The next client device turn must complete");
	MW_EXPECT_EQ(Test, std::size_t{1}, ProductDropCountAfterFirstDeliveryTurn, "The next client frame must attempt the first delivery");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ProductConnectedResult, "The product client must process the accepted retry");
	MW_EXPECT_TRUE(Test, ServerWorld.Get()->HasActiveNetworkPeer(), "The server must expose its admitted product peer");
	MW_EXPECT_TRUE(Test, ProductClientWorld.Get()->HasActiveNetworkPeer(), "The product client must expose its active server peer");
	MW_EXPECT_TRUE(Test, ProbeClientWorld.Get() != nullptr, "The isolated fixture must retain its probe World");
	MW_EXPECT_EQ(
		Test, std::size_t{2}, Fixture.ServerDevice.ReceivedConnectAttemptCount, "The server must consume the first request and one heartbeat retry");
	MW_EXPECT_EQ(
		Test, std::size_t{2}, Fixture.ProductClientDevice.ReceivedAcceptanceCount, "The product client must consume both accepted responses");
	MW_EXPECT_EQ(
		Test,
		Fixture.ServerDevice.ReceivedConnectAttemptIds[0],
		Fixture.ServerDevice.ReceivedConnectAttemptIds[1],
		"Every heartbeat retry must preserve its original attempt id");
	MW_EXPECT_EQ(
		Test,
		Fixture.ProductClientDevice.ReceivedAcceptances[0].Peer.Index,
		Fixture.ProductClientDevice.ReceivedAcceptances[1].Peer.Index,
		"The retry must retain the admitted peer index");
	MW_EXPECT_EQ(
		Test,
		Fixture.ProductClientDevice.ReceivedAcceptances[0].Peer.Generation,
		Fixture.ProductClientDevice.ReceivedAcceptances[1].Peer.Generation,
		"The retry must retain the admitted peer generation");
	Fixture.End(Test);
}

/**
 * Motivation: Proves one admitted route rejects a distinct route through the public Full response.
 * Responsibilities: Verify the probe receives and decodes Full without acquiring an active peer.
 */
MW_TEST_CASE(EngineTwoNodeLoraLinkRejectsSecondRouteAsFull)
{
	FTwoNodeLoraLinkFixture Fixture{};
	Fixture.Configure(Test);
	const auto ServerWorld = Fixture.ServerEngine.CreateWorld();
	const auto ProductClientWorld = Fixture.ProductClientEngine.CreateWorld();
	const auto ProbeClientWorld = Fixture.ProbeClientEngine.CreateWorld();
	Fixture.Begin(Test);
	AttachInitialClient(Test, Fixture);
	Fixture.ProbeClientDevice.SetTransfersEnabled(true);
	Fixture.TickProbeClient(Test, 24);
	Fixture.TickProbeClient(Test, 25);
	Fixture.TickServer(Test, 25);
	Fixture.TickServer(Test, 26);
	const std::size_t ProbeInboundBeforeReceive = Fixture.ProbeClientDevice.PendingInboundPacketCount();
	const std::size_t ProbeReceivesBeforeFull = Fixture.ProbeClientDevice.SuccessfulReceiveCount;
	const std::size_t ProbeFullsBeforeReceive = Fixture.ProbeClientDevice.ReceivedFullRejectionCount;
	const ERuntimeResult ProbeFullResponseReceiveResult = Fixture.TickProbeClient(Test, 26);

	MW_EXPECT_TRUE(Test, ServerWorld.Get()->HasActiveNetworkPeer(), "The initial route must remain admitted");
	MW_EXPECT_TRUE(Test, ProductClientWorld.Get() != nullptr, "The isolated fixture must retain its product World");
	MW_EXPECT_EQ(Test, std::size_t{1}, ProbeInboundBeforeReceive, "The probe must hold the Full frame before its Engine receive turn");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ProbeFullResponseReceiveResult, "The probe must process the rejection frame");
	MW_EXPECT_EQ(
		Test,
		ProbeReceivesBeforeFull + std::size_t{1},
		Fixture.ProbeClientDevice.SuccessfulReceiveCount,
		"The probe receive turn must consume the queued Full frame");
	MW_EXPECT_EQ(
		Test,
		ProbeFullsBeforeReceive + std::size_t{1},
		Fixture.ProbeClientDevice.ReceivedFullRejectionCount,
		"The consumed probe frame must decode as Full");
	MW_EXPECT_EQ(
		Test, std::size_t{0}, Fixture.ProbeClientDevice.PendingInboundPacketCount(), "The consumed Full frame must leave no unread probe packet");
	MW_EXPECT_TRUE(Test, !ProbeClientWorld.Get()->HasActiveNetworkPeer(), "A Full response must not create a second active server peer");
	Fixture.End(Test);
}

/**
 * Motivation: Proves link loss retires the route while a disconnected World still progresses on the next Engine frame.
 * Responsibilities: Verify timeout retirement and exactly one later World tick through public Engine behavior.
 */
MW_TEST_CASE(EngineTwoNodeLoraLinkTimesOutAndKeepsWorldProgressing)
{
	FTwoNodeLoraLinkFixture Fixture{};
	Fixture.Configure(Test);
	const auto ServerWorld = Fixture.ServerEngine.CreateWorld();
	const auto ProductClientWorld = Fixture.ProductClientEngine.CreateWorld();
	const auto ProbeClientWorld = Fixture.ProbeClientEngine.CreateWorld();
	FWorldTickObservation ProductClientWorldTicks{};
	const auto ProductClientTickActor = ProductClientWorld.Get()->SpawnActor<FWorldTickActor>(&ProductClientWorldTicks);
	Fixture.Begin(Test);
	AttachInitialClient(Test, Fixture);
	const bool bServerActiveBeforeTimeout = ServerWorld.Get()->HasActiveNetworkPeer();
	const bool bProductClientActiveBeforeTimeout = ProductClientWorld.Get()->HasActiveNetworkPeer();
	Fixture.ServerDevice.SetTransfersEnabled(false);
	Fixture.ProductClientDevice.SetTransfersEnabled(false);
	Fixture.ProbeClientDevice.SetTransfersEnabled(false);
	const ERuntimeResult ProductTimeoutResult = Fixture.TickProductClient(Test, 54);
	const ERuntimeResult ServerTimeoutResult = Fixture.TickServer(Test, 54);
	const std::size_t ProductTicksAfterTimeout = ProductClientWorldTicks.TickCount;
	const ERuntimeResult ProductDisconnectedWorldTickResult = Fixture.TickProductClient(Test, 55);

	MW_EXPECT_EQ(Test, EActorSpawnRequestResult::Queued, ProductClientTickActor.Result, "The product tick observer must queue before play");
	MW_EXPECT_TRUE(Test, ProbeClientWorld.Get() != nullptr, "The isolated fixture must retain its probe World");
	MW_EXPECT_TRUE(Test, bServerActiveBeforeTimeout, "The server must attach before the deliberate silence");
	MW_EXPECT_TRUE(Test, bProductClientActiveBeforeTimeout, "The product client must attach before the deliberate silence");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ProductTimeoutResult, "The product timeout frame must complete");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ServerTimeoutResult, "The server timeout frame must complete");
	MW_EXPECT_TRUE(Test, !ProductClientWorld.Get()->HasActiveNetworkPeer(), "Stopping traffic must retire the product client peer after timeout");
	MW_EXPECT_TRUE(Test, !ServerWorld.Get()->HasActiveNetworkPeer(), "Stopping traffic must retire the server peer after timeout");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ProductDisconnectedWorldTickResult, "The disconnected product World must keep ticking");
	MW_EXPECT_EQ(
		Test,
		ProductTicksAfterTimeout + std::size_t{1},
		ProductClientWorldTicks.TickCount,
		"A separate disconnected Engine tick must advance the World exactly once");
	Fixture.End(Test);
}

} // namespace
