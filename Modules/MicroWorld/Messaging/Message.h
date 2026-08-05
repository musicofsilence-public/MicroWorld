#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Messaging/MessageSourceId.h>
#include <MicroWorld/Messaging/MessagingRoute.h>
#include <MicroWorld/Messaging/NameId.h>

namespace MicroWorld::Messaging
{

/**
 * Motivation: Carries one named payload and its sender without owning or allocating payload storage.
 * Responsibilities: Retain a non-owned payload span only for its sending call or delivery call, respectively; callers
	 *   own send memory, the
 * system owns delivery memory, and subscribers copy bytes before retaining them. Preserve optional
	 *   local-only sender route and source
 * context without serializing either into the payload.
 * Example: FMessage Message; Message.SetMessageNameId("Telemetry");
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
	 * Responsibilities: Copy the message name, non-owned payload view, sender route, and source context without allocation.
	 */
	FMessage(const FMessage&) noexcept = default;

	/**
	 * Motivation: Lets bounded queues replace a message value without copying its caller-owned payload bytes.
	 * Responsibilities: Copy the message name, non-owned payload view, sender route, and source context without allocation.
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
	 * Motivation: Records address-only sender context for compatibility with direct Messaging consumers.
	 * Responsibilities: Replace the stored
	 * sender address, clear any stale complete route, and return no query result.
	 */
	void SetSenderContext(const Core::FDeviceAddress& InSender) noexcept
	{
		Sender = InSender;
		SenderRoute = {};
	}

	/**
	 * Motivation: Lets consumers reply or inspect provenance without copying the address value; an empty sender identifies this-node origin.
	 * Responsibilities: Return a read-only reference to the stored sender address without changing this message.
	 */
	const Core::FDeviceAddress& GetSender() const noexcept { return Sender; }

	/**
	 * Motivation: Preserves the complete device registration and address that supplied one inbound frame.
	 * Responsibilities: Replace
	 * sender-route context and keep the legacy address query synchronized with its address.
	 */
	void SetSenderContext(const FMessagingRoute& InSenderRoute) noexcept
	{
		SenderRoute = InSenderRoute;
		Sender = InSenderRoute.Address;
	}

	/**
	 * Motivation: Lets routing policy validate inbound origin without recovering a device from an address alone.
	 * Responsibilities:
	 * Return the local-only sender route without mutation; an invalid link denotes no wire origin.
	 */
	const FMessagingRoute& GetSenderRoute() const noexcept { return SenderRoute; }

	/**
	 * Motivation: Lets higher layers republish accepted inbound payloads as local messages without leaking endpoint metadata.
	 *
	 * Responsibilities: Clear only the sender route and legacy sender address.
	 */
	void ClearSenderContext() noexcept
	{
		SenderRoute = {};
		Sender = {};
	}

	/**
	 * Motivation: Lets a validating higher layer stamp an opaque live-session source for synchronous application delivery.
	 *
	 * Responsibilities: Replace only the local-only source value and never serialize it.
	 */
	void SetSourceId(const FMessageSourceId InSourceId) noexcept { SourceId = InSourceId; }

	/**
	 * Motivation: Lets consumers recover a higher-layer source only when a validator explicitly supplied it.
	 * Responsibilities: Return
	 * the opaque source value without mutation.
	 */
	FMessageSourceId GetSourceId() const noexcept { return SourceId; }

private:
	/** Motivation: Selects the message kind without storing the source name string. */
	FNameId MessageNameId{};

	/** Motivation: Views non-owned application bytes without allocating or copying them. */
	Core::TSpan<const std::uint8_t> Payload{};

	/** Motivation: Retains the source route, or the empty sender that identifies this-node origin. */
	Core::FDeviceAddress Sender{};

	/** Motivation: Retains the complete inbound device route, or an invalid route for local application messages. */
	FMessagingRoute SenderRoute{};

	/** Motivation: Retains optional local-only higher-layer sender context, never application wire data. */
	FMessageSourceId SourceId{};
};

} // namespace MicroWorld::Messaging
