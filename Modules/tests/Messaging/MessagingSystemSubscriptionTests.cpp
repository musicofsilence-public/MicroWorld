#include "TestSupport.h"
#include "MessagingSubscriptionTestHelpers.h"

#include <MicroWorld/Messaging/ChannelInformation.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessagingResult.h>

#include <cstddef>
#include <utility>

namespace
{

using namespace ::MicroWorld::Tests;

using MicroWorld::Messaging::FChannelInformation;

/**
 * Motivation: Makes a local-only channel useful by allowing one bound subscriber to register successfully.
 * Responsibilities: Verify a valid subscriber on an existing channel reports Success.
 */
MW_TEST_CASE(MessagingSystem_SubscribesToAnExistingChannel)
{
	// Arrange
	FMessagingSystem System;
	const FChannelInformation ChannelInformation{"Telemetry", false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	FSubscriberDelegate Subscriber;
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
	FMessagingSystem System;
	FSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([](const FMessage&) noexcept {});

	// Act
	const EMessagingResult SubscribeResult = System.SubscribeToChannel("Missing", std::move(Subscriber));

	// Assert
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The unknown-channel subscriber should bind within inline storage");
	MW_EXPECT_EQ(Test, EMessagingResult::NotFound, SubscribeResult, "A subscription should reject a channel that was never created");
}

/**
 * Motivation: Makes the concrete system-wide subscriber capacity boundary visible to callers.
 * Responsibilities: Fill every concrete subscription slot, then verify one additional bound subscriber reports Full.
 */
MW_TEST_CASE(MessagingSystem_RejectsSubscriptionPastCapacity)
{
	// Arrange
	FMessagingSystem System;
	const FChannelInformation ChannelInformation{"Telemetry", false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	const EMessagingResult FillResult = FillSubscriptionSlots(System, "Telemetry", FMessagingSystem::MaxSubscriptions);
	FSubscriberDelegate OverflowSubscriber;
	const EDelegateResult OverflowBindingResult = OverflowSubscriber.Bind([](const FMessage&) noexcept {});

	// Act
	const EMessagingResult OverflowSubscribeResult = System.SubscribeToChannel("Telemetry", std::move(OverflowSubscriber));

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The capacity-test channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FillResult, "Every concrete subscription slot should be filled");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, OverflowBindingResult, "The overflow subscriber should bind within inline storage");
	MW_EXPECT_EQ(Test, EMessagingResult::Full, OverflowSubscribeResult, "A subscriber beyond concrete capacity should be rejected");
}

/**
 * Motivation: Prevents inert callback storage from becoming a subscription that appears valid but cannot receive delivery.
 * Responsibilities: Verify an unbound delegate reports Invalid without requiring a send.
 */
MW_TEST_CASE(MessagingSystem_RejectsAnUnboundSubscriber)
{
	// Arrange
	FMessagingSystem System;
	const FChannelInformation ChannelInformation{"Telemetry", false, nullptr, {}};
	const EMessagingResult CreateResult = System.CreateChannel(ChannelInformation);
	FSubscriberDelegate Subscriber;

	// Act
	const EMessagingResult SubscribeResult = System.SubscribeToChannel("Telemetry", std::move(Subscriber));

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The test channel should be created before subscription");
	MW_EXPECT_EQ(Test, EMessagingResult::Invalid, SubscribeResult, "An unbound delegate should not become a subscriber");
}

} // namespace
