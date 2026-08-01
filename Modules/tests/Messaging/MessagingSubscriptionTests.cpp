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

} // namespace
