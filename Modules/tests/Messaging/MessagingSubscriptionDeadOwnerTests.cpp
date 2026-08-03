#include "TestSupport.h"
#include "MessagingSubscriptionTestHelpers.h"

#include <MicroWorld/Core/WeakOwner.h>
#include <MicroWorld/Messaging/Message.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{

using namespace ::MicroWorld::Tests;

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
	FMessagingSystem System;
	const FChannelInformation ChannelInformation{ChannelNameId, false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	std::uint32_t OwnerGenerationCounter = CapturedOwnerGeneration;
	OwnerGenerationCounter += OwnerDestructionIncrement;
	const FWeakOwner DeadOwner{&OwnerGenerationCounter, CapturedOwnerGeneration, bHasOwner};
	std::size_t DeliveryCount{0};
	FSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	FMessagingSystem::FSubscriptionHandle Handle{FMessagingSystem::FSubscriptionHandle::InvalidIndex, InitialHandleGeneration};
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
	FMessagingSystem System;
	const FChannelInformation ChannelInformation{ChannelNameId, false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	std::uint32_t OwnerGenerationCounter = CapturedOwnerGeneration;
	const FWeakOwner Owner{&OwnerGenerationCounter, CapturedOwnerGeneration, bHasOwner};
	std::size_t DeliveryCount{0};
	FSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	const EMessagingResult SubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(Subscriber), Owner);
	OwnerGenerationCounter += OwnerDestructionIncrement;
	FMessage Message;
	Message.SetMessageNameId(MessageNameId);

	// Act
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, ChannelNameId);
	const std::uint32_t ReclaimedSubscriptionCount = System.GetReclaimedDeadOwnerSubscriptionCount();

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The reclamation channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The dead-owner subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The owner should register while it is live");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The reclamation message should send");
	MW_EXPECT_EQ(Test, ExpectedDeliveryCount, DeliveryCount, "A dead owner should not receive delivery");
	MW_EXPECT_EQ(Test, ExpectedReclaimedSubscriptionCount, ReclaimedSubscriptionCount, "The dead owner should be reclaimed once");
}

/**
 * Motivation: Returns bounded subscription capacity after delivery discovers an owner is dead.
 * Responsibilities: Fill concrete capacity including one dead owner, then verify delivery reclaims its slot for a replacement subscriber.
 */
MW_TEST_CASE(MessagingSubscription_ReclaimedDeadOwnerSlotIsReusable)
{
	// Arrange
	/** Motivation: Names the local route whose concrete subscription capacity is filled. */
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
	FMessagingSystem System;
	const FChannelInformation ChannelInformation{ChannelNameId, false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	std::uint32_t OwnerGenerationCounter = CapturedOwnerGeneration;
	const FWeakOwner Owner{&OwnerGenerationCounter, CapturedOwnerGeneration, bHasOwner};
	const EMessagingResult LiveFillResult = FillSubscriptionSlots(System, ChannelNameId, FMessagingSystem::MaxSubscriptions - 1);
	FSubscriberDelegate DeadOwnerSubscriber;
	const EDelegateResult DeadOwnerBindingResult = DeadOwnerSubscriber.Bind([](const FMessage&) noexcept {});
	const EMessagingResult DeadOwnerSubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(DeadOwnerSubscriber), Owner);
	OwnerGenerationCounter += OwnerDestructionIncrement;
	FMessage Message;
	Message.SetMessageNameId(MessageNameId);
	const EMessagingResult ReclaimingSendResult = System.SendMessageToChannel(Message, ChannelNameId);
	std::size_t ReplacementDeliveryCount{0};
	FSubscriberDelegate ReplacementSubscriber;
	const EDelegateResult ReplacementBindingResult =
		ReplacementSubscriber.Bind([&ReplacementDeliveryCount](const FMessage&) noexcept { ++ReplacementDeliveryCount; });

	// Act
	const EMessagingResult ReplacementSubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(ReplacementSubscriber));
	const EMessagingResult ReplacementSendResult = System.SendMessageToChannel(Message, ChannelNameId);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The concrete-capacity channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, LiveFillResult, "All remaining concrete subscription slots should be occupied");
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
	FMessagingSystem System;
	const FChannelInformation QuietChannelInformation{QuietChannelNameId, false, nullptr, {}};
	const FChannelInformation BusyChannelInformation{BusyChannelNameId, false, nullptr, {}};
	const EMessagingResult QuietCreateResult = System.CreateChannel(QuietChannelInformation);
	const EMessagingResult BusyCreateResult = System.CreateChannel(BusyChannelInformation);
	std::uint32_t OwnerGenerationCounter = CapturedOwnerGeneration;
	const FWeakOwner Owner{&OwnerGenerationCounter, CapturedOwnerGeneration, bHasOwner};
	FSubscriberDelegate QuietSubscriber;
	const EDelegateResult BindingResult = QuietSubscriber.Bind([](const FMessage&) noexcept {});
	const EMessagingResult SubscribeResult = System.SubscribeToChannel(QuietChannelNameId, std::move(QuietSubscriber), Owner);
	OwnerGenerationCounter += OwnerDestructionIncrement;
	FMessage Message;
	Message.SetMessageNameId(MessageNameId);

	// Act
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, BusyChannelNameId);
	const std::uint32_t ReclaimedSubscriptionCount = System.GetReclaimedDeadOwnerSubscriptionCount();

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, QuietCreateResult, "The quiet channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, BusyCreateResult, "The busy channel should be created");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The quiet subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The quiet owner should register while live");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The busy message should send");
	MW_EXPECT_EQ(Test, ExpectedReclaimedSubscriptionCount, ReclaimedSubscriptionCount, "A busy channel should reclaim a dead quiet-channel owner");
}

/**
 * Motivation: Returns capacity even when a dead owner belongs to a channel that remains quiet.
 * Responsibilities: Fill concrete capacity including one dead owner, then verify a replacement subscribe reclaims it without sending.
 */
MW_TEST_CASE(MessagingSubscription_SubscribeReclaimsDeadOwnerWithoutSending)
{
	// Arrange
	/** Motivation: Names the local route whose concrete subscription capacity is filled. */
	const FNameId ChannelNameId{"Telemetry"};
	/** Motivation: Represents the generation captured while the first owner was live. */
	const std::uint32_t CapturedOwnerGeneration{41};
	/** Motivation: Advances the first owner counter to represent destruction before replacement subscription. */
	const std::uint32_t OwnerDestructionIncrement{1};
	/** Motivation: Marks the first token as lifecycle-bound. */
	const bool bHasOwner{true};
	/** Motivation: States the one dead-owner reclamation expected during replacement subscription. */
	const std::uint32_t ExpectedReclaimedSubscriptionCount{1};
	FMessagingSystem System;
	const FChannelInformation ChannelInformation{ChannelNameId, false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	std::uint32_t OwnerGenerationCounter = CapturedOwnerGeneration;
	const FWeakOwner Owner{&OwnerGenerationCounter, CapturedOwnerGeneration, bHasOwner};
	const EMessagingResult LiveFillResult = FillSubscriptionSlots(System, ChannelNameId, FMessagingSystem::MaxSubscriptions - 1);
	FSubscriberDelegate FirstSubscriber;
	const EDelegateResult FirstBindingResult = FirstSubscriber.Bind([](const FMessage&) noexcept {});
	const EMessagingResult FirstSubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(FirstSubscriber), Owner);
	OwnerGenerationCounter += OwnerDestructionIncrement;
	FSubscriberDelegate ReplacementSubscriber;
	const EDelegateResult ReplacementBindingResult = ReplacementSubscriber.Bind([](const FMessage&) noexcept {});

	// Act
	const EMessagingResult ReplacementSubscribeResult = System.SubscribeToChannel(ChannelNameId, std::move(ReplacementSubscriber));
	const std::uint32_t ReclaimedSubscriptionCount = System.GetReclaimedDeadOwnerSubscriptionCount();

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The subscribe-reclamation channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, LiveFillResult, "All remaining concrete subscription slots should be occupied");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, FirstBindingResult, "The dead-owner subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstSubscribeResult, "The first live owner should register");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, ReplacementBindingResult, "The replacement subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ReplacementSubscribeResult, "A full subscribe should reclaim a dead owner");
	MW_EXPECT_EQ(Test, ExpectedReclaimedSubscriptionCount, ReclaimedSubscriptionCount, "Subscribe-time dead-owner reclamation should be observable");
}

} // namespace
