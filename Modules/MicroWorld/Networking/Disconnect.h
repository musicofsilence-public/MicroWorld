#pragma once

#include <MicroWorld/Messaging/MessageReader.h>
#include <MicroWorld/Messaging/MessageWriter.h>
#include <MicroWorld/Messaging/NameId.h>
#include <MicroWorld/Networking/DisconnectReason.h>
#include <MicroWorld/Networking/PeerId.h>

#include <cstdint>

namespace MicroWorld::Networking
{

/**
 * Motivation: Ends one current session explicitly so both endpoints invalidate the same peer handle.
 * Responsibilities: Carry peer identity, attempt id, and a bounded disconnect reason.
 * Example: FDisconnect Disconnect{Peer, Attempt, EDisconnectReason::Requested};
 */
struct FDisconnect final
{
	/** Motivation: Names the connection being retired. */
	FPeerId Peer{};

	/** Motivation: Rejects a stale disconnect from an earlier connect attempt. */
	std::uint32_t AttemptId{0};

	/** Motivation: Preserves the observable close cause for listeners. */
	EDisconnectReason Reason{EDisconnectReason::Requested};
};

/**
 * Motivation: Gives Disconnect a stable Messaging protocol name.
 * Responsibilities: Return its fixed name id without mutation.
 */
constexpr Messaging::FNameId GetMessageNameId(const FDisconnect&) noexcept { return Messaging::MakeNameId("__NetworkDisconnect"); }
/**
 * Motivation: Encodes one disconnect.
 * Responsibilities: Reject unsupported reason values before writing partial bytes.
 */
Messaging::EMessagingResult EncodeMessagePayload(const FDisconnect& InMessage, Messaging::FMessageWriter& InWriter) noexcept;
/**
 * Motivation: Decodes one disconnect.
 * Responsibilities: Preserve OutMessage unless the full payload and reason are valid.
 */
Messaging::EMessagingResult DecodeMessagePayload(Messaging::FMessageReader& InReader, FDisconnect& OutMessage) noexcept;

} // namespace MicroWorld::Networking
