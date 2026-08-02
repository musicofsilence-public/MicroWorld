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

} // namespace
