#pragma once

#include <MicroWorld/Core/WeakOwner.h>
#include <MicroWorld/Messaging/ChannelInformation.h>
#include <MicroWorld/Messaging/DefaultMessagingTraits.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessagingResult.h>
#include <MicroWorld/Messaging/MessagingSystem.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace MicroWorld::Tests
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

} // namespace MicroWorld::Tests
