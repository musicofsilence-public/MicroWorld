#pragma once

#include <MicroWorld/Core/ByteCodecConstants.h>
#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Delegates/Delegate.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Messaging
{

/** Motivation: Identifies what kind of message this is, reserving 0 as the invalid sentinel. */
using FMessageTypeId = std::uint16_t;

/** Motivation: Identifies one messaging actor as a caller-assigned value, reserving 0 as the broadcast target. */
using FMessageActorId = std::uint16_t;

/** Motivation: Names the target id that reaches every handler registered for the type. */
inline constexpr FMessageActorId BroadcastActorId = 0;

/** Motivation: Identifies one configured channel on a router, reserving 0 as the built-in local channel. */
using FMessageChannelId = std::uint8_t;

/** Motivation: Names the channel id for local, same-world, no-wire delivery that is always available. */
inline constexpr FMessageChannelId LocalChannelId = 0;

/** Motivation: Fixes the encoded size of the three-field actor-message header for offset math. */
inline constexpr std::size_t ActorMessageHeaderBytes = 6;

/** Motivation: Fixes the byte offset of the message type id field within an encoded actor message. */
inline constexpr std::size_t MessageTypeIdOffset = 0;

/** Motivation: Fixes the byte offset of the target actor id field within an encoded actor message. */
inline constexpr std::size_t TargetActorIdOffset = 2;

/** Motivation: Fixes the byte offset of the sender actor id field within an encoded actor message. */
inline constexpr std::size_t SenderActorIdOffset = 4;

/** Motivation: Fixes the encoded size of one 16-bit header field for the per-field encode/decode helpers. */
inline constexpr std::size_t MessageHeaderFieldBytes = 2;

/**
 * Motivation: Gives every messaging operation one result vocabulary that does not borrow unrelated lifecycle errors.
 * Responsibilities: Distinguish success from capacity, duplicate, type, channel, handler, handle-staleness, dispatch,
 *   payload-size, and availability failures so callers branch on one closed set.
 * Example:
 *   if (Router.SendMessageToActor(ChannelId, TypeId, Actor, Sender, Payload) == EMessageResult::Success) { Notify(); }
 */
enum class EMessageResult : std::uint8_t
{
	Success, ///< Motivation: Confirms that the requested messaging operation completed.

	CapacityExceeded, ///< Motivation: Reports that no reusable handler, queue, or channel slot remains.

	Duplicate, ///< Motivation: Rejects a registration that already exists under the same identity.

	InvalidType, ///< Motivation: Rejects a message whose MessageTypeId is zero.

	InvalidChannel, ///< Motivation: Rejects an operation naming a channel id the router does not have configured.

	InvalidHandler, ///< Motivation: Rejects a handler registration or removal that fails structural validation.

	StaleHandle, ///< Motivation: Rejects a handle whose slot is free, retired, removed, or holds another generation.

	DispatchLocked, ///< Motivation: Prevents registration, removal, or nested dispatch from mutating an active delivery pass.

	PayloadTooLarge, ///< Motivation: Rejects an encoded or decoded message that cannot fit the caller-supplied bytes.

	Unavailable, ///< Motivation: Reports that a channel or transport cannot accept the request right now.
};

/**
 * Motivation: Carries the three routing ids every actor message needs in front of its payload, so encode and decode
 *   share one fixed header shape independent of payload content.
 * Responsibilities: Hold MessageTypeId, TargetActorId, and SenderActorId in a fixed order and carry no behavior.
 * Example:
 *   FActorMessageHeader Header{MovementTypeId, TargetActor, SenderActor};
 *   EncodeActorMessage(Header, Payload, Encoded, Written);
 */
struct FActorMessageHeader
{
	/** Motivation: Selects the message's meaning and, by extension, which handlers may receive it. */
	FMessageTypeId MessageTypeId{0};

	/** Motivation: Selects BroadcastActorId for every subscriber or one specific listener actor. */
	FMessageActorId TargetActorId{BroadcastActorId};

	/** Motivation: Identifies the actor that queued this message, for the receiving handler's own use. */
	FMessageActorId SenderActorId{0};
};

/**
 * Motivation: Lets header encode helpers write one 16-bit value without each inlining the byte split.
 * Responsibilities: Write the value least-significant byte first into the two bytes at OutBytes.
 */
inline void WriteMessageUint16LittleEndian(const std::uint16_t InValue, std::uint8_t* const OutBytes) noexcept
{
	OutBytes[0] = static_cast<std::uint8_t>(InValue & Core::LowByteMask);
	OutBytes[1] = static_cast<std::uint8_t>((InValue >> Core::HighByteShift) & Core::LowByteMask);
}

/**
 * Motivation: Lets header decode helpers read one 16-bit value without each inlining the byte merge.
 * Responsibilities: Read the value least-significant byte first from the two bytes at InBytes.
 */
inline std::uint16_t ReadMessageUint16LittleEndian(const std::uint8_t* const InBytes) noexcept
{
	return static_cast<std::uint16_t>(static_cast<std::uint16_t>(InBytes[0]) | (static_cast<std::uint16_t>(InBytes[1]) << Core::HighByteShift));
}

/**
 * Motivation: Lets a caller flatten one actor-message header and payload into a contiguous byte span a channel can send.
 * Responsibilities: Reject a zero Header.MessageTypeId as InvalidType and a payload that will not fit OutEncoded as
 *   PayloadTooLarge, both before any byte is written so OutEncoded and OutWrittenBytes stay exactly as passed; on Success
 *   write the three header fields little-endian followed immediately by the payload bytes and set OutWrittenBytes to
 *   ActorMessageHeaderBytes plus Payload.Size().
 */
inline EMessageResult EncodeActorMessage(
	const FActorMessageHeader& InHeader,
	const Core::TSpan<const std::uint8_t> InPayload,
	const Core::TSpan<std::uint8_t> InEncoded,
	std::size_t& OutWrittenBytes) noexcept
{
	if (InHeader.MessageTypeId == 0)
	{
		return EMessageResult::InvalidType;
	}

	const std::size_t EncodedSize = ActorMessageHeaderBytes + InPayload.Size();
	if (EncodedSize > InEncoded.Size())
	{
		return EMessageResult::PayloadTooLarge;
	}

	std::uint8_t* const Destination = InEncoded.Data();
	WriteMessageUint16LittleEndian(InHeader.MessageTypeId, &Destination[MessageTypeIdOffset]);
	WriteMessageUint16LittleEndian(InHeader.TargetActorId, &Destination[TargetActorIdOffset]);
	WriteMessageUint16LittleEndian(InHeader.SenderActorId, &Destination[SenderActorIdOffset]);

	const std::uint8_t* const PayloadData = InPayload.Data();
	for (std::size_t Index = 0; Index < InPayload.Size(); ++Index)
	{
		Destination[ActorMessageHeaderBytes + Index] = PayloadData[Index];
	}

	OutWrittenBytes = EncodedSize;
	return EMessageResult::Success;
}

/**
 * Motivation: Lets a caller split an encoded actor message back into its header and a payload view without copying bytes.
 * Responsibilities: Reject an Encoded span shorter than ActorMessageHeaderBytes as PayloadTooLarge before reading any
 *   field and a decoded MessageTypeId of zero as InvalidType before touching either output, so OutHeader and OutPayload
 *   stay exactly as passed; on Success fill OutHeader with the three little-endian fields and point OutPayload at the
 *   remaining bytes of Encoded.
 */
inline EMessageResult DecodeActorMessage(
	const Core::TSpan<const std::uint8_t> InEncoded, FActorMessageHeader& OutHeader, Core::TSpan<const std::uint8_t>& OutPayload) noexcept
{
	if (InEncoded.Size() < ActorMessageHeaderBytes)
	{
		return EMessageResult::PayloadTooLarge;
	}

	const std::uint8_t* const Source = InEncoded.Data();
	FActorMessageHeader DecodedHeader;
	DecodedHeader.MessageTypeId = ReadMessageUint16LittleEndian(&Source[MessageTypeIdOffset]);
	DecodedHeader.TargetActorId = ReadMessageUint16LittleEndian(&Source[TargetActorIdOffset]);
	DecodedHeader.SenderActorId = ReadMessageUint16LittleEndian(&Source[SenderActorIdOffset]);
	if (DecodedHeader.MessageTypeId == 0)
	{
		return EMessageResult::InvalidType;
	}

	OutHeader = DecodedHeader;
	OutPayload = Core::TSpan<const std::uint8_t>(Source + ActorMessageHeaderBytes, InEncoded.Size() - ActorMessageHeaderBytes);
	return EMessageResult::Success;
}

/**
 * Motivation: Bundles everything a handler needs from one delivered message into a single argument it cannot misuse.
 * Responsibilities: Combine the header, the channel id the message arrived on, and a payload view valid only for the
 *   duration of the callback, and carry no behavior.
 * Example:
 *   Router.AddMessageHandler(TypeId, Actor, TDelegate([&](const FMessageView& View) { Handle(View); }), Handle);
 */
struct FMessageView
{
	/** Motivation: Carries the message identity and routing ids this delivery holds. */
	FActorMessageHeader Header;

	/** Motivation: Names the channel the message arrived on (LocalChannelId for same-world sends). */
	FMessageChannelId ArrivedOnChannelId{LocalChannelId};

	/** Motivation: Views the message body following the header, valid only for the duration of the callback. */
	Core::TSpan<const std::uint8_t> Payload;
};

/** Motivation: Fixes the inline byte budget for one message-handler callable, matching the TTransportHost precedent. */
inline constexpr std::size_t MessageHandlerInlineBytes = 32;

/** Motivation: Names the delegate type actors bind to receive messages, carrying the callable inline. */
using FMessageHandlerBinding = Core::TDelegate<void(const FMessageView&), MessageHandlerInlineBytes>;

/**
 * Motivation: Lets a caller carry one live message-handler identity without exposing router storage or extending the
 *   callback's lifetime, mirroring FTimerHandle's {index, generation} shape.
 * Responsibilities: Pair a slot index with a generation and never mutate on its own; a handle is local to the router
 *   that issued it and must not be carried between routers.
 * Example:
 *   FMessageHandlerHandle Handle;
 *   if (Handle.IsValid()) { Router.RemoveMessageHandler(Handle); }
 */
struct FMessageHandlerHandle final
{
	/** Motivation: Reserves the maximum uint16 value as the invalid sentinel independent of router capacity. */
	static constexpr std::uint16_t InvalidIndex = 0xFFFFu;

	/** Motivation: Selects the fixed handler slot while preserving an explicit invalid sentinel. */
	std::uint16_t Index{InvalidIndex};

	/** Motivation: Distinguishes successive registrations that occupy the same slot. */
	std::uint32_t Generation{0};

	/**
	 * Motivation: Lets a caller reject a default or stale value before consulting its owning router.
	 * Responsibilities: Report true only when the index and generation together look like a live handler.
	 */
	constexpr bool IsValid() const noexcept { return Index != InvalidIndex && Generation != 0; }

	/**
	 * Motivation: Lets containers compare two handles by complete stable identity.
	 * Responsibilities: Return true only when both index and generation match.
	 */
	friend constexpr bool operator==(const FMessageHandlerHandle InLeft, const FMessageHandlerHandle InRight) noexcept
	{
		return InLeft.Index == InRight.Index && InLeft.Generation == InRight.Generation;
	}

	/**
	 * Motivation: Lets a caller tell two handles apart by stable identity.
	 * Responsibilities: Return true whenever the slot or generation identity differs.
	 */
	friend constexpr bool operator!=(const FMessageHandlerHandle InLeft, const FMessageHandlerHandle InRight) noexcept
	{
		return !(InLeft == InRight);
	}
};

/**
 * Motivation: Lets a channel or channel wrapper hand received bytes to a sink without knowing whether it is a router
 *   or another wrapper.
 * Responsibilities: Offer one inbound entry point that accepts a channel id and an encoded actor message, leaving the
 *   sink's queued state unchanged on rejection.
 * Example:
 *   IEncodedMessageSink& Sink = Router;
 *   Sink.ReceiveEncodedMessage(ChannelId, Encoded);
 */
class IEncodedMessageSink
{
public:
	/**
	 * Motivation: Lets derived sinks be destroyed through this interface without leaking their concrete type.
	 * Responsibilities: Stay virtual and perform no work beyond the derived destructor.
	 */
	virtual ~IEncodedMessageSink() noexcept = default;

	/**
	 * Motivation: Lets a channel hand one encoded message that arrived on a wire byte to the router side that queues it.
	 * Responsibilities: Accept the channel id and complete encoded actor message, returning CapacityExceeded,
	 *   InvalidChannel, or DispatchLocked without changing queued state so the caller may retry with the same bytes
	 *   on a later frame.
	 */
	virtual EMessageResult ReceiveEncodedMessage(FMessageChannelId InArrivedOnChannelId, Core::TSpan<const std::uint8_t> InEncoded) noexcept = 0;
};

/**
 * Motivation: Names the outbound side of one configured channel so a router can hand bytes to a binding, a wrapper, or
 *   any future sender behind one type.
 * Responsibilities: Expose the channel's caller-assigned id, its single-send byte budget, and one send entry point,
 *   without owning the bytes a caller passes.
 * Example:
 *   IMessageChannel& Channel = Binding;
 *   if (Channel.TrySendEncodedMessage(Encoded) == EMessageResult::Success) { Sent(); }
 */
class IMessageChannel
{
public:
	/**
	 * Motivation: Lets derived channels be destroyed through this interface without leaking their concrete type.
	 * Responsibilities: Stay virtual and perform no work beyond the derived destructor.
	 */
	virtual ~IMessageChannel() noexcept = default;

	/**
	 * Motivation: Lets a router identify a channel without holding a concrete binding type.
	 * Responsibilities: Return this channel's caller-assigned id, never LocalChannelId.
	 */
	virtual FMessageChannelId GetChannelId() const noexcept = 0;

	/**
	 * Motivation: Lets a caller size an encoded message against the channel before sending.
	 * Responsibilities: Return the largest encoded message this channel can carry in one send.
	 */
	virtual std::size_t MaxEncodedMessageBytes() const noexcept = 0;

	/**
	 * Motivation: Lets a router hand one encoded message to the channel's transport for transmission.
	 * Responsibilities: Accept the complete encoded actor message, returning Success on acceptance; Unavailable or
	 *   PayloadTooLarge means the caller may retry on a later frame or after re-encoding within
	 *   MaxEncodedMessageBytes rather than the send being lost.
	 */
	virtual EMessageResult TrySendEncodedMessage(Core::TSpan<const std::uint8_t> InEncoded) noexcept = 0;
};

/**
 * Motivation: Gives actors one messaging API they hold by reference, so they never see channels, transports, or the
 *   router's storage.
 * Responsibilities: Extend IEncodedMessageSink with handler registration and removal plus per-actor and broadcast
 *   send entry points, owning no bytes a caller passes.
 * Example:
 *   IMessageRouter& Router = ConcreteRouter;
 *   Router.SendMessageToActor(LocalChannelId, MovementTypeId, TargetActor, SenderActor, Payload);
 */
class IMessageRouter : public IEncodedMessageSink
{
public:
	/**
	 * Motivation: Lets an actor register one callback for a message type it wants to receive.
	 * Responsibilities: Move the handler into router storage on success and publish a fresh generation-checked handle
	 *   in OutHandle; on failure clear OutHandle and leave the handler bound to the caller.
	 */
	virtual EMessageResult AddMessageHandler(
		FMessageTypeId InMessageTypeId,
		FMessageActorId InListenerActorId,
		FMessageHandlerBinding&& InHandler,
		FMessageHandlerHandle& OutHandle) noexcept = 0;

	/**
	 * Motivation: Lets an actor retire one callback it no longer needs.
	 * Responsibilities: Remove exactly the registration the handle identifies, rejecting a stale or already-removed
	 *   handle.
	 */
	virtual EMessageResult RemoveMessageHandler(FMessageHandlerHandle InHandle) noexcept = 0;

	/**
	 * Motivation: Lets an actor queue one message for a specific target actor on a channel.
	 * Responsibilities: Encode the header and caller-owned payload for the chosen channel, rejecting a zero type id,
	 *   an unconfigured channel, or a payload that cannot fit before the queue is touched.
	 */
	virtual EMessageResult SendMessageToActor(
		FMessageChannelId InChannelId,
		FMessageTypeId InMessageTypeId,
		FMessageActorId InTargetActorId,
		FMessageActorId InSenderActorId,
		Core::TSpan<const std::uint8_t> InPayload) noexcept = 0;

	/**
	 * Motivation: Lets an actor queue one message for every subscriber of the type without naming each target.
	 * Responsibilities: Encode the header and caller-owned payload for the chosen channel with BroadcastActorId as the
	 *   target, applying the same validation as SendMessageToActor.
	 */
	virtual EMessageResult BroadcastMessage(
		FMessageChannelId InChannelId,
		FMessageTypeId InMessageTypeId,
		FMessageActorId InSenderActorId,
		Core::TSpan<const std::uint8_t> InPayload) noexcept = 0;
};

} // namespace MicroWorld::Messaging
