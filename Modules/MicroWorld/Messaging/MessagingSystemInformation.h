#pragma once

#include <MicroWorld/Core/Time.h>

#include <cstdint>

namespace MicroWorld::Messaging
{

/**
 * Motivation: Supplies the bounded reliability and receive-work policy shared by one Messaging system.
 * Responsibilities: Define retry timing, the maximum send attempts, and the per-device receive budget without owning scheduler state.
 * Example:
 *   FMessagingSystemInformation Information{};
 */
struct FMessagingSystemInformation
{
	/** Motivation: Sets the exact milliseconds between reliable resend attempts after a send remains unacknowledged. */
	Core::DurationMilliseconds ReliableRetryIntervalMilliseconds{200};

	/** Motivation: Sets the exact total attempts one reliable message may make before Messaging stops retrying it. */
	std::uint8_t MaxReliableSendAttempts{8};

	/** Motivation: Limits how many successful wire frames one unique device may contribute during one pre-advance turn. */
	std::uint8_t MaxReceiveFramesPerDevicePerAdvance{4};
};

} // namespace MicroWorld::Messaging
