#include "TestSupport.h"

#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Messaging/MessageTypes.h>
#include <MicroWorld/Messaging/MessagingSystem.h>
#include <MicroWorld/Messaging/NameId.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{
using MicroWorld::Core::DurationMilliseconds;
using MicroWorld::Core::EDelegateResult;
using MicroWorld::Core::ERuntimeResult;
using MicroWorld::Core::ETransportResult;
using MicroWorld::Core::FDeviceAddress;
using MicroWorld::Core::FReceiveResult;
using MicroWorld::Core::IPlaySystem;
using MicroWorld::Core::ITransportDevice;
using MicroWorld::Core::TimePointMilliseconds;
using MicroWorld::Core::TSpan;
using MicroWorld::Engine::FDefaultEngineTraits;
using MicroWorld::Engine::FGarbageCollectionBudget;
using MicroWorld::Engine::TEngine;
using MicroWorld::Messaging::EMessagingResult;
using MicroWorld::Messaging::FChannelInformation;
using MicroWorld::Messaging::FMessage;
using MicroWorld::Messaging::FMessagingSystem;
using MicroWorld::Messaging::FMessagingSystemInformation;
using MicroWorld::Messaging::FNameId;
using MicroWorld::Messaging::MakeNameId;

/** Motivation: Names the production-shaped engine type exercised by this composition test suite. */
using FEngine = TEngine<FDefaultEngineTraits>;

/**
 * Motivation: Finishes a whole collection cycle in one slice so no case here is shaped by a partial one; these tests
 *   create no garbage, so the budget only has to stay out of the way.
 */
constexpr FGarbageCollectionBudget EngineMessagingCollectionBudget{
	static_cast<std::uint32_t>(FEngine::MaxRoots), static_cast<std::uint32_t>(FEngine::MaxObjects), static_cast<std::uint32_t>(FEngine::MaxObjects)};

/** Motivation: Names the bounded subscriber delegate accepted by the engine-owned Messaging system. */
using FSubscriberDelegate = FMessagingSystem::FSubscriberDelegate;

/** Motivation: Identifies the bit count in one byte for independent little-endian test-frame encoding. */
constexpr std::size_t BitsPerByte = 8;

/** Motivation: Identifies the byte count in a wire name id for independent test-frame encoding. */
constexpr std::size_t NameIdByteCount = sizeof(std::uint32_t);

/**
 * Motivation: Builds inbound wire identifiers without reusing Messaging's production encoder.
 * Responsibilities: Write InNameId into the caller-owned four-byte destination in little-endian order.
 */
void WriteNameIdLittleEndian(const FNameId InNameId, std::uint8_t* const OutDestination) noexcept
{
	for (std::size_t ByteIndex = 0; ByteIndex < NameIdByteCount; ++ByteIndex)
	{
		OutDestination[ByteIndex] = static_cast<std::uint8_t>(InNameId.Value >> (ByteIndex * BitsPerByte));
	}
}

/**
 * Motivation: Gives engine-owned Messaging a deterministic single-frame device without hardware or wall-clock time.
 * Responsibilities: Count outbound sends, hand one queued inbound frame to Messaging, and otherwise report no input.
 * Example:
 *   FTestTransportDevice Device;
 */
class FTestTransportDevice final : public ITransportDevice
{
public:
	/** Motivation: Bounds one test frame to the largest frame the default Messaging system can process. */
	static constexpr std::size_t MaxTestPacketBytes = FMessagingSystem::MaxFrameBytes;

	/**
	 * Motivation: Lets a test provide one inbound frame before the engine's pre-advance turn drains it.
	 * Responsibilities: Copy one frame that fits the fixed test storage and make it available to exactly one receive call.
	 */
	void QueueInboundFrame(const TSpan<const std::uint8_t> InFrame) noexcept
	{
		if (InFrame.Size() > MaxTestPacketBytes)
		{
			return;
		}

		QueuedFrameByteCount = InFrame.Size();
		for (std::size_t ByteIndex = 0; ByteIndex < QueuedFrameByteCount; ++ByteIndex)
		{
			QueuedFrameBytes[ByteIndex] = InFrame[ByteIndex];
		}
		bHasQueuedFrame = true;
	}

	/**
	 * Motivation: Lets outbound Messaging behavior be observed without emulating a physical medium.
	 * Responsibilities: Count each complete send request and report that the fake device accepted it.
	 */
	ETransportResult TrySend(const FDeviceAddress&, const TSpan<const std::uint8_t>) noexcept override
	{
		++TrySendCallCount;
		return ETransportResult::Success;
	}

	/**
	 * Motivation: Supplies the one queued inbound frame to Messaging's device drain turn.
	 * Responsibilities: Copy the queued frame transactionally when it fits, then report Unavailable forever after.
	 */
	ETransportResult TryReceive(FDeviceAddress& OutFrom, TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept override
	{
		if (!bHasQueuedFrame)
		{
			return ETransportResult::Unavailable;
		}

		bHasQueuedFrame = false;
		if (QueuedFrameByteCount > InDestination.Size())
		{
			return ETransportResult::Invalid;
		}

		for (std::size_t ByteIndex = 0; ByteIndex < QueuedFrameByteCount; ++ByteIndex)
		{
			InDestination[ByteIndex] = QueuedFrameBytes[ByteIndex];
		}
		OutFrom = FDeviceAddress{};
		OutResult.BytesReceived = QueuedFrameByteCount;
		return ETransportResult::Success;
	}

	/**
	 * Motivation: Lets Messaging preflight the fake device's bounded packet capacity.
	 * Responsibilities: Return the fixed maximum packet byte count.
	 */
	std::size_t MaxPacketBytes() const noexcept override { return MaxTestPacketBytes; }

	/**
	 * Motivation: Completes IPlaySystem for a device drained directly by Messaging rather than by the engine binding.
	 * Responsibilities: Perform no independent pre-advance work.
	 */
	void PreAdvance(TimePointMilliseconds) noexcept override {}

	/**
	 * Motivation: Completes IPlaySystem for a device drained directly by Messaging rather than by the engine binding.
	 * Responsibilities: Perform no independent post-advance work.
	 */
	void PostAdvance(TimePointMilliseconds) noexcept override {}

	/** Motivation: Counts outbound frames Messaging asks this fake device to accept. */
	std::size_t TrySendCallCount{};

private:
	/** Motivation: Owns the bytes of the single inbound frame until Messaging consumes them. */
	std::array<std::uint8_t, MaxTestPacketBytes> QueuedFrameBytes{};

	/** Motivation: Identifies how many queued bytes TryReceive may copy into its caller-owned destination. */
	std::size_t QueuedFrameByteCount{};

	/** Motivation: Distinguishes one queued frame from a device that has no input available. */
	bool bHasQueuedFrame{};
};

/**
 * Motivation: Retains an inbound message's observable data after the subscriber's borrowed payload span expires.
 * Responsibilities: Count deliveries and copy the latest message name and bounded payload bytes for assertions.
 * Example:
 *   FInboundMessageRecorder Recorder;
 */
struct FInboundMessageRecorder final
{
	/** Motivation: Reserves storage large enough for any payload copied from the default Messaging frame. */
	static constexpr std::size_t MaxRecordedPayloadBytes = FMessagingSystem::MaxFrameBytes;

	/**
	 * Motivation: Captures the message received during the engine-driven inbound turn.
	 * Responsibilities: Copy the name and payload into recorder-owned storage without retaining a transient span.
	 */
	void Record(const FMessage& InMessage) noexcept
	{
		++DeliveryCount;
		MessageNameId = InMessage.GetMessageNameId();
		const TSpan<const std::uint8_t> Payload = InMessage.GetPayload();
		PayloadByteCount = Payload.Size();
		const std::size_t CopiedByteCount = PayloadByteCount < MaxRecordedPayloadBytes ? PayloadByteCount : MaxRecordedPayloadBytes;
		for (std::size_t ByteIndex = 0; ByteIndex < CopiedByteCount; ++ByteIndex)
		{
			PayloadBytes[ByteIndex] = Payload[ByteIndex];
		}
	}

	/** Motivation: Counts inbound deliveries that reached the subscriber. */
	std::size_t DeliveryCount{};

	/** Motivation: Preserves the received message identity for routing assertions. */
	FNameId MessageNameId{};

	/** Motivation: Preserves the received payload length for post-delivery assertions. */
	std::size_t PayloadByteCount{};

	/** Motivation: Retains copied payload bytes after the subscriber's callback returns. */
	std::array<std::uint8_t, MaxRecordedPayloadBytes> PayloadBytes{};
};

/**
 * Motivation: Makes the caller-bound play-system turns externally observable in one composition test.
 * Responsibilities: Count each lifecycle and frame turn without adding behavior or dependencies.
 * Example:
 *   FRecordingPlaySystem System;
 */
class FRecordingPlaySystem final : public IPlaySystem
{
public:
	/**
	 * Motivation: Observes that the engine starts its caller-bound system when it begins play.
	 * Responsibilities: Increment only the begin-play observation count.
	 */
	void BeginPlay(TimePointMilliseconds) noexcept override { ++BeginPlayCallCount; }

	/**
	 * Motivation: Observes that the engine drives the caller-bound inbound turn.
	 * Responsibilities: Increment only the pre-advance observation count.
	 */
	void PreAdvance(TimePointMilliseconds) noexcept override { ++PreAdvanceCallCount; }

	/**
	 * Motivation: Observes that the engine drives the caller-bound outbound turn.
	 * Responsibilities: Increment only the post-advance observation count.
	 */
	void PostAdvance(TimePointMilliseconds) noexcept override { ++PostAdvanceCallCount; }

	/**
	 * Motivation: Observes that the engine ends its caller-bound system when play ends.
	 * Responsibilities: Increment only the end-play observation count.
	 */
	void EndPlay() noexcept override { ++EndPlayCallCount; }

	/** Motivation: Counts engine begin-play calls routed to this caller-bound system. */
	std::size_t BeginPlayCallCount{};

	/** Motivation: Counts engine pre-advance calls routed to this caller-bound system. */
	std::size_t PreAdvanceCallCount{};

	/** Motivation: Counts engine post-advance calls routed to this caller-bound system. */
	std::size_t PostAdvanceCallCount{};

	/** Motivation: Counts engine end-play calls routed to this caller-bound system. */
	std::size_t EndPlayCallCount{};
};

/**
 * Motivation: Read the optional Messaging system from a newly constructed engine.
 * Responsibilities: Verify an engine exposes no Messaging pointer before creation succeeds.
 */
MW_TEST_CASE(EngineMessagingSystemIsNullBeforeCreation)
{
	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};

	// Act
	FMessagingSystem* const MessagingSystem = Engine.GetMessagingSystem();

	// Assert
	MW_EXPECT_TRUE(Test, MessagingSystem == nullptr, "A fresh engine should expose no Messaging system");
}

/**
 * Motivation: Create Messaging through the engine and read the owned system back.
 * Responsibilities: Verify successful creation publishes one non-null Messaging system pointer.
 */
MW_TEST_CASE(EngineCreatesAndExposesMessagingSystem)
{
	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	const FMessagingSystemInformation Information{};

	// Act
	const ERuntimeResult CreateResult = Engine.CreateMessagingSystem(Information);
	FMessagingSystem* const MessagingSystem = Engine.GetMessagingSystem();

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, CreateResult, "The engine should create its Messaging system");
	MW_EXPECT_TRUE(Test, MessagingSystem != nullptr, "A successful create should publish the owned Messaging system");
}

/**
 * Motivation: Attempt a second Messaging creation after adding a channel to the original system.
 * Responsibilities: Verify duplicate creation preserves the original system pointer and channel state.
 */
MW_TEST_CASE(EngineRejectsDuplicateMessagingCreationWithoutReplacingState)
{
	/** Motivation: Identifies the channel that must survive a duplicate Messaging-system creation request. */
	constexpr FNameId PreservedChannelNameId = MakeNameId("PreservedChannel");

	/** Motivation: Keeps the preserved channel best-effort because this case only proves channel identity survives. */
	constexpr bool bPreservedChannelIsReliable = false;

	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	const FMessagingSystemInformation Information{};
	const ERuntimeResult FirstCreateResult = Engine.CreateMessagingSystem(Information);
	FMessagingSystem* const FirstMessagingSystem = Engine.GetMessagingSystem();
	const FChannelInformation PreservedChannelInformation{PreservedChannelNameId, bPreservedChannelIsReliable, nullptr, {}};
	const EMessagingResult InitialChannelCreateResult =
		FirstMessagingSystem != nullptr ? FirstMessagingSystem->CreateChannel(PreservedChannelInformation) : EMessagingResult::Invalid;

	// Act
	const ERuntimeResult DuplicateCreateResult = Engine.CreateMessagingSystem(Information);
	FMessagingSystem* const MessagingSystemAfterDuplicate = Engine.GetMessagingSystem();
	const EMessagingResult PreservedChannelCreateResult = MessagingSystemAfterDuplicate != nullptr
		? MessagingSystemAfterDuplicate->CreateChannel(PreservedChannelInformation)
		: EMessagingResult::Invalid;

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, FirstCreateResult, "The first Messaging creation should succeed");
	MW_EXPECT_TRUE(Test, FirstMessagingSystem != nullptr, "The first Messaging creation should publish a system");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, InitialChannelCreateResult, "The original system should accept its first channel");
	MW_EXPECT_EQ(Test, ERuntimeResult::Duplicate, DuplicateCreateResult, "A second Messaging creation should be rejected as duplicate");
	MW_EXPECT_TRUE(
		Test, FirstMessagingSystem == MessagingSystemAfterDuplicate, "A duplicate request should retain the original Messaging-system pointer");
	MW_EXPECT_EQ(Test, EMessagingResult::Duplicate, PreservedChannelCreateResult, "A duplicate request should retain original channel state");
}

/**
 * Motivation: Create Messaging before any engine world exists.
 * Responsibilities: Verify Messaging ownership is independent from world construction.
 */
MW_TEST_CASE(EngineCreatesMessagingSystemBeforeWorldCreation)
{
	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	const FMessagingSystemInformation Information{};

	// Act
	const ERuntimeResult CreateResult = Engine.CreateMessagingSystem(Information);
	FMessagingSystem* const MessagingSystem = Engine.GetMessagingSystem();

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, CreateResult, "Messaging creation should not require a world");
	MW_EXPECT_TRUE(Test, MessagingSystem != nullptr, "Messaging created before a world should remain available");
}

/**
 * Motivation: Queue one inbound frame, then advance an engine once after play begins.
 * Responsibilities: Verify the engine's pre-advance turn delivers the decoded frame during that first tick.
 */
MW_TEST_CASE(EngineDrivesInboundMessagingDuringFirstTick)
{
	/** Motivation: Names the device-backed channel that the queued inbound frame targets. */
	constexpr FNameId InboundChannelNameId = MakeNameId("EngineInboundChannel");

	/** Motivation: Names the message the inbound subscriber must receive from the queued wire frame. */
	constexpr FNameId ExpectedInboundMessageNameId = MakeNameId("EngineInboundMessage");

	/** Motivation: Keeps the inbound wire frame free of reliable sequence bytes that this routing test does not need. */
	constexpr bool bInboundChannelIsReliable = false;

	/** Motivation: States the byte count the inbound subscriber should receive from the test frame. */
	constexpr std::size_t ExpectedInboundPayloadByteCount = 3;

	/** Motivation: Supplies the first application byte the inbound subscriber must observe. */
	constexpr std::uint8_t ExpectedInboundPayloadFirstByte = 11;

	/** Motivation: Supplies the second application byte the inbound subscriber must observe. */
	constexpr std::uint8_t ExpectedInboundPayloadSecondByte = 22;

	/** Motivation: Supplies the third application byte the inbound subscriber must observe. */
	constexpr std::uint8_t ExpectedInboundPayloadThirdByte = 33;

	/** Motivation: Holds the exact application bytes that follow the independently encoded wire header. */
	constexpr std::array<std::uint8_t, ExpectedInboundPayloadByteCount> ExpectedInboundPayloadBytes{
		ExpectedInboundPayloadFirstByte,
		ExpectedInboundPayloadSecondByte,
		ExpectedInboundPayloadThirdByte,
	};

	/** Motivation: States how many subscriber calls one queued frame must produce. */
	constexpr std::size_t ExpectedInboundDeliveryCount = 1;

	/** Motivation: Opens the engine and Messaging system before its first inbound frame turn. */
	constexpr TimePointMilliseconds InboundBeginPlayMilliseconds{1000};

	/** Motivation: Advances the first inbound frame turn after the play-start baseline. */
	constexpr TimePointMilliseconds InboundTickMilliseconds{1010};

	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	FTestTransportDevice Device;
	FInboundMessageRecorder Recorder;
	const FMessagingSystemInformation Information{};
	const ERuntimeResult CreateMessagingResult = Engine.CreateMessagingSystem(Information);
	FMessagingSystem* const MessagingSystem = Engine.GetMessagingSystem();
	const FChannelInformation ChannelInformation{InboundChannelNameId, bInboundChannelIsReliable, &Device, {}};
	const EMessagingResult CreateChannelResult =
		MessagingSystem != nullptr ? MessagingSystem->CreateChannel(ChannelInformation) : EMessagingResult::Invalid;
	FSubscriberDelegate Subscriber;
	const EDelegateResult BindResult = Subscriber.Bind([&Recorder](const FMessage& InMessage) noexcept { Recorder.Record(InMessage); });
	const EMessagingResult SubscribeResult =
		MessagingSystem != nullptr ? MessagingSystem->SubscribeToChannel(InboundChannelNameId, std::move(Subscriber)) : EMessagingResult::Invalid;
	std::array<std::uint8_t, FMessagingSystem::FrameHeaderBytes + ExpectedInboundPayloadByteCount> InboundFrame{};
	WriteNameIdLittleEndian(InboundChannelNameId, InboundFrame.data());
	WriteNameIdLittleEndian(ExpectedInboundMessageNameId, InboundFrame.data() + NameIdByteCount);
	for (std::size_t ByteIndex = 0; ByteIndex < ExpectedInboundPayloadByteCount; ++ByteIndex)
	{
		InboundFrame[FMessagingSystem::FrameHeaderBytes + ByteIndex] = ExpectedInboundPayloadBytes[ByteIndex];
	}
	Device.QueueInboundFrame(TSpan<const std::uint8_t>(InboundFrame.data(), InboundFrame.size()));
	const auto World = Engine.CreateWorld();
	const bool bWorldCreated = World.Get() != nullptr;

	// Act
	const ERuntimeResult BeginPlayResult = Engine.BeginPlay(InboundBeginPlayMilliseconds);
	const ERuntimeResult TickResult = Engine.Tick(InboundTickMilliseconds);

	// Assert
	bool bPayloadMatchesExpectedBytes = Recorder.PayloadByteCount == ExpectedInboundPayloadByteCount;
	for (std::size_t ByteIndex = 0; ByteIndex < ExpectedInboundPayloadByteCount; ++ByteIndex)
	{
		bPayloadMatchesExpectedBytes = bPayloadMatchesExpectedBytes && Recorder.PayloadBytes[ByteIndex] == ExpectedInboundPayloadBytes[ByteIndex];
	}
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, CreateMessagingResult, "The engine should create Messaging before inbound setup");
	MW_EXPECT_TRUE(Test, MessagingSystem != nullptr, "Inbound setup should access the engine-owned Messaging system");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateChannelResult, "The inbound device-backed channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindResult, "The inbound subscriber should fit the bounded delegate");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The inbound subscriber should register successfully");
	MW_EXPECT_TRUE(Test, bWorldCreated, "The engine should create a world before play begins");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginPlayResult, "The inbound engine should begin play");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, TickResult, "The first inbound engine tick should complete");
	MW_EXPECT_EQ(Test, ExpectedInboundDeliveryCount, Recorder.DeliveryCount, "One tick should deliver the queued inbound frame once");
	MW_EXPECT_EQ(Test, ExpectedInboundMessageNameId, Recorder.MessageNameId, "The subscriber should observe the encoded inbound message name");
	MW_EXPECT_TRUE(Test, bPayloadMatchesExpectedBytes, "The subscriber should observe the encoded inbound payload bytes");
}

/**
 * Motivation: Send one reliable message, advance inside its retry interval, then advance past the interval.
 * Responsibilities: Verify engine post-advance runs the Messaging retry turn at the caller's supplied time.
 */
MW_TEST_CASE(EngineDrivesOutboundMessagingRetriesDuringPostAdvance)
{
	/** Motivation: Names the reliable device-backed channel exercised by the outbound retry turn. */
	constexpr FNameId OutboundChannelNameId = MakeNameId("EngineOutboundChannel");

	/** Motivation: Names the reliable message whose unacknowledged frame must be retried. */
	constexpr FNameId OutboundMessageNameId = MakeNameId("EngineOutboundMessage");

	/** Motivation: Selects reliable delivery so the engine-driven post-advance turn has retry work to perform. */
	constexpr bool bOutboundChannelIsReliable = true;

	/** Motivation: Makes the second scheduled tick fall inside the reliable retry interval. */
	constexpr DurationMilliseconds OutboundReliableRetryInterval{50};

	/** Motivation: Permits the initial send and one retry before reliable delivery gives up. */
	constexpr std::uint8_t OutboundMaximumReliableSendAttempts = 2;

	/** Motivation: Establishes the lifecycle baseline before the first Messaging turn stamps its time. */
	constexpr TimePointMilliseconds OutboundBeginPlayMilliseconds{1000};

	/** Motivation: Stamps Messaging's initial frame time before the reliable message is sent. */
	constexpr TimePointMilliseconds OutboundInitialTickMilliseconds{1000};

	/** Motivation: Advances inside the retry interval, when the send count must stay unchanged. */
	constexpr TimePointMilliseconds OutboundInsideRetryIntervalMilliseconds{1020};

	/** Motivation: Advances past the retry interval, when the retained reliable frame must be sent again. */
	constexpr TimePointMilliseconds OutboundPastRetryIntervalMilliseconds{1100};

	/** Motivation: States the outbound device send count immediately after the reliable message is first sent. */
	constexpr std::size_t ExpectedInitialOutboundSendCount = 1;

	/** Motivation: States the outbound device send count while the reliable retry interval has not elapsed. */
	constexpr std::size_t ExpectedInsideRetryIntervalSendCount = 1;

	/** Motivation: States the outbound device send count after the reliable retry interval elapses. */
	constexpr std::size_t ExpectedRetryOutboundSendCount = 2;

	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	FTestTransportDevice Device;
	const FMessagingSystemInformation Information{OutboundReliableRetryInterval, OutboundMaximumReliableSendAttempts};
	const ERuntimeResult CreateMessagingResult = Engine.CreateMessagingSystem(Information);
	FMessagingSystem* const MessagingSystem = Engine.GetMessagingSystem();
	const FChannelInformation ChannelInformation{OutboundChannelNameId, bOutboundChannelIsReliable, &Device, {}};
	const EMessagingResult CreateChannelResult =
		MessagingSystem != nullptr ? MessagingSystem->CreateChannel(ChannelInformation) : EMessagingResult::Invalid;
	FMessage OutboundMessage;
	OutboundMessage.SetMessageNameId(OutboundMessageNameId);
	const auto World = Engine.CreateWorld();
	const bool bWorldCreated = World.Get() != nullptr;
	const ERuntimeResult BeginPlayResult = Engine.BeginPlay(OutboundBeginPlayMilliseconds);
	const ERuntimeResult InitialTickResult = Engine.Tick(OutboundInitialTickMilliseconds);

	// Act
	const EMessagingResult SendResult =
		MessagingSystem != nullptr ? MessagingSystem->SendMessageToChannel(OutboundMessage, OutboundChannelNameId) : EMessagingResult::Invalid;
	const std::size_t SendCountAfterInitialSend = Device.TrySendCallCount;
	const ERuntimeResult InsideRetryIntervalTickResult = Engine.Tick(OutboundInsideRetryIntervalMilliseconds);
	const std::size_t SendCountInsideRetryInterval = Device.TrySendCallCount;
	const ERuntimeResult PastRetryIntervalTickResult = Engine.Tick(OutboundPastRetryIntervalMilliseconds);
	const std::size_t SendCountAfterRetry = Device.TrySendCallCount;

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, CreateMessagingResult, "The engine should create Messaging before outbound setup");
	MW_EXPECT_TRUE(Test, MessagingSystem != nullptr, "Outbound setup should access the engine-owned Messaging system");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateChannelResult, "The reliable outbound channel should be created");
	MW_EXPECT_TRUE(Test, bWorldCreated, "The engine should create a world before outbound play begins");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginPlayResult, "The outbound engine should begin play");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, InitialTickResult, "The first outbound tick should stamp Messaging time");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The reliable outbound message should send successfully");
	MW_EXPECT_EQ(Test, ExpectedInitialOutboundSendCount, SendCountAfterInitialSend, "The reliable message should send once immediately");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, InsideRetryIntervalTickResult, "The inside-interval engine tick should complete");
	MW_EXPECT_EQ(Test, ExpectedInsideRetryIntervalSendCount, SendCountInsideRetryInterval, "No retry should occur inside the retry interval");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, PastRetryIntervalTickResult, "The post-interval engine tick should complete");
	MW_EXPECT_EQ(Test, ExpectedRetryOutboundSendCount, SendCountAfterRetry, "Post-advance should retry the reliable frame after its interval");
}

/**
 * Motivation: Bind a caller-owned play system and create engine-owned Messaging in the same engine.
 * Responsibilities: Verify the engine drives every caller-bound turn and still delivers the Messaging inbound turn.
 */
MW_TEST_CASE(EngineDrivesBoundPlaySystemAndMessagingSystemTogether)
{
	/** Motivation: Names the device-backed channel used to prove Messaging remains active beside the bound system. */
	constexpr FNameId BoundSystemMessagingChannelNameId = MakeNameId("BoundSystemMessagingChannel");

	/** Motivation: Names the inbound message that proves the engine drives Messaging beside its bound system. */
	constexpr FNameId BoundSystemMessagingMessageNameId = MakeNameId("BoundSystemMessagingMessage");

	/** Motivation: Keeps the combined-system wire frame simple because this case proves turn ownership, not reliability. */
	constexpr bool bBoundSystemMessagingChannelIsReliable = false;

	/** Motivation: Opens both engine-owned and caller-bound systems before their shared frame turn. */
	constexpr TimePointMilliseconds BoundSystemsBeginPlayMilliseconds{2000};

	/** Motivation: Advances both systems once so every recorded turn has one observable call. */
	constexpr TimePointMilliseconds BoundSystemsTickMilliseconds{2010};

	/** Motivation: States the one inbound delivery that proves Messaging ran beside the bound system. */
	constexpr std::size_t ExpectedBoundSystemsMessagingDeliveryCount = 1;

	/** Motivation: States the begin-play count the caller-bound system must observe. */
	constexpr std::size_t ExpectedBoundSystemBeginPlayCallCount = 1;

	/** Motivation: States the pre-advance count the caller-bound system must observe. */
	constexpr std::size_t ExpectedBoundSystemPreAdvanceCallCount = 1;

	/** Motivation: States the post-advance count the caller-bound system must observe. */
	constexpr std::size_t ExpectedBoundSystemPostAdvanceCallCount = 1;

	/** Motivation: States the end-play count the caller-bound system must observe. */
	constexpr std::size_t ExpectedBoundSystemEndPlayCallCount = 1;

	// Arrange
	FRecordingPlaySystem BoundSystem;
	FEngine Engine{EngineMessagingCollectionBudget, BoundSystem};
	FTestTransportDevice Device;
	std::size_t MessagingDeliveryCount{};

	/**
	 * Motivation: Observes the engine's documented inbound order — Messaging takes its turn first, so a frame's arriving
	 *   messages are already delivered when the application's own system runs. Stays false if the subscriber never runs.
	 */
	bool bMessagingDeliveredBeforeBoundSystemPreAdvance{false};
	const FMessagingSystemInformation Information{};
	const ERuntimeResult CreateMessagingResult = Engine.CreateMessagingSystem(Information);
	FMessagingSystem* const MessagingSystem = Engine.GetMessagingSystem();
	const FChannelInformation ChannelInformation{BoundSystemMessagingChannelNameId, bBoundSystemMessagingChannelIsReliable, &Device, {}};
	const EMessagingResult CreateChannelResult =
		MessagingSystem != nullptr ? MessagingSystem->CreateChannel(ChannelInformation) : EMessagingResult::Invalid;
	FSubscriberDelegate Subscriber;
	const EDelegateResult BindResult = Subscriber.Bind(
		[&MessagingDeliveryCount, &bMessagingDeliveredBeforeBoundSystemPreAdvance, &BoundSystem](const FMessage&) noexcept
		{
			++MessagingDeliveryCount;
			bMessagingDeliveredBeforeBoundSystemPreAdvance = BoundSystem.PreAdvanceCallCount == 0;
		});
	const EMessagingResult SubscribeResult = MessagingSystem != nullptr
		? MessagingSystem->SubscribeToChannel(BoundSystemMessagingChannelNameId, std::move(Subscriber))
		: EMessagingResult::Invalid;
	std::array<std::uint8_t, FMessagingSystem::FrameHeaderBytes> InboundFrame{};
	WriteNameIdLittleEndian(BoundSystemMessagingChannelNameId, InboundFrame.data());
	WriteNameIdLittleEndian(BoundSystemMessagingMessageNameId, InboundFrame.data() + NameIdByteCount);
	Device.QueueInboundFrame(TSpan<const std::uint8_t>(InboundFrame.data(), InboundFrame.size()));
	const auto World = Engine.CreateWorld();
	const bool bWorldCreated = World.Get() != nullptr;

	// Act
	const ERuntimeResult BeginPlayResult = Engine.BeginPlay(BoundSystemsBeginPlayMilliseconds);
	const ERuntimeResult TickResult = Engine.Tick(BoundSystemsTickMilliseconds);
	const ERuntimeResult EndPlayResult = Engine.EndPlay();

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, CreateMessagingResult, "The combined engine should create Messaging");
	MW_EXPECT_TRUE(Test, MessagingSystem != nullptr, "The combined engine should expose its Messaging system");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateChannelResult, "The combined engine should create its Messaging channel");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindResult, "The combined Messaging subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The combined Messaging subscriber should register");
	MW_EXPECT_TRUE(Test, bWorldCreated, "The combined engine should create a world before play begins");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginPlayResult, "The combined engine should begin play");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, TickResult, "The combined engine should tick");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, EndPlayResult, "The combined engine should end play");
	MW_EXPECT_EQ(
		Test, ExpectedBoundSystemsMessagingDeliveryCount, MessagingDeliveryCount, "Messaging should deliver while the bound system is active");
	MW_EXPECT_EQ(Test, ExpectedBoundSystemBeginPlayCallCount, BoundSystem.BeginPlayCallCount, "The bound system should receive begin play");
	MW_EXPECT_EQ(Test, ExpectedBoundSystemPreAdvanceCallCount, BoundSystem.PreAdvanceCallCount, "The bound system should receive pre-advance");
	MW_EXPECT_EQ(Test, ExpectedBoundSystemPostAdvanceCallCount, BoundSystem.PostAdvanceCallCount, "The bound system should receive post-advance");
	MW_EXPECT_EQ(Test, ExpectedBoundSystemEndPlayCallCount, BoundSystem.EndPlayCallCount, "The bound system should receive end play");
	MW_EXPECT_TRUE(
		Test, bMessagingDeliveredBeforeBoundSystemPreAdvance, "Messaging should deliver its inbound frame before the bound system pre-advances");
}

/**
 * Motivation: Run a complete engine lifecycle without creating its optional Messaging system.
 * Responsibilities: Verify absent Messaging wiring leaves ordinary world lifecycle behavior unchanged.
 */
MW_TEST_CASE(EngineLifecycleRemainsOptionalWithoutMessagingSystem)
{
	/** Motivation: Opens the engine world without a Messaging system attached. */
	constexpr TimePointMilliseconds NoMessagingBeginPlayMilliseconds{3000};

	/** Motivation: Advances the no-Messaging engine world after its play-start baseline. */
	constexpr TimePointMilliseconds NoMessagingTickMilliseconds{3010};

	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	const auto World = Engine.CreateWorld();
	const bool bWorldCreated = World.Get() != nullptr;

	// Act
	const ERuntimeResult BeginPlayResult = Engine.BeginPlay(NoMessagingBeginPlayMilliseconds);
	const ERuntimeResult TickResult = Engine.Tick(NoMessagingTickMilliseconds);
	const ERuntimeResult EndPlayResult = Engine.EndPlay();
	FMessagingSystem* const MessagingSystem = Engine.GetMessagingSystem();

	// Assert
	MW_EXPECT_TRUE(Test, bWorldCreated, "The no-Messaging engine should still create a world");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginPlayResult, "The no-Messaging engine should begin play normally");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, TickResult, "The no-Messaging engine should tick normally");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, EndPlayResult, "The no-Messaging engine should end play normally");
	MW_EXPECT_TRUE(Test, MessagingSystem == nullptr, "The no-Messaging lifecycle should leave the optional system absent");
}

} // namespace
