#include "TestSupport.h"

#include <MicroWorld/Messaging/MessagingSystem.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{

using MicroWorld::Core::EDelegateResult;
using MicroWorld::Core::ETransportResult;
using MicroWorld::Core::FDeviceAddress;
using MicroWorld::Core::FReceiveResult;
using MicroWorld::Core::ITransportDevice;
using MicroWorld::Core::TimePointMilliseconds;
using MicroWorld::Core::TSpan;
using MicroWorld::Messaging::EMessagingResult;
using MicroWorld::Messaging::FChannelInformation;
using MicroWorld::Messaging::FDefaultMessagingTraits;
using MicroWorld::Messaging::FMessage;
using MicroWorld::Messaging::FMessagingSystemInformation;
using MicroWorld::Messaging::FNameId;
using MicroWorld::Messaging::InvalidNameId;
using MicroWorld::Messaging::TMessagingSystem;

/** Motivation: Names the default Messaging system for reusable local-delivery test recorders. */
using FDefaultMessagingSystem = TMessagingSystem<>;

/** Motivation: Names the bounded default subscriber delegate without repeating its system-qualified declaration. */
using FDefaultSubscriberDelegate = FDefaultMessagingSystem::FSubscriberDelegate;

/**
 * Motivation: Provides a non-owning device pointer for channel creation tests.
 * Responsibilities: Satisfy the transport device contract without retaining packets or performing I/O.
 * Example:
 *   FTestTransportDevice Device;
 */
class FTestTransportDevice final : public ITransportDevice
{
public:
	/**
	 * Motivation: Lets a test count a channel's outbound frames without modelling a medium.
	 * Responsibilities: Record each send request and report success without retaining the supplied packet or destination.
	 */
	ETransportResult TrySend(const FDeviceAddress&, const TSpan<const std::uint8_t>) noexcept override
	{
		++TrySendCallCount;
		return ETransportResult::Success;
	}

	/**
	 * Motivation: Completes the transport contract without modelling inbound packets for channel creation tests.
	 * Responsibilities: Report no packet available without changing output values.
	 */
	ETransportResult TryReceive(FDeviceAddress&, TSpan<std::uint8_t>, FReceiveResult&) noexcept override { return ETransportResult::Unavailable; }

	/**
	 * Motivation: Supplies a bounded device packet size without requiring hardware state.
	 * Responsibilities: Return the fixed test packet byte limit.
	 */
	std::size_t MaxPacketBytes() const noexcept override { return 64; }

	/**
	 * Motivation: Completes the caller-driven device lifecycle for a no-op test double.
	 * Responsibilities: Perform no pre-advance work.
	 */
	void PreAdvance(TimePointMilliseconds) noexcept override {}

	/**
	 * Motivation: Completes the caller-driven device lifecycle for a no-op test double.
	 * Responsibilities: Perform no post-advance work.
	 */
	void PostAdvance(TimePointMilliseconds) noexcept override {}

	/**
	 * Motivation: Lets a test assert exactly how many remote transmissions one send initiated.
	 * Responsibilities: Return the number of TrySend requests observed without changing the device.
	 */
	std::size_t GetTrySendCallCount() const noexcept { return TrySendCallCount; }

private:
	/** Motivation: Records every remote transmission this device was asked to make. */
	std::size_t TrySendCallCount{0};
};

/**
 * Motivation: Makes capacity behavior testable without depending on the production channel limit.
 * Responsibilities: Bound this test system to two channel creation slots.
 * Example:
 *   TMessagingSystem<FSmallMessagingTraits> System;
 */
struct FSmallMessagingTraits : FDefaultMessagingTraits
{
	/** Motivation: Bounds this test system to two channels while every other capacity stays at its default. */
	static constexpr std::size_t MaxChannels = 2;
};

/**
 * Motivation: Makes the subscription capacity boundary independently testable.
 * Responsibilities: Bound this test system to two subscriptions while keeping one valid channel available.
 * Example:
 *   TMessagingSystem<FSmallSubscriptionTraits> System;
 */
struct FSmallSubscriptionTraits : FDefaultMessagingTraits
{
	/** Motivation: Keeps one channel slot available for subscription capacity tests. */
	static constexpr std::size_t MaxChannels = 1;

	/** Motivation: Limits this test system to two stored subscriber registrations. */
	static constexpr std::size_t MaxSubscriptions = 2;
};

/**
 * Motivation: Captures a delivered message's observable identity and bytes without retaining its transient payload span.
 * Responsibilities: Count deliveries and copy the bounded test payload for assertions after the subscriber returns.
 * Example:
 *   FMessageRecorder Recorder;
 *   Recorder.Record(Message);
 */
struct FMessageRecorder final
{
	/** Motivation: Bounds copied bytes to the exact payload size used by the delivery test. */
	static constexpr std::size_t MaxRecordedPayloadBytes = 3;

	/**
	 * Motivation: Lets tests inspect delivery after Messaging releases the subscriber's transient payload view.
	 * Responsibilities: Count the delivery and copy its name and at most MaxRecordedPayloadBytes payload bytes.
	 */
	void Record(const FMessage& InMessage) noexcept
	{
		++DeliveryCount;
		MessageNameId = InMessage.GetMessageNameId();
		PayloadSize = InMessage.GetPayload().Size();

		const std::size_t RecordedByteCount = PayloadSize < MaxRecordedPayloadBytes ? PayloadSize : MaxRecordedPayloadBytes;
		for (std::size_t ByteIndex = 0; ByteIndex < RecordedByteCount; ++ByteIndex)
		{
			PayloadBytes[ByteIndex] = InMessage.GetPayload()[ByteIndex];
		}
	}

	/** Motivation: Records how many messages reached this subscriber. */
	std::size_t DeliveryCount{0};

	/** Motivation: Preserves the latest delivered message identity for direct routing assertions. */
	FNameId MessageNameId{};

	/** Motivation: Preserves the latest payload length for direct payload assertions. */
	std::size_t PayloadSize{0};

	/** Motivation: Retains the copied bytes after the borrowed payload span expires. */
	std::uint8_t PayloadBytes[MaxRecordedPayloadBytes]{};
};

/**
 * Motivation: Captures delivery order without relying on subscriber implementation details.
 * Responsibilities: Store the fixed sequence of subscriber labels in the order Messaging invokes them.
 * Example:
 *   FDeliveryOrderRecorder Recorder;
 *   Recorder.Record(1);
 */
struct FDeliveryOrderRecorder final
{
	/** Motivation: Bounds this recorder to the two subscribers in the registration-order test. */
	static constexpr std::size_t MaxRecordedDeliveries = 2;

	/**
	 * Motivation: Lets two subscribers make their observable delivery order available to one assertion block.
	 * Responsibilities: Store InSubscriberOrder while room remains and never allocate.
	 */
	void Record(const std::uint8_t InSubscriberOrder) noexcept
	{
		if (DeliveryCount < MaxRecordedDeliveries)
		{
			SubscriberOrders[DeliveryCount] = InSubscriberOrder;
			++DeliveryCount;
		}
	}

	/** Motivation: Identifies how many subscriber calls this recorder observed. */
	std::size_t DeliveryCount{0};

	/** Motivation: Retains each observed subscriber label in synchronous delivery order. */
	std::uint8_t SubscriberOrders[MaxRecordedDeliveries]{};
};

/**
 * Motivation: Shares reentrant-dispatch observations without exceeding the subscriber delegate's inline callable budget.
 * Responsibilities: Retain the system context and results needed to prove added subscriptions do not alter an active send.
 * Example:
 *   FReentrantDispatchContext Context{&System, "Telemetry", "Outer", "Nested"};
 */
struct FReentrantDispatchContext final
{
	/** Motivation: Gives the subscriber the system where it may safely add one subscription and send one nested message. */
	FDefaultMessagingSystem* MessagingSystem{nullptr};

	/** Motivation: Identifies the one channel used by the outer and nested sends. */
	FNameId ChannelNameId{};

	/** Motivation: Identifies the outer message that performs the reentrant action exactly once. */
	FNameId OuterMessageNameId{};

	/** Motivation: Identifies the nested message that proves recursive delivery terminates. */
	FNameId NestedMessageNameId{};

	/** Motivation: Records whether the callback has already added its subscriber and sent its nested message. */
	bool bHasPerformedReentrantWork{false};

	/** Motivation: Records the result of adding a subscriber while an outer send is active. */
	EMessagingResult AddedSubscriberResult{EMessagingResult::Invalid};

	/** Motivation: Records the result of the one nested send made by the callback. */
	EMessagingResult NestedSendResult{EMessagingResult::Invalid};

	/** Motivation: Counts nested-message deliveries to prove the controlled reentrant send ran exactly once. */
	std::size_t NestedDeliveryCount{0};

	/** Motivation: Counts outer-message deliveries to the subscriber added during the first dispatch. */
	std::size_t AddedSubscriberDeliveryCount{0};
};

/**
 * Motivation: Exercises subscription mutation from inside synchronous delivery without a large callable capture.
 * Responsibilities: Add one outer-message subscriber and emit one nested message only for the first outer delivery.
 * Example:
 *   FReentrantSubscriber Subscriber{&Context};
 */
struct FReentrantSubscriber final
{
	/**
	 * Motivation: Lets the callback prove dispatch uses the subscriber count captured at send start.
	 * Responsibilities: Perform the context's one reentrant subscription and nested send only for its first outer message.
	 */
	void operator()(const FMessage& InMessage) noexcept
	{
		if (Context == nullptr || Context->bHasPerformedReentrantWork || InMessage.GetMessageNameId() != Context->OuterMessageNameId)
		{
			return;
		}

		Context->bHasPerformedReentrantWork = true;
		FDefaultSubscriberDelegate AddedSubscriber;
		const EDelegateResult BindingResult =
			AddedSubscriber.Bind([InContext = Context](const FMessage&) noexcept { ++InContext->AddedSubscriberDeliveryCount; });
		if (BindingResult != EDelegateResult::Success)
		{
			Context->AddedSubscriberResult = EMessagingResult::Invalid;
			return;
		}

		Context->AddedSubscriberResult =
			Context->MessagingSystem->SubscribeToChannel(Context->ChannelNameId, Context->OuterMessageNameId, std::move(AddedSubscriber));

		FMessage NestedMessage;
		NestedMessage.SetMessageNameId(Context->NestedMessageNameId);
		Context->NestedSendResult = Context->MessagingSystem->SendMessageToChannel(NestedMessage, Context->ChannelNameId);
	}

	/** Motivation: Reaches the shared test observations while keeping the bound callback pointer-sized. */
	FReentrantDispatchContext* Context{nullptr};
};

/**
 * Motivation: Confirms a valid named channel starts an empty system successfully.
 * Responsibilities: Verify valid local-only channel creation reports success.
 */
MW_TEST_CASE(MessagingSystem_CreatesAValidChannel)
{
	// Arrange
	TMessagingSystem<> System;
	const FChannelInformation Information{"Telemetry", false, nullptr, {}};

	// Act
	const EMessagingResult Result = System.CreateChannel(Information);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, Result, "A valid channel should be created");
}

/**
 * Motivation: Confirms distinct channel identities occupy independent slots.
 * Responsibilities: Verify two valid names can be created in the same system.
 */
MW_TEST_CASE(MessagingSystem_CreatesTwoChannelsWithDifferentNames)
{
	// Arrange
	TMessagingSystem<> System;
	const FChannelInformation FirstInformation{"Telemetry", false, nullptr, {}};
	const FChannelInformation SecondInformation{"Commands", false, nullptr, {}};

	// Act
	const EMessagingResult FirstResult = System.CreateChannel(FirstInformation);
	const EMessagingResult SecondResult = System.CreateChannel(SecondInformation);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstResult, "The first valid channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondResult, "A different valid channel should be created");
}

/**
 * Motivation: Prevents the unset name sentinel from becoming an addressable live channel.
 * Responsibilities: Verify invalid channel creation reports Invalid before consuming capacity.
 */
MW_TEST_CASE(MessagingSystem_RejectsAnUnsetChannelName)
{
	// Arrange
	TMessagingSystem<> System;
	FChannelInformation Information{};
	Information.ChannelNameId = InvalidNameId;

	// Act
	const EMessagingResult Result = System.CreateChannel(Information);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Invalid, Result, "An unset channel name should be invalid");
}

/**
 * Motivation: Prevents two channel configurations from sharing one routing identity.
 * Responsibilities: Verify a repeated valid name reports Duplicate.
 */
MW_TEST_CASE(MessagingSystem_RejectsDuplicateChannelNames)
{
	// Arrange
	TMessagingSystem<> System;
	const FChannelInformation Information{"Telemetry", false, nullptr, {}};
	System.CreateChannel(Information);

	// Act
	const EMessagingResult Result = System.CreateChannel(Information);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Duplicate, Result, "An existing channel name should be rejected as duplicate");
}

/**
 * Motivation: Confirms a rejected duplicate never consumes bounded channel capacity.
 * Responsibilities: Verify a full system still reports Duplicate for an existing name and Full for a new name.
 */
MW_TEST_CASE(MessagingSystem_DuplicateDoesNotConsumeChannelCapacity)
{
	// Arrange
	TMessagingSystem<FSmallMessagingTraits> System;
	const FChannelInformation FirstInformation{"Telemetry", false, nullptr, {}};
	const FChannelInformation SecondInformation{"Commands", false, nullptr, {}};
	const FChannelInformation ThirdInformation{"Status", false, nullptr, {}};
	System.CreateChannel(FirstInformation);
	System.CreateChannel(SecondInformation);

	// Act
	const EMessagingResult DuplicateResult = System.CreateChannel(FirstInformation);
	const EMessagingResult FullResult = System.CreateChannel(ThirdInformation);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Duplicate, DuplicateResult, "A duplicate should remain distinguishable after capacity is full");
	MW_EXPECT_EQ(Test, EMessagingResult::Full, FullResult, "A new channel should still observe the original full capacity");
}

/**
 * Motivation: Makes fixed channel storage exhaustion visible to callers.
 * Responsibilities: Verify one valid channel beyond the configured capacity reports Full.
 */
MW_TEST_CASE(MessagingSystem_RejectsAChannelPastCapacity)
{
	// Arrange
	TMessagingSystem<FSmallMessagingTraits> System;
	const FChannelInformation FirstInformation{"Telemetry", false, nullptr, {}};
	const FChannelInformation SecondInformation{"Commands", false, nullptr, {}};
	const FChannelInformation ThirdInformation{"Status", false, nullptr, {}};
	System.CreateChannel(FirstInformation);
	System.CreateChannel(SecondInformation);

	// Act
	const EMessagingResult Result = System.CreateChannel(ThirdInformation);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Full, Result, "A channel beyond capacity should be rejected");
}

/**
 * Motivation: Preserves local-only messaging as a valid channel configuration.
 * Responsibilities: Verify a null transport device does not make a valid channel invalid.
 */
MW_TEST_CASE(MessagingSystem_AcceptsALocalOnlyChannel)
{
	// Arrange
	TMessagingSystem<> System;
	const FChannelInformation Information{"Telemetry", false, nullptr, {}};

	// Act
	const EMessagingResult Result = System.CreateChannel(Information);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, Result, "A local-only channel should be accepted");
}

/**
 * Motivation: Confirms remote channel configuration accepts an explicit device and route.
 * Responsibilities: Verify a non-null transport device and non-default address create a channel.
 */
MW_TEST_CASE(MessagingSystem_AcceptsAChannelWithDeviceAndAddress)
{
	// Arrange
	TMessagingSystem<> System;
	FTestTransportDevice Device;
	FDeviceAddress Address{};
	Address.Bytes[0] = 7;
	Address.Size = 1;
	const FChannelInformation Information{"Telemetry", false, &Device, Address};

	// Act
	const EMessagingResult Result = System.CreateChannel(Information);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, Result, "A device-backed channel should be accepted");
}

/**
 * Motivation: Allows separate named routes to share one physical transport device.
 * Responsibilities: Verify distinct addresses on one device create two independent channels.
 */
MW_TEST_CASE(MessagingSystem_AcceptsTwoAddressesOnOneDevice)
{
	// Arrange
	TMessagingSystem<> System;
	FTestTransportDevice Device;
	FDeviceAddress FirstAddress{};
	FirstAddress.Bytes[0] = 3;
	FirstAddress.Size = 1;
	FDeviceAddress SecondAddress{};
	SecondAddress.Bytes[0] = 4;
	SecondAddress.Size = 1;
	const FChannelInformation FirstInformation{"Telemetry", false, &Device, FirstAddress};
	const FChannelInformation SecondInformation{"Commands", false, &Device, SecondAddress};

	// Act
	const EMessagingResult FirstResult = System.CreateChannel(FirstInformation);
	const EMessagingResult SecondResult = System.CreateChannel(SecondInformation);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstResult, "The first device route should be accepted");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondResult, "The second device route should be accepted");
}

/**
 * Motivation: Lets composition roots provide reliability policy before channel setup.
 * Responsibilities: Verify the system exposes exactly the policy supplied at construction.
 */
MW_TEST_CASE(MessagingSystem_ReturnsSuppliedSystemInformation)
{
	// Arrange
	FMessagingSystemInformation Information{};
	Information.ReliableRetryIntervalMilliseconds = 400;
	Information.MaxReliableSendAttempts = 3;
	TMessagingSystem<> System{Information};

	// Act
	const FMessagingSystemInformation& ReturnedInformation = System.GetInformation();

	// Assert
	MW_EXPECT_EQ(
		Test,
		Information.ReliableRetryIntervalMilliseconds,
		ReturnedInformation.ReliableRetryIntervalMilliseconds,
		"The retry interval should be preserved");
	MW_EXPECT_EQ(Test, Information.MaxReliableSendAttempts, ReturnedInformation.MaxReliableSendAttempts, "The attempt budget should be preserved");
}

/**
 * Motivation: Reserves lifecycle calls for later device and queue work without changing channel identity today.
 * Responsibilities: Verify empty lifecycle turns preserve the channel set and duplicate behavior.
 */
MW_TEST_CASE(MessagingSystem_LifecycleTurnsPreserveChannels)
{
	// Arrange
	TMessagingSystem<> System;
	const FChannelInformation Information{"Telemetry", false, nullptr, {}};
	System.CreateChannel(Information);

	// Act
	System.PreAdvance(TimePointMilliseconds{125});
	System.PostAdvance(TimePointMilliseconds{250});
	const EMessagingResult Result = System.CreateChannel(Information);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Duplicate, Result, "Lifecycle turns should preserve existing channels");
}

/**
 * Motivation: Makes a local-only channel useful by allowing one bound subscriber to register successfully.
 * Responsibilities: Verify a valid subscriber on an existing channel reports Success.
 */
MW_TEST_CASE(MessagingSystem_SubscribesToAnExistingChannel)
{
	// Arrange
	FDefaultMessagingSystem System;
	const FChannelInformation ChannelInformation{"Telemetry", false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	FDefaultSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([](const FMessage&) noexcept {});

	// Act
	const EMessagingResult SubscribeResult = System.SubscribeToChannel("Telemetry", std::move(Subscriber));

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The test channel should be created before subscription");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The test subscriber should bind within inline storage");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "A bound subscriber should register on an existing channel");
}

/**
 * Motivation: Keeps a subscription from silently naming a channel the system does not own.
 * Responsibilities: Verify a bound subscriber targeting an unknown channel reports NotFound.
 */
MW_TEST_CASE(MessagingSystem_RejectsSubscriptionToAnUnknownChannel)
{
	// Arrange
	FDefaultMessagingSystem System;
	FDefaultSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([](const FMessage&) noexcept {});

	// Act
	const EMessagingResult SubscribeResult = System.SubscribeToChannel("Missing", std::move(Subscriber));

	// Assert
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The unknown-channel subscriber should bind within inline storage");
	MW_EXPECT_EQ(Test, EMessagingResult::NotFound, SubscribeResult, "A subscription should reject a channel that was never created");
}

/**
 * Motivation: Makes system-wide subscriber capacity exhaustion visible without depending on production trait limits.
 * Responsibilities: Verify the first subscriber beyond a small configured limit reports Full.
 */
MW_TEST_CASE(MessagingSystem_RejectsSubscriptionPastCapacity)
{
	// Arrange
	TMessagingSystem<FSmallSubscriptionTraits> System;
	const FChannelInformation ChannelInformation{"Telemetry", false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	TMessagingSystem<FSmallSubscriptionTraits>::FSubscriberDelegate FirstSubscriber;
	TMessagingSystem<FSmallSubscriptionTraits>::FSubscriberDelegate SecondSubscriber;
	TMessagingSystem<FSmallSubscriptionTraits>::FSubscriberDelegate ThirdSubscriber;
	const EDelegateResult FirstBindingResult = FirstSubscriber.Bind([](const FMessage&) noexcept {});
	const EDelegateResult SecondBindingResult = SecondSubscriber.Bind([](const FMessage&) noexcept {});
	const EDelegateResult ThirdBindingResult = ThirdSubscriber.Bind([](const FMessage&) noexcept {});
	const EMessagingResult FirstSubscribeResult = System.SubscribeToChannel("Telemetry", std::move(FirstSubscriber));
	const EMessagingResult SecondSubscribeResult = System.SubscribeToChannel("Telemetry", std::move(SecondSubscriber));

	// Act
	const EMessagingResult ThirdSubscribeResult = System.SubscribeToChannel("Telemetry", std::move(ThirdSubscriber));

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The capacity-test channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, FirstBindingResult, "The first subscriber should bind within inline storage");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SecondBindingResult, "The second subscriber should bind within inline storage");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, ThirdBindingResult, "The overflow subscriber should bind within inline storage");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstSubscribeResult, "The first subscriber should consume the first available slot");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondSubscribeResult, "The second subscriber should consume the final available slot");
	MW_EXPECT_EQ(Test, EMessagingResult::Full, ThirdSubscribeResult, "A subscriber beyond the configured capacity should be rejected");
}

/**
 * Motivation: Prevents inert callback storage from becoming a subscription that appears valid but cannot receive delivery.
 * Responsibilities: Verify an unbound delegate reports Invalid without requiring a send.
 */
MW_TEST_CASE(MessagingSystem_RejectsAnUnboundSubscriber)
{
	// Arrange
	FDefaultMessagingSystem System;
	const FChannelInformation ChannelInformation{"Telemetry", false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	FDefaultSubscriberDelegate Subscriber;

	// Act
	const EMessagingResult SubscribeResult = System.SubscribeToChannel("Telemetry", std::move(Subscriber));

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The test channel should be created before subscription");
	MW_EXPECT_EQ(Test, EMessagingResult::Invalid, SubscribeResult, "An unbound delegate should not become a subscriber");
}

/**
 * Motivation: Proves local-only Messaging delivers a caller's named bytes synchronously to an unfiltered subscriber.
 * Responsibilities: Verify the subscriber observes the sent message name and exact payload bytes.
 */
MW_TEST_CASE(MessagingSystem_DeliversAnUnfilteredMessageLocally)
{
	// Arrange
	FDefaultMessagingSystem System;
	const FChannelInformation ChannelInformation{"Telemetry", false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	FMessageRecorder Recorder;
	FDefaultSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&Recorder](const FMessage& InMessage) noexcept { Recorder.Record(InMessage); });
	const EMessagingResult SubscribeResult = System.SubscribeToChannel("Telemetry", std::move(Subscriber));
	const std::uint8_t Payload[FMessageRecorder::MaxRecordedPayloadBytes] = {11, 22, 33};
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");
	Message.SetPayload(TSpan<const std::uint8_t>(Payload));

	// Act
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, "Telemetry");

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The local channel should be created before delivery");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The local subscriber should bind within inline storage");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The local subscriber should register successfully");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "A named message should send to an existing local channel");
	MW_EXPECT_EQ(Test, std::size_t{1}, Recorder.DeliveryCount, "The unfiltered subscriber should receive exactly one message");
	MW_EXPECT_EQ(Test, FNameId{"TemperatureUpdated"}, Recorder.MessageNameId, "The subscriber should observe the sent message name");
	MW_EXPECT_EQ(Test, FMessageRecorder::MaxRecordedPayloadBytes, Recorder.PayloadSize, "The subscriber should observe the sent payload length");
	MW_EXPECT_EQ(Test, std::uint8_t{11}, Recorder.PayloadBytes[0], "The subscriber should observe the first payload byte");
	MW_EXPECT_EQ(Test, std::uint8_t{22}, Recorder.PayloadBytes[1], "The subscriber should observe the second payload byte");
	MW_EXPECT_EQ(Test, std::uint8_t{33}, Recorder.PayloadBytes[2], "The subscriber should observe the third payload byte");
}

/**
 * Motivation: Lets one channel carry several message kinds without waking unrelated subscribers.
 * Responsibilities: Verify a matching filter receives a message while a different filter on the same channel does not.
 */
MW_TEST_CASE(MessagingSystem_DeliversOnlyToMatchingMessageFilters)
{
	// Arrange
	FDefaultMessagingSystem System;
	const FChannelInformation ChannelInformation{"Telemetry", false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	std::size_t MatchingDeliveryCount = 0;
	std::size_t MismatchedDeliveryCount = 0;
	FDefaultSubscriberDelegate MatchingSubscriber;
	FDefaultSubscriberDelegate MismatchedSubscriber;
	const EDelegateResult MatchingBindingResult =
		MatchingSubscriber.Bind([&MatchingDeliveryCount](const FMessage&) noexcept { ++MatchingDeliveryCount; });
	const EDelegateResult MismatchedBindingResult =
		MismatchedSubscriber.Bind([&MismatchedDeliveryCount](const FMessage&) noexcept { ++MismatchedDeliveryCount; });
	const EMessagingResult MatchingSubscribeResult = System.SubscribeToChannel("Telemetry", "TemperatureUpdated", std::move(MatchingSubscriber));
	const EMessagingResult MismatchedSubscribeResult = System.SubscribeToChannel("Telemetry", "CommandReceived", std::move(MismatchedSubscriber));
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");

	// Act
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, "Telemetry");

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The filtered-delivery channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, MatchingBindingResult, "The matching subscriber should bind within inline storage");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, MismatchedBindingResult, "The mismatched subscriber should bind within inline storage");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, MatchingSubscribeResult, "The matching filter should register successfully");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, MismatchedSubscribeResult, "The mismatched filter should register successfully");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "A filtered message should send successfully on its channel");
	MW_EXPECT_EQ(Test, std::size_t{1}, MatchingDeliveryCount, "The matching filter should receive exactly one message");
	MW_EXPECT_EQ(Test, std::size_t{0}, MismatchedDeliveryCount, "The different filter should not receive the message");
}

/**
 * Motivation: Makes local broadcast sequencing predictable for statically composed subscribers.
 * Responsibilities: Verify two matching subscribers receive one send in their registration order.
 */
MW_TEST_CASE(MessagingSystem_DeliversSubscribersInRegistrationOrder)
{
	// Arrange
	FDefaultMessagingSystem System;
	const FChannelInformation ChannelInformation{"Telemetry", false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	FDeliveryOrderRecorder Recorder;
	FDefaultSubscriberDelegate FirstSubscriber;
	FDefaultSubscriberDelegate SecondSubscriber;
	const EDelegateResult FirstBindingResult = FirstSubscriber.Bind([&Recorder](const FMessage&) noexcept { Recorder.Record(1); });
	const EDelegateResult SecondBindingResult = SecondSubscriber.Bind([&Recorder](const FMessage&) noexcept { Recorder.Record(2); });
	const EMessagingResult FirstSubscribeResult = System.SubscribeToChannel("Telemetry", std::move(FirstSubscriber));
	const EMessagingResult SecondSubscribeResult = System.SubscribeToChannel("Telemetry", std::move(SecondSubscriber));
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");

	// Act
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, "Telemetry");

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The registration-order channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, FirstBindingResult, "The first ordered subscriber should bind within inline storage");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SecondBindingResult, "The second ordered subscriber should bind within inline storage");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstSubscribeResult, "The first ordered subscriber should register successfully");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondSubscribeResult, "The second ordered subscriber should register successfully");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The ordered broadcast should send successfully");
	MW_EXPECT_EQ(Test, std::size_t{2}, Recorder.DeliveryCount, "Both ordered subscribers should receive the send");
	MW_EXPECT_EQ(Test, std::uint8_t{1}, Recorder.SubscriberOrders[0], "The first registered subscriber should receive first");
	MW_EXPECT_EQ(Test, std::uint8_t{2}, Recorder.SubscriberOrders[1], "The second registered subscriber should receive second");
}

/**
 * Motivation: Prevents a mistyped destination from delivering a message through an unrelated channel.
 * Responsibilities: Verify an unknown channel reports NotFound and does not invoke an existing subscriber.
 */
MW_TEST_CASE(MessagingSystem_RejectsSendingToAnUnknownChannel)
{
	// Arrange
	FDefaultMessagingSystem System;
	const FChannelInformation ChannelInformation{"Telemetry", false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	std::size_t DeliveryCount = 0;
	FDefaultSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	const EMessagingResult SubscribeResult = System.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");

	// Act
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, "Missing");

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The unrelated subscriber channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The unrelated subscriber should bind within inline storage");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The unrelated subscriber should register successfully");
	MW_EXPECT_EQ(Test, EMessagingResult::NotFound, SendResult, "A send to an unknown channel should report NotFound");
	MW_EXPECT_EQ(Test, std::size_t{0}, DeliveryCount, "An unknown channel send should deliver to no subscriber");
}

/**
 * Motivation: Rejects anonymous messages before any local subscriber can observe ambiguous routing input.
 * Responsibilities: Verify an unset message name reports Invalid and delivers nothing.
 */
MW_TEST_CASE(MessagingSystem_RejectsSendingAnUnnamedMessage)
{
	// Arrange
	FDefaultMessagingSystem System;
	const FChannelInformation ChannelInformation{"Telemetry", false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	std::size_t DeliveryCount = 0;
	FDefaultSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	const EMessagingResult SubscribeResult = System.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FMessage Message;

	// Act
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, "Telemetry");

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The unnamed-message channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The unnamed-message subscriber should bind within inline storage");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The unnamed-message subscriber should register successfully");
	MW_EXPECT_EQ(Test, EMessagingResult::Invalid, SendResult, "A message with no name should report Invalid");
	MW_EXPECT_EQ(Test, std::size_t{0}, DeliveryCount, "An unnamed message should deliver to no subscriber");
}

/**
 * Motivation: Treats a channel with no local listeners as a normal composition state rather than a delivery failure.
 * Responsibilities: Verify a named message on an existing unsubscribed channel reports Success.
 */
MW_TEST_CASE(MessagingSystem_AcceptsSendingToAChannelWithNoSubscribers)
{
	// Arrange
	FDefaultMessagingSystem System;
	const FChannelInformation ChannelInformation{"Telemetry", false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");

	// Act
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, "Telemetry");

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The unsubscribed channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "A send with zero matching subscribers should still succeed");
}

/**
 * Motivation: Proves a device never replaces local delivery, which is what makes Messaging complete with no transport linked.
 * Responsibilities: Verify a device-backed channel delivers to its local subscriber and asks its device to send exactly once.
 */
MW_TEST_CASE(MessagingSystem_DeliversLocallyAndRemotelyOnADeviceBackedChannel)
{
	// Arrange
	FDefaultMessagingSystem System;
	FTestTransportDevice Device;
	FDeviceAddress Address{};
	Address.Bytes[0] = 7;
	Address.Size = 1;
	const FChannelInformation ChannelInformation{"Telemetry", false, &Device, Address};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	std::size_t DeliveryCount = 0;
	FDefaultSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	const EMessagingResult SubscribeResult = System.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");

	// Act
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, "Telemetry");

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The device-backed channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The device-backed subscriber should bind within inline storage");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The device-backed subscriber should register successfully");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "A device-backed local send should succeed");
	MW_EXPECT_EQ(Test, std::size_t{1}, DeliveryCount, "A device-backed channel should still deliver locally");
	MW_EXPECT_EQ(Test, std::size_t{1}, Device.GetTrySendCallCount(), "A device-backed channel should also send one frame to its device");
}

/**
 * Motivation: Keeps synchronous subscriber composition safe when a callback expands the subscription table and sends once more.
 * Responsibilities: Verify a newly added subscriber is skipped by the active send while one nested send completes without recursion.
 */
MW_TEST_CASE(MessagingSystem_SkipsSubscriptionsAddedDuringAnActiveSend)
{
	// Arrange
	FDefaultMessagingSystem System;
	const FChannelInformation ChannelInformation{"Telemetry", false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	FReentrantDispatchContext Context{};
	Context.MessagingSystem = &System;
	Context.ChannelNameId = "Telemetry";
	Context.OuterMessageNameId = "Outer";
	Context.NestedMessageNameId = "Nested";
	FReentrantSubscriber ReentrantSubscriber{&Context};
	FDefaultSubscriberDelegate ReentrantDelegate;
	FDefaultSubscriberDelegate NestedDelegate;
	const EDelegateResult ReentrantBindingResult = ReentrantDelegate.Bind(std::move(ReentrantSubscriber));
	const EDelegateResult NestedBindingResult = NestedDelegate.Bind([&Context](const FMessage&) noexcept { ++Context.NestedDeliveryCount; });
	const EMessagingResult ReentrantSubscribeResult = System.SubscribeToChannel("Telemetry", std::move(ReentrantDelegate));
	const EMessagingResult NestedSubscribeResult = System.SubscribeToChannel("Telemetry", "Nested", std::move(NestedDelegate));
	FMessage OuterMessage;
	OuterMessage.SetMessageNameId("Outer");

	// Act
	const EMessagingResult FirstOuterSendResult = System.SendMessageToChannel(OuterMessage, "Telemetry");

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The reentrant-delivery channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, ReentrantBindingResult, "The reentrant subscriber should bind within inline storage");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, NestedBindingResult, "The nested-message subscriber should bind within inline storage");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReentrantSubscribeResult, "The reentrant subscriber should register successfully");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, NestedSubscribeResult, "The nested-message subscriber should register successfully");
	MW_EXPECT_EQ(
		Test, EMessagingResult::Success, FirstOuterSendResult, "The outer send should complete while a subscriber adds another subscription");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, Context.AddedSubscriberResult, "The callback should add its subscriber during outer delivery");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, Context.NestedSendResult, "The callback should complete one nested send");
	MW_EXPECT_EQ(Test, std::size_t{1}, Context.NestedDeliveryCount, "The nested send should deliver exactly once without recursive repetition");
	MW_EXPECT_EQ(
		Test, std::size_t{0}, Context.AddedSubscriberDeliveryCount, "The subscriber added during dispatch should not receive the active outer send");

	// Act: prove the added subscription participates in the next independent send.
	const EMessagingResult SecondOuterSendResult = System.SendMessageToChannel(OuterMessage, "Telemetry");

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondOuterSendResult, "The next outer send should complete after subscription growth");
	MW_EXPECT_EQ(
		Test, std::size_t{1}, Context.AddedSubscriberDeliveryCount, "The newly added subscriber should receive the next outer send exactly once");
}

/**
 * Motivation: Keeps independent named channels from leaking local messages to each other's subscribers.
 * Responsibilities: Verify a subscriber on channel A receives nothing when a message is sent on channel B.
 */
MW_TEST_CASE(MessagingSystem_SeparatesSubscribersByChannelName)
{
	// Arrange
	FDefaultMessagingSystem System;
	const FChannelInformation FirstChannelInformation{"Telemetry", false, nullptr, {}};
	const FChannelInformation SecondChannelInformation{"Commands", false, nullptr, {}};
	const EMessagingResult FirstCreateResult = System.CreateChannel(FirstChannelInformation);
	const EMessagingResult SecondCreateResult = System.CreateChannel(SecondChannelInformation);
	std::size_t DeliveryCount = 0;
	FDefaultSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	const EMessagingResult SubscribeResult = System.SubscribeToChannel("Telemetry", std::move(Subscriber));
	FMessage Message;
	Message.SetMessageNameId("TemperatureUpdated");

	// Act
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, "Commands");

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstCreateResult, "The subscriber channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondCreateResult, "The sent-to channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The isolated subscriber should bind within inline storage");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The isolated subscriber should register successfully");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "A send on the second channel should succeed");
	MW_EXPECT_EQ(Test, std::size_t{0}, DeliveryCount, "A subscriber on the first channel should not receive a second-channel send");
}

} // namespace
