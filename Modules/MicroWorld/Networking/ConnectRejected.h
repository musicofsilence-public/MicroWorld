#pragma once

#include <MicroWorld/Messaging/MessageReader.h>
#include <MicroWorld/Messaging/MessageWriter.h>
#include <MicroWorld/Messaging/NameId.h>
#include <MicroWorld/Networking/ConnectionRejectReason.h>

#include <cstdint>

namespace MicroWorld::Networking
{

/**
 * Motivation: Refuses one connect attempt with a bounded reason instead of leaving a client ambiguous.
 * Responsibilities: Echo the rejected attempt and identify the server policy reason.
 * Example: FConnectRejected Rejected{Attempt, EConnectionRejectReason::Full};
 */
struct FConnectRejected final
{
	/** Motivation: Matches this refusal to the client attempt that prompted it. */
	std::uint32_t AttemptId{0};

	/** Motivation: Explains the deterministic admission-policy refusal. */
	EConnectionRejectReason Reason{EConnectionRejectReason::ProtocolMismatch};
};

/**
 * Motivation: Gives ConnectRejected a stable Messaging protocol name.
 * Responsibilities: Return its fixed name id without mutation.
 */
constexpr Messaging::FNameId GetMessageNameId(const FConnectRejected&) noexcept { return Messaging::MakeNameId("__NetworkConnectRejected"); }
/**
 * Motivation: Encodes one rejected connection.
 * Responsibilities: Reject unsupported reason values before writing partial bytes.
 */
Messaging::EMessagingResult EncodeMessagePayload(const FConnectRejected& InMessage, Messaging::FMessageWriter& InWriter) noexcept;
/**
 * Motivation: Decodes one rejected connection.
 * Responsibilities: Reject unknown reason values and preserve OutMessage on failure.
 */
Messaging::EMessagingResult DecodeMessagePayload(Messaging::FMessageReader& InReader, FConnectRejected& OutMessage) noexcept;

} // namespace MicroWorld::Networking
