#pragma once

#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessageReader.h>
#include <MicroWorld/Messaging/MessageWriter.h>
#include <MicroWorld/Messaging/MessagingResult.h>
#include <MicroWorld/Messaging/NameId.h>

#include <type_traits>
#include <utility>

namespace MicroWorld::Messaging
{

/**
 * Motivation: Detects the exact ADL codec customization surface before a typed send or decode instantiates it.
 * Responsibilities: Report true only for types that provide the required noexcept name, encode, and decode functions with Messaging result types.
 * Example: static_assert(TIsTypedMessageCodec<FProtocolMessage>::value, "Protocol messages require a codec.");
 */
template<typename MessageType, typename = void>
struct TIsTypedMessageCodec final : std::false_type
{
};

/**
 * Motivation: Recognizes protocol types that supply Messaging's allocation-free ADL codec contract.
 * Responsibilities: Require constexpr-compatible name lookup and exact encode/decode result expressions without registering global state.
 * Example: static_assert(TIsTypedMessageCodec<FProtocolMessage>::value, "Protocol messages require a codec.");
 */
template<typename MessageType>
struct TIsTypedMessageCodec<
	MessageType,
	std::void_t<
		decltype(GetMessageNameId(std::declval<const MessageType&>())),
		decltype(EncodeMessagePayload(std::declval<const MessageType&>(), std::declval<FMessageWriter&>())),
		decltype(DecodeMessagePayload(std::declval<FMessageReader&>(), std::declval<MessageType&>()))>>
	final :
	std::integral_constant<
		bool,
		std::is_same<decltype(GetMessageNameId(std::declval<const MessageType&>())), FNameId>::value
			&& std::is_same<decltype(EncodeMessagePayload(std::declval<const MessageType&>(), std::declval<FMessageWriter&>())), EMessagingResult>::
				value
			&& std::is_same<decltype(DecodeMessagePayload(std::declval<FMessageReader&>(), std::declval<MessageType&>())), EMessagingResult>::value
			&& noexcept(GetMessageNameId(std::declval<const MessageType&>()))
			&& noexcept(EncodeMessagePayload(std::declval<const MessageType&>(), std::declval<FMessageWriter&>()))
			&& noexcept(DecodeMessagePayload(std::declval<FMessageReader&>(), std::declval<MessageType&>()))>
{
};

/**
 * Motivation: Decodes a named generic message into one typed local candidate so malformed input cannot partially alter caller state.
 * Responsibilities: Find the codec by ADL, require its exact name and complete payload consumption, and assign OutMessage only on success.
 * Example: DecodeTypedMessage(Message, ProtocolMessage);
 */
template<typename MessageType>
EMessagingResult DecodeTypedMessage(const FMessage& InMessage, MessageType& OutMessage) noexcept
{
	static_assert(TIsTypedMessageCodec<MessageType>::value, "Typed messages must provide the Messaging ADL codec contract.");

	MessageType Candidate{};
	if (InMessage.GetMessageNameId() != GetMessageNameId(Candidate))
	{
		return EMessagingResult::Invalid;
	}

	FMessageReader Reader(InMessage.GetPayload());
	if (DecodeMessagePayload(Reader, Candidate) != EMessagingResult::Success || Reader.Remaining() != 0)
	{
		return EMessagingResult::Invalid;
	}

	OutMessage = std::move(Candidate);
	return EMessagingResult::Success;
}

} // namespace MicroWorld::Messaging
