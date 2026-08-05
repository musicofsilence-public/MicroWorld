#pragma once

#include <MicroWorld/Messaging/MessageReader.h>
#include <MicroWorld/Messaging/MessageWriter.h>
#include <MicroWorld/Messaging/NameId.h>

#include <cstdint>

namespace MicroWorld::Networking
{

/**
 * Motivation: Starts one client admission attempt without assigning a stable user identity.
 * Responsibilities: Carry the protocol version and monotonically increasing client attempt id.
 * Example: Messaging.SendTypedMessageToRemoteChannel(FConnectRequest{}, Channel, Route);
 */
struct FConnectRequest final
{
	/** Motivation: Declares the schema version the client requires from the server. */
	std::uint8_t ProtocolVersion{0};

	/** Motivation: Distinguishes this request from stale accepts or rejects after reconnect. */
	std::uint32_t AttemptId{0};
};

/**
 * Motivation: Gives ConnectRequest a stable Messaging protocol name.
 * Responsibilities: Return its fixed name id without mutation.
 */
constexpr Messaging::FNameId GetMessageNameId(const FConnectRequest&) noexcept { return Messaging::MakeNameId("__NetworkConnectRequest"); }
/**
 * Motivation: Encodes a bounded connect request.
 * Responsibilities: Write its fields in protocol order or return the first writer failure.
 */
Messaging::EMessagingResult EncodeMessagePayload(const FConnectRequest& InMessage, Messaging::FMessageWriter& InWriter) noexcept;
/**
 * Motivation: Decodes a connect request transactionally.
 * Responsibilities: Assign OutMessage only when every field is readable.
 */
Messaging::EMessagingResult DecodeMessagePayload(Messaging::FMessageReader& InReader, FConnectRequest& OutMessage) noexcept;

} // namespace MicroWorld::Networking
