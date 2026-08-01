#pragma once

#include <MicroWorld/Core/Containers/StaticVector.h>
#include <MicroWorld/Core/Delegates/Delegate.h>
#include <MicroWorld/Core/PlaySystem.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Messaging/MessageTypes.h>
#include <MicroWorld/Messaging/NameId.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace MicroWorld::Messaging
{

/**
 * Motivation: Defines the complete default fixed memory footprint for one Messaging system.
 * Responsibilities: Bound every channel, subscription, queue, message, and reliable-pending allocation at compile time.
 * Example:
 *   FMessagingSystem System;
 */
struct FDefaultMessagingTraits
{
	/** Motivation: Bounds how many named channels one Messaging system may store. */
	static constexpr std::size_t MaxChannels = 4;

	/** Motivation: Bounds how many subscriber registrations one Messaging system may store. */
	static constexpr std::size_t MaxSubscriptions = 16;

	/** Motivation: Bounds the inline storage one subscriber callable may occupy before Messaging rejects it without allocating. */
	static constexpr std::size_t MaxSubscriberCallableBytes = 32;

	/** Motivation: Bounds how many outbound messages one Messaging system may queue. */
	static constexpr std::size_t MaxQueuedMessages = 8;

	/** Motivation: Bounds the bytes one Messaging system may retain for each queued message. */
	static constexpr std::size_t MaxMessageBytes = 96;

	/** Motivation: Bounds how many reliable messages one Messaging system may retain awaiting acknowledgement. */
	static constexpr std::size_t MaxReliablePendingMessages = 8;
};

/**
 * Motivation: Owns the bounded set of named channels that future Messaging operations will use.
 * Responsibilities: Create valid unique channels without allocating and participate in the caller-driven play lifecycle.
 * Example:
 *   TMessagingSystem<> System;
 *   System.CreateChannel({"Telemetry", false, nullptr, {}});
 */
template<typename TTraits = FDefaultMessagingTraits>
class TMessagingSystem final : public Core::IPlaySystem
{
public:
	/**
	 * Motivation: Gives channel subscribers one bounded callable type that keeps local delivery allocation-free.
	 * Responsibilities: Receive a message by const reference and treat its payload span as valid only for the duration of the call; subscribers that
	 * retain bytes must copy them.
	 */
	using FSubscriberDelegate = Core::TDelegate<void(const FMessage&), TTraits::MaxSubscriberCallableBytes>;

	/**
	 * Motivation: Gives callers an empty Messaging system when the default reliability policy is sufficient.
	 * Responsibilities: Initialize no live channels and retain default system information without allocation.
	 */
	TMessagingSystem() noexcept = default;

	/**
	 * Motivation: Lets a composition root configure reliability policy before it creates channels.
	 * Responsibilities: Retain the supplied system information and initialize no live channels without allocation.
	 */
	explicit TMessagingSystem(const FMessagingSystemInformation& InInformation) noexcept : Information(InInformation) {}

	/**
	 * Motivation: Prevents copying a system whose future channel references must remain stable.
	 * Responsibilities: Reject copy construction because the engine owns this object in place.
	 */
	TMessagingSystem(const TMessagingSystem&) = delete;

	/**
	 * Motivation: Prevents assignment from replacing a system whose future channel references must remain stable.
	 * Responsibilities: Reject copy assignment because the engine owns this object in place.
	 */
	TMessagingSystem& operator=(const TMessagingSystem&) = delete;

	/**
	 * Motivation: Prevents relocation of a system whose future channel references must remain stable.
	 * Responsibilities: Reject move construction because the engine owns this object in place.
	 */
	TMessagingSystem(TMessagingSystem&&) = delete;

	/**
	 * Motivation: Prevents relocation through assignment of a system whose future channel references must remain stable.
	 * Responsibilities: Reject move assignment because the engine owns this object in place.
	 */
	TMessagingSystem& operator=(TMessagingSystem&&) = delete;

	/**
	 * Motivation: Lets later messaging operations read their shared reliability policy without mutating the system.
	 * Responsibilities: Return the stored policy by read-only reference.
	 */
	const FMessagingSystemInformation& GetInformation() const noexcept { return Information; }

	/**
	 * Motivation: Gives callers one explicit, bounded operation for adding a named Messaging channel.
	 * Responsibilities: Reject unset names, preserve existing channels on duplicates or capacity exhaustion, and store each valid unique channel.
	 */
	EMessagingResult CreateChannel(const FChannelInformation& InChannelInformation) noexcept
	{
		if (InChannelInformation.ChannelNameId == InvalidNameId)
		{
			return EMessagingResult::Invalid;
		}

		if (IsChannelNameInUse(InChannelInformation.ChannelNameId))
		{
			return EMessagingResult::Duplicate;
		}

		// Capacity exhaustion is the only way Add fails, so its result is the whole capacity rule.
		return Channels.Add(InChannelInformation) == Core::ERuntimeResult::Success ? EMessagingResult::Success : EMessagingResult::Full;
	}

	/**
	 * Motivation: Lets a caller receive every message sent locally through one existing channel.
	 * Responsibilities: Store one bound subscriber with no message-name filter, or report the first applicable validation or capacity failure without
	 * changing state.
	 */
	EMessagingResult SubscribeToChannel(const FNameId InChannelNameId, FSubscriberDelegate&& InSubscriber) noexcept
	{
		return SubscribeToChannel(InChannelNameId, InvalidNameId, std::move(InSubscriber));
	}

	/**
	 * Motivation: Lets a caller receive only one named kind of message sent locally through one existing channel.
	 * Responsibilities: Validate the channel and bound subscriber before storing a filtered registration within the fixed system-wide subscription
	 * capacity.
	 */
	EMessagingResult SubscribeToChannel(const FNameId InChannelNameId, const FNameId InMessageNameFilter, FSubscriberDelegate&& InSubscriber) noexcept
	{
		if (!IsChannelNameInUse(InChannelNameId))
		{
			return EMessagingResult::NotFound;
		}

		if (!InSubscriber.IsBound())
		{
			return EMessagingResult::Invalid;
		}

		// Capacity exhaustion is the only way Emplace fails, so its result is the whole capacity rule.
		return Subscriptions.Emplace(InChannelNameId, InMessageNameFilter, std::move(InSubscriber)) == Core::ERuntimeResult::Success
			? EMessagingResult::Success
			: EMessagingResult::Full;
	}

	/**
	 * Motivation: Makes locally composed Messaging useful before any transport device exists or adds remote reach.
	 * Responsibilities: Reject an unset message name or missing channel, then synchronously deliver the message once to every matching subscriber in
	 * registration order; capture the subscription count before dispatch so a callback can add a subscriber without changing this in-flight
	 * iteration.
	 */
	EMessagingResult SendMessageToChannel(const FMessage& InMessage, const FNameId InChannelNameId) noexcept
	{
		if (InMessage.GetMessageNameId() == InvalidNameId)
		{
			return EMessagingResult::Invalid;
		}

		if (!IsChannelNameInUse(InChannelNameId))
		{
			return EMessagingResult::NotFound;
		}

		// The captured count excludes registrations added by a subscriber during this synchronous delivery.
		const std::size_t SubscriptionCountAtDispatchStart = Subscriptions.Size();
		for (std::size_t SubscriptionIndex = 0; SubscriptionIndex < SubscriptionCountAtDispatchStart; ++SubscriptionIndex)
		{
			FSubscription& Subscription = Subscriptions[SubscriptionIndex];
			if (Subscription.ChannelNameId != InChannelNameId
				|| (Subscription.MessageNameFilter != InvalidNameId && Subscription.MessageNameFilter != InMessage.GetMessageNameId()))
			{
				continue;
			}

			(void)Subscription.Subscriber.Execute(InMessage);
		}

		return EMessagingResult::Success;
	}

	/**
	 * Motivation: Reserves the inbound Messaging lifecycle turn required before world advancement.
	 * Responsibilities: Perform no work until later tasks add device pumping.
	 */
	void PreAdvance(Core::TimePointMilliseconds InNowMilliseconds) noexcept override { (void)InNowMilliseconds; }

	/**
	 * Motivation: Reserves the outbound Messaging lifecycle turn required after world advancement.
	 * Responsibilities: Perform no work until later tasks add queue flushing.
	 */
	void PostAdvance(Core::TimePointMilliseconds InNowMilliseconds) noexcept override { (void)InNowMilliseconds; }

private:
	/**
	 * Motivation: Keeps each local subscriber's routing criteria and its bounded callable together in flat system-owned storage.
	 * Responsibilities: Retain one channel identity, optional message-name filter, and uniquely owned callable without allocating.
	 * Example:
	 *   FSubscription Subscription{"Telemetry", InvalidNameId, std::move(Subscriber)};
	 */
	struct FSubscription final
	{
		/**
		 * Motivation: Lets the system publish one fully configured local subscription in a single fixed-storage insertion.
		 * Responsibilities: Retain the supplied channel, optional filter, and uniquely owned bound callable without allocating.
		 */
		FSubscription(const FNameId InChannelNameId, const FNameId InMessageNameFilter, FSubscriberDelegate&& InSubscriber) noexcept
			: ChannelNameId(InChannelNameId), MessageNameFilter(InMessageNameFilter), Subscriber(std::move(InSubscriber))
		{
		}

		/** Motivation: Identifies the channel whose local sends can reach this subscriber. */
		FNameId ChannelNameId{};

		/** Motivation: Narrows delivery to one message name, with the unset id accepting every message on the channel. */
		FNameId MessageNameFilter{};

		/** Motivation: Owns the inline callable that observes matching local messages. */
		FSubscriberDelegate Subscriber{};
	};

	/**
	 * Motivation: Lets channel creation reject an existing name before it changes bounded channel storage.
	 * Responsibilities: Report whether a live channel already has InChannelNameId without mutating any channel.
	 */
	bool IsChannelNameInUse(const FNameId InChannelNameId) const noexcept
	{
		for (const FChannelInformation& ChannelInformation : Channels)
		{
			if (ChannelInformation.ChannelNameId == InChannelNameId)
			{
				return true;
			}
		}

		return false;
	}

	/** Motivation: Retains the reliability policy future Messaging work must consult without a global configuration. */
	FMessagingSystemInformation Information{};

	/** Motivation: Owns each live channel's immutable creation information within the compile-time channel limit. */
	Core::TStaticVector<FChannelInformation, TTraits::MaxChannels> Channels;

	/** Motivation: Owns every local subscription in registration order within one system-wide fixed capacity. */
	Core::TStaticVector<FSubscription, TTraits::MaxSubscriptions> Subscriptions;
};

/** Motivation: Names the default fixed-capacity Messaging system used by engine-facing code. */
using FMessagingSystem = TMessagingSystem<>;

} // namespace MicroWorld::Messaging
