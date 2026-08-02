#pragma once

#include <cstddef>

namespace MicroWorld::Messaging
{

/**
 * Motivation: Defines the complete default fixed memory footprint for one Messaging system.
 * Responsibilities: Bound every channel, subscription, frame, and reliable-pending allocation at compile time.
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

	/** Motivation: Bounds the application bytes one complete Messaging frame may carry. */
	static constexpr std::size_t MaxMessageBytes = 96;

	/** Motivation: Bounds how many reliable messages one Messaging system may retain awaiting acknowledgement. */
	static constexpr std::size_t MaxReliablePendingMessages = 8;
};

} // namespace MicroWorld::Messaging
