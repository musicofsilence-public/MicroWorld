#pragma once

#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Core/PlaySystem.h>
#include <MicroWorld/Core/Time.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace MicroWorld::Messaging
{

/**
 * Motivation: Routes actor messages between handlers and channels, so actors hold one IMessageRouter& instead of
 *   touching transports and TEngine pumps it like any play system.
 * Responsibilities: Deliver queued inbound messages to matching handlers in PreAdvance, hand queued outbound messages
 *   to their channels in PostAdvance, and keep non-copyable handler callbacks and slot identity behind a fixed
 *   application-owned lifetime.
 * Example:
 *   TMessageRouter<8, 16, 64, 4> Router;
 *   Router.AddMessageHandler(TypeId, Actor, std::move(Handler), Handle);
 *   Router.SendMessageToActor(LocalChannelId, TypeId, TargetActor, SenderActor, Payload);
 */
template<std::size_t MaxHandlers, std::size_t MaxQueuedMessages, std::size_t MaxMessageBytes, std::size_t MaxChannels>
class TMessageRouter final : public IMessageRouter, public Core::IPlaySystem
{
	static_assert(MaxHandlers < FMessageHandlerHandle::InvalidIndex, "A message router's handler capacity must fit below the reserved handle index.");
	static_assert(MaxMessageBytes >= ActorMessageHeaderBytes, "A message router's per-message byte budget must be able to hold at least a header.");

public:
	/**
	 * Motivation: Gives the router a clean starting state a caller can construct directly.
	 * Responsibilities: Produce a router with no registered handlers, channels, or queued messages.
	 */
	TMessageRouter() noexcept = default;

	/**
	 * Motivation: Stops copy construction from duplicating uniquely owned inline handler callbacks and slot identity.
	 * Responsibilities: Reject copy construction outright so the router stays the single owner of its slots.
	 */
	TMessageRouter(const TMessageRouter&) = delete;

	/**
	 * Motivation: Stops copy assignment from duplicating uniquely owned callback and slot identity.
	 * Responsibilities: Reject copy assignment outright so the router stays the single owner of its slots.
	 */
	TMessageRouter& operator=(const TMessageRouter&) = delete;

	/**
	 * Motivation: Keeps the router at one deliberately simple application-owned lifetime and identity, matching
	 *   TTimerManager, since actors hold it as IMessageRouter& and TEngine pumps it as IPlaySystem*.
	 * Responsibilities: Reject move construction outright so handles and references are never carried across a
	 *   relocation the language would not mechanically rewrite.
	 */
	TMessageRouter(TMessageRouter&&) = delete;

	/**
	 * Motivation: Stops move assignment for the same application-owned lifetime and identity reason as the deleted move ctor.
	 * Responsibilities: Reject move assignment outright so the router keeps one fixed identity.
	 */
	TMessageRouter& operator=(TMessageRouter&&) = delete;

	/**
	 * Motivation: Lets an actor register one callback for a message type it wants to receive.
	 * Responsibilities: Reject an unbound Handler as InvalidHandler, a full handler table as CapacityExceeded, and any
	 *   mutation during a dispatch pass as DispatchLocked, leaving Handler bound and OutHandle cleared on failure; on
	 *   success publish a fresh generation-checked handle.
	 */
	EMessageResult AddMessageHandler(
		const FMessageTypeId InMessageTypeId,
		const FMessageActorId InListenerActorId,
		FMessageHandlerBinding&& InHandler,
		FMessageHandlerHandle& OutHandle) noexcept override
	{
		OutHandle = {};
		if (bDispatchActive)
		{
			return EMessageResult::DispatchLocked;
		}
		if (!InHandler.IsBound())
		{
			return EMessageResult::InvalidHandler;
		}

		FHandlerSlot* const AvailableSlot = FindAvailableHandlerSlot();
		if (AvailableSlot == nullptr)
		{
			return EMessageResult::CapacityExceeded;
		}

		const std::size_t SlotIndex = static_cast<std::size_t>(AvailableSlot - HandlerSlots);
		AvailableSlot->TypeId = InMessageTypeId;
		AvailableSlot->ListenerActorId = InListenerActorId;
		AvailableSlot->Delegate = std::move(InHandler);
		AvailableSlot->bActive = true;

		const FMessageHandlerHandle PublishedHandle{static_cast<std::uint16_t>(SlotIndex), AvailableSlot->Generation};
		HandlerOrder[ActiveHandlerCount] = PublishedHandle;
		++ActiveHandlerCount;
		OutHandle = PublishedHandle;
		return EMessageResult::Success;
	}

	/**
	 * Motivation: Lets an actor retire one callback it no longer needs.
	 * Responsibilities: Reject a structurally invalid handle as InvalidHandler, a handle whose slot is free or holds
	 *   another generation as StaleHandle, and any mutation during a dispatch pass as DispatchLocked; on success remove
	 *   exactly that registration and advance its slot identity.
	 */
	EMessageResult RemoveMessageHandler(const FMessageHandlerHandle InHandle) noexcept override
	{
		if (bDispatchActive)
		{
			return EMessageResult::DispatchLocked;
		}
		if (!InHandle.IsValid() || static_cast<std::size_t>(InHandle.Index) >= MaxHandlers)
		{
			return EMessageResult::InvalidHandler;
		}

		FHandlerSlot& Slot = HandlerSlots[InHandle.Index];
		if (!Slot.bActive || Slot.Generation != InHandle.Generation)
		{
			return EMessageResult::StaleHandle;
		}

		Slot.Delegate.Reset();
		Slot.bActive = false;
		AdvanceHandlerGenerationOrRetire(Slot);
		RemoveHandlerFromOrder(InHandle);
		--ActiveHandlerCount;
		return EMessageResult::Success;
	}

	/**
	 * Motivation: Lets an actor queue one message for a specific target actor on a channel for later delivery or send.
	 * Responsibilities: Validate in order as InvalidType, InvalidChannel, PayloadTooLarge, then CapacityExceeded, encode
	 *   once into the outbound queue's tail entry, and enqueue, leaving the queue exactly as it was on any rejection.
	 */
	EMessageResult SendMessageToActor(
		const FMessageChannelId InChannelId,
		const FMessageTypeId InMessageTypeId,
		const FMessageActorId InTargetActorId,
		const FMessageActorId InSenderActorId,
		const Core::TSpan<const std::uint8_t> InPayload) noexcept override
	{
		if (InMessageTypeId == 0)
		{
			return EMessageResult::InvalidType;
		}

		IMessageChannel* WiredChannel = nullptr;
		if (InChannelId != LocalChannelId)
		{
			WiredChannel = FindChannel(InChannelId);
			if (WiredChannel == nullptr)
			{
				return EMessageResult::InvalidChannel;
			}
		}

		const std::size_t EncodedSize = ActorMessageHeaderBytes + InPayload.Size();
		if (EncodedSize > MaxMessageBytes)
		{
			return EMessageResult::PayloadTooLarge;
		}
		if (ExceedsWiredChannelCapacity(WiredChannel, EncodedSize))
		{
			return EMessageResult::PayloadTooLarge;
		}
		if (OutboundCount >= MaxQueuedMessages)
		{
			return EMessageResult::CapacityExceeded;
		}

		const FActorMessageHeader Header{InMessageTypeId, InTargetActorId, InSenderActorId};
		FQueuedMessage& TailEntry = OutboundEntries[OutboundTailIndex];
		std::size_t WrittenBytes = 0;
		const EMessageResult EncodeResult =
			EncodeActorMessage(Header, InPayload, Core::TSpan<std::uint8_t>(TailEntry.Bytes, MaxMessageBytes), WrittenBytes);
		if (EncodeResult != EMessageResult::Success)
		{
			// Unreachable given the size checks above, kept only so a future change to
			// EncodeActorMessage's contract cannot silently corrupt the outbound queue.
			return EncodeResult;
		}

		TailEntry.ChannelId = InChannelId;
		TailEntry.LengthBytes = static_cast<std::uint16_t>(WrittenBytes);
		OutboundTailIndex = (OutboundTailIndex + 1) % MaxQueuedMessages;
		++OutboundCount;
		return EMessageResult::Success;
	}

	/**
	 * Motivation: Lets an actor queue one message for every subscriber of a type without naming each target.
	 * Responsibilities: Delegate to SendMessageToActor with BroadcastActorId as the target, inheriting its validation
	 *   and transactional enqueue behavior.
	 */
	EMessageResult BroadcastMessage(
		const FMessageChannelId InChannelId,
		const FMessageTypeId InMessageTypeId,
		const FMessageActorId InSenderActorId,
		const Core::TSpan<const std::uint8_t> InPayload) noexcept override
	{
		return SendMessageToActor(InChannelId, InMessageTypeId, BroadcastActorId, InSenderActorId, InPayload);
	}

	/**
	 * Motivation: Lets a channel hand one inbound encoded message to the router for later delivery in PreAdvance.
	 * Responsibilities: Reject a length outside [ActorMessageHeaderBytes, MaxMessageBytes] as PayloadTooLarge; on a full
	 *   inbound queue increment DroppedInboundCount and return CapacityExceeded while leaving the queue unchanged.
	 */
	EMessageResult ReceiveEncodedMessage(
		const FMessageChannelId InArrivedOnChannelId, const Core::TSpan<const std::uint8_t> InEncoded) noexcept override
	{
		if (!IsEncodedSizeAccepted(InEncoded.Size()))
		{
			return EMessageResult::PayloadTooLarge;
		}

		if (!EnqueueRawMessage(InboundEntries, InboundTailIndex, InboundCount, InArrivedOnChannelId, InEncoded.Data(), InEncoded.Size()))
		{
			++DroppedInboundMessageCount;
			return EMessageResult::CapacityExceeded;
		}
		return EMessageResult::Success;
	}

	/**
	 * Motivation: Lets TEngine deliver queued inbound messages to matching handlers as one framed pump step.
	 * Responsibilities: Deliver exactly the messages queued at entry, oldest first, and lock handler mutation for the
	 *   whole pass so messages enqueued during it (including from a handler's own send) wait for the next PreAdvance.
	 */
	void PreAdvance(const Core::TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		(void)InNowMilliseconds; // The router orders delivery by queue position, not by wall-clock time.

		const std::size_t MessagesToDeliver = InboundCount;
		bDispatchActive = true;
		for (std::size_t Index = 0; Index < MessagesToDeliver; ++Index)
		{
			DispatchOneQueuedInboundMessage();
		}
		bDispatchActive = false;
	}

	/**
	 * Motivation: Lets TEngine flush queued outbound messages to their channels or back to the inbound queue as one
	 *   framed pump step.
	 * Responsibilities: Drain from the head in order, moving a LocalChannelId entry to the inbound queue and handing a
	 *   wired entry to its channel, and stop on any failure so a stalled channel also holds back every later entry
	 *   queued for a different channel.
	 */
	void PostAdvance(const Core::TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		(void)InNowMilliseconds; // Flushing drains whatever is queued; it does not itself schedule by time.

		while (OutboundCount > 0)
		{
			const FQueuedMessage& HeadEntry = OutboundEntries[OutboundHeadIndex];
			if (HeadEntry.ChannelId == LocalChannelId)
			{
				const bool bMovedToInbound =
					EnqueueRawMessage(InboundEntries, InboundTailIndex, InboundCount, LocalChannelId, HeadEntry.Bytes, HeadEntry.LengthBytes);
				if (!bMovedToInbound)
				{
					break;
				}
			}
			else
			{
				IMessageChannel* const Channel = FindChannel(HeadEntry.ChannelId);
				if (Channel == nullptr)
				{
					// No API removes a configured channel, so this is unreachable in practice;
					// treated the same as a failed send so a future change cannot drop the message.
					break;
				}
				const EMessageResult SendResult =
					Channel->TrySendEncodedMessage(Core::TSpan<const std::uint8_t>(HeadEntry.Bytes, HeadEntry.LengthBytes));
				if (SendResult != EMessageResult::Success)
				{
					break;
				}
			}

			OutboundHeadIndex = (OutboundHeadIndex + 1) % MaxQueuedMessages;
			--OutboundCount;
		}
	}

	/**
	 * Motivation: Lets a caller register one outbound wired channel so the router can route sends to it.
	 * Responsibilities: Reject LocalChannelId, a duplicate id, and a full channel table before storing the channel in
	 *   the first free slot.
	 */
	EMessageResult AddChannel(IMessageChannel& InChannel) noexcept
	{
		const FMessageChannelId ChannelId = InChannel.GetChannelId();
		if (ChannelId == LocalChannelId)
		{
			return EMessageResult::InvalidChannel;
		}
		if (FindChannel(ChannelId) != nullptr)
		{
			return EMessageResult::Duplicate;
		}

		FChannelSlot* const AvailableSlot = FindAvailableChannelSlot();
		if (AvailableSlot == nullptr)
		{
			return EMessageResult::CapacityExceeded;
		}

		AvailableSlot->ChannelId = ChannelId;
		AvailableSlot->ChannelPtr = &InChannel;
		AvailableSlot->bOccupied = true;
		return EMessageResult::Success;
	}

	/**
	 * Motivation: Lets a caller observe how many messages the next PreAdvance pass will deliver.
	 * Responsibilities: Report the exact current inbound queue occupancy.
	 */
	std::size_t QueuedInboundCount() const noexcept { return InboundCount; }

	/**
	 * Motivation: Lets a caller observe how many messages the next PostAdvance pass will send or deliver locally.
	 * Responsibilities: Report the exact current outbound queue occupancy.
	 */
	std::size_t QueuedOutboundCount() const noexcept { return OutboundCount; }

	/**
	 * Motivation: Lets a caller observe the number of handlers the next PreAdvance pass may match against.
	 * Responsibilities: Report the exact count of currently registered handlers.
	 */
	std::size_t HandlerCount() const noexcept { return ActiveHandlerCount; }

	/**
	 * Motivation: Lets a caller observe inbound pressure from a congested source.
	 * Responsibilities: Report how many ReceiveEncodedMessage calls were rejected because the inbound queue was full.
	 */
	std::uint32_t DroppedInboundCount() const noexcept { return DroppedInboundMessageCount; }

private:
	/**
	 * Motivation: Owns one registered message handler's match keys and callback in one reusable slot.
	 * Responsibilities: Hold the type id, listener actor id, delegate, and generation-checked identity that decide
	 *   whether a message reaches this handler.
	 * Example:
	 *   FHandlerSlot Slot;
	 *   Slot.TypeId = TypeId; Slot.Delegate = std::move(Handler); Slot.bActive = true;
	 */
	struct FHandlerSlot final
	{
		/** Motivation: Holds the message type this slot's delegate is invoked for while active. */
		FMessageTypeId TypeId{0};

		/** Motivation: Holds the actor id this slot matches against a targeted message's TargetActorId. */
		FMessageActorId ListenerActorId{BroadcastActorId};

		/** Motivation: Holds the callback invoked for every message that matches this slot while active. */
		FMessageHandlerBinding Delegate;

		/** Motivation: Distinguishes successive registrations that occupy this slot. */
		std::uint32_t Generation{1};

		/** Motivation: Distinguishes a live handler from reusable unoccupied slot state. */
		bool bActive{false};

		/** Motivation: Permanently removes this slot once its generation space is exhausted. */
		bool bRetired{false};
	};

	/**
	 * Motivation: Holds one queued message copied into fixed-size storage for later local delivery or channel send.
	 * Responsibilities: Store the channel id, encoded length, and bytes for one message without referencing caller storage.
	 * Example:
	 *   FQueuedMessage Entry; Entry.ChannelId = LocalChannelId; Entry.LengthBytes = Written;
	 */
	struct FQueuedMessage final
	{
		/** Motivation: Holds the channel this message arrived on (inbound) or is destined for (outbound). */
		FMessageChannelId ChannelId{LocalChannelId};

		/** Motivation: Holds the count of valid encoded bytes at the front of Bytes. */
		std::uint16_t LengthBytes{0};

		/** Motivation: Provides the fixed backing storage for one encoded actor message. */
		std::uint8_t Bytes[MaxMessageBytes == 0 ? 1 : MaxMessageBytes]{};
	};

	/**
	 * Motivation: Holds one configured outbound channel binding keyed by the channel's own id.
	 * Responsibilities: Store the channel id and pointer together with an occupancy flag so lookups and reuse stay simple.
	 * Example:
	 *   FChannelSlot Slot; Slot.ChannelId = Id; Slot.ChannelPtr = &Channel; Slot.bOccupied = true;
	 */
	struct FChannelSlot final
	{
		/** Motivation: Holds the configured channel's id, meaningful only while bOccupied is true. */
		FMessageChannelId ChannelId{LocalChannelId};

		/** Motivation: Holds the externally owned channel this slot forwards to. */
		IMessageChannel* ChannelPtr{nullptr};

		/** Motivation: Distinguishes a configured channel from reusable unoccupied slot state. */
		bool bOccupied{false};
	};

	/**
	 * Motivation: Lets AddMessageHandler claim the next free slot without disturbing insertion order.
	 * Responsibilities: Return the lowest unoccupied, unretired handler slot, or null when none remains.
	 */
	FHandlerSlot* FindAvailableHandlerSlot() noexcept
	{
		for (std::size_t Index = 0; Index < MaxHandlers; ++Index)
		{
			FHandlerSlot& Slot = HandlerSlots[Index];
			if (!Slot.bActive && !Slot.bRetired)
			{
				return &Slot;
			}
		}
		return nullptr;
	}

	/**
	 * Motivation: Keeps a reused handler slot from matching an old handle as its generation approaches wrap.
	 * Responsibilities: Advance the generation, or permanently retire the slot before it can wrap.
	 */
	static void AdvanceHandlerGenerationOrRetire(FHandlerSlot& Slot) noexcept
	{
		if (Slot.Generation == std::numeric_limits<std::uint32_t>::max())
		{
			Slot.bRetired = true;
			return;
		}
		++Slot.Generation;
	}

	/**
	 * Motivation: Lets RemoveMessageHandler close the gap a removal leaves in insertion order.
	 * Responsibilities: Shift later handler-order entries down without changing any remaining slot identity or
	 *   relative order.
	 */
	void RemoveHandlerFromOrder(const FMessageHandlerHandle InRemovedHandle) noexcept
	{
		std::size_t OrderIndex = ActiveHandlerCount;
		for (std::size_t SearchIndex = 0; SearchIndex < ActiveHandlerCount; ++SearchIndex)
		{
			if (HandlerOrder[SearchIndex] == InRemovedHandle)
			{
				OrderIndex = SearchIndex;
				break;
			}
		}
		if (OrderIndex == ActiveHandlerCount)
		{
			return;
		}
		for (std::size_t ShiftIndex = OrderIndex; ShiftIndex + 1U < ActiveHandlerCount; ++ShiftIndex)
		{
			HandlerOrder[ShiftIndex] = HandlerOrder[ShiftIndex + 1U];
		}
		HandlerOrder[ActiveHandlerCount - 1U] = {};
	}

	/**
	 * Motivation: Lets PreAdvance drive one queued inbound message through decode and delivery.
	 * Responsibilities: Decode the inbound head, dispatch it to matching handlers if it is well-formed, and pop it
	 *   regardless, silently dropping a message whose bytes leave no usable header to match against.
	 */
	void DispatchOneQueuedInboundMessage() noexcept
	{
		const FQueuedMessage& HeadEntry = InboundEntries[InboundHeadIndex];
		FActorMessageHeader Header{};
		Core::TSpan<const std::uint8_t> Payload;
		const EMessageResult DecodeResult =
			DecodeActorMessage(Core::TSpan<const std::uint8_t>(HeadEntry.Bytes, HeadEntry.LengthBytes), Header, Payload);
		if (DecodeResult == EMessageResult::Success)
		{
			InvokeMatchingHandlers(FMessageView{Header, HeadEntry.ChannelId, Payload});
		}

		InboundHeadIndex = (InboundHeadIndex + 1) % MaxQueuedMessages;
		--InboundCount;
	}

	/**
	 * Motivation: Lets PreAdvance reach every handler interested in one delivered message.
	 * Responsibilities: Invoke each handler whose TypeId matches the view in registration order, applying a broadcast
	 *   target to every match and any other target only to the handler whose ListenerActorId equals it.
	 */
	void InvokeMatchingHandlers(const FMessageView& InView) noexcept
	{
		for (std::size_t OrderIndex = 0; OrderIndex < ActiveHandlerCount; ++OrderIndex)
		{
			const FMessageHandlerHandle Handle = HandlerOrder[OrderIndex];
			FHandlerSlot& Slot = HandlerSlots[Handle.Index];
			if (Slot.TypeId != InView.Header.MessageTypeId)
			{
				continue;
			}
			if (InView.Header.TargetActorId != BroadcastActorId && Slot.ListenerActorId != InView.Header.TargetActorId)
			{
				continue;
			}
			(void)Slot.Delegate.Execute(InView);
		}
	}

	/**
	 * Motivation: Lets ReceiveEncodedMessage and send validation agree on one accepted length window.
	 * Responsibilities: Report whether a length lies within [ActorMessageHeaderBytes, MaxMessageBytes].
	 */
	static bool IsEncodedSizeAccepted(const std::size_t InEncodedSize) noexcept
	{
		return InEncodedSize >= ActorMessageHeaderBytes && InEncodedSize <= MaxMessageBytes;
	}

	/**
	 * Motivation: Lets SendMessageToActor reject a payload before queuing that a wired channel could not send.
	 * Responsibilities: Report whether a non-null channel exists and rejects the encoded size as too large.
	 */
	static bool ExceedsWiredChannelCapacity(const IMessageChannel* const InChannel, const std::size_t InEncodedSize) noexcept
	{
		return InChannel != nullptr && InEncodedSize > InChannel->MaxEncodedMessageBytes();
	}

	/**
	 * Motivation: Lets send and flush look up the wired channel registered under one id.
	 * Responsibilities: Return the channel pointer for the occupied matching slot, or null when none is configured.
	 */
	IMessageChannel* FindChannel(const FMessageChannelId InChannelId) noexcept
	{
		for (std::size_t Index = 0; Index < MaxChannels; ++Index)
		{
			FChannelSlot& Slot = ChannelSlots[Index];
			if (Slot.bOccupied && Slot.ChannelId == InChannelId)
			{
				return Slot.ChannelPtr;
			}
		}
		return nullptr;
	}

	/**
	 * Motivation: Lets AddChannel claim the next free channel slot.
	 * Responsibilities: Return the lowest unoccupied channel slot, or null when the table is full.
	 */
	FChannelSlot* FindAvailableChannelSlot() noexcept
	{
		for (std::size_t Index = 0; Index < MaxChannels; ++Index)
		{
			if (!ChannelSlots[Index].bOccupied)
			{
				return &ChannelSlots[Index];
			}
		}
		return nullptr;
	}

	/**
	 * Motivation: Lets inbound receive and local outbound flush share one bounded ring-append step.
	 * Responsibilities: Copy ChannelId, Length, and Bytes into the tail entry, advance the tail index and count, and
	 *   return false without touching the queue when it is already full.
	 */
	bool EnqueueRawMessage(
		FQueuedMessage (&InOutEntries)[MaxQueuedMessages == 0 ? 1 : MaxQueuedMessages],
		std::size_t& InOutTailIndex,
		std::size_t& InOutCount,
		const FMessageChannelId InChannelId,
		const std::uint8_t* const InBytes,
		const std::size_t InLength) noexcept
	{
		if (InOutCount >= MaxQueuedMessages)
		{
			return false;
		}

		FQueuedMessage& TailEntry = InOutEntries[InOutTailIndex];
		TailEntry.ChannelId = InChannelId;
		TailEntry.LengthBytes = static_cast<std::uint16_t>(InLength);
		for (std::size_t Index = 0; Index < InLength; ++Index)
		{
			TailEntry.Bytes[Index] = InBytes[Index];
		}

		InOutTailIndex = (InOutTailIndex + 1) % MaxQueuedMessages;
		++InOutCount;
		return true;
	}

	// C++ forbids zero-length arrays; the "== 0 ? 1" guard on the arrays below keeps a
	// zero-capacity router (MaxHandlers/MaxQueuedMessages/MaxChannels == 0) well-formed.

	/** Motivation: Owns all bounded handler callback storage independently of registration order. */
	FHandlerSlot HandlerSlots[MaxHandlers == 0 ? 1 : MaxHandlers];

	/** Motivation: Preserves deterministic registration order while handler slots are removed and reused. */
	FMessageHandlerHandle HandlerOrder[MaxHandlers == 0 ? 1 : MaxHandlers];

	/** Motivation: Bounds HandlerOrder traversal and makes the current handler count observable. */
	std::size_t ActiveHandlerCount{0};

	/** Motivation: Rejects AddMessageHandler and RemoveMessageHandler while a PreAdvance pass is active. */
	bool bDispatchActive{false};

	/** Motivation: Backing ring storage for messages awaiting the next PreAdvance. */
	FQueuedMessage InboundEntries[MaxQueuedMessages == 0 ? 1 : MaxQueuedMessages];

	/** Motivation: Indexes the next inbound message PreAdvance will deliver. */
	std::size_t InboundHeadIndex{0};

	/** Motivation: Indexes the next free inbound slot so enqueues append without overwriting the head. */
	std::size_t InboundTailIndex{0};

	/** Motivation: Tracks inbound occupancy so full and empty states are observable without wrap arithmetic. */
	std::size_t InboundCount{0};

	/** Motivation: Counts ReceiveEncodedMessage calls rejected because the inbound queue was full. */
	std::uint32_t DroppedInboundMessageCount{0};

	/** Motivation: Backing ring storage for messages awaiting the next PostAdvance. */
	FQueuedMessage OutboundEntries[MaxQueuedMessages == 0 ? 1 : MaxQueuedMessages];

	/** Motivation: Indexes the next outbound message PostAdvance will attempt to send or deliver locally. */
	std::size_t OutboundHeadIndex{0};

	/** Motivation: Indexes the next free outbound slot so enqueues append without overwriting the head. */
	std::size_t OutboundTailIndex{0};

	/** Motivation: Tracks outbound occupancy so full and empty states are observable without wrap arithmetic. */
	std::size_t OutboundCount{0};

	/** Motivation: Owns the configured wired-channel bindings, keyed by each channel's own id. */
	FChannelSlot ChannelSlots[MaxChannels == 0 ? 1 : MaxChannels];
};

} // namespace MicroWorld::Messaging
