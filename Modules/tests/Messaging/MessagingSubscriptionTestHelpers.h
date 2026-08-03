#pragma once

#include "MessagingSystemTestHelpers.h"

#include <MicroWorld/Core/WeakOwner.h>
#include <MicroWorld/Messaging/ChannelInformation.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace MicroWorld::Tests
{

using MicroWorld::Core::FWeakOwner;
using MicroWorld::Messaging::FChannelInformation;
using MicroWorld::Messaging::FNameId;

/**
 * Motivation: Keeps concrete-capacity test setup concise while exercising only the public subscription API.
 * Responsibilities: On a live channel with no prior subscriptions, bind and register InCount no-op subscribers and return the first failure or
 * Success.
 */
inline EMessagingResult FillSubscriptionSlots(
	FMessagingSystem& InSystem, const FNameId InChannelNameId, const std::size_t InCount, const FWeakOwner InOwner = {}) noexcept
{
	for (std::size_t SubscriptionIndex = 0; SubscriptionIndex < InCount; ++SubscriptionIndex)
	{
		FSubscriberDelegate Subscriber;
		const EDelegateResult BindingResult = Subscriber.Bind([](const FMessage&) noexcept {});
		if (BindingResult != EDelegateResult::Success)
		{
			return EMessagingResult::Invalid;
		}

		const EMessagingResult SubscribeResult = InSystem.SubscribeToChannel(InChannelNameId, std::move(Subscriber), InOwner);
		if (SubscribeResult != EMessagingResult::Success)
		{
			return SubscribeResult;
		}
	}

	return EMessagingResult::Success;
}

} // namespace MicroWorld::Tests
