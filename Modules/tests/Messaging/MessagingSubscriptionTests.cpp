#include "TestSupport.h"

#include <MicroWorld/Core/WeakOwner.h>
#include <MicroWorld/Messaging/MessagingSystem.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{

using MicroWorld::Core::EDelegateResult;
using MicroWorld::Core::FWeakOwner;
using MicroWorld::Messaging::EMessagingResult;
using MicroWorld::Messaging::FChannelInformation;
using MicroWorld::Messaging::FDefaultMessagingTraits;
using MicroWorld::Messaging::FMessage;
using MicroWorld::Messaging::FNameId;
using MicroWorld::Messaging::TMessagingSystem;

/** Motivation: Names the default Messaging system used by subscription ownership tests. */
using FDefaultMessagingSystem = TMessagingSystem<>;

/** Motivation: Names the default bounded subscriber delegate without repeating its system-qualified declaration. */
using FDefaultSubscriberDelegate = FDefaultMessagingSystem::FSubscriberDelegate;

/**
 * Motivation: Makes slot reclamation and capacity behavior observable without using the production subscription limit.
 * Responsibilities: Reserve two channels and two subscription slots while retaining every other default Messaging capacity.
 * Example:
 *   TMessagingSystem<FSmallSubscriptionTraits> System;
 */
struct FSmallSubscriptionTraits final : FDefaultMessagingTraits
{
	/** Motivation: Leaves one quiet and one busy channel available for cross-channel reclamation tests. */
	static constexpr std::size_t MaxChannels = 2;

	/** Motivation: Limits tests to two subscription slots so full and reuse paths stay small. */
	static constexpr std::size_t MaxSubscriptions = 2;
};

/**
 * Motivation: Makes one-slot reuse observable after delivery reclaims a dead owner.
 * Responsibilities: Reserve one channel and one subscription slot while retaining every other default Messaging capacity.
 * Example:
 *   TMessagingSystem<FOneSubscriptionTraits> System;
 */
struct FOneSubscriptionTraits final : FDefaultMessagingTraits
{
	/** Motivation: Leaves exactly one valid channel for the one-slot reclamation test. */
	static constexpr std::size_t MaxChannels = 1;

	/** Motivation: Forces a reclaimed dead-owner slot to be reused by the next valid subscription. */
	static constexpr std::size_t MaxSubscriptions = 1;
};

/** Motivation: Names the reduced-capacity Messaging system used for full and reentrant subscription tests. */
using FSmallSubscriptionMessagingSystem = TMessagingSystem<FSmallSubscriptionTraits>;

/** Motivation: Names the one-slot Messaging system used to prove dead-owner capacity reuse. */
using FOneSubscriptionMessagingSystem = TMessagingSystem<FOneSubscriptionTraits>;

/**
 * Motivation: Preserves ownerless subscriptions for standalone lambdas that do not capture an object lifetime.
 * Responsibilities: Verify an ownerless bound subscriber registers and receives one matching local message.
 */
MW_TEST_CASE(MessagingSubscription_OwnerlessSubscriberStillDelivers)
{
	// Arrange
	/** Motivation: Names the local route used by the ownerless subscription. */
	const FNameId ChannelNameId{"Telemetry"};
	/** Motivation: Names the message delivered to the ownerless subscription. */
	const FNameId MessageNameId{"TemperatureUpdated"};
	/** Motivation: States the single delivery expected for a live ownerless registration. */
	const std::size_t ExpectedDeliveryCount{1};
	FDefaultMessagingSystem System;
	const FChannelInformation ChannelInformation{ChannelNameId, false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	std::size_t DeliveryCount{0};
	FDefaultSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	const EMessagingResult SubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(Subscriber));
	FMessage Message;
	Message.SetMessageNameId(MessageNameId);

	// Act
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, ChannelNameId);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The ownerless channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The ownerless subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "An ownerless subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The ownerless message should send");
	MW_EXPECT_EQ(Test, ExpectedDeliveryCount, DeliveryCount, "An ownerless subscriber should receive the message");
}

/**
 * Motivation: Gives optional handle consumers a stable identity while preserving the existing no-handle call shape.
 * Responsibilities: Verify successful subscription writes a non-default handle only when the caller requests one.
 */
MW_TEST_CASE(MessagingSubscription_WritesRequestedHandleAndAllowsNoHandle)
{
	// Arrange
	/** Motivation: Names the local route used by both handle registration paths. */
	const FNameId ChannelNameId{"Telemetry"};
	/** Motivation: Distinguishes the first handle-observed subscriber from the second no-handle subscriber. */
	const FNameId FirstMessageNameId{"TemperatureUpdated"};
	/** Motivation: Distinguishes the second no-handle subscriber from the first handle-observed subscriber. */
	const FNameId SecondMessageNameId{"PressureUpdated"};
	/** Motivation: Names the generation no occupied slot ever holds, so a written handle must differ from it. */
	const std::uint16_t InvalidHandleGeneration{0};
	FSmallSubscriptionMessagingSystem System;
	const FChannelInformation ChannelInformation{ChannelNameId, false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	FSmallSubscriptionMessagingSystem::FSubscriberDelegate FirstSubscriber;
	FSmallSubscriptionMessagingSystem::FSubscriberDelegate SecondSubscriber;
	const EDelegateResult FirstBindingResult = FirstSubscriber.Bind([](const FMessage&) noexcept {});
	const EDelegateResult SecondBindingResult = SecondSubscriber.Bind([](const FMessage&) noexcept {});
	FSmallSubscriptionMessagingSystem::FSubscriptionHandle Handle{};

	// Act
	const EMessagingResult FirstSubscribeResult =
		System.SubscribeToChannel(ChannelNameId, FirstMessageNameId, std::move(FirstSubscriber), {}, &Handle);
	const EMessagingResult SecondSubscribeResult = System.SubscribeToChannel(ChannelNameId, SecondMessageNameId, std::move(SecondSubscriber));

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The handle channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, FirstBindingResult, "The handle subscriber should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SecondBindingResult, "The no-handle subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstSubscribeResult, "A requested handle should be written on successful subscription");
	MW_EXPECT_TRUE(
		Test, Handle.Index != FSmallSubscriptionMessagingSystem::FSubscriptionHandle::InvalidIndex, "A successful handle should identify a slot");
	MW_EXPECT_TRUE(Test, Handle.Generation != InvalidHandleGeneration, "A successful handle should have a live generation");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondSubscribeResult, "A caller may omit the optional handle");
}

/**
 * Motivation: Rejects registrations tied to an owner that is already dead before they consume a slot.
 * Responsibilities: Verify a dead owner reports Invalid and its callable cannot receive a later message.
 */
MW_TEST_CASE(MessagingSubscription_RejectsDeadOwnerWithoutDelivery)
{
	// Arrange
	/** Motivation: Names the local route used by the rejected owner-bound subscription. */
	const FNameId ChannelNameId{"Telemetry"};
	/** Motivation: Names the message sent after the dead-owner subscription was rejected. */
	const FNameId MessageNameId{"TemperatureUpdated"};
	/** Motivation: Represents the generation the subscription captured before its owner died. */
	const std::uint32_t CapturedOwnerGeneration{7};
	/** Motivation: Advances the owner counter to represent destruction before subscription. */
	const std::uint32_t OwnerDestructionIncrement{1};
	/** Motivation: Marks the token as tied to an owner rather than intentionally ownerless. */
	const bool bHasOwner{true};
	/** Motivation: States that a rejected dead-owner subscriber must never receive the later message. */
	const std::size_t ExpectedDeliveryCount{0};
	/** Motivation: Preserves a recognizable generation to prove failed subscription leaves an optional handle untouched. */
	const std::uint16_t InitialHandleGeneration{29};
	FDefaultMessagingSystem System;
	const FChannelInformation ChannelInformation{ChannelNameId, false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	std::uint32_t OwnerGenerationCounter = CapturedOwnerGeneration;
	OwnerGenerationCounter += OwnerDestructionIncrement;
	const FWeakOwner DeadOwner{&OwnerGenerationCounter, CapturedOwnerGeneration, bHasOwner};
	std::size_t DeliveryCount{0};
	FDefaultSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	FDefaultMessagingSystem::FSubscriptionHandle Handle{FDefaultMessagingSystem::FSubscriptionHandle::InvalidIndex, InitialHandleGeneration};
	FMessage Message;
	Message.SetMessageNameId(MessageNameId);

	// Act
	const EMessagingResult SubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(Subscriber), DeadOwner, &Handle);
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, ChannelNameId);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The dead-owner channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The dead-owner subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Invalid, SubscribeResult, "A dead owner should be rejected");
	MW_EXPECT_EQ(Test, InitialHandleGeneration, Handle.Generation, "A failed dead-owner subscription should leave the optional handle untouched");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The later message should still send");
	MW_EXPECT_EQ(Test, ExpectedDeliveryCount, DeliveryCount, "A rejected dead-owner subscriber should not receive delivery");
}

/**
 * Motivation: Keeps owner-bound subscriptions usable until their owner actually dies.
 * Responsibilities: Verify a matching owner generation allows normal local delivery.
 */
MW_TEST_CASE(MessagingSubscription_LiveOwnerReceivesMessage)
{
	// Arrange
	/** Motivation: Names the local route used by the live owner subscription. */
	const FNameId ChannelNameId{"Telemetry"};
	/** Motivation: Names the message delivered while the owner remains live. */
	const FNameId MessageNameId{"TemperatureUpdated"};
	/** Motivation: Represents the live generation captured by the subscription. */
	const std::uint32_t LiveOwnerGeneration{11};
	/** Motivation: Marks the token as tied to a still-live owner. */
	const bool bHasOwner{true};
	/** Motivation: States the one expected delivery while the owner generation remains unchanged. */
	const std::size_t ExpectedDeliveryCount{1};
	FDefaultMessagingSystem System;
	const FChannelInformation ChannelInformation{ChannelNameId, false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	std::uint32_t OwnerGenerationCounter = LiveOwnerGeneration;
	const FWeakOwner LiveOwner{&OwnerGenerationCounter, LiveOwnerGeneration, bHasOwner};
	std::size_t DeliveryCount{0};
	FDefaultSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	const EMessagingResult SubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(Subscriber), LiveOwner);
	FMessage Message;
	Message.SetMessageNameId(MessageNameId);

	// Act
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, ChannelNameId);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The live-owner channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The live-owner subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "A live owner should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The live-owner message should send");
	MW_EXPECT_EQ(Test, ExpectedDeliveryCount, DeliveryCount, "A live owner subscriber should receive delivery");
}

/**
 * Motivation: Prevents the next delivery from invoking a callable whose captured owner died after registration.
 * Responsibilities: Verify dead-owner delivery is skipped and reclamation becomes observable exactly once.
 */
MW_TEST_CASE(MessagingSubscription_ReclaimsDeadOwnerBeforeInvocation)
{
	// Arrange
	/** Motivation: Names the local route used by the owner-bound subscription. */
	const FNameId ChannelNameId{"Telemetry"};
	/** Motivation: Names the message that triggers dead-owner reclamation. */
	const FNameId MessageNameId{"TemperatureUpdated"};
	/** Motivation: Represents the generation captured while the owner was alive. */
	const std::uint32_t CapturedOwnerGeneration{13};
	/** Motivation: Advances the owner generation to represent destruction before delivery. */
	const std::uint32_t OwnerDestructionIncrement{1};
	/** Motivation: Marks the token as tied to the destroyed owner. */
	const bool bHasOwner{true};
	/** Motivation: States that no dead-owner callback invocation is permitted. */
	const std::size_t ExpectedDeliveryCount{0};
	/** Motivation: States the one dead-owner slot reclamation expected from the triggering delivery. */
	const std::uint32_t ExpectedReclaimedSubscriptionCount{1};
	FDefaultMessagingSystem System;
	const FChannelInformation ChannelInformation{ChannelNameId, false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	std::uint32_t OwnerGenerationCounter = CapturedOwnerGeneration;
	const FWeakOwner Owner{&OwnerGenerationCounter, CapturedOwnerGeneration, bHasOwner};
	std::size_t DeliveryCount{0};
	FDefaultSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	const EMessagingResult SubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(Subscriber), Owner);
	OwnerGenerationCounter += OwnerDestructionIncrement;
	FMessage Message;
	Message.SetMessageNameId(MessageNameId);

	// Act
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, ChannelNameId);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The reclamation channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The dead-owner subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The owner should register while it is live");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The reclamation message should send");
	MW_EXPECT_EQ(Test, ExpectedDeliveryCount, DeliveryCount, "A dead owner should not receive delivery");
	MW_EXPECT_EQ(
		Test, ExpectedReclaimedSubscriptionCount, System.GetReclaimedDeadOwnerSubscriptionCount(), "The dead owner should be reclaimed once");
}

/**
 * Motivation: Returns bounded subscription capacity after delivery discovers an owner is dead.
 * Responsibilities: Verify a reclaimed dead-owner slot accepts and delivers to one replacement subscriber.
 */
MW_TEST_CASE(MessagingSubscription_ReclaimedDeadOwnerSlotIsReusable)
{
	// Arrange
	/** Motivation: Names the only local route in the one-slot subscription system. */
	const FNameId ChannelNameId{"Telemetry"};
	/** Motivation: Names the message that reclaims the old subscription and reaches the replacement. */
	const FNameId MessageNameId{"TemperatureUpdated"};
	/** Motivation: Represents the generation captured while the first owner was alive. */
	const std::uint32_t CapturedOwnerGeneration{17};
	/** Motivation: Advances the first owner generation to represent its destruction. */
	const std::uint32_t OwnerDestructionIncrement{1};
	/** Motivation: Marks the first token as tied to an owner. */
	const bool bHasOwner{true};
	/** Motivation: States the one delivery expected for the replacement subscriber. */
	const std::size_t ExpectedReplacementDeliveryCount{1};
	FOneSubscriptionMessagingSystem System;
	const FChannelInformation ChannelInformation{ChannelNameId, false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	std::uint32_t OwnerGenerationCounter = CapturedOwnerGeneration;
	const FWeakOwner Owner{&OwnerGenerationCounter, CapturedOwnerGeneration, bHasOwner};
	FOneSubscriptionMessagingSystem::FSubscriberDelegate DeadOwnerSubscriber;
	const EDelegateResult DeadOwnerBindingResult = DeadOwnerSubscriber.Bind([](const FMessage&) noexcept {});
	const EMessagingResult DeadOwnerSubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(DeadOwnerSubscriber), Owner);
	OwnerGenerationCounter += OwnerDestructionIncrement;
	FMessage Message;
	Message.SetMessageNameId(MessageNameId);
	const EMessagingResult ReclaimingSendResult = System.SendMessageToChannel(Message, ChannelNameId);
	std::size_t ReplacementDeliveryCount{0};
	FOneSubscriptionMessagingSystem::FSubscriberDelegate ReplacementSubscriber;
	const EDelegateResult ReplacementBindingResult =
		ReplacementSubscriber.Bind([&ReplacementDeliveryCount](const FMessage&) noexcept { ++ReplacementDeliveryCount; });

	// Act
	const EMessagingResult ReplacementSubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(ReplacementSubscriber));
	const EMessagingResult ReplacementSendResult = System.SendMessageToChannel(Message, ChannelNameId);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The one-slot channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, DeadOwnerBindingResult, "The first subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, DeadOwnerSubscribeResult, "The first live owner should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReclaimingSendResult, "The dead-owner send should reclaim capacity");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, ReplacementBindingResult, "The replacement subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReplacementSubscribeResult, "A reclaimed slot should accept a replacement");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReplacementSendResult, "The replacement message should send");
	MW_EXPECT_EQ(Test, ExpectedReplacementDeliveryCount, ReplacementDeliveryCount, "The replacement subscriber should receive the next message");
}

/**
 * Motivation: Reclaims dead owners from every occupied slot even when their own channel is quiet.
 * Responsibilities: Verify delivery on a busy channel cleans a dead subscription on a different channel.
 */
MW_TEST_CASE(MessagingSubscription_ReclaimsDeadOwnerFromDifferentChannel)
{
	// Arrange
	/** Motivation: Names the quiet channel that holds the dead-owner subscription. */
	const FNameId QuietChannelNameId{"Quiet"};
	/** Motivation: Names the busy channel whose delivery triggers global dead-owner reclamation. */
	const FNameId BusyChannelNameId{"Busy"};
	/** Motivation: Names the message sent only through the busy channel. */
	const FNameId MessageNameId{"TemperatureUpdated"};
	/** Motivation: Represents the generation captured while the quiet-channel owner was alive. */
	const std::uint32_t CapturedOwnerGeneration{19};
	/** Motivation: Advances the quiet-channel owner generation to represent its destruction. */
	const std::uint32_t OwnerDestructionIncrement{1};
	/** Motivation: Marks the quiet-channel token as tied to an owner. */
	const bool bHasOwner{true};
	/** Motivation: States the one quiet-channel slot reclamation expected from busy-channel delivery. */
	const std::uint32_t ExpectedReclaimedSubscriptionCount{1};
	FSmallSubscriptionMessagingSystem System;
	const FChannelInformation QuietChannelInformation{QuietChannelNameId, false, nullptr, {}};
	const FChannelInformation BusyChannelInformation{BusyChannelNameId, false, nullptr, {}};
	const EMessagingResult QuietCreateResult = System.CreateChannel(QuietChannelInformation);
	const EMessagingResult BusyCreateResult = System.CreateChannel(BusyChannelInformation);
	std::uint32_t OwnerGenerationCounter = CapturedOwnerGeneration;
	const FWeakOwner Owner{&OwnerGenerationCounter, CapturedOwnerGeneration, bHasOwner};
	FSmallSubscriptionMessagingSystem::FSubscriberDelegate QuietSubscriber;
	const EDelegateResult BindingResult = QuietSubscriber.Bind([](const FMessage&) noexcept {});
	const EMessagingResult SubscribeResult = System.SubscribeToChannel(QuietChannelNameId, std::move(QuietSubscriber), Owner);
	OwnerGenerationCounter += OwnerDestructionIncrement;
	FMessage Message;
	Message.SetMessageNameId(MessageNameId);

	// Act
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, BusyChannelNameId);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, QuietCreateResult, "The quiet channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, BusyCreateResult, "The busy channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The quiet subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The quiet owner should register while live");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The busy message should send");
	MW_EXPECT_EQ(
		Test,
		ExpectedReclaimedSubscriptionCount,
		System.GetReclaimedDeadOwnerSubscriptionCount(),
		"A busy channel should reclaim a dead quiet-channel owner");
}

/**
 * Motivation: Makes fixed subscription capacity visible when every configured slot is occupied by a live subscriber.
 * Responsibilities: Verify the first two subscriptions succeed and the third reports Full without changing the live slots.
 */
MW_TEST_CASE(MessagingSubscription_ReportsFullAfterEverySlotIsOccupied)
{
	// Arrange
	/** Motivation: Names the local route used to fill every configured subscription slot. */
	const FNameId ChannelNameId{"Telemetry"};
	FSmallSubscriptionMessagingSystem System;
	const FChannelInformation ChannelInformation{ChannelNameId, false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	FSmallSubscriptionMessagingSystem::FSubscriberDelegate FirstSubscriber;
	FSmallSubscriptionMessagingSystem::FSubscriberDelegate SecondSubscriber;
	FSmallSubscriptionMessagingSystem::FSubscriberDelegate OverflowSubscriber;
	const EDelegateResult FirstBindingResult = FirstSubscriber.Bind([](const FMessage&) noexcept {});
	const EDelegateResult SecondBindingResult = SecondSubscriber.Bind([](const FMessage&) noexcept {});
	const EDelegateResult OverflowBindingResult = OverflowSubscriber.Bind([](const FMessage&) noexcept {});
	const EMessagingResult FirstSubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(FirstSubscriber));
	const EMessagingResult SecondSubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(SecondSubscriber));

	// Act
	const EMessagingResult OverflowSubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(OverflowSubscriber));

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The full-capacity channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, FirstBindingResult, "The first full-capacity subscriber should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SecondBindingResult, "The second full-capacity subscriber should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, OverflowBindingResult, "The overflow subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstSubscribeResult, "The first slot should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondSubscribeResult, "The second slot should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Full, OverflowSubscribeResult, "A subscriber beyond every occupied slot should report Full");
}

/**
 * Motivation: Holds the state a subscriber needs to remove itself without exceeding fixed delegate callable storage.
 * Responsibilities: Provide one Messaging system, handle, delivery count, and unsubscribe result to the self-removing subscriber.
 * Example:
 *   FSelfUnsubscribeContext Context{&System};
 */
struct FSelfUnsubscribeContext final
{
	/** Motivation: Gives the callback the system that owns its subscription. */
	FDefaultMessagingSystem* MessagingSystem{nullptr};

	/** Motivation: Identifies the callback's own subscription after successful registration. */
	FDefaultMessagingSystem::FSubscriptionHandle Handle{};

	/** Motivation: Counts callback invocations before self-removal prevents later delivery. */
	std::size_t DeliveryCount{0};

	/** Motivation: Retains the removal result observed by the callback. */
	EMessagingResult UnsubscribeResult{EMessagingResult::Invalid};
};

/**
 * Motivation: Exercises self-removal through a pointer-sized delegate target rather than a capture too large for inline callable storage.
 * Responsibilities: Count one invocation and release the context's subscription when called.
 * Example:
 *   FSelfUnsubscribeSubscriber Subscriber{&Context};
 */
struct FSelfUnsubscribeSubscriber final
{
	/**
	 * Motivation: Proves a running subscriber can release its own slot without destroying its callable before return.
	 * Responsibilities: Count the invocation and store the result of removing the context's subscription.
	 */
	void operator()(const FMessage&) noexcept
	{
		++Context->DeliveryCount;
		Context->UnsubscribeResult = Context->MessagingSystem->Unsubscribe(Context->Handle);
	}

	/** Motivation: Reaches the bounded self-removal state without copying it into the delegate. */
	FSelfUnsubscribeContext* Context{nullptr};
};

/**
 * Motivation: Gives a successful subscriber handle an explicit lifecycle end.
 * Responsibilities: Verify removal stops later matching local delivery.
 */
MW_TEST_CASE(MessagingSubscription_UnsubscribeStopsDelivery)
{
	// Arrange
	/** Motivation: Names the route carrying both deliveries around explicit removal. */
	const FNameId ChannelNameId{"Telemetry"};
	/** Motivation: Names the message sent before and after explicit removal. */
	const FNameId MessageNameId{"TemperatureUpdated"};
	/** Motivation: States the one delivery expected before removal. */
	const std::size_t ExpectedFirstDeliveryCount{1};
	/** Motivation: States the unchanged count expected after removal. */
	const std::size_t ExpectedDeliveryCountAfterUnsubscribe{1};
	FDefaultMessagingSystem System;
	const FChannelInformation ChannelInformation{ChannelNameId, false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	std::size_t DeliveryCount{0};
	FDefaultSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	FDefaultMessagingSystem::FSubscriptionHandle Handle{};
	const EMessagingResult SubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(Subscriber), {}, &Handle);
	FMessage Message;
	Message.SetMessageNameId(MessageNameId);

	// Act
	const EMessagingResult FirstSendResult = System.SendMessageToChannel(Message, ChannelNameId);
	const std::size_t DeliveryCountBeforeUnsubscribe = DeliveryCount;
	const EMessagingResult UnsubscribeResult = System.Unsubscribe(Handle);
	const EMessagingResult SecondSendResult = System.SendMessageToChannel(Message, ChannelNameId);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The explicit-unsubscribe channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The explicit-unsubscribe subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The explicit-unsubscribe subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstSendResult, "The first message should send before removal");
	MW_EXPECT_EQ(Test, ExpectedFirstDeliveryCount, DeliveryCountBeforeUnsubscribe, "The subscriber should receive the first message");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, UnsubscribeResult, "A current handle should release its subscription");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondSendResult, "The second message should still send after removal");
	MW_EXPECT_EQ(Test, ExpectedDeliveryCountAfterUnsubscribe, DeliveryCount, "A removed subscriber should receive no later message");
}

/**
 * Motivation: Makes handles single-use after their matching subscription is removed.
 * Responsibilities: Verify a repeated removal reports NotFound and leaves another live subscriber intact.
 */
MW_TEST_CASE(MessagingSubscription_UnsubscribeTwiceLeavesOtherSubscriberLive)
{
	// Arrange
	/** Motivation: Names the route shared by the removed and retained subscribers. */
	const FNameId ChannelNameId{"Telemetry"};
	/** Motivation: Names the message used to observe the retained subscription. */
	const FNameId MessageNameId{"TemperatureUpdated"};
	/** Motivation: States that the removed subscriber receives no later delivery. */
	const std::size_t ExpectedRemovedSubscriberDeliveryCount{0};
	/** Motivation: States that the retained subscriber receives one later delivery. */
	const std::size_t ExpectedLiveSubscriberDeliveryCount{1};
	FSmallSubscriptionMessagingSystem System;
	const FChannelInformation ChannelInformation{ChannelNameId, false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	std::size_t RemovedSubscriberDeliveryCount{0};
	std::size_t LiveSubscriberDeliveryCount{0};
	FSmallSubscriptionMessagingSystem::FSubscriberDelegate RemovedSubscriber;
	FSmallSubscriptionMessagingSystem::FSubscriberDelegate LiveSubscriber;
	const EDelegateResult RemovedBindingResult =
		RemovedSubscriber.Bind([&RemovedSubscriberDeliveryCount](const FMessage&) noexcept { ++RemovedSubscriberDeliveryCount; });
	const EDelegateResult LiveBindingResult =
		LiveSubscriber.Bind([&LiveSubscriberDeliveryCount](const FMessage&) noexcept { ++LiveSubscriberDeliveryCount; });
	FSmallSubscriptionMessagingSystem::FSubscriptionHandle RemovedHandle{};
	const EMessagingResult RemovedSubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(RemovedSubscriber), {}, &RemovedHandle);
	const EMessagingResult LiveSubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(LiveSubscriber));
	FMessage Message;
	Message.SetMessageNameId(MessageNameId);

	// Act
	const EMessagingResult FirstUnsubscribeResult = System.Unsubscribe(RemovedHandle);
	const EMessagingResult SecondUnsubscribeResult = System.Unsubscribe(RemovedHandle);
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, ChannelNameId);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The repeated-unsubscribe channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, RemovedBindingResult, "The removed subscriber should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, LiveBindingResult, "The live subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, RemovedSubscribeResult, "The removed subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, LiveSubscribeResult, "The live subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstUnsubscribeResult, "The first removal should release the matching subscription");
	MW_EXPECT_EQ(Test, EMessagingResult::NotFound, SecondUnsubscribeResult, "The released handle should not remove a subscription twice");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The retained subscriber message should send");
	MW_EXPECT_EQ(Test, ExpectedRemovedSubscriberDeliveryCount, RemovedSubscriberDeliveryCount, "The removed subscriber should remain inactive");
	MW_EXPECT_EQ(Test, ExpectedLiveSubscriberDeliveryCount, LiveSubscriberDeliveryCount, "The other live subscriber should remain active");
}

/**
 * Motivation: Makes an optional output handle safe to pass back before any subscription succeeds.
 * Responsibilities: Verify the default out-of-range handle reports NotFound without accessing slot storage.
 */
MW_TEST_CASE(MessagingSubscription_DefaultHandleReportsNotFound)
{
	// Arrange
	FDefaultMessagingSystem System;
	const FDefaultMessagingSystem::FSubscriptionHandle DefaultHandle{};

	// Act
	const EMessagingResult UnsubscribeResult = System.Unsubscribe(DefaultHandle);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::NotFound, UnsubscribeResult, "A default handle should not identify a subscription");
}

/**
 * Motivation: Prevents a stale handle from removing a replacement that reused its former slot.
 * Responsibilities: Verify a released-and-reused handle reports NotFound while the replacement still receives delivery.
 */
MW_TEST_CASE(MessagingSubscription_StaleHandleCannotRemoveReplacement)
{
	// Arrange
	/** Motivation: Names the only route in the one-slot system. */
	const FNameId ChannelNameId{"Telemetry"};
	/** Motivation: Names the message used to observe the replacement subscriber. */
	const FNameId MessageNameId{"TemperatureUpdated"};
	/** Motivation: States the one delivery expected for the replacement subscriber. */
	const std::size_t ExpectedReplacementDeliveryCount{1};
	FOneSubscriptionMessagingSystem System;
	const FChannelInformation ChannelInformation{ChannelNameId, false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	FOneSubscriptionMessagingSystem::FSubscriberDelegate FirstSubscriber;
	const EDelegateResult FirstBindingResult = FirstSubscriber.Bind([](const FMessage&) noexcept {});
	FOneSubscriptionMessagingSystem::FSubscriptionHandle StaleHandle{};
	const EMessagingResult FirstSubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(FirstSubscriber), {}, &StaleHandle);
	const EMessagingResult FirstUnsubscribeResult = System.Unsubscribe(StaleHandle);
	std::size_t ReplacementDeliveryCount{0};
	FOneSubscriptionMessagingSystem::FSubscriberDelegate ReplacementSubscriber;
	const EDelegateResult ReplacementBindingResult =
		ReplacementSubscriber.Bind([&ReplacementDeliveryCount](const FMessage&) noexcept { ++ReplacementDeliveryCount; });
	const EMessagingResult ReplacementSubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(ReplacementSubscriber));
	FMessage Message;
	Message.SetMessageNameId(MessageNameId);

	// Act
	const EMessagingResult StaleUnsubscribeResult = System.Unsubscribe(StaleHandle);
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, ChannelNameId);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The stale-handle channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, FirstBindingResult, "The first one-slot subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstSubscribeResult, "The first one-slot subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstUnsubscribeResult, "The first handle should release its subscription");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, ReplacementBindingResult, "The replacement subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReplacementSubscribeResult, "The replacement should reuse the released slot");
	MW_EXPECT_EQ(Test, EMessagingResult::NotFound, StaleUnsubscribeResult, "A stale generation should not remove the replacement");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The replacement message should send");
	MW_EXPECT_EQ(Test, ExpectedReplacementDeliveryCount, ReplacementDeliveryCount, "The replacement should remain subscribed");
}

/**
 * Motivation: Keeps the ordinary callback lifecycle safe when a subscriber removes itself while executing.
 * Responsibilities: Verify self-removal succeeds without a crash and suppresses later delivery.
 */
MW_TEST_CASE(MessagingSubscription_SelfUnsubscribeIsSafeAndStopsLaterDelivery)
{
	// Arrange
	/** Motivation: Names the route used to invoke and later verify the self-removing subscriber. */
	const FNameId ChannelNameId{"Telemetry"};
	/** Motivation: Names both messages sent around self-removal. */
	const FNameId MessageNameId{"TemperatureUpdated"};
	/** Motivation: States the one callback invocation allowed before self-removal. */
	const std::size_t ExpectedDeliveryCount{1};
	FDefaultMessagingSystem System;
	const FChannelInformation ChannelInformation{ChannelNameId, false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	FSelfUnsubscribeContext Context{&System};
	FSelfUnsubscribeSubscriber SelfUnsubscribeSubscriber{&Context};
	FDefaultSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind(std::move(SelfUnsubscribeSubscriber));
	const EMessagingResult SubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(Subscriber), {}, &Context.Handle);
	FMessage Message;
	Message.SetMessageNameId(MessageNameId);

	// Act
	const EMessagingResult FirstSendResult = System.SendMessageToChannel(Message, ChannelNameId);
	const EMessagingResult SecondSendResult = System.SendMessageToChannel(Message, ChannelNameId);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The self-removal channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The self-removing subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The self-removing subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstSendResult, "The first self-removal message should send");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, Context.UnsubscribeResult, "The callback should release its own subscription");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondSendResult, "The second message should send after self-removal");
	MW_EXPECT_EQ(Test, ExpectedDeliveryCount, Context.DeliveryCount, "The self-removing subscriber should receive only its first message");
}

/**
 * Motivation: Gives a callback the state needed to release and immediately re-register from inside its own invocation.
 * Responsibilities: Retain the system, the callback's own handle, the channel, and both outcomes observed while running.
 * Example:
 *   FResubscribeDuringOwnCallbackContext Context{&System, {}, ChannelNameId};
 */
struct FResubscribeDuringOwnCallbackContext final
{
	/** Motivation: Gives the callback the system that owns the slot it is executing in. */
	FOneSubscriptionMessagingSystem* MessagingSystem{nullptr};

	/** Motivation: Identifies the callback's own subscription so it can release it while running. */
	FOneSubscriptionMessagingSystem::FSubscriptionHandle Handle{};

	/** Motivation: Names the channel the callback re-registers on. */
	FNameId ChannelNameId{};

	/** Motivation: Records whether the replacement subscriber bound before it was offered to the system. */
	EDelegateResult ReplacementBindingResult{EDelegateResult::InvalidHandle};

	/** Motivation: Retains the self-removal result observed from inside the callback. */
	EMessagingResult UnsubscribeResult{EMessagingResult::Invalid};

	/** Motivation: Retains the re-registration result, which must not be the slot still executing. */
	EMessagingResult ResubscribeResult{EMessagingResult::Invalid};
};

/**
 * Motivation: Drives the one sequence that would destroy a running callable: release its own slot, then ask for a slot back.
 * Responsibilities: Unsubscribe the context's own handle and attempt one replacement registration, retaining both outcomes.
 * Example:
 *   FResubscribeDuringOwnCallbackSubscriber Subscriber{&Context};
 */
struct FResubscribeDuringOwnCallbackSubscriber final
{
	/**
	 * Motivation: Proves the system refuses to hand a callable the storage it is currently running from.
	 * Responsibilities: Release the context's subscription, bind a replacement, and record what the system answers.
	 */
	void operator()(const FMessage&) noexcept
	{
		Context->UnsubscribeResult = Context->MessagingSystem->Unsubscribe(Context->Handle);

		FOneSubscriptionMessagingSystem::FSubscriberDelegate ReplacementSubscriber;
		Context->ReplacementBindingResult = ReplacementSubscriber.Bind([](const FMessage&) noexcept {});
		if (Context->ReplacementBindingResult != EDelegateResult::Success)
		{
			return;
		}

		Context->ResubscribeResult = Context->MessagingSystem->SubscribeToChannel(Context->ChannelNameId, std::move(ReplacementSubscriber));
	}

	/** Motivation: Reaches the shared state while keeping the bound callable pointer-sized. */
	FResubscribeDuringOwnCallbackContext* Context{nullptr};
};

/**
 * Motivation: Keeps a self-removing callback from being overwritten by the very subscription it registers next.
 * Responsibilities: Verify the only slot is withheld while its callable runs, so re-registration reports Full instead of reusing it.
 */
MW_TEST_CASE(MessagingSubscription_CallbackCannotReuseTheSlotItIsRunningIn)
{
	// Arrange
	/** Motivation: Names the only route in the one-slot system. */
	const FNameId ChannelNameId{"Telemetry"};
	/** Motivation: Names the message that invokes the self-removing callback. */
	const FNameId MessageNameId{"TemperatureUpdated"};
	FOneSubscriptionMessagingSystem System;
	const FChannelInformation ChannelInformation{ChannelNameId, false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	FResubscribeDuringOwnCallbackContext Context{&System, {}, ChannelNameId};
	FResubscribeDuringOwnCallbackSubscriber ResubscribingSubscriber{&Context};
	FOneSubscriptionMessagingSystem::FSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind(std::move(ResubscribingSubscriber));
	const EMessagingResult SubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(Subscriber), {}, &Context.Handle);
	FMessage Message;
	Message.SetMessageNameId(MessageNameId);

	// Act
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, ChannelNameId);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The self-reuse channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The re-registering subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The re-registering subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The self-reuse message should send");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, Context.UnsubscribeResult, "The callback should release its own subscription");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, Context.ReplacementBindingResult, "The replacement subscriber should bind");
	MW_EXPECT_EQ(
		Test,
		EMessagingResult::Full,
		Context.ResubscribeResult,
		"The only slot belongs to the running callable, so re-registration must report Full rather than overwrite it");
}

/**
 * Motivation: Lets one owner release all of its lifecycle-bound subscriptions without disturbing another owner.
 * Responsibilities: Verify both subscriptions for one owner are released while a second owner remains live.
 */
MW_TEST_CASE(MessagingSubscription_UnsubscribeAllKeepsOtherOwnersSubscribed)
{
	// Arrange
	/** Motivation: Names the route shared by both owners. */
	const FNameId ChannelNameId{"Telemetry"};
	/** Motivation: Names the message used to observe owner-specific removal. */
	const FNameId MessageNameId{"TemperatureUpdated"};
	/** Motivation: Identifies the first live owner generation. */
	const std::uint32_t FirstOwnerGeneration{31};
	/** Motivation: Identifies the second live owner generation. */
	const std::uint32_t SecondOwnerGeneration{37};
	/** Motivation: Marks both tokens as lifecycle-bound owners. */
	const bool bHasOwner{true};
	/** Motivation: States that both first-owner subscribers receive no message after bulk removal. */
	const std::size_t ExpectedFirstOwnerDeliveryCount{0};
	/** Motivation: States that the second-owner subscriber remains live for one delivery. */
	const std::size_t ExpectedSecondOwnerDeliveryCount{1};
	FDefaultMessagingSystem System;
	const FChannelInformation ChannelInformation{ChannelNameId, false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	std::uint32_t FirstOwnerGenerationCounter = FirstOwnerGeneration;
	std::uint32_t SecondOwnerGenerationCounter = SecondOwnerGeneration;
	const FWeakOwner FirstOwner{&FirstOwnerGenerationCounter, FirstOwnerGeneration, bHasOwner};
	const FWeakOwner SecondOwner{&SecondOwnerGenerationCounter, SecondOwnerGeneration, bHasOwner};
	std::size_t FirstOwnerDeliveryCount{0};
	std::size_t SecondOwnerDeliveryCount{0};
	FDefaultSubscriberDelegate FirstOwnerSubscriber;
	FDefaultSubscriberDelegate SecondFirstOwnerSubscriber;
	FDefaultSubscriberDelegate SecondOwnerSubscriber;
	const EDelegateResult FirstOwnerBindingResult =
		FirstOwnerSubscriber.Bind([&FirstOwnerDeliveryCount](const FMessage&) noexcept { ++FirstOwnerDeliveryCount; });
	const EDelegateResult SecondFirstOwnerBindingResult =
		SecondFirstOwnerSubscriber.Bind([&FirstOwnerDeliveryCount](const FMessage&) noexcept { ++FirstOwnerDeliveryCount; });
	const EDelegateResult SecondOwnerBindingResult =
		SecondOwnerSubscriber.Bind([&SecondOwnerDeliveryCount](const FMessage&) noexcept { ++SecondOwnerDeliveryCount; });
	const EMessagingResult FirstOwnerSubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(FirstOwnerSubscriber), FirstOwner);
	const EMessagingResult SecondFirstOwnerSubscribeResult =
		System.SubscribeToChannel(ChannelNameId, std::move(SecondFirstOwnerSubscriber), FirstOwner);
	const EMessagingResult SecondOwnerSubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(SecondOwnerSubscriber), SecondOwner);
	FMessage Message;
	Message.SetMessageNameId(MessageNameId);

	// Act
	System.UnsubscribeAll(FirstOwner);
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, ChannelNameId);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The owner-bulk-removal channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, FirstOwnerBindingResult, "The first-owner subscriber should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SecondFirstOwnerBindingResult, "The second first-owner subscriber should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SecondOwnerBindingResult, "The second-owner subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstOwnerSubscribeResult, "The first owner should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondFirstOwnerSubscribeResult, "The second first-owner subscription should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondOwnerSubscribeResult, "The second owner should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The owner-bulk-removal message should send");
	MW_EXPECT_EQ(Test, ExpectedFirstOwnerDeliveryCount, FirstOwnerDeliveryCount, "The removed owner should receive no message");
	MW_EXPECT_EQ(Test, ExpectedSecondOwnerDeliveryCount, SecondOwnerDeliveryCount, "The other owner should remain subscribed");
}

/**
 * Motivation: Protects standalone ownerless subscribers from a bulk removal request with no owner identity.
 * Responsibilities: Verify an ownerless token removes neither of two ownerless registrations.
 */
MW_TEST_CASE(MessagingSubscription_UnsubscribeAllOwnerlessTokenLeavesOwnerlessSubscribers)
{
	// Arrange
	/** Motivation: Names the route shared by the ownerless subscriptions. */
	const FNameId ChannelNameId{"Telemetry"};
	/** Motivation: Names the message used to observe both ownerless subscriptions. */
	const FNameId MessageNameId{"TemperatureUpdated"};
	/** Motivation: States the one delivery expected for the first ownerless subscriber. */
	const std::size_t ExpectedFirstDeliveryCount{1};
	/** Motivation: States the one delivery expected for the second ownerless subscriber. */
	const std::size_t ExpectedSecondDeliveryCount{1};
	FSmallSubscriptionMessagingSystem System;
	const FChannelInformation ChannelInformation{ChannelNameId, false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	std::size_t FirstDeliveryCount{0};
	std::size_t SecondDeliveryCount{0};
	FSmallSubscriptionMessagingSystem::FSubscriberDelegate FirstSubscriber;
	FSmallSubscriptionMessagingSystem::FSubscriberDelegate SecondSubscriber;
	const EDelegateResult FirstBindingResult = FirstSubscriber.Bind([&FirstDeliveryCount](const FMessage&) noexcept { ++FirstDeliveryCount; });
	const EDelegateResult SecondBindingResult = SecondSubscriber.Bind([&SecondDeliveryCount](const FMessage&) noexcept { ++SecondDeliveryCount; });
	const EMessagingResult FirstSubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(FirstSubscriber));
	const EMessagingResult SecondSubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(SecondSubscriber));
	FMessage Message;
	Message.SetMessageNameId(MessageNameId);

	// Act
	System.UnsubscribeAll(FWeakOwner{});
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, ChannelNameId);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The ownerless-bulk-removal channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, FirstBindingResult, "The first ownerless subscriber should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, SecondBindingResult, "The second ownerless subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstSubscribeResult, "The first ownerless subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondSubscribeResult, "The second ownerless subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The ownerless-bulk-removal message should send");
	MW_EXPECT_EQ(Test, ExpectedFirstDeliveryCount, FirstDeliveryCount, "The first ownerless subscriber should remain live");
	MW_EXPECT_EQ(Test, ExpectedSecondDeliveryCount, SecondDeliveryCount, "The second ownerless subscriber should remain live");
}

/**
 * Motivation: Returns capacity even when a dead owner belongs to a channel that remains quiet.
 * Responsibilities: Verify a full subscribe reclaims a dead owner without sending any message and records that reclamation.
 */
MW_TEST_CASE(MessagingSubscription_SubscribeReclaimsDeadOwnerWithoutSending)
{
	// Arrange
	/** Motivation: Names the only route in the one-slot system. */
	const FNameId ChannelNameId{"Telemetry"};
	/** Motivation: Represents the generation captured while the first owner was live. */
	const std::uint32_t CapturedOwnerGeneration{41};
	/** Motivation: Advances the first owner counter to represent destruction before replacement subscription. */
	const std::uint32_t OwnerDestructionIncrement{1};
	/** Motivation: Marks the first token as lifecycle-bound. */
	const bool bHasOwner{true};
	/** Motivation: States the one dead-owner reclamation expected during replacement subscription. */
	const std::uint32_t ExpectedReclaimedSubscriptionCount{1};
	FOneSubscriptionMessagingSystem System;
	const FChannelInformation ChannelInformation{ChannelNameId, false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	std::uint32_t OwnerGenerationCounter = CapturedOwnerGeneration;
	const FWeakOwner Owner{&OwnerGenerationCounter, CapturedOwnerGeneration, bHasOwner};
	FOneSubscriptionMessagingSystem::FSubscriberDelegate FirstSubscriber;
	const EDelegateResult FirstBindingResult = FirstSubscriber.Bind([](const FMessage&) noexcept {});
	const EMessagingResult FirstSubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(FirstSubscriber), Owner);
	OwnerGenerationCounter += OwnerDestructionIncrement;
	FOneSubscriptionMessagingSystem::FSubscriberDelegate ReplacementSubscriber;
	const EDelegateResult ReplacementBindingResult = ReplacementSubscriber.Bind([](const FMessage&) noexcept {});

	// Act
	const EMessagingResult ReplacementSubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(ReplacementSubscriber));

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The subscribe-reclamation channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, FirstBindingResult, "The dead-owner subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstSubscribeResult, "The first live owner should register");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, ReplacementBindingResult, "The replacement subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReplacementSubscribeResult, "A full subscribe should reclaim a dead owner");
	MW_EXPECT_EQ(
		Test,
		ExpectedReclaimedSubscriptionCount,
		System.GetReclaimedDeadOwnerSubscriptionCount(),
		"Subscribe-time dead-owner reclamation should be observable");
}

/**
 * Motivation: Preserves capacity failure when ownership checks find no dead subscriber to reclaim.
 * Responsibilities: Verify a full one-slot system with a live owner still reports Full.
 */
MW_TEST_CASE(MessagingSubscription_ReportsFullWhenEveryOwnerIsLive)
{
	// Arrange
	/** Motivation: Names the only route in the one-slot system. */
	const FNameId ChannelNameId{"Telemetry"};
	/** Motivation: Identifies the live owner generation retained by the occupied slot. */
	const std::uint32_t LiveOwnerGeneration{43};
	/** Motivation: Marks the occupied token as lifecycle-bound. */
	const bool bHasOwner{true};
	FOneSubscriptionMessagingSystem System;
	const FChannelInformation ChannelInformation{ChannelNameId, false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	std::uint32_t OwnerGenerationCounter = LiveOwnerGeneration;
	const FWeakOwner LiveOwner{&OwnerGenerationCounter, LiveOwnerGeneration, bHasOwner};
	FOneSubscriptionMessagingSystem::FSubscriberDelegate LiveOwnerSubscriber;
	FOneSubscriptionMessagingSystem::FSubscriberDelegate OverflowSubscriber;
	const EDelegateResult LiveOwnerBindingResult = LiveOwnerSubscriber.Bind([](const FMessage&) noexcept {});
	const EDelegateResult OverflowBindingResult = OverflowSubscriber.Bind([](const FMessage&) noexcept {});
	const EMessagingResult LiveOwnerSubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(LiveOwnerSubscriber), LiveOwner);

	// Act
	const EMessagingResult OverflowSubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(OverflowSubscriber));

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The live-owner-full channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, LiveOwnerBindingResult, "The live-owner subscriber should bind");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, OverflowBindingResult, "The overflow subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, LiveOwnerSubscribeResult, "The live owner should occupy the only slot");
	MW_EXPECT_EQ(Test, EMessagingResult::Full, OverflowSubscribeResult, "A live owner should not be reclaimed to make capacity");
}

} // namespace
