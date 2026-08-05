#pragma once

#include <MicroWorld/Messaging/MessageReader.h>
#include <MicroWorld/Messaging/MessageWriter.h>
#include <MicroWorld/Messaging/NameId.h>
#include <MicroWorld/Networking/PeerId.h>

#include <cstdint>

namespace MicroWorld::Networking
{

/**
 * Motivation: Keeps a live session observable without a hidden clock or raw transport access.
 * Responsibilities: Carry the assigned peer id and active attempt id for route/session validation.
 * Example: FHeartbeat Heartbeat{Peer, Attempt};
 */
struct FHeartbeat final
{
	/** Motivation: Names the session whose liveness this message refreshes. */
	FPeerId Peer{};

	/** Motivation: Prevents traffic from an older connect attempt refreshing a new session. */
	std::uint32_t AttemptId{0};
};

/**
 * Motivation: Gives Heartbeat a stable Messaging protocol name.
 * Responsibilities: Return its fixed name id without mutation.
 */
constexpr Messaging::FNameId GetMessageNameId(const FHeartbeat&) noexcept { return Messaging::MakeNameId("__NetworkHeartbeat"); }
/**
 * Motivation: Encodes one heartbeat.
 * Responsibilities: Write its live session identity in protocol order.
 */
Messaging::EMessagingResult EncodeMessagePayload(const FHeartbeat& InMessage, Messaging::FMessageWriter& InWriter) noexcept;
/**
 * Motivation: Decodes one heartbeat.
 * Responsibilities: Assign OutMessage only after all fields decode.
 */
Messaging::EMessagingResult DecodeMessagePayload(Messaging::FMessageReader& InReader, FHeartbeat& OutMessage) noexcept;

} // namespace MicroWorld::Networking
