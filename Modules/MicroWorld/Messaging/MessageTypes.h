#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Messaging/NameId.h>

#include <cstddef>
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

/**
 * Motivation: Carries one named payload and its sender without owning or allocating payload storage.
 * Responsibilities: Retain a non-owned payload span only for its sending call or delivery call, respectively; callers
 *   own send memory, the system owns delivery memory, and subscribers copy bytes before retaining them.
 * Example:
 *   FMessage Message;
 *   Message.SetMessageNameId("Telemetry");
 */
class FMessage final
{
public:
	/**
	 * Motivation: Gives callers an empty message before they provide its fields.
	 * Responsibilities: Initialize the message name, non-owned payload view, and sender to their default values without allocation.
	 */
	FMessage() noexcept = default;

	/**
	 * Motivation: Lets bounded queues copy a message value without copying its caller-owned payload bytes.
	 * Responsibilities: Copy the message name, non-owned payload view, and sender value without allocation.
	 */
	FMessage(const FMessage&) noexcept = default;

	/**
	 * Motivation: Lets bounded queues replace a message value without copying its caller-owned payload bytes.
	 * Responsibilities: Copy the message name, non-owned payload view, and sender value without allocation.
	 */
	FMessage& operator=(const FMessage&) noexcept = default;

	/**
	 * Motivation: Associates this message with a readable protocol name through its compact id.
	 * Responsibilities: Replace only the stored message name id and return no query result.
	 */
	void SetMessageNameId(const FNameId InMessageNameId) noexcept { MessageNameId = InMessageNameId; }

	/**
	 * Motivation: Lets routing select subscribers without exposing mutable message state.
	 * Responsibilities: Return the stored message name id without changing this message.
	 */
	FNameId GetMessageNameId() const noexcept { return MessageNameId; }

	/**
	 * Motivation: Lets callers attach bounded caller-owned bytes for later Messaging processing.
	 * Responsibilities: Replace only the stored non-owned payload view and return no query result.
	 */
	void SetPayload(const Core::TSpan<const std::uint8_t> InPayload) noexcept { Payload = InPayload; }

	/**
	 * Motivation: Lets encoders and subscribers observe the message bytes without mutable access.
	 * Responsibilities: Return the stored non-owned payload view without changing this message.
	 */
	Core::TSpan<const std::uint8_t> GetPayload() const noexcept { return Payload; }

	/**
	 * Motivation: Records the route that supplied this message when it arrived from a device.
	 * Responsibilities: Replace only the stored sender address and return no query result.
	 */
	void SetSender(const Core::FDeviceAddress& InSender) noexcept { Sender = InSender; }

	/**
	 * Motivation: Lets consumers reply or inspect provenance without copying the address value; an empty sender identifies this-node origin.
	 * Responsibilities: Return a read-only reference to the stored sender address without changing this message.
	 */
	const Core::FDeviceAddress& GetSender() const noexcept { return Sender; }

private:
	/** Motivation: Selects the message kind without storing the source name string. */
	FNameId MessageNameId{};

	/** Motivation: Views non-owned application bytes without allocating or copying them. */
	Core::TSpan<const std::uint8_t> Payload{};

	/** Motivation: Retains the source route, or the empty sender that identifies this-node origin. */
	Core::FDeviceAddress Sender{};
};

/**
 * Motivation: Supplies the fixed configuration required to create one named Messaging channel.
 * Responsibilities: Describe identity, reliability, optional transport device, and destination address without owning them.
 * Example:
 *   FChannelInformation Information{ "Telemetry", false, &Device, Address };
 */
struct FChannelInformation
{
	/** Motivation: Identifies this channel independently of its device or destination, with an unset name invalid for creation. */
	FNameId ChannelNameId{};

	/** Motivation: Selects resend-until-ack behavior; false leaves the channel best-effort and local delivery still occurs. */
	bool bIsReliable{false};

	/** Motivation: Reaches the optional non-owning device used for remote delivery; null is the normal local-only channel shape. */
	Core::ITransportDevice* TransportDevice{nullptr};

	/** Motivation: Selects the device-defined destination route; zero active bytes select its default route, and multiple channels may share one
	 * device while using different addresses. */
	Core::FDeviceAddress Address{};
};

/**
 * Motivation: Supplies the bounded reliability policy shared by one Messaging system.
 * Responsibilities: Define retry timing and the maximum send attempts without owning scheduler state.
 * Example:
 *   FMessagingSystemInformation Information{};
 */
struct FMessagingSystemInformation
{
	/** Motivation: Sets the exact milliseconds between reliable resend attempts after a send remains unacknowledged. */
	Core::DurationMilliseconds ReliableRetryIntervalMilliseconds{200};

	/** Motivation: Sets the exact total attempts one reliable message may make before Messaging stops retrying it. */
	std::uint8_t MaxReliableSendAttempts{8};
};

} // namespace MicroWorld::Messaging
