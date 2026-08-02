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
