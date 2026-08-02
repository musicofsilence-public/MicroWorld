#pragma once

#include <cstdint>

namespace MicroWorld::Core
{

/**
 * Motivation: Gives each non-blocking packet transport one result vocabulary that distinguishes acceptance from
 *   backpressure, invalid requests, and unavailable devices.
 * Responsibilities: Report the outcome of one whole packet operation without exposing device-specific failures.
 * Example:
 *   if (Device.TrySend(To, Packet) == ETransportResult::Full) { RetryLater(); }
 */
enum class ETransportResult : std::uint8_t
{
	/** Motivation: Reports that the whole requested operation completed. */
	Success,
	/** Motivation: Reports that the device has no capacity now and the caller may retry later. */
	Full,
	/** Motivation: Reports a malformed, oversize, or unroutable request that retrying unchanged cannot repair. */
	Invalid,
	/** Motivation: Reports that no packet is available or the device cannot be used yet. */
	Unavailable,
};

} // namespace MicroWorld::Core
