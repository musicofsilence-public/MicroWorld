#pragma once

#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/EngineSystem.h>
#include <MicroWorld/Time.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace MicroWorld
{

/**
 * Routes actor messages between handlers and channels.
 * Implements IEngineSystem so TEngine pumps it like any net frame:
 * PreAdvance delivers queued inbound messages to matching handlers;
 * PostAdvance hands queued outbound messages to their channels.
 */
template<std::size_t MaxHandlers, std::size_t MaxQueuedMessages, std::size_t MaxMessageBytes, std::size_t MaxChannels>
class TMessageRouter final : public IMessageRouter, public IEngineSystem
{
	static_assert(MaxHandlers < FMessageHandlerHandle::InvalidIndex, "A message router's handler capacity must fit below the reserved handle index.");
	static_assert(MaxMessageBytes >= ActorMessageHeaderBytes, "A message router's per-message byte budget must be able to hold at least a header.");

public:
	/** Creates a router with no registered handlers, channels, or queued messages. */
	TMessageRouter() noexcept = default;

	/** Prevents copying: the router uniquely owns non-copyable inline handler callbacks and slot identity. */
	TMessageRouter(const TMessageRouter&) = delete;

	/** Prevents copy assignment: it would duplicate uniquely owned callback and slot identity. */
	TMessageRouter& operator=(const TMessageRouter&) = delete;

	/**
	 * Prevents moving so the router keeps one deliberately simple application-owned lifetime and
	 * identity. Actors hold it as IMessageRouter& and TEngine pumps it as IEngineSystem*, and
	 * relocation would not mechanically rewrite those references; forbidding move keeps the ownership
	 * boundary explicit, matching TTimerManager.
	 */
	TMessageRouter(TMessageRouter&&) = delete;

	/** Prevents move assignment for the same application-owned lifetime/identity reason as the deleted move ctor. */
	TMessageRouter& operator=(TMessageRouter&&) = delete;

	/**
	 * Registers a callback for one message type.
	 *
	 * Rejects an unbound Handler as InvalidHandler and a full handler table as
	 * CapacityExceeded, both leaving Handler bound to the caller and OutHandle
	 * cleared; rejects every mutation while a dispatch pass is active as DispatchLocked.
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
	 * Removes one previously registered callback.
	 *
	 * Rejects a structurally invalid handle as InvalidHandler and a handle whose
	 * slot is free or holds another generation as StaleHandle; rejects every
	 * mutation while a dispatch pass is active as DispatchLocked.
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
	 * Queues one message for a specific target actor on the given channel.
	 *
	 * Validates in order (InvalidType, InvalidChannel, PayloadTooLarge,
	 * CapacityExceeded), encodes once into the outbound queue's tail entry, and
	 * enqueues; every rejection leaves the outbound queue exactly as it was.
	 */
	EMessageResult SendMessageToActor(
		const FMessageChannelId InChannelId,
		const FMessageTypeId InMessageTypeId,
		const FMessageActorId InTargetActorId,
		const FMessageActorId InSenderActorId,
		const TSpan<const std::uint8_t> InPayload) noexcept override
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
		if (WiredChannel != nullptr && EncodedSize > WiredChannel->MaxEncodedMessageBytes())
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
			EncodeActorMessage(Header, InPayload, TSpan<std::uint8_t>(TailEntry.Bytes, MaxMessageBytes), WrittenBytes);
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

	/** Queues one message for every subscriber of the type on the given channel, using BroadcastActorId as the target. */
	EMessageResult BroadcastMessage(
		const FMessageChannelId InChannelId,
		const FMessageTypeId InMessageTypeId,
		const FMessageActorId InSenderActorId,
		const TSpan<const std::uint8_t> InPayload) noexcept override
	{
		return SendMessageToActor(InChannelId, InMessageTypeId, BroadcastActorId, InSenderActorId, InPayload);
	}

	/**
	 * Queues one encoded message that arrived on the given channel.
	 *
	 * Rejects a length outside [ActorMessageHeaderBytes, MaxMessageBytes] as
	 * PayloadTooLarge; on a full inbound queue increments DroppedInboundCount
	 * and returns CapacityExceeded while leaving the queue unchanged.
	 */
	EMessageResult ReceiveEncodedMessage(const FMessageChannelId InArrivedOnChannelId, const TSpan<const std::uint8_t> InEncoded) noexcept override
	{
		if (InEncoded.Size() < ActorMessageHeaderBytes || InEncoded.Size() > MaxMessageBytes)
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
	 * Delivers exactly the inbound messages queued at entry, oldest first, to their matching handlers.
	 * Messages enqueued during this pass (including from a handler's own send) wait for the next PreAdvance.
	 */
	void PreAdvance(const TimePointMilliseconds InNowMilliseconds) noexcept override
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
	 * Flushes outbound messages from the head, in order: a LocalChannelId entry moves to the inbound
	 * queue; a wired entry is handed to its channel. On any failure (inbound full, or the channel
	 * returning non-Success) the head entry is retained and flushing stops for this tick, so a
	 * stalled channel also holds back every later entry queued for a different channel (accepted v1
	 * head-of-line behavior, matching TNetManager::AdvanceSend's retained-head discipline).
	 */
	void PostAdvance(const TimePointMilliseconds InNowMilliseconds) noexcept override
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
				const EMessageResult SendResult = Channel->TrySendEncodedMessage(TSpan<const std::uint8_t>(HeadEntry.Bytes, HeadEntry.LengthBytes));
				if (SendResult != EMessageResult::Success)
				{
					break;
				}
			}

			OutboundHeadIndex = (OutboundHeadIndex + 1) % MaxQueuedMessages;
			--OutboundCount;
		}
	}

	/** Registers one outbound channel under its id; rejects LocalChannelId, a duplicate id, and a full channel table. */
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

	/** Reports how many messages are currently queued for PreAdvance to deliver. */
	std::size_t QueuedInboundCount() const noexcept { return InboundCount; }

	/** Reports how many messages are currently queued for PostAdvance to send or deliver locally. */
	std::size_t QueuedOutboundCount() const noexcept { return OutboundCount; }

	/** Reports the exact number of handlers that the next PreAdvance pass may match against. */
	std::size_t HandlerCount() const noexcept { return ActiveHandlerCount; }

	/** Reports how many ReceiveEncodedMessage calls were rejected because the inbound queue was full. */
	std::uint32_t DroppedInboundCount() const noexcept { return DroppedInboundMessageCount; }

private:
	/** One registered message handler slot, matched by type and optionally by listener actor. */
	struct FHandlerSlot final
	{
		/** Message type this slot's delegate is invoked for while active. */
		FMessageTypeId TypeId{0};

		/** Actor id this slot matches against a targeted message's TargetActorId; BroadcastActorId means broadcasts only. */
		FMessageActorId ListenerActorId{BroadcastActorId};

		/** Callback invoked for every message that matches this slot while active. */
		FMessageHandlerBinding Delegate;

		/** Distinguishes successive registrations that occupy this slot. */
		std::uint32_t Generation{1};

		/** Distinguishes a live handler from reusable unoccupied slot state. */
		bool bActive{false};

		/** Permanently removes this slot once its generation space is exhausted. */
		bool bRetired{false};
	};

	/** One queued message copied into fixed-size storage for later local delivery or channel transmission. */
	struct FQueuedMessage final
	{
		/** Channel this message arrived on (inbound) or is destined for (outbound). */
		FMessageChannelId ChannelId{LocalChannelId};

		/** Number of valid encoded bytes at the front of Bytes. */
		std::uint16_t LengthBytes{0};

		/** Fixed backing storage for one encoded actor message. */
		std::uint8_t Bytes[MaxMessageBytes == 0 ? 1 : MaxMessageBytes]{};
	};

	/** One configured outbound channel binding, keyed by the channel's own id. */
	struct FChannelSlot final
	{
		/** Configured channel's id; meaningful only while bOccupied is true. */
		FMessageChannelId ChannelId{LocalChannelId};

		/** Externally owned channel this slot forwards to; never owned here. */
		IMessageChannel* ChannelPtr{nullptr};

		/** Distinguishes a configured channel from reusable unoccupied slot state. */
		bool bOccupied{false};
	};

	/** Finds the lowest reusable handler slot while insertion order remains separately recorded. */
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

	/** Advances a reusable handler slot's identity or retires it before generation wrap can cause ABA. */
	static void AdvanceHandlerGenerationOrRetire(FHandlerSlot& Slot) noexcept
	{
		if (Slot.Generation == std::numeric_limits<std::uint32_t>::max())
		{
			Slot.bRetired = true;
			return;
		}
		++Slot.Generation;
	}

	/** Compacts HandlerOrder after a removal without changing any remaining slot identity or relative order. */
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
	 * Decodes and delivers the current inbound head entry to every matching handler, then pops it.
	 * A decode failure (malformed bytes from a channel) silently drops the message: it is popped
	 * without invoking any handler, since DecodeActorMessage leaves no usable header to match against.
	 */
	void DispatchOneQueuedInboundMessage() noexcept
	{
		const FQueuedMessage& HeadEntry = InboundEntries[InboundHeadIndex];
		FActorMessageHeader Header{};
		TSpan<const std::uint8_t> Payload;
		const EMessageResult DecodeResult = DecodeActorMessage(TSpan<const std::uint8_t>(HeadEntry.Bytes, HeadEntry.LengthBytes), Header, Payload);
		if (DecodeResult == EMessageResult::Success)
		{
			InvokeMatchingHandlers(FMessageView{Header, HeadEntry.ChannelId, Payload});
		}

		InboundHeadIndex = (InboundHeadIndex + 1) % MaxQueuedMessages;
		--InboundCount;
	}

	/**
	 * Invokes every handler whose TypeId matches the view in registration order.
	 * BroadcastActorId targets every matching-type handler; any other target reaches only the
	 * handler whose ListenerActorId equals it. Add/RemoveMessageHandler are locked out for the
	 * whole pass, so HandlerOrder cannot change underneath this loop.
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

	/** Finds the channel currently registered under ChannelId, or nullptr when none is configured. */
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

	/** Finds the lowest unoccupied channel slot, or nullptr when the channel table is full. */
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
	 * Copies ChannelId, Length, and Bytes into the tail of a fixed-capacity ring queue.
	 * Returns false without touching the queue when Count has already reached MaxQueuedMessages.
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

	/** Owns all bounded handler callback storage independently of registration order. */
	FHandlerSlot HandlerSlots[MaxHandlers == 0 ? 1 : MaxHandlers];

	/** Preserves deterministic registration order while handler slots are removed and reused. */
	FMessageHandlerHandle HandlerOrder[MaxHandlers == 0 ? 1 : MaxHandlers];

	/** Bounds HandlerOrder traversal and makes the current handler count observable. */
	std::size_t ActiveHandlerCount{0};

	/** Rejects AddMessageHandler and RemoveMessageHandler while a PreAdvance pass is active. */
	bool bDispatchActive{false};

	/** Backing ring storage for messages awaiting the next PreAdvance. */
	FQueuedMessage InboundEntries[MaxQueuedMessages == 0 ? 1 : MaxQueuedMessages];

	/** Indexes the next inbound message PreAdvance will deliver. */
	std::size_t InboundHeadIndex{0};

	/** Indexes the next free inbound slot so enqueues append without overwriting the head. */
	std::size_t InboundTailIndex{0};

	/** Tracks inbound occupancy so full and empty states are observable without wrap arithmetic. */
	std::size_t InboundCount{0};

	/** Counts ReceiveEncodedMessage calls rejected because the inbound queue was full. */
	std::uint32_t DroppedInboundMessageCount{0};

	/** Backing ring storage for messages awaiting the next PostAdvance. */
	FQueuedMessage OutboundEntries[MaxQueuedMessages == 0 ? 1 : MaxQueuedMessages];

	/** Indexes the next outbound message PostAdvance will attempt to send or deliver locally. */
	std::size_t OutboundHeadIndex{0};

	/** Indexes the next free outbound slot so enqueues append without overwriting the head. */
	std::size_t OutboundTailIndex{0};

	/** Tracks outbound occupancy so full and empty states are observable without wrap arithmetic. */
	std::size_t OutboundCount{0};

	/** Owns the configured wired-channel bindings, keyed by each channel's own id. */
	FChannelSlot ChannelSlots[MaxChannels == 0 ? 1 : MaxChannels];
};

} // namespace MicroWorld
