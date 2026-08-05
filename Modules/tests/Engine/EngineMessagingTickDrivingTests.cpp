#include "TestSupport.h"
#include "EngineMessagingTestHelpers.h"

#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Messaging/ChannelInformation.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessagingResult.h>
#include <MicroWorld/Messaging/MessagingSystemInformation.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{

using namespace ::MicroWorld::Tests;

using MicroWorld::Core::DurationMilliseconds;
using MicroWorld::Core::EDelegateResult;
using MicroWorld::Core::ERuntimeResult;
using MicroWorld::Core::TimePointMilliseconds;
using MicroWorld::Messaging::EMessagingResult;
using MicroWorld::Messaging::FChannelInformation;
using MicroWorld::Messaging::FMessage;
using MicroWorld::Messaging::FMessagingSystem;
using MicroWorld::Messaging::FMessagingSystemInformation;

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
	 * Motivation: Observes the engine's documented inbound order — the bound composition system takes its turn before
	 *   Messaging delivers arriving frames. Stays false if the subscriber never runs.
	 */
	bool bBoundSystemPreAdvancedBeforeMessagingDelivery{false};
	const FMessagingSystemInformation Information{};
	const ERuntimeResult CreateMessagingResult = Engine.CreateMessagingSystem(Information);
	FMessagingSystem* const MessagingSystem = Engine.GetMessagingSystem();
	const FChannelInformation ChannelInformation{BoundSystemMessagingChannelNameId, bBoundSystemMessagingChannelIsReliable, &Device, {}};
	const EMessagingResult CreateChannelResult =
		MessagingSystem != nullptr ? MessagingSystem->CreateChannel(ChannelInformation) : EMessagingResult::Invalid;
	FSubscriberDelegate Subscriber;
	const EDelegateResult BindResult = Subscriber.Bind(
		[&MessagingDeliveryCount, &bBoundSystemPreAdvancedBeforeMessagingDelivery, &BoundSystem](const FMessage&) noexcept
		{
			++MessagingDeliveryCount;
			bBoundSystemPreAdvancedBeforeMessagingDelivery = BoundSystem.PreAdvanceCallCount > 0;
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
		Test, bBoundSystemPreAdvancedBeforeMessagingDelivery, "The bound system should pre-advance before Messaging delivers its inbound frame");
}

} // namespace
