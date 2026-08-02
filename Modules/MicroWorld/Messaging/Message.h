#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Messaging/NameId.h>

namespace MicroWorld::Messaging
{

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

} // namespace MicroWorld::Messaging
