#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Core/PlaySystem.h>
#include <MicroWorld/Core/Time.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Messaging
{

/** Motivation: Fixes the encoded byte count of the reliable wrapper's header prefix for offset math. */
inline constexpr std::size_t ReliableHeaderBytes = 3;

/** Motivation: Names half the 16-bit sequence space, the threshold that separates newer from older after wrap. */
inline constexpr std::uint16_t HalfSequenceSpace = 0x8000u;

/** Motivation: Fixes the first sequence ever sent, since 0 is reserved as never-sent so allocations start here. */
inline constexpr std::uint16_t FirstOutgoingSequence = 1;

/** Motivation: Fixes the byte offset of the little-endian sequence field within a reliable header. */
inline constexpr std::size_t ReliableSequenceFieldByteIndex = 1;

/** Motivation: Fixes the byte offset of the Kind byte within a reliable header. */
inline constexpr std::size_t ReliableKindByteIndex = 0;

/**
 * Motivation: Names, on the reliable wire format, whether bytes are an original message awaiting acknowledgement or a
 *   bare acknowledgement, so one Kind byte selects the parsing path.
 * Responsibilities: Distinguish the Data shape from the Acknowledgement shape and carry no behavior.
 * Example:
 *   EReliablePacketKind Kind = EReliablePacketKind::Data;
 */
enum class EReliablePacketKind : std::uint8_t
{
	Data = 1, ///< Motivation: Marks a wrapped original message awaiting acknowledgement, carrying sequence and payload.

	Acknowledgement = 2, ///< Motivation: Marks a bare acknowledgement of one received sequence, carrying no payload.
};

/**
 * Motivation: Groups the retry and acknowledgement settings for one guaranteed channel into one value a caller passes
 *   at construction.
 * Responsibilities: Hold the retry interval and attempt ceiling and carry no behavior.
 * Example:
 *   FReliableChannelConfig Config; Config.RetryIntervalMilliseconds = 200; Config.MaxSendAttempts = 5;
 */
struct FReliableChannelConfig
{
	/** Motivation: Holds the minimum wall-clock gap between successive resend attempts for one unacknowledged message. */
	Core::DurationMilliseconds RetryIntervalMilliseconds{250};

	/** Motivation: Holds the total send attempts (initial plus retries) before a message is abandoned. */
	std::uint8_t MaxSendAttempts{8};
};

/**
 * Motivation: Adds guaranteed delivery to one IMessageChannel so an unreliable wire can still carry messages the router
 *   cannot afford to lose, sitting between a channel binding and the router in both directions.
 * Responsibilities: Prefix [Kind][Sequence] outbound and keep a copy until acknowledged, acknowledge inbound data,
 *   drop duplicates via a serial-number window, forward fresh payloads to ForwardSink, and resend due unacknowledged
 *   messages from PostAdvance; point-to-point only.
 * Example:
 *   TReliableChannel<4, 64> Reliable(Router, Config);
 *   Reliable.SetInnerChannel(Binding);
 *   Router.AddChannel(Reliable);
 */
template<std::size_t MaxPendingMessages, std::size_t MaxMessageBytes>
class TReliableChannel final : public IMessageChannel, public IEncodedMessageSink, public Core::IPlaySystem
{
public:
	/**
	 * Motivation: Lets a caller build the wrapper with its forward sink and retry config in one call, binding the inner
	 *   channel later when the composition order allows.
	 * Responsibilities: Store ForwardSink and Config, leaving the inner channel unset until SetInnerChannel.
	 */
	TReliableChannel(IEncodedMessageSink& InForwardSink, const FReliableChannelConfig InConfig) noexcept
		: ForwardSink(InForwardSink), Config(InConfig)
	{
	}

	/**
	 * Motivation: Lets a caller destroy the wrapper through its base classes without owning an external resource.
	 * Responsibilities: Destroy the wrapper and its pending-message storage, invoking no callbacks.
	 */
	~TReliableChannel() noexcept override = default;

	// Held by reference at a fixed owner in the application entry point (the wrapper<->binding cycle breaker, see
	// SetInnerChannel) and captured by pointer in a frame set, matching TMessageRouter's fixed-identity rule.
	/**
	 * Motivation: Stops copy construction from duplicating the wrapper the entry point and pending set point at.
	 * Responsibilities: Reject copy construction outright so the wrapper keeps one fixed identity.
	 */
	TReliableChannel(const TReliableChannel&) = delete;
	/**
	 * Motivation: Stops copy assignment from rebinding the wrapper the entry point and pending set point at.
	 * Responsibilities: Reject copy assignment outright so the wrapper keeps one fixed identity.
	 */
	TReliableChannel& operator=(const TReliableChannel&) = delete;
	/**
	 * Motivation: Stops move construction from relocating the wrapper the entry point and pending set point at.
	 * Responsibilities: Reject move construction outright so the wrapper keeps one fixed identity.
	 */
	TReliableChannel(TReliableChannel&&) = delete;
	/**
	 * Motivation: Stops move assignment from relocating the wrapper the entry point and pending set point at.
	 * Responsibilities: Reject move assignment outright so the wrapper keeps one fixed identity.
	 */
	TReliableChannel& operator=(TReliableChannel&&) = delete;

	/**
	 * Motivation: Lets a caller break the wrapper<->binding reference cycle by binding the wrapped channel once at
	 *   composition, after this wrapper exists to serve as the binding's sink.
	 * Responsibilities: Store the inner channel pointer before AddChannel is called, since GetChannelId needs the
	 *   inner id thereafter.
	 */
	void SetInnerChannel(IMessageChannel& InInnerChannel) noexcept { InnerChannel = &InInnerChannel; }

	/**
	 * Motivation: Lets the router identify this wrapper by the id of the channel it wraps.
	 * Responsibilities: Return the inner channel's id, or LocalChannelId before SetInnerChannel has been called.
	 */
	FMessageChannelId GetChannelId() const noexcept override { return InnerChannel != nullptr ? InnerChannel->GetChannelId() : LocalChannelId; }

	/**
	 * Motivation: Lets a caller size an encoded message against the reliable wrapper's reduced budget before sending.
	 * Responsibilities: Return the inner channel's budget minus ReliableHeaderBytes (floored at 0), or 0 before
	 *   SetInnerChannel has been called.
	 */
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
	 * Motivation: Lets the router send one encoded message through the reliable layer as guaranteed delivery.
	 * Responsibilities: Reject transactionally (no sequence consumed, nothing sent, no state change) when the inner
	 *   channel is unset (Unavailable), the wrapped size cannot fit a pending slot or the inner budget
	 *   (PayloadTooLarge), or every pending slot is in use (CapacityExceeded); otherwise store the slot even when the
	 *   initial inner send does not report Success, since PostAdvance retries it later.
	 */
	EMessageResult TrySendEncodedMessage(const Core::TSpan<const std::uint8_t> InEncoded) noexcept override
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

		return InnerChannel->TrySendEncodedMessage(Core::TSpan<const std::uint8_t>(Slot->Bytes, Slot->Length));
	}

	/**
	 * Motivation: Lets the inner channel hand one inbound wire payload to the reliable layer for acknowledgement,
	 *   deduplication, and forwarding.
	 * Responsibilities: Reject a payload shorter than ReliableHeaderBytes or carrying an unrecognized Kind byte as
	 *   PayloadTooLarge; on a Data packet always ack (fresh or duplicate), then forward a fresh payload to ForwardSink
	 *   once or count a duplicate; on an Acknowledgement free the matching pending slot.
	 */
	EMessageResult ReceiveEncodedMessage(
		const FMessageChannelId InArrivedOnChannelId, const Core::TSpan<const std::uint8_t> InEncoded) noexcept override
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

	/**
	 * Motivation: Lets TEngine call the play-system pump entry this wrapper does not need.
	 * Responsibilities: Do nothing, since inbound arrives via ReceiveEncodedMessage and retries run in PostAdvance.
	 */
	void PreAdvance(const Core::TimePointMilliseconds InNowMilliseconds) noexcept override { (void)InNowMilliseconds; }

	/**
	 * Motivation: Lets TEngine drive the retry cadence for every unacknowledged pending message as one framed pump step.
	 * Responsibilities: Record the retry baseline on the first flush after a send without resending that tick, resend
	 *   once RetryIntervalMilliseconds has elapsed while attempts remain, and drop and count a slot that has exhausted
	 *   Config.MaxSendAttempts.
	 */
	void PostAdvance(const Core::TimePointMilliseconds InNowMilliseconds) noexcept override
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

	/**
	 * Motivation: Lets a caller observe how many messages still await acknowledgement.
	 * Responsibilities: Return the exact count of currently occupied pending slots.
	 */
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

	/**
	 * Motivation: Lets a caller observe retry pressure on the reliable layer.
	 * Responsibilities: Return how many retry resends PostAdvance has issued so far.
	 */
	std::uint32_t ResentCount() const noexcept { return ResentTotal; }

	/**
	 * Motivation: Lets a caller observe delivery failures the reliable layer could not recover.
	 * Responsibilities: Return how many pending messages were abandoned after exhausting Config.MaxSendAttempts.
	 */
	std::uint32_t LostCount() const noexcept { return LostTotal; }

	/**
	 * Motivation: Lets a caller observe duplicate inbound pressure on the reliable layer.
	 * Responsibilities: Return how many inbound Data packets were recognized as duplicates and not forwarded.
	 */
	std::uint32_t DuplicateDroppedCount() const noexcept { return DuplicateDroppedTotal; }

private:
	/**
	 * Motivation: Holds one outbound message awaiting acknowledgement, with its wrapped bytes and retry bookkeeping.
	 * Responsibilities: Store the wrapped Data packet, its length and sequence, the send-attempt count and retry
	 *   baseline, and an occupancy flag.
	 * Example:
	 *   FPendingMessage Slot; Slot.Sequence = 1; Slot.bInUse = true;
	 */
	struct FPendingMessage final
	{
		/** Motivation: Holds the wrapped Data packet bytes, [Kind][Sequence][original encoded message]. */
		std::uint8_t Bytes[MaxMessageBytes == 0 ? 1 : MaxMessageBytes]{};

		/** Motivation: Holds the valid byte count at the front of Bytes. */
		std::size_t Length{0};

		/** Motivation: Holds the sequence number this slot was sent under, matched against an inbound Acknowledgement. */
		std::uint16_t Sequence{0};

		/** Motivation: Holds the total send attempts so far, including the initial send as attempt 1. */
		std::uint8_t Attempts{0};

		/** Motivation: Distinguishes no retry baseline yet from a real LastSendTimeMilliseconds. */
		bool bBaselineTimeSet{false};

		/** Motivation: Holds the wall-clock time PostAdvance last (re)sent this slot, once bBaselineTimeSet is true. */
		Core::TimePointMilliseconds LastSendTimeMilliseconds{0};

		/** Motivation: Distinguishes an occupied slot from reusable free storage. */
		bool bInUse{false};
	};

	/**
	 * Motivation: Lets the duplicate window compare sequence numbers as newer-or-older across 16-bit wrap, so a rolled
	 *   counter never looks older than the current highest.
	 * Responsibilities: Decide newness by the smaller forward distance between InCandidate and InReference, using
	 *   HalfSequenceSpace as the threshold.
	 */
	static bool IsNewer(const std::uint16_t InCandidate, const std::uint16_t InReference) noexcept
	{
		return (InCandidate != InReference) && (static_cast<std::uint16_t>(InCandidate - InReference) < HalfSequenceSpace);
	}

	/** Motivation: Fixes the width of the duplicate-detection window at one bit per sequence older than the highest seen. */
	static constexpr std::uint32_t DuplicateWindowWidth = 32;

	/**
	 * Motivation: Lets the inbound path tell a fresh Data packet from a redelivery before forwarding it.
	 * Responsibilities: Report InSequence as seen when it is the current highest, anything marked within
	 *   DuplicateWindowWidth below it, or anything older than the window.
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

	/**
	 * Motivation: Lets the inbound path record a sequence so a later redelivery is recognized as a duplicate.
	 * Responsibilities: Mark a fresh InSequence seen and slide the window forward when it becomes the new highest,
	 *   dropping bits that fall past DuplicateWindowWidth.
	 */
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

	/**
	 * Motivation: Lets send and ack paths write the reliable header bytes from one Kind and Sequence without each
	 *   inlining the layout.
	 * Responsibilities: Write the three-byte [Kind][Sequence LE] header at the front of OutBytes.
	 */
	static void WriteReliableHeader(std::uint8_t* const OutBytes, const EReliablePacketKind InKind, const std::uint16_t InSequence) noexcept
	{
		OutBytes[ReliableKindByteIndex] = static_cast<std::uint8_t>(InKind);
		WriteMessageUint16LittleEndian(InSequence, &OutBytes[ReliableSequenceFieldByteIndex]);
	}

	/**
	 * Motivation: Lets the inbound path read the sequence field of a reliable-header-prefixed payload in one place.
	 * Responsibilities: Read the little-endian Sequence field starting at byte index 1.
	 */
	static std::uint16_t ReadSequence(const std::uint8_t* const InBytes) noexcept
	{
		return ReadMessageUint16LittleEndian(&InBytes[ReliableSequenceFieldByteIndex]);
	}

	/**
	 * Motivation: Lets the send path copy the wrapped payload bytes behind one named helper.
	 * Responsibilities: Copy InLength bytes from InSource to OutDestination, handling a zero length.
	 */
	static void CopyBytes(std::uint8_t* const OutDestination, const std::uint8_t* const InSource, const std::size_t InLength) noexcept
	{
		for (std::size_t Index = 0; Index < InLength; ++Index)
		{
			OutDestination[Index] = InSource[Index];
		}
	}

	/**
	 * Motivation: Lets TrySendEncodedMessage give each sent Data packet a distinct sequence without the caller tracking it.
	 * Responsibilities: Return the current sequence and advance NextSequenceToSend, skipping 0 on wrap so 0 is never sent.
	 */
	std::uint16_t AllocateNextSequence() noexcept
	{
		const std::uint16_t Sequence = NextSequenceToSend;
		const std::uint16_t Incremented = static_cast<std::uint16_t>(NextSequenceToSend + 1);
		NextSequenceToSend = Incremented == 0 ? FirstOutgoingSequence : Incremented;
		return Sequence;
	}

	/**
	 * Motivation: Lets TrySendEncodedMessage claim the next free pending slot for a new send.
	 * Responsibilities: Return the lowest free pending slot, or null when every slot is in use.
	 */
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

	/**
	 * Motivation: Lets an inbound Acknowledgement release the slot holding the message it confirms.
	 * Responsibilities: Free the pending slot whose Sequence matches, and silently ignore an unmatched Acknowledgement.
	 */
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
	 * Motivation: Lets the inbound Data path acknowledge, deduplicate, and forward one received message in one step.
	 * Responsibilities: Ack InSequence unconditionally, then forward the inner payload once for a fresh sequence or only
	 *   count a duplicate; accept that a ForwardSink rejection of a fresh forward is an unrecoverable v1 limitation
	 *   since the message is already acked and marked seen.
	 */
	EMessageResult HandleInboundData(
		const FMessageChannelId InArrivedOnChannelId, const std::uint16_t InSequence, const Core::TSpan<const std::uint8_t> InEncoded) noexcept
	{
		std::uint8_t AckBytes[ReliableHeaderBytes];
		WriteReliableHeader(AckBytes, EReliablePacketKind::Acknowledgement, InSequence);
		// Best-effort: a lost ack is recovered by the sender's own retry-driven resend, not by us.
		(void)InnerChannel->TrySendEncodedMessage(Core::TSpan<const std::uint8_t>(AckBytes, ReliableHeaderBytes));

		if (WasSeen(InSequence))
		{
			++DuplicateDroppedTotal;
			return EMessageResult::Success;
		}

		MarkSeen(InSequence);
		const Core::TSpan<const std::uint8_t> InnerPayload(InEncoded.Data() + ReliableHeaderBytes, InEncoded.Size() - ReliableHeaderBytes);
		(void)ForwardSink.ReceiveEncodedMessage(InArrivedOnChannelId, InnerPayload);
		return EMessageResult::Success;
	}

	/**
	 * Motivation: Lets PostAdvance advance one pending slot's retry state by one tick's worth of elapsed time.
	 * Responsibilities: Set the retry baseline on the first tick, resend once the interval elapses while attempts
	 *   remain, and drop and count a slot that has exhausted Config.MaxSendAttempts.
	 */
	void TickOnePendingSlot(FPendingMessage& InSlot, const Core::TimePointMilliseconds InNowMilliseconds) noexcept
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

		(void)InnerChannel->TrySendEncodedMessage(Core::TSpan<const std::uint8_t>(InSlot.Bytes, InSlot.Length));
		++InSlot.Attempts;
		++ResentTotal;
		InSlot.LastSendTimeMilliseconds = InNowMilliseconds;
	}

	/** Motivation: Holds the externally owned sink that receives forwarded fresh payloads. */
	IEncodedMessageSink& ForwardSink;

	/** Motivation: Holds the retry interval and attempt ceiling this channel was constructed with. */
	FReliableChannelConfig Config;

	/** Motivation: Holds the externally owned wrapped channel bound by SetInnerChannel, null until then. */
	IMessageChannel* InnerChannel{nullptr};

	/** Motivation: Holds the next Data sequence TrySendEncodedMessage will assign, starting at 1 since 0 is never sent. */
	std::uint16_t NextSequenceToSend{1};

	/** Motivation: Holds the fixed table of outbound messages awaiting acknowledgement. */
	FPendingMessage Pending[MaxPendingMessages == 0 ? 1 : MaxPendingMessages]{};

	/** Motivation: Holds the highest inbound Data sequence observed so far, with 0 meaning none yet. */
	std::uint16_t HighestSequenceSeen{0};

	/** Motivation: Holds the duplicate-detection window, where bit i marks sequence (HighestSequenceSeen - (i+1)) seen. */
	std::uint32_t SeenMask{0};

	/** Motivation: Counts the retry resends issued by PostAdvance. */
	std::uint32_t ResentTotal{0};

	/** Motivation: Counts pending messages abandoned after exhausting Config.MaxSendAttempts. */
	std::uint32_t LostTotal{0};

	/** Motivation: Counts inbound Data packets recognized as duplicates and not forwarded. */
	std::uint32_t DuplicateDroppedTotal{0};
};

} // namespace MicroWorld::Messaging
