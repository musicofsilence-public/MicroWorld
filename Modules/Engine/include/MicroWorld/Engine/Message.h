#pragma once

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Delegates/Delegate.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld
{

/** Identifies what kind of message this is; 0 is invalid. */
using FMessageTypeId = std::uint16_t;

/** Identifies an actor in messaging; caller-assigned; 0 is the broadcast target. */
using FMessageActorId = std::uint16_t;

/** Target id meaning "every handler registered for the type". */
inline constexpr FMessageActorId BroadcastActorId = 0;

/** Identifies one configured channel on a router; 0 is the built-in local channel. */
using FMessageChannelId = std::uint8_t;

/** Channel id for local (same-world, no wire) delivery; always available. */
inline constexpr FMessageChannelId LocalChannelId = 0;

/** Encoded size of the three-field actor-message header. */
inline constexpr std::size_t ActorMessageHeaderBytes = 6;

/** Reports the outcome of every messaging operation without borrowing unrelated lifecycle errors. */
enum class EMessageResult : std::uint8_t
{
	/** Confirms that the requested messaging operation completed. */
	Success,

	/** Reports that no reusable handler, queue, or channel slot remains. */
	CapacityExceeded,

	/** Rejects a registration that already exists under the same identity. */
	Duplicate,

	/** Rejects a message whose MessageTypeId is zero. */
	InvalidType,

	/** Rejects an operation naming a channel id the router does not have configured. */
	InvalidChannel,

	/** Rejects a handler registration or removal that fails structural validation. */
	InvalidHandler,

	/** Rejects a handle whose slot is free, retired, removed, or holds another generation. */
	StaleHandle,

	/** Prevents registration, removal, or nested dispatch from mutating an active delivery pass. */
	DispatchLocked,

	/** Rejects an encoded or decoded message that cannot fit the caller-supplied bytes. */
	PayloadTooLarge,

	/** Reports that a channel or transport cannot accept the request right now. */
	Unavailable,
};

/**
 * The three ids every actor message carries in front of its payload.
 * TargetActorId selects BroadcastActorId for every subscriber of MessageTypeId or one specific actor.
 */
struct FActorMessageHeader
{
	/** Selects the message's meaning and, by extension, which handlers may receive it. */
	FMessageTypeId MessageTypeId{0};

	/** Selects BroadcastActorId for every subscriber or one specific listener actor. */
	FMessageActorId TargetActorId{BroadcastActorId};

	/** Identifies the actor that queued this message, for the receiving handler's own use. */
	FMessageActorId SenderActorId{0};
};

namespace Detail
{

	/** Writes one 16-bit value into two bytes, least-significant byte first. */
	inline void WriteMessageUint16LittleEndian(const std::uint16_t Value, std::uint8_t* const OutBytes) noexcept
	{
		OutBytes[0] = static_cast<std::uint8_t>(Value & 0xFFu);
		OutBytes[1] = static_cast<std::uint8_t>((Value >> 8) & 0xFFu);
	}

	/** Reads one 16-bit value from two bytes, least-significant byte first. */
	inline std::uint16_t ReadMessageUint16LittleEndian(const std::uint8_t* const Bytes) noexcept
	{
		return static_cast<std::uint16_t>(static_cast<std::uint16_t>(Bytes[0]) | (static_cast<std::uint16_t>(Bytes[1]) << 8));
	}

} // namespace Detail

/**
 * Writes one actor-message header and payload little-endian into OutEncoded.
 *
 * Rejects a zero Header.MessageTypeId as InvalidType and rejects a payload that will not fit
 * OutEncoded as PayloadTooLarge, both before any byte is written, so both rejections are
 * transactional: OutEncoded and OutWrittenBytes are left exactly as the caller passed them. On
 * Success the header's three fields are written little-endian at the front of OutEncoded followed
 * immediately by the payload bytes, and OutWrittenBytes is set to ActorMessageHeaderBytes plus
 * Payload.Size().
 *
 * @param Header Message identity written into the six-byte encoded header.
 * @param Payload Caller-owned payload bytes copied immediately after the header.
 * @param OutEncoded Caller-owned destination for the encoded header and payload.
 * @param OutWrittenBytes Filled with the total byte count written only on Success.
 * @return Outcome of the single encode attempt.
 */
inline EMessageResult EncodeActorMessage(
	const FActorMessageHeader& Header,
	const TSpan<const std::uint8_t> Payload,
	const TSpan<std::uint8_t> OutEncoded,
	std::size_t& OutWrittenBytes) noexcept
{
	if (Header.MessageTypeId == 0)
	{
		return EMessageResult::InvalidType;
	}

	const std::size_t EncodedSize = ActorMessageHeaderBytes + Payload.Size();
	if (EncodedSize > OutEncoded.Size())
	{
		return EMessageResult::PayloadTooLarge;
	}

	std::uint8_t* const Destination = OutEncoded.Data();
	Detail::WriteMessageUint16LittleEndian(Header.MessageTypeId, &Destination[0]);
	Detail::WriteMessageUint16LittleEndian(Header.TargetActorId, &Destination[2]);
	Detail::WriteMessageUint16LittleEndian(Header.SenderActorId, &Destination[4]);

	const std::uint8_t* const PayloadData = Payload.Data();
	for (std::size_t Index = 0; Index < Payload.Size(); ++Index)
	{
		Destination[ActorMessageHeaderBytes + Index] = PayloadData[Index];
	}

	OutWrittenBytes = EncodedSize;
	return EMessageResult::Success;
}

/**
 * Splits an encoded actor message back into its header and a payload view.
 *
 * Rejects an Encoded span shorter than ActorMessageHeaderBytes as PayloadTooLarge before reading
 * any field, and rejects a decoded MessageTypeId of zero as InvalidType before touching either
 * output, so both rejections are transactional: OutHeader and OutPayload are left exactly as the
 * caller passed them. On Success OutHeader receives the three little-endian header fields and
 * OutPayload views the remaining bytes of Encoded without copying them.
 *
 * @param Encoded Caller-owned bytes previously produced by EncodeActorMessage.
 * @param OutHeader Filled with the decoded header fields only on Success.
 * @param OutPayload Filled with a view of the payload bytes only on Success.
 * @return Outcome of the single decode attempt.
 */
inline EMessageResult DecodeActorMessage(
	const TSpan<const std::uint8_t> Encoded, FActorMessageHeader& OutHeader, TSpan<const std::uint8_t>& OutPayload) noexcept
{
	if (Encoded.Size() < ActorMessageHeaderBytes)
	{
		return EMessageResult::PayloadTooLarge;
	}

	const std::uint8_t* const Source = Encoded.Data();
	FActorMessageHeader DecodedHeader;
	DecodedHeader.MessageTypeId = Detail::ReadMessageUint16LittleEndian(&Source[0]);
	DecodedHeader.TargetActorId = Detail::ReadMessageUint16LittleEndian(&Source[2]);
	DecodedHeader.SenderActorId = Detail::ReadMessageUint16LittleEndian(&Source[4]);
	if (DecodedHeader.MessageTypeId == 0)
	{
		return EMessageResult::InvalidType;
	}

	OutHeader = DecodedHeader;
	OutPayload = TSpan<const std::uint8_t>(Source + ActorMessageHeaderBytes, Encoded.Size() - ActorMessageHeaderBytes);
	return EMessageResult::Success;
}

/** One delivered message as a handler sees it, combining the header and payload with the channel it arrived on. */
struct FMessageView
{
	/** The message identity and routing ids this delivery carries. */
	FActorMessageHeader Header;

	/** Channel the message arrived on (LocalChannelId for same-world sends). */
	FMessageChannelId ArrivedOnChannelId{LocalChannelId};

	/** View of the message body following the header; valid only for the duration of the callback. */
	TSpan<const std::uint8_t> Payload;
};

/** Inline byte budget for one message-handler callable (TNetHost precedent). */
inline constexpr std::size_t MessageHandlerInlineBytes = 32;

/** Callback type actors bind to receive messages. */
using FMessageHandlerBinding = TDelegate<void(const FMessageView&), MessageHandlerInlineBytes>;

/**
 * Identifies one registered message handler without exposing router storage.
 * A handle is local to the router that issued it, mirroring FTimerHandle's {index, generation} shape.
 */
struct FMessageHandlerHandle final
{
	/** Reserves the maximum uint16 value as the invalid sentinel independent of router capacity. */
	static constexpr std::uint16_t InvalidIndex = 0xFFFFu;

	/** Selects the fixed handler slot while preserving an explicit invalid sentinel. */
	std::uint16_t Index{InvalidIndex};

	/** Distinguishes successive registrations that occupy the same slot. */
	std::uint32_t Generation{0};

	/** Reports whether the value can identify a handler before consulting its owning router. */
	constexpr bool IsValid() const noexcept { return Index != InvalidIndex && Generation != 0; }

	/** Compares the complete stable handler identity. */
	friend constexpr bool operator==(const FMessageHandlerHandle Left, const FMessageHandlerHandle Right) noexcept
	{
		return Left.Index == Right.Index && Left.Generation == Right.Generation;
	}

	/** Distinguishes handles whose slot or generation identity differs. */
	friend constexpr bool operator!=(const FMessageHandlerHandle Left, const FMessageHandlerHandle Right) noexcept { return !(Left == Right); }
};

/**
 * Anything that can accept one encoded actor message arriving from a channel.
 * Channels and channel wrappers hand received bytes to a sink without knowing whether it is a router or another wrapper.
 */
class IEncodedMessageSink
{
public:
	/** Allows derived sinks to be destroyed through this interface. */
	virtual ~IEncodedMessageSink() noexcept = default;

	/**
	 * Queues one encoded message that arrived on the given channel.
	 *
	 * Rejection (CapacityExceeded, InvalidChannel, or DispatchLocked) leaves the sink's queued
	 * state unchanged; the caller may retry with the same bytes on a later frame.
	 *
	 * @param ArrivedOnChannelId Channel the encoded bytes were received on.
	 * @param Encoded Complete encoded actor message, as produced by EncodeActorMessage.
	 * @return Outcome of the single queue attempt.
	 */
	virtual EMessageResult ReceiveEncodedMessage(FMessageChannelId ArrivedOnChannelId, TSpan<const std::uint8_t> Encoded) noexcept = 0;
};

/** Outbound side of one configured channel; implemented by channel bindings and wrappers such as a reliability layer. */
class IMessageChannel
{
public:
	/** Allows derived channels to be destroyed through this interface. */
	virtual ~IMessageChannel() noexcept = default;

	/** Returns this channel's caller-assigned id (never LocalChannelId). */
	virtual FMessageChannelId GetChannelId() const noexcept = 0;

	/** Returns the largest encoded message this channel can carry in one send. */
	virtual std::size_t MaxEncodedMessageBytes() const noexcept = 0;

	/**
	 * Hands one encoded message to the channel's transport.
	 *
	 * @param Encoded Complete encoded actor message, as produced by EncodeActorMessage.
	 * @return Success once accepted; Unavailable or PayloadTooLarge means the caller may retry
	 *         (a later frame, or after re-encoding within MaxEncodedMessageBytes) rather than the send being lost.
	 */
	virtual EMessageResult TrySendEncodedMessage(TSpan<const std::uint8_t> Encoded) noexcept = 0;
};

/** The actor-facing messaging API; actors hold this by reference and never see channels or transports directly. */
class IMessageRouter : public IEncodedMessageSink
{
public:
	/**
	 * Registers a callback for one message type.
	 *
	 * Failure clears OutHandle and leaves Handler bound to the caller; success publishes a fresh
	 * generation-checked handle usable with RemoveMessageHandler.
	 *
	 * @param MessageTypeId Message type this handler will be invoked for.
	 * @param ListenerActorId Actor id to match against TargetActorId; 0 (BroadcastActorId) means broadcasts only.
	 * @param Handler Callback moved into the router's storage on success.
	 * @param OutHandle Filled with a valid handle only on Success.
	 * @return Outcome of the single registration attempt.
	 */
	virtual EMessageResult AddMessageHandler(
		FMessageTypeId MessageTypeId,
		FMessageActorId ListenerActorId,
		FMessageHandlerBinding&& Handler,
		FMessageHandlerHandle& OutHandle) noexcept = 0;

	/**
	 * Removes one previously registered callback.
	 *
	 * @param Handle Handle previously published by AddMessageHandler; a stale or already-removed handle is rejected.
	 * @return Outcome of the single removal attempt.
	 */
	virtual EMessageResult RemoveMessageHandler(FMessageHandlerHandle Handle) noexcept = 0;

	/**
	 * Queues one message for a specific target actor on the given channel.
	 *
	 * @param ChannelId Channel the message is queued on; LocalChannelId delivers within the same world.
	 * @param MessageTypeId Message type identifying which handlers may receive this message.
	 * @param TargetActorId Actor id that must match a handler's ListenerActorId to receive this message.
	 * @param SenderActorId Actor id recorded as the message's sender for the receiving handler.
	 * @param Payload Caller-owned message body following the header.
	 * @return Outcome of the single queue attempt.
	 */
	virtual EMessageResult SendMessageToActor(
		FMessageChannelId ChannelId,
		FMessageTypeId MessageTypeId,
		FMessageActorId TargetActorId,
		FMessageActorId SenderActorId,
		TSpan<const std::uint8_t> Payload) noexcept = 0;

	/**
	 * Queues one message for every subscriber of the type on the given channel.
	 *
	 * @param ChannelId Channel the message is queued on; LocalChannelId delivers within the same world.
	 * @param MessageTypeId Message type identifying which handlers may receive this message.
	 * @param SenderActorId Actor id recorded as the message's sender for the receiving handlers.
	 * @param Payload Caller-owned message body following the header.
	 * @return Outcome of the single queue attempt.
	 */
	virtual EMessageResult BroadcastMessage(
		FMessageChannelId ChannelId, FMessageTypeId MessageTypeId, FMessageActorId SenderActorId, TSpan<const std::uint8_t> Payload) noexcept = 0;
};

} // namespace MicroWorld
