#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Transport/ByteReader.h>
#include <MicroWorld/Transport/EControlMessageType.h>
#include <MicroWorld/Transport/FMessageHeader.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace MicroWorld::Transport
{

/** Motivation: Reserves the channel value for session-control messages so channels 1..255 stay application-defined. */
constexpr std::uint8_t ControlChannel = 0;

/** Motivation: Sizes the largest control payload so the Welcome fields fit in one fixed array. */
constexpr std::size_t MaxControlPayloadBytes = 4;

/** Motivation: Fixes the payload length the per-type control validators expect for Hello. */
constexpr std::size_t HelloControlPayloadBytes = 2;

/** Motivation: Fixes the payload length the per-type control validators expect for Welcome. */
constexpr std::size_t WelcomeControlPayloadBytes = 4;

/** Motivation: Fixes the payload length the per-type control validators expect for Heartbeat and Bye. */
constexpr std::size_t HeartbeatControlPayloadBytes = 1;

/** Motivation: Locates the type byte within every channel-0 control payload. */
constexpr std::size_t ControlTypeByteIndex = 0;

/** Motivation: Locates the protocol version byte in a Hello control payload. */
constexpr std::size_t HelloProtocolVersionByteIndex = 1;

/** Motivation: Locates the protocol version byte in a Welcome control payload. */
constexpr std::size_t WelcomeProtocolVersionByteIndex = 1;

/** Motivation: Locates the assigned peer index byte in a Welcome control payload. */
constexpr std::size_t WelcomePeerIndexByteIndex = 2;

/** Motivation: Locates the assigned peer generation byte in a Welcome control payload. */
constexpr std::size_t WelcomePeerGenerationByteIndex = 3;

/**
 * Motivation: Gives the host one decoded view of a channel-0 control message after length validation.
 * Responsibilities: Carry only the type and the per-type fields (Hello uses ProtocolVersion; Welcome uses ProtocolVersion,
 *   PeerIndex, and PeerGeneration; Heartbeat and Bye use none), which the decoder populates only after enforcing the exact
 *   per-type length.
 * Example:
 *   FControlMessage Control;
 *   if (ReadControlMessage(Payload, Control) == Core::ETransportResult::Success) { Handle(Control); }
 */
struct FControlMessage
{
	/** Motivation: Carries the control message type from the first payload byte. */
	EControlMessageType Type{EControlMessageType::Heartbeat};

	/** Motivation: Carries the protocol version used by Hello and Welcome. */
	std::uint8_t ProtocolVersion{0};

	/** Motivation: Carries the assigned peer index used by Welcome. */
	std::uint8_t PeerIndex{0};

	/** Motivation: Carries the assigned peer generation used by Welcome. */
	std::uint8_t PeerGeneration{0};
};

/**
 * Motivation: Encodes one channel-0 control message by reusing WriteMessage so framing stays consistent.
 * Responsibilities: Build the per-type control payload in a fixed local array, frame it on the control channel, and return
 *   Invalid without touching the writer for an unknown type.
 */
inline Core::ETransportResult WriteControlMessage(FByteWriter& InWriter, const FControlMessage& InMessage) noexcept
{
	std::array<std::uint8_t, MaxControlPayloadBytes> Payload{};
	Payload[ControlTypeByteIndex] = static_cast<std::uint8_t>(InMessage.Type);
	std::size_t PayloadLength = HeartbeatControlPayloadBytes;
	switch (InMessage.Type)
	{
		case EControlMessageType::Hello:
			Payload[HelloProtocolVersionByteIndex] = InMessage.ProtocolVersion;
			PayloadLength = HelloControlPayloadBytes;
			break;
		case EControlMessageType::Welcome:
			Payload[HelloProtocolVersionByteIndex] = InMessage.ProtocolVersion;
			Payload[WelcomePeerIndexByteIndex] = InMessage.PeerIndex;
			Payload[WelcomePeerGenerationByteIndex] = InMessage.PeerGeneration;
			PayloadLength = WelcomeControlPayloadBytes;
			break;
		case EControlMessageType::Heartbeat:
		case EControlMessageType::Bye:
			PayloadLength = HeartbeatControlPayloadBytes;
			break;
		default:
			// An unknown type has no defined encoding; reject before any write.
			return Core::ETransportResult::Invalid;
	}
	return WriteMessage(InWriter, ControlChannel, Core::TSpan<const std::uint8_t>(Payload.data(), PayloadLength));
}

/**
 * Motivation: Guards control decoding against a type byte its length does not match so a malformed payload is rejected early.
 * Responsibilities: Return Success only when the type byte is known and the payload size matches its expected length,
 *   otherwise Invalid.
 */
inline Core::ETransportResult ValidateControlPayloadLength(const std::uint8_t InTypeByte, const std::size_t InPayloadSize) noexcept
{
	switch (InTypeByte)
	{
		case static_cast<std::uint8_t>(EControlMessageType::Hello):
			if (InPayloadSize != HelloControlPayloadBytes)
			{
				return Core::ETransportResult::Invalid;
			}
			return Core::ETransportResult::Success;
		case static_cast<std::uint8_t>(EControlMessageType::Welcome):
			if (InPayloadSize != WelcomeControlPayloadBytes)
			{
				return Core::ETransportResult::Invalid;
			}
			return Core::ETransportResult::Success;
		case static_cast<std::uint8_t>(EControlMessageType::Heartbeat):
		case static_cast<std::uint8_t>(EControlMessageType::Bye):
			if (InPayloadSize != HeartbeatControlPayloadBytes)
			{
				return Core::ETransportResult::Invalid;
			}
			return Core::ETransportResult::Success;
		default:
			// The type byte names no known control message; the caller drops it.
			return Core::ETransportResult::Invalid;
	}
}

/**
 * Motivation: Reads the per-type control fields from a reader positioned past the type byte so one length-checked payload decodes.
 * Responsibilities: Populate OutMessage with the fields its validated type carries and leave the others default.
 */
inline void DecodeControlFields(FByteReader& InReader, const EControlMessageType InType, FControlMessage& OutMessage) noexcept
{
	FControlMessage Decoded{};
	Decoded.Type = InType;
	if (InType == EControlMessageType::Hello || InType == EControlMessageType::Welcome)
	{
		(void)InReader.ReadByte(Decoded.ProtocolVersion);
	}
	if (InType == EControlMessageType::Welcome)
	{
		(void)InReader.ReadByte(Decoded.PeerIndex);
		(void)InReader.ReadByte(Decoded.PeerGeneration);
	}
	OutMessage = Decoded;
}

/**
 * Motivation: Decodes one channel-0 control payload transactionally so a malformed payload never yields a partial control message.
 * Responsibilities: Read the type byte through a local FByteReader, validate it against Hello, Welcome, Heartbeat, and Bye,
 *   enforce the exact per-type length before any field read, and write OutMessage only on Success.
 */
inline Core::ETransportResult ReadControlMessage(Core::TSpan<const std::uint8_t> InPayload, FControlMessage& OutMessage) noexcept
{
	FByteReader Reader(InPayload);
	std::uint8_t TypeByte = 0;
	if (Reader.ReadByte(TypeByte) != Core::ETransportResult::Success)
	{
		// An empty control payload carries no type byte at all.
		return Core::ETransportResult::Invalid;
	}
	const Core::ETransportResult LengthResult = ValidateControlPayloadLength(TypeByte, InPayload.Size());
	if (LengthResult != Core::ETransportResult::Success)
	{
		return LengthResult;
	}
	DecodeControlFields(Reader, static_cast<EControlMessageType>(TypeByte), OutMessage);
	return Core::ETransportResult::Success;
}

} // namespace MicroWorld::Transport
