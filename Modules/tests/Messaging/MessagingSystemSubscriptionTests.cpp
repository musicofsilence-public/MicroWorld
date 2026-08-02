#include "TestSupport.h"
#include "MessagingSystemTestHelpers.h"

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

} // namespace
