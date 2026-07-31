#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Core/PlaySystem.h>
#include <MicroWorld/Core/Time.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Messaging
{

/** Encoded byte count of the reliable wrapper's own header prefix: one Kind byte plus a little-endian u16 sequence. */
inline constexpr std::size_t ReliableHeaderBytes = 3;

/** Half of the 16-bit sequence space; the threshold that separates "newer" from "older" after wrap. */
inline constexpr std::uint16_t HalfSequenceSpace = 0x8000u;

/** First sequence number ever sent; 0 is reserved as "never sent" so allocations start here. */
inline constexpr std::uint16_t FirstOutgoingSequence = 1;

/** Byte offset of the little-endian sequence field within a reliable header. */
inline constexpr std::size_t ReliableSequenceFieldByteIndex = 1;

/** Byte offset of the Kind byte within a reliable header. */
inline constexpr std::size_t ReliableKindByteIndex = 0;

/** Distinguishes an original wrapped message from a bare acknowledgement on the reliable wire format. */
enum class EReliablePacketKind : std::uint8_t
{
	/** A wrapped original message awaiting acknowledgement: [Data][Sequence][original encoded message]. */
	Data = 1,

	/** A bare acknowledgement of one received sequence: [Acknowledgement][Sequence], no payload. */
	Acknowledgement = 2,
};

/** Retry/acknowledgement settings for one guaranteed channel. */
struct FReliableChannelConfig
{
	/** Minimum wall-clock gap between successive resend attempts for one unacknowledged message. */
	DurationMilliseconds RetryIntervalMilliseconds{250};

	/** Total send attempts (the initial send plus retries) before a message is abandoned. */
	std::uint8_t MaxSendAttempts{8};
};

/**
 * Guaranteed-delivery wrapper around one IMessageChannel (roadmap D6-D8).
 * Sits between a channel binding and the router in both directions: outbound it prefixes
 * [Kind][Sequence] and keeps a copy until acknowledged; inbound it acknowledges data, drops
 * duplicates via a serial-number window, and forwards fresh payloads to ForwardSink.
 * Implements IPlaySystem so PostAdvance resends due unacknowledged messages; point-to-point only.
 */
template<std::size_t MaxPendingMessages, std::size_t MaxMessageBytes>
class TReliableChannel final : public IMessageChannel, public IEncodedMessageSink, public IPlaySystem
{
public:
	/** Stores the forward sink and retry configuration; the inner channel is bound later via SetInnerChannel. */
	TReliableChannel(IEncodedMessageSink& InForwardSink, const FReliableChannelConfig InConfig) noexcept
		: ForwardSink(InForwardSink), Config(InConfig)
	{
	}

	/** Virtual destructor via the bases; this wrapper owns no external resource. */
	~TReliableChannel() noexcept override = default;

	// Held by reference at a fixed composition root (the wrapper<->binding cycle breaker, see
	// SetInnerChannel) and captured by pointer in a frame set, matching TMessageRouter's fixed-identity rule.
	TReliableChannel(const TReliableChannel&) = delete;
	TReliableChannel& operator=(const TReliableChannel&) = delete;
	TReliableChannel(TReliableChannel&&) = delete;
	TReliableChannel& operator=(TReliableChannel&&) = delete;

	/**
	 * Binds the wrapped channel once at composition, breaking the wrapper<->binding reference cycle
	 * (the binding's constructor needs this wrapper as its sink, so this wrapper cannot take the
	 * binding in its own constructor). Call before Router.AddChannel(*this): GetChannelId needs the inner id.
	 */
	void SetInnerChannel(IMessageChannel& InInnerChannel) noexcept { InnerChannel = &InInnerChannel; }

	/** Returns the inner channel's id, or LocalChannelId before SetInnerChannel has been called. */
	FMessageChannelId GetChannelId() const noexcept override { return InnerChannel != nullptr ? InnerChannel->GetChannelId() : LocalChannelId; }

	/** Returns the inner channel's budget minus ReliableHeaderBytes (floored at 0), or 0 before SetInnerChannel has been called. */
	std::size_t MaxEncodedMessageBytes() const noexcept override
	{
		if (InnerChannel == nullptr)
		{
			return 0;
		}
		const std::size_t InnerBudget = InnerChannel->MaxEncodedMessageBytes();
		return InnerBudget >= ReliableHeaderBytes ? InnerBudget - ReliableHeaderBytes : 0;
	}

	/**
	 * Wraps Encoded as a Data packet, stores it pending acknowledgement, and sends it via the inner channel.
	 * Rejects transactionally (no sequence consumed, nothing sent, no state change) when the inner channel is
	 * unset (Unavailable), the wrapped size cannot fit a pending slot or the inner budget (PayloadTooLarge), or
	 * every pending slot is already in use (CapacityExceeded). Otherwise keeps the pending slot even when the
	 * initial inner send does not report Success, since PostAdvance retries it later instead of losing it.
	 */
	EMessageResult TrySendEncodedMessage(const TSpan<const std::uint8_t> InEncoded) noexcept override
	{
		if (InnerChannel == nullptr)
		{
			return EMessageResult::Unavailable;
		}
		if (ReliableHeaderBytes + InEncoded.Size() > MaxMessageBytes || InEncoded.Size() > MaxEncodedMessageBytes())
		{
			return EMessageResult::PayloadTooLarge;
		}
		FPendingMessage* const Slot = FindFreePendingSlot();
		if (Slot == nullptr)
		{
			return EMessageResult::CapacityExceeded;
		}

		const std::uint16_t Sequence = AllocateNextSequence();
		WriteReliableHeader(Slot->Bytes, EReliablePacketKind::Data, Sequence);
		CopyBytes(&Slot->Bytes[ReliableHeaderBytes], InEncoded.Data(), InEncoded.Size());
		Slot->Length = ReliableHeaderBytes + InEncoded.Size();
		Slot->Sequence = Sequence;
		Slot->Attempts = 1;
		Slot->bBaselineTimeSet = false;
		Slot->bInUse = true;

		return InnerChannel->TrySendEncodedMessage(TSpan<const std::uint8_t>(Slot->Bytes, Slot->Length));
	}

	/**
	 * Handles one inbound wire payload from the inner channel. A payload shorter than ReliableHeaderBytes or
	 * carrying an unrecognized Kind byte is rejected as PayloadTooLarge and dropped. A Data packet always
	 * triggers an ack (fresh or duplicate, since the sender's first ack may itself have been lost), then
	 * forwards a fresh payload to ForwardSink once or counts a duplicate; an Acknowledgement frees the matching pending slot.
	 */
	EMessageResult ReceiveEncodedMessage(const FMessageChannelId InArrivedOnChannelId, const TSpan<const std::uint8_t> InEncoded) noexcept override
	{
		if (InEncoded.Size() < ReliableHeaderBytes)
		{
			return EMessageResult::PayloadTooLarge;
		}

		const EReliablePacketKind Kind = static_cast<EReliablePacketKind>(InEncoded.Data()[0]);
		const std::uint16_t Sequence = ReadSequence(InEncoded.Data());

		if (Kind == EReliablePacketKind::Data)
		{
			if (InnerChannel == nullptr)
			{
				// Cannot ack without the inner channel; a correct composition always calls
				// SetInnerChannel before any inbound traffic can reach this sink, but guard anyway.
				return EMessageResult::Unavailable;
			}
			return HandleInboundData(InArrivedOnChannelId, Sequence, InEncoded);
		}
		if (Kind == EReliablePacketKind::Acknowledgement)
		{
			FreePendingBySequence(Sequence);
			return EMessageResult::Success;
		}
		return EMessageResult::PayloadTooLarge;
	}

	/** No-op: this wrapper has no inbound polling of its own; inbound arrives via ReceiveEncodedMessage. */
	void PreAdvance(const TimePointMilliseconds InNowMilliseconds) noexcept override { (void)InNowMilliseconds; }

	/**
	 * Paces retries for every pending slot: the first flush after a send only records the retry baseline
	 * (never resending that same tick), a later flush resends once RetryIntervalMilliseconds has elapsed
	 * and the slot has not exhausted Config.MaxSendAttempts, and an exhausted slot is dropped and counted lost instead.
	 */
	void PostAdvance(const TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		if (InnerChannel == nullptr)
		{
			return;
		}
		for (std::size_t Index = 0; Index < MaxPendingMessages; ++Index)
		{
			FPendingMessage& Slot = Pending[Index];
			if (Slot.bInUse)
			{
				TickOnePendingSlot(Slot, InNowMilliseconds);
			}
		}
	}

	/** Reports how many pending slots currently await acknowledgement. */
	std::size_t PendingCount() const noexcept
	{
		std::size_t Count = 0;
		for (std::size_t Index = 0; Index < MaxPendingMessages; ++Index)
		{
			if (Pending[Index].bInUse)
			{
				++Count;
			}
		}
		return Count;
	}

	/** Reports how many retry resends PostAdvance has issued so far. */
	std::uint32_t ResentCount() const noexcept { return ResentTotal; }

	/** Reports how many pending messages were abandoned after exhausting Config.MaxSendAttempts. */
	std::uint32_t LostCount() const noexcept { return LostTotal; }

	/** Reports how many inbound Data packets were recognized as duplicates and not forwarded. */
	std::uint32_t DuplicateDroppedCount() const noexcept { return DuplicateDroppedTotal; }

private:
	/** One outbound message awaiting acknowledgement: its wrapped bytes, retry bookkeeping, and occupancy. */
	struct FPendingMessage final
	{
		/** Wrapped Data packet bytes: [Kind][Sequence][original encoded message]. */
		std::uint8_t Bytes[MaxMessageBytes == 0 ? 1 : MaxMessageBytes]{};

		/** Valid byte count at the front of Bytes. */
		std::size_t Length{0};

		/** Sequence number this slot was sent under; matched against an inbound Acknowledgement. */
		std::uint16_t Sequence{0};

		/** Total send attempts so far, including the initial send (attempt 1). */
		std::uint8_t Attempts{0};

		/** Distinguishes "no retry baseline established yet" from a real LastSendTimeMilliseconds. */
		bool bBaselineTimeSet{false};

		/** Wall-clock time PostAdvance last (re)sent this slot, meaningful only once bBaselineTimeSet is true. */
		TimePointMilliseconds LastSendTimeMilliseconds{0};

		/** Distinguishes an occupied slot from reusable free storage. */
		bool bInUse{false};
	};

	/**
	 * Serial-number "is newer" comparison over the 16-bit sequence space (roadmap 4.3, normative):
	 * 0x8000 is half that space, so the smaller forward distance between InCandidate and InReference decides which is newer.
	 */
	static bool IsNewer(const std::uint16_t InCandidate, const std::uint16_t InReference) noexcept
	{
		return (InCandidate != InReference) && (static_cast<std::uint16_t>(InCandidate - InReference) < HalfSequenceSpace);
	}

	/** Width of the duplicate-detection window: SeenMask's bit count, one bit per sequence older than HighestSequenceSeen. */
	static constexpr std::uint32_t DuplicateWindowWidth = 32;

	/**
	 * Reports whether InSequence has already been observed: the current highest itself, anything within
	 * the DuplicateWindowWidth-wide mask below it and already marked, or anything older than the window.
	 */
	bool WasSeen(const std::uint16_t InSequence) const noexcept
	{
		if (InSequence == HighestSequenceSeen)
		{
			return true;
		}
		if (IsNewer(InSequence, HighestSequenceSeen))
		{
			return false;
		}
		const std::uint16_t Delta = static_cast<std::uint16_t>(HighestSequenceSeen - InSequence);
		if (Delta > DuplicateWindowWidth)
		{
			return true;
		}
		return ((SeenMask >> (Delta - 1)) & 1u) != 0u;
	}

	/** Records a fresh InSequence as seen, sliding the window forward when InSequence becomes the new highest. */
	void MarkSeen(const std::uint16_t InSequence) noexcept
	{
		if (IsNewer(InSequence, HighestSequenceSeen))
		{
			const std::uint16_t Shift = static_cast<std::uint16_t>(InSequence - HighestSequenceSeen);
			if (Shift > DuplicateWindowWidth)
			{
				// The old highest has slid entirely past the window; nothing below the new highest stays tracked.
				SeenMask = 0u;
			}
			else if (Shift == DuplicateWindowWidth)
			{
				// The old highest lands exactly on the window's last bit; every record older than it has slid past.
				// (Shifting a 32-bit mask left by 32 is undefined, so this boundary is handled without the shift.)
				SeenMask = 1u << (DuplicateWindowWidth - 1);
			}
			else
			{
				// The old highest now sits at bit Shift-1; earlier records shift up with it, any past bit 31 fall off.
				SeenMask = (SeenMask << Shift) | (1u << (Shift - 1));
			}
			HighestSequenceSeen = InSequence;
		}
		else
		{
			const std::uint16_t Delta = static_cast<std::uint16_t>(HighestSequenceSeen - InSequence);
			SeenMask |= (1u << (Delta - 1));
		}
	}

	/** Writes the three-byte [Kind][Sequence LE] reliable header at the front of OutBytes. */
	static void WriteReliableHeader(std::uint8_t* const OutBytes, const EReliablePacketKind InKind, const std::uint16_t InSequence) noexcept
	{
		OutBytes[ReliableKindByteIndex] = static_cast<std::uint8_t>(InKind);
		WriteMessageUint16LittleEndian(InSequence, &OutBytes[ReliableSequenceFieldByteIndex]);
	}

	/** Reads the little-endian Sequence field starting at byte index 1 of a reliable-header-prefixed payload. */
	static std::uint16_t ReadSequence(const std::uint8_t* const InBytes) noexcept
	{
		return ReadMessageUint16LittleEndian(&InBytes[ReliableSequenceFieldByteIndex]);
	}

	/** Copies InLength bytes from InSource to OutDestination; InLength may be 0. */
	static void CopyBytes(std::uint8_t* const OutDestination, const std::uint8_t* const InSource, const std::size_t InLength) noexcept
	{
		for (std::size_t Index = 0; Index < InLength; ++Index)
		{
			OutDestination[Index] = InSource[Index];
		}
	}

	/** Assigns the next Data sequence and advances NextSequenceToSend, skipping 0 on wrap (sequences start at 1; 0 is never sent). */
	std::uint16_t AllocateNextSequence() noexcept
	{
		const std::uint16_t Sequence = NextSequenceToSend;
		const std::uint16_t Incremented = static_cast<std::uint16_t>(NextSequenceToSend + 1);
		NextSequenceToSend = Incremented == 0 ? FirstOutgoingSequence : Incremented;
		return Sequence;
	}

	/** Finds the lowest free pending slot, or nullptr when every slot is in use. */
	FPendingMessage* FindFreePendingSlot() noexcept
	{
		for (std::size_t Index = 0; Index < MaxPendingMessages; ++Index)
		{
			if (!Pending[Index].bInUse)
			{
				return &Pending[Index];
			}
		}
		return nullptr;
	}

	/** Frees the pending slot whose Sequence matches, if any; an unmatched Acknowledgement is silently ignored. */
	void FreePendingBySequence(const std::uint16_t InSequence) noexcept
	{
		for (std::size_t Index = 0; Index < MaxPendingMessages; ++Index)
		{
			if (Pending[Index].bInUse && Pending[Index].Sequence == InSequence)
			{
				Pending[Index].bInUse = false;
				return;
			}
		}
	}

	/**
	 * Acks InSequence unconditionally, then forwards the inner payload once for a fresh sequence or
	 * only counts a duplicate; ForwardSink rejecting a fresh forward is an accepted v1 limitation
	 * (the message is already acked and marked seen, so this wrapper never un-acks or re-delivers it).
	 */
	EMessageResult HandleInboundData(
		const FMessageChannelId InArrivedOnChannelId, const std::uint16_t InSequence, const TSpan<const std::uint8_t> InEncoded) noexcept
	{
		std::uint8_t AckBytes[ReliableHeaderBytes];
		WriteReliableHeader(AckBytes, EReliablePacketKind::Acknowledgement, InSequence);
		// Best-effort: a lost ack is recovered by the sender's own retry-driven resend, not by us.
		(void)InnerChannel->TrySendEncodedMessage(TSpan<const std::uint8_t>(AckBytes, ReliableHeaderBytes));

		if (WasSeen(InSequence))
		{
			++DuplicateDroppedTotal;
			return EMessageResult::Success;
		}

		MarkSeen(InSequence);
		const TSpan<const std::uint8_t> InnerPayload(InEncoded.Data() + ReliableHeaderBytes, InEncoded.Size() - ReliableHeaderBytes);
		(void)ForwardSink.ReceiveEncodedMessage(InArrivedOnChannelId, InnerPayload);
		return EMessageResult::Success;
	}

	/** Advances one pending slot's retry state by exactly one PostAdvance's worth of elapsed time. */
	void TickOnePendingSlot(FPendingMessage& InSlot, const TimePointMilliseconds InNowMilliseconds) noexcept
	{
		if (!InSlot.bBaselineTimeSet)
		{
			InSlot.LastSendTimeMilliseconds = InNowMilliseconds;
			InSlot.bBaselineTimeSet = true;
			return;
		}
		if (InNowMilliseconds - InSlot.LastSendTimeMilliseconds < Config.RetryIntervalMilliseconds)
		{
			return;
		}
		if (InSlot.Attempts >= Config.MaxSendAttempts)
		{
			InSlot.bInUse = false;
			++LostTotal;
			return;
		}

		(void)InnerChannel->TrySendEncodedMessage(TSpan<const std::uint8_t>(InSlot.Bytes, InSlot.Length));
		++InSlot.Attempts;
		++ResentTotal;
		InSlot.LastSendTimeMilliseconds = InNowMilliseconds;
	}

	/** Externally owned sink that receives forwarded fresh payloads; never owned here. */
	IEncodedMessageSink& ForwardSink;

	/** Retry interval and attempt ceiling this channel was constructed with. */
	FReliableChannelConfig Config;

	/** Externally owned wrapped channel bound by SetInnerChannel; never owned here, null until then. */
	IMessageChannel* InnerChannel{nullptr};

	/** Next Data sequence TrySendEncodedMessage will assign; starts at 1 since 0 is never sent. */
	std::uint16_t NextSequenceToSend{1};

	/** Fixed table of outbound messages awaiting acknowledgement. */
	FPendingMessage Pending[MaxPendingMessages == 0 ? 1 : MaxPendingMessages]{};

	/** Highest inbound Data sequence observed so far; 0 means none yet. */
	std::uint16_t HighestSequenceSeen{0};

	/** Bit i set means sequence (HighestSequenceSeen - (i+1)) has already been seen. */
	std::uint32_t SeenMask{0};

	/** Counts retry resends issued by PostAdvance. */
	std::uint32_t ResentTotal{0};

	/** Counts pending messages abandoned after exhausting Config.MaxSendAttempts. */
	std::uint32_t LostTotal{0};

	/** Counts inbound Data packets recognized as duplicates and not forwarded. */
	std::uint32_t DuplicateDroppedTotal{0};
};

} // namespace MicroWorld::Messaging
