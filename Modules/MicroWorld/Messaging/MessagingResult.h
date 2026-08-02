#pragma once

#include <cstdint>

namespace MicroWorld::Messaging
{

/**
 * Motivation: Gives Messaging one bounded result vocabulary for channel and message operations.
 * Responsibilities: Distinguish successful work from duplicate, missing, capacity, and malformed requests.
 * Example:
 *   if (Result == EMessagingResult::Full) { RetryLater(); }
 */
enum class EMessagingResult : std::uint8_t
{
	/** Motivation: Reports that the requested Messaging operation completed with its promised state change. */
	Success,
	/** Motivation: Reports a duplicate channel name while leaving the existing channels and the requested creation state untouched. */
	Duplicate,
	/** Motivation: Reports that the requested name id does not identify an existing channel or subscription. */
	NotFound,
	/** Motivation: Reports that fixed channel, subscription, queue, or reliable-send capacity is exhausted and state is unchanged. */
	Full,
	/** Motivation: Reports an unset or oversize request whose unchanged retry cannot succeed and leaves state unchanged. */
	Invalid,
};

} // namespace MicroWorld::Messaging
