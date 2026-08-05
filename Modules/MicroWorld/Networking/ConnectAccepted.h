#pragma once

#include <MicroWorld/Messaging/MessageReader.h>
#include <MicroWorld/Messaging/MessageWriter.h>
#include <MicroWorld/Messaging/NameId.h>
#include <MicroWorld/Networking/PeerId.h>

#include <cstdint>

namespace MicroWorld::Networking
{

/**
 * Motivation: Confirms one current client attempt and gives both ends the same live connection handle.
 * Responsibilities: Echo the attempt id and carry the server-assigned generation-checked peer id.
 * Example: FConnectAccepted Accepted{Attempt, Peer};
 */
struct FConnectAccepted final
{
	/** Motivation: Matches this acceptance to the client request still awaiting admission. */
	std::uint32_t AttemptId{0};

	/** Motivation: Names the connection on both sides until disconnect or generation change. */
	FPeerId Peer{};
};

/**
 * Motivation: Gives ConnectAccepted a stable Messaging protocol name.
 * Responsibilities: Return its fixed name id without mutation.
 */
constexpr Messaging::FNameId GetMessageNameId(const FConnectAccepted&) noexcept { return Messaging::MakeNameId("__NetworkConnectAccepted"); }
/**
 * Motivation: Encodes one accepted connection.
 * Responsibilities: Write attempt and peer identity without partial success.
 */
Messaging::EMessagingResult EncodeMessagePayload(const FConnectAccepted& InMessage, Messaging::FMessageWriter& InWriter) noexcept;
/**
 * Motivation: Decodes one accepted connection.
 * Responsibilities: Assign only a fully decoded candidate.
 */
Messaging::EMessagingResult DecodeMessagePayload(Messaging::FMessageReader& InReader, FConnectAccepted& OutMessage) noexcept;

} // namespace MicroWorld::Networking
