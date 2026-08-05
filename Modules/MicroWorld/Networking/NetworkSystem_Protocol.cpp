#include <MicroWorld/Networking/ConnectAccepted.h>
#include <MicroWorld/Networking/ConnectRejected.h>
#include <MicroWorld/Networking/ConnectRequest.h>
#include <MicroWorld/Networking/Disconnect.h>
#include <MicroWorld/Networking/Heartbeat.h>
#include <MicroWorld/Networking/RoutedMessage.h>

namespace MicroWorld::Networking
{
namespace
{

/**
 * Motivation: Writes a peer in one shared protocol order.
 * Responsibilities: Reject invalid peers before writing a partial peer identity.
 */
Messaging::EMessagingResult WritePeerId(const FPeerId InPeer, Messaging::FMessageWriter& InWriter) noexcept
{
	if (!InPeer.IsValid())
	{
		return Messaging::EMessagingResult::Invalid;
	}
	if (InWriter.WriteU8(InPeer.Index) != Messaging::EMessagingResult::Success)
	{
		return Messaging::EMessagingResult::Full;
	}
	return InWriter.WriteU32(InPeer.Generation);
}

/**
 * Motivation: Decodes a peer through a local candidate shared by all schemas.
 * Responsibilities: Reject invalid index values without altering output.
 */
Messaging::EMessagingResult ReadPeerId(Messaging::FMessageReader& InReader, FPeerId& OutPeer) noexcept
{
	FPeerId Candidate{};
	if (InReader.ReadU8(Candidate.Index) != Messaging::EMessagingResult::Success
		|| InReader.ReadU32(Candidate.Generation) != Messaging::EMessagingResult::Success || !Candidate.IsValid())
	{
		return Messaging::EMessagingResult::Invalid;
	}
	OutPeer = Candidate;
	return Messaging::EMessagingResult::Success;
}

/**
 * Motivation: Keeps enum decoding closed to current protocol values.
 * Responsibilities: Return false for any unsupported wire enum.
 */
bool IsValidRejectReason(const std::uint8_t InValue) noexcept
{
	return InValue == static_cast<std::uint8_t>(EConnectionRejectReason::ProtocolMismatch)
		|| InValue == static_cast<std::uint8_t>(EConnectionRejectReason::Full);
}

/**
 * Motivation: Keeps disconnect decoding closed to current protocol values.
 * Responsibilities: Return false for any unsupported wire enum.
 */
bool IsValidDisconnectReason(const std::uint8_t InValue) noexcept
{
	return InValue == static_cast<std::uint8_t>(EDisconnectReason::Requested) || InValue == static_cast<std::uint8_t>(EDisconnectReason::Timeout);
}

} // namespace

Messaging::EMessagingResult EncodeMessagePayload(const FConnectRequest& InMessage, Messaging::FMessageWriter& InWriter) noexcept
{
	if (InWriter.WriteU8(InMessage.ProtocolVersion) != Messaging::EMessagingResult::Success)
	{
		return Messaging::EMessagingResult::Full;
	}
	return InWriter.WriteU32(InMessage.AttemptId);
}

Messaging::EMessagingResult DecodeMessagePayload(Messaging::FMessageReader& InReader, FConnectRequest& OutMessage) noexcept
{
	FConnectRequest Candidate{};
	if (InReader.ReadU8(Candidate.ProtocolVersion) != Messaging::EMessagingResult::Success
		|| InReader.ReadU32(Candidate.AttemptId) != Messaging::EMessagingResult::Success)
	{
		return Messaging::EMessagingResult::Invalid;
	}
	OutMessage = Candidate;
	return Messaging::EMessagingResult::Success;
}

Messaging::EMessagingResult EncodeMessagePayload(const FConnectAccepted& InMessage, Messaging::FMessageWriter& InWriter) noexcept
{
	if (InWriter.WriteU32(InMessage.AttemptId) != Messaging::EMessagingResult::Success)
	{
		return Messaging::EMessagingResult::Full;
	}
	return WritePeerId(InMessage.Peer, InWriter);
}

Messaging::EMessagingResult DecodeMessagePayload(Messaging::FMessageReader& InReader, FConnectAccepted& OutMessage) noexcept
{
	FConnectAccepted Candidate{};
	if (InReader.ReadU32(Candidate.AttemptId) != Messaging::EMessagingResult::Success || ReadPeerId(InReader, Candidate.Peer) != Messaging::EMessagingResult::Success)
	{
		return Messaging::EMessagingResult::Invalid;
	}
	OutMessage = Candidate;
	return Messaging::EMessagingResult::Success;
}

Messaging::EMessagingResult EncodeMessagePayload(const FConnectRejected& InMessage, Messaging::FMessageWriter& InWriter) noexcept
{
	if (!IsValidRejectReason(static_cast<std::uint8_t>(InMessage.Reason)) || InWriter.WriteU32(InMessage.AttemptId) != Messaging::EMessagingResult::Success)
	{
		return Messaging::EMessagingResult::Invalid;
	}
	return InWriter.WriteU8(static_cast<std::uint8_t>(InMessage.Reason));
}

Messaging::EMessagingResult DecodeMessagePayload(Messaging::FMessageReader& InReader, FConnectRejected& OutMessage) noexcept
{
	FConnectRejected Candidate{};
	std::uint8_t ReasonValue = 0;
	if (InReader.ReadU32(Candidate.AttemptId) != Messaging::EMessagingResult::Success || InReader.ReadU8(ReasonValue) != Messaging::EMessagingResult::Success
		|| !IsValidRejectReason(ReasonValue))
	{
		return Messaging::EMessagingResult::Invalid;
	}
	Candidate.Reason = static_cast<EConnectionRejectReason>(ReasonValue);
	OutMessage = Candidate;
	return Messaging::EMessagingResult::Success;
}

Messaging::EMessagingResult EncodeMessagePayload(const FHeartbeat& InMessage, Messaging::FMessageWriter& InWriter) noexcept
{
	return WritePeerId(InMessage.Peer, InWriter) == Messaging::EMessagingResult::Success ? InWriter.WriteU32(InMessage.AttemptId)
		: Messaging::EMessagingResult::Invalid;
}

Messaging::EMessagingResult DecodeMessagePayload(Messaging::FMessageReader& InReader, FHeartbeat& OutMessage) noexcept
{
	FHeartbeat Candidate{};
	if (ReadPeerId(InReader, Candidate.Peer) != Messaging::EMessagingResult::Success
		|| InReader.ReadU32(Candidate.AttemptId) != Messaging::EMessagingResult::Success)
	{
		return Messaging::EMessagingResult::Invalid;
	}
	OutMessage = Candidate;
	return Messaging::EMessagingResult::Success;
}

Messaging::EMessagingResult EncodeMessagePayload(const FDisconnect& InMessage, Messaging::FMessageWriter& InWriter) noexcept
{
	if (!IsValidDisconnectReason(static_cast<std::uint8_t>(InMessage.Reason)) || WritePeerId(InMessage.Peer, InWriter) != Messaging::EMessagingResult::Success
		|| InWriter.WriteU32(InMessage.AttemptId) != Messaging::EMessagingResult::Success)
	{
		return Messaging::EMessagingResult::Invalid;
	}
	return InWriter.WriteU8(static_cast<std::uint8_t>(InMessage.Reason));
}

Messaging::EMessagingResult DecodeMessagePayload(Messaging::FMessageReader& InReader, FDisconnect& OutMessage) noexcept
{
	FDisconnect Candidate{};
	std::uint8_t ReasonValue = 0;
	if (ReadPeerId(InReader, Candidate.Peer) != Messaging::EMessagingResult::Success
		|| InReader.ReadU32(Candidate.AttemptId) != Messaging::EMessagingResult::Success || InReader.ReadU8(ReasonValue) != Messaging::EMessagingResult::Success
		|| !IsValidDisconnectReason(ReasonValue))
	{
		return Messaging::EMessagingResult::Invalid;
	}
	Candidate.Reason = static_cast<EDisconnectReason>(ReasonValue);
	OutMessage = Candidate;
	return Messaging::EMessagingResult::Success;
}

Messaging::EMessagingResult EncodeMessagePayload(const FRoutedMessage& InMessage, Messaging::FMessageWriter& InWriter) noexcept
{
	if (!InMessage.Peer.IsValid() || InMessage.ChannelNameId == Messaging::InvalidNameId || InMessage.MessageNameId == Messaging::InvalidNameId
		|| InMessage.PayloadSize > FRoutedMessage::MaxPayloadBytes || WritePeerId(InMessage.Peer, InWriter) != Messaging::EMessagingResult::Success
		|| InWriter.WriteU32(InMessage.ChannelNameId.Value) != Messaging::EMessagingResult::Success
		|| InWriter.WriteU32(InMessage.MessageNameId.Value) != Messaging::EMessagingResult::Success
		|| InWriter.WriteU8(InMessage.PayloadSize) != Messaging::EMessagingResult::Success)
	{
		return Messaging::EMessagingResult::Invalid;
	}
	return InWriter.WriteBytes(InMessage.GetPayload());
}

Messaging::EMessagingResult DecodeMessagePayload(Messaging::FMessageReader& InReader, FRoutedMessage& OutMessage) noexcept
{
	FRoutedMessage Candidate{};
	if (ReadPeerId(InReader, Candidate.Peer) != Messaging::EMessagingResult::Success
		|| InReader.ReadU32(Candidate.ChannelNameId.Value) != Messaging::EMessagingResult::Success
		|| InReader.ReadU32(Candidate.MessageNameId.Value) != Messaging::EMessagingResult::Success
		|| InReader.ReadU8(Candidate.PayloadSize) != Messaging::EMessagingResult::Success || Candidate.ChannelNameId == Messaging::InvalidNameId
		|| Candidate.MessageNameId == Messaging::InvalidNameId || Candidate.PayloadSize > FRoutedMessage::MaxPayloadBytes
		|| InReader.ReadBytes(Core::TSpan<std::uint8_t>(Candidate.Payload, Candidate.PayloadSize)) != Messaging::EMessagingResult::Success)
	{
		return Messaging::EMessagingResult::Invalid;
	}
	OutMessage = Candidate;
	return Messaging::EMessagingResult::Success;
}

} // namespace MicroWorld::Networking
