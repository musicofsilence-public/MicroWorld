#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Messaging/MessageReader.h>
#include <MicroWorld/Messaging/MessageWriter.h>
#include <MicroWorld/Messaging/NameId.h>
#include <MicroWorld/Messaging/MessagingSystem.h>
#include <MicroWorld/Networking/PeerId.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Networking
{

/**
 * Motivation: Carries one application message through a private Network wire channel while preserving logical sender identity.
 * Responsibilities: Hold bounded application metadata and bytes; never expose an endpoint or raw device to application code.
 * Example: FRoutedMessage Routed; Routed.Peer = Peer; Routed.PayloadSize = Message.GetPayload().Size();
 */
struct FRoutedMessage final
{
	/** Motivation: Names the fixed routed-envelope fields encoded before application bytes. */
	static constexpr std::size_t EncodedHeaderBytes = sizeof(std::uint8_t) + sizeof(std::uint32_t) + sizeof(Messaging::FNameId) * 2 + sizeof(std::uint8_t);

	/** Motivation: Bounds copied application bytes within the reliable Messaging payload budget after route-envelope fields. */
	static constexpr std::size_t MaxPayloadBytes = Messaging::FMessagingSystem::MaxReliableMessagePayloadBytes - EncodedHeaderBytes;

	/** Motivation: Names the validated live connection that sent or receives this application message. */
	FPeerId Peer{};

	/** Motivation: Selects the application-local channel without using either private Network wire channel. */
	Messaging::FNameId ChannelNameId{};

	/** Motivation: Selects the application message type delivered to local subscribers. */
	Messaging::FNameId MessageNameId{};

	/** Motivation: Owns the bounded application bytes while typed Messaging encode/decode executes synchronously. */
	std::uint8_t Payload[MaxPayloadBytes]{};

	/** Motivation: Records how many leading Payload bytes belong to this routed application message. */
	std::uint8_t PayloadSize{0};

	/**
	 * Motivation: Lets application delivery borrow exactly the encoded bytes without exposing mutable storage.
	 * Responsibilities: Return the active payload span.
	 */
	Core::TSpan<const std::uint8_t> GetPayload() const noexcept { return Core::TSpan<const std::uint8_t>(Payload, PayloadSize); }
};

/**
 * Motivation: Gives RoutedMessage a stable Messaging protocol name.
 * Responsibilities: Return its fixed name id without mutation.
 */
constexpr Messaging::FNameId GetMessageNameId(const FRoutedMessage&) noexcept { return Messaging::MakeNameId("__NetworkRoutedMessage"); }
/**
 * Motivation: Encodes a bounded routed application envelope.
 * Responsibilities: Reject invalid ids or oversized payloads before partial writes.
 */
Messaging::EMessagingResult EncodeMessagePayload(const FRoutedMessage& InMessage, Messaging::FMessageWriter& InWriter) noexcept;
/**
 * Motivation: Decodes a routed application envelope.
 * Responsibilities: Preserve OutMessage unless every bounded field and payload decode.
 */
Messaging::EMessagingResult DecodeMessagePayload(Messaging::FMessageReader& InReader, FRoutedMessage& OutMessage) noexcept;

} // namespace MicroWorld::Networking
