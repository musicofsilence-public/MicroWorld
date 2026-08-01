#pragma once

#include <MicroWorld/Core/Containers/StaticVector.h>
#include <MicroWorld/Core/Delegates/Delegate.h>
#include <MicroWorld/Core/PlaySystem.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Core/WeakOwner.h>
#include <MicroWorld/Messaging/MessageTypes.h>
#include <MicroWorld/Messaging/NameId.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace MicroWorld::Messaging
{

/** Motivation: Reserves the protocol message name used only for reliable-delivery acknowledgements. */
inline constexpr FNameId MessageAcknowledgementNameId = MakeNameId("__MessageAck");

/**
 * Motivation: Defines the complete default fixed memory footprint for one Messaging system.
 * Responsibilities: Bound every channel, subscription, frame, and reliable-pending allocation at compile time.
 * Example:
 *   FMessagingSystem System;
 */
struct FDefaultMessagingTraits
{
	/** Motivation: Bounds how many named channels one Messaging system may store. */
	static constexpr std::size_t MaxChannels = 4;

	/** Motivation: Bounds how many subscriber registrations one Messaging system may store. */
	static constexpr std::size_t MaxSubscriptions = 16;

	/** Motivation: Bounds the inline storage one subscriber callable may occupy before Messaging rejects it without allocating. */
	static constexpr std::size_t MaxSubscriberCallableBytes = 32;

	/** Motivation: Bounds the application bytes one complete Messaging frame may carry. */
	static constexpr std::size_t MaxMessageBytes = 96;

	/** Motivation: Bounds how many reliable messages one Messaging system may retain awaiting acknowledgement. */
	static constexpr std::size_t MaxReliablePendingMessages = 8;
};

/**
 * Motivation: Owns the bounded set of named channels and carries their messages to local subscribers and, where a channel holds a device, the wire.
 * Responsibilities: Create valid unique channels without allocating, deliver local messages, and move complete best-effort and reliable frames
 *   through externally driven transport devices; never call a device's own PreAdvance or PostAdvance, because its composition root owns those turns
 *   and a shared device must not be ticked twice.
 * Example:
 *   TMessagingSystem<> System;
 *   System.CreateChannel({"Telemetry", false, nullptr, {}});
 */
template<typename TTraits = FDefaultMessagingTraits>
class TMessagingSystem final : public Core::IPlaySystem
{
	/** Motivation: Fixes the count of bytes in one 32-bit name id on the wire. */
	static constexpr std::size_t NameIdBytes = sizeof(std::uint32_t);

	/** Motivation: Fixes the number of bits in one wire byte for explicit little-endian shifts. */
	static constexpr std::size_t BitsPerByte = 8;

	/** Motivation: Fixes the byte width of one reliable-message sequence number on the wire. */
	static constexpr std::size_t SequenceNumberBytes = sizeof(std::uint16_t);

	/** Motivation: Identifies the first byte of the frame's encoded channel name id. */
	static constexpr std::size_t ChannelNameIdByteIndex = 0;

	/** Motivation: Identifies the first byte of the frame's encoded message name id. */
	static constexpr std::size_t MessageNameIdByteIndex = ChannelNameIdByteIndex + NameIdBytes;

public:
	/** Motivation: Fixes where the two name ids end, which is also where application payload bytes begin. */
	static constexpr std::size_t FrameHeaderBytes = MessageNameIdByteIndex + NameIdBytes;

	/** Motivation: Exposes the largest complete wire frame this Messaging system can construct without dynamic allocation. */
	static constexpr std::size_t MaxFrameBytes = FrameHeaderBytes + TTraits::MaxMessageBytes;

	// An acknowledgement frame is a header plus one sequence number, so a payload budget below that width could not hold one.
	static_assert(TTraits::MaxMessageBytes >= SequenceNumberBytes, "MaxMessageBytes must leave room for a reliable sequence number.");

	// A zero-slot reliable system could never track its first reliable send.
	static_assert(TTraits::MaxReliablePendingMessages > 0, "MaxReliablePendingMessages must reserve at least one reliable pending slot.");

	/**
	 * Motivation: Gives channel subscribers one bounded callable type that keeps local delivery allocation-free.
	 * Responsibilities: Receive a message by const reference and treat its payload span as valid only for the duration of the call; subscribers that
	 * retain bytes must copy them.
	 */
	using FSubscriberDelegate = Core::TDelegate<void(const FMessage&), TTraits::MaxSubscriberCallableBytes>;

	/**
	 * Motivation: Lets callers retain one optional identity for a subscription without exposing its storage slot.
	 * Responsibilities: Identify a currently occupied slot only when its index and generation both match that slot.
	 * Example:
	 *   FSubscriptionHandle Handle;
	 *   System.SubscribeToChannel("Telemetry", std::move(Subscriber), Owner, &Handle);
	 */
	struct FSubscriptionHandle final
	{
		/** Motivation: Marks a handle that does not identify any slot in this Messaging system. */
		static constexpr std::uint16_t InvalidIndex = 0xFFFFu;

		/** Motivation: Identifies the fixed subscription slot selected on a successful registration. */
		std::uint16_t Index{InvalidIndex};

		/** Motivation: Distinguishes this handle from earlier occupants of the same fixed slot. */
		std::uint16_t Generation{0};
	};

	/**
	 * Motivation: Gives callers an empty Messaging system when the default reliability policy is sufficient.
	 * Responsibilities: Initialize no live channels and retain default system information without allocation.
	 */
	TMessagingSystem() noexcept = default;

	/**
	 * Motivation: Lets a composition root configure reliability policy before it creates channels.
	 * Responsibilities: Retain the supplied system information and initialize no live channels without allocation.
	 */
	explicit TMessagingSystem(const FMessagingSystemInformation& InInformation) noexcept : Information(InInformation) {}

	/**
	 * Motivation: Prevents copying a system whose future channel references must remain stable.
	 * Responsibilities: Reject copy construction because the engine owns this object in place.
	 */
	TMessagingSystem(const TMessagingSystem&) = delete;

	/**
	 * Motivation: Prevents assignment from replacing a system whose future channel references must remain stable.
	 * Responsibilities: Reject copy assignment because the engine owns this object in place.
	 */
	TMessagingSystem& operator=(const TMessagingSystem&) = delete;

	/**
	 * Motivation: Prevents relocation of a system whose future channel references must remain stable.
	 * Responsibilities: Reject move construction because the engine owns this object in place.
	 */
	TMessagingSystem(TMessagingSystem&&) = delete;

	/**
	 * Motivation: Prevents relocation through assignment of a system whose future channel references must remain stable.
	 * Responsibilities: Reject move assignment because the engine owns this object in place.
	 */
	TMessagingSystem& operator=(TMessagingSystem&&) = delete;

	/**
	 * Motivation: Lets later messaging operations read their shared reliability policy without mutating the system.
	 * Responsibilities: Return the stored policy by read-only reference.
	 */
	const FMessagingSystemInformation& GetInformation() const noexcept { return Information; }

	/**
	 * Motivation: Gives callers one explicit, bounded operation for adding a named Messaging channel.
	 * Responsibilities: Reject unset names, preserve existing channels on duplicates or capacity exhaustion, and store each valid unique channel.
	 */
	EMessagingResult CreateChannel(const FChannelInformation& InChannelInformation) noexcept
	{
		if (InChannelInformation.ChannelNameId == InvalidNameId)
		{
			return EMessagingResult::Invalid;
		}

		if (FindChannel(InChannelInformation.ChannelNameId) != nullptr)
		{
			return EMessagingResult::Duplicate;
		}

		// Capacity exhaustion is the only way Emplace fails, so its result is the whole capacity rule.
		return Channels.Emplace(InChannelInformation) == Core::ERuntimeResult::Success ? EMessagingResult::Success : EMessagingResult::Full;
	}

	/**
	 * Motivation: Lets a caller receive every message sent locally through one existing channel.
	 * Responsibilities: Store one bound subscriber with no message-name filter, or report the first applicable validation or capacity failure without
	 * changing state.
	 */
	EMessagingResult SubscribeToChannel(
		const FNameId InChannelNameId,
		FSubscriberDelegate&& InSubscriber,
		Core::FWeakOwner InOwner = {},
		FSubscriptionHandle* OutHandle = nullptr) noexcept
	{
		return SubscribeToChannel(InChannelNameId, InvalidNameId, std::move(InSubscriber), InOwner, OutHandle);
	}

	/**
	 * Motivation: Lets a caller receive only one named kind of message sent locally through one existing channel.
	 * Responsibilities: Validate the channel and bound subscriber before storing a filtered registration within the fixed system-wide subscription
	 * capacity.
	 */
	EMessagingResult SubscribeToChannel(
		const FNameId InChannelNameId,
		const FNameId InMessageNameFilter,
		FSubscriberDelegate&& InSubscriber,
		Core::FWeakOwner InOwner = {},
		FSubscriptionHandle* OutHandle = nullptr) noexcept
	{
		if (FindChannel(InChannelNameId) == nullptr)
		{
			return EMessagingResult::NotFound;
		}

		if (!InSubscriber.IsBound())
		{
			return EMessagingResult::Invalid;
		}

		if (!InOwner.IsLive())
		{
			return EMessagingResult::Invalid;
		}

		for (std::size_t SlotIndex = 0; SlotIndex < TTraits::MaxSubscriptions; ++SlotIndex)
		{
			FSubscriptionSlot& Slot = SubscriptionSlots[SlotIndex];
			if (Slot.bIsOccupied)
			{
				continue;
			}

			Slot.SubscriptionSequence = NextSubscriptionSequence;
			++NextSubscriptionSequence;
			Slot.ChannelNameId = InChannelNameId;
			Slot.MessageNameFilter = InMessageNameFilter;
			Slot.Owner = InOwner;
			Slot.Subscriber = std::move(InSubscriber);
			Slot.bIsOccupied = true;
			if (OutHandle != nullptr)
			{
				OutHandle->Index = static_cast<std::uint16_t>(SlotIndex);
				OutHandle->Generation = Slot.Generation;
			}

			return EMessagingResult::Success;
		}

		return EMessagingResult::Full;
	}

	/**
	 * Motivation: Makes locally composed Messaging useful before any transport device exists and extends it with best-effort remote reach.
	 * Responsibilities: Reject an unset message name or missing channel, then synchronously deliver locally once to every matching subscriber before
	 * attempting one complete wire frame through the channel's device, if any; return a transport-capacity or invalid-request result without undoing
	 * that local delivery. A reliable frame is retained for later retry even when its first device send reports Full.
	 */
	EMessagingResult SendMessageToChannel(const FMessage& InMessage, const FNameId InChannelNameId) noexcept
	{
		if (InMessage.GetMessageNameId() == InvalidNameId || InMessage.GetMessageNameId() == MessageAcknowledgementNameId)
		{
			return EMessagingResult::Invalid;
		}

		FChannel* const Channel = FindChannel(InChannelNameId);
		if (Channel == nullptr)
		{
			return EMessagingResult::NotFound;
		}

		DeliverToMatchingSubscribers(InMessage, InChannelNameId);

		if (Channel->Information.TransportDevice == nullptr)
		{
			return EMessagingResult::Success;
		}

		const Core::TSpan<const std::uint8_t> Payload = InMessage.GetPayload();
		const std::size_t ReliableHeaderBytes = Channel->Information.bIsReliable ? SequenceNumberBytes : 0;
		const std::size_t FrameSize = FrameHeaderBytes + ReliableHeaderBytes + Payload.Size();
		if (FrameSize > MaxFrameBytes || FrameSize > Channel->Information.TransportDevice->MaxPacketBytes())
		{
			return EMessagingResult::Full;
		}

		FPendingReliableMessage* PendingMessage = nullptr;
		if (Channel->Information.bIsReliable)
		{
			PendingMessage = FindFreeReliablePendingMessage();
			if (PendingMessage == nullptr)
			{
				// Local delivery already happened, but an untracked reliable wire send would promise delivery this system cannot recover.
				return EMessagingResult::Full;
			}
		}

		std::uint8_t FrameBytes[MaxFrameBytes]{};
		EncodeFrameHeader(InChannelNameId, InMessage.GetMessageNameId(), FrameBytes);
		if (Channel->Information.bIsReliable)
		{
			WriteUnsignedLittleEndian(Channel->NextOutgoingSequenceNumber, &FrameBytes[FrameHeaderBytes], SequenceNumberBytes);
		}
		CopyBytes(&FrameBytes[FrameHeaderBytes + ReliableHeaderBytes], Payload);

		const Core::ETransportResult TransportSendResult =
			Channel->Information.TransportDevice->TrySend(Channel->Information.Address, Core::TSpan<const std::uint8_t>(FrameBytes, FrameSize));
		if (Channel->Information.bIsReliable)
		{
			// A Full device is busy rather than terminal: retaining this frame lets the bounded retry policy recover that first miss.
			TrackReliableMessage(
				*PendingMessage,
				InChannelNameId,
				Channel->NextOutgoingSequenceNumber,
				Core::TSpan<const std::uint8_t>(FrameBytes, FrameSize),
				MostRecentTimeMilliseconds);
			// Wrapping at the 16-bit range is intentional; reusing a number for a different attempted frame is not.
			++Channel->NextOutgoingSequenceNumber;
		}

		return MapTransportSendResult(TransportSendResult);
	}

	/**
	 * Motivation: Pumps inbound best-effort frames before the world advances so this node's subscribers see device input in the current turn.
	 * Responsibilities: Drain each distinct channel device once, route each complete frame by its encoded channel name, and count malformed or
	 * unroutable frames without asserting and retain the supplied time for sends occurring between lifecycle turns.
	 */
	void PreAdvance(Core::TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		MostRecentTimeMilliseconds = InNowMilliseconds;

		for (std::size_t ChannelIndex = 0; ChannelIndex < Channels.Size(); ++ChannelIndex)
		{
			Core::ITransportDevice* const TransportDevice = Channels[ChannelIndex].Information.TransportDevice;
			if (TransportDevice == nullptr || IsDeviceUsedByEarlierChannel(TransportDevice, ChannelIndex))
			{
				continue;
			}

			DrainDevice(*TransportDevice);
		}
	}

	/**
	 * Motivation: Gives reliable frames their caller-timed opportunity to retry after world advancement.
	 * Responsibilities: Retain the supplied time for between-turn sends, process every occupied reliable pending slot once, and never advance a
	 * transport device's lifecycle turns.
	 */
	void PostAdvance(Core::TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		MostRecentTimeMilliseconds = InNowMilliseconds;
		for (FPendingReliableMessage& PendingMessage : ReliablePendingMessages)
		{
			ProcessReliablePendingMessage(PendingMessage, InNowMilliseconds);
		}
	}

	/**
	 * Motivation: Lets callers observe inbound frames this system received but could not route to a live channel.
	 * Responsibilities: Return the cumulative count of frames shorter than a header, naming an unknown channel, or too large for this system's frame
	 * budget, without changing system state.
	 */
	std::uint32_t GetDroppedFrameCount() const noexcept { return DroppedFrameCount; }

	/**
	 * Motivation: Lets callers distinguish reliable delivery that is still retrying from delivery whose bounded attempt policy gave up.
	 * Responsibilities: Return the cumulative number of released reliable pending messages that exhausted their attempts or lost their channel,
	 * without changing system state.
	 */
	std::uint32_t GetAbandonedReliableMessageCount() const noexcept { return AbandonedReliableMessageCount; }

	/**
	 * Motivation: Lets callers observe subscriptions removed because their bound owner died.
	 * Responsibilities: Return the cumulative number of dead-owner subscriptions reclaimed during delivery without changing system state.
	 */
	std::uint32_t GetReclaimedDeadOwnerSubscriptionCount() const noexcept { return ReclaimedDeadOwnerSubscriptionCount; }

private:
	/**
	 * Motivation: Keeps a channel's immutable configuration and its mutable reliable-send state in one bounded storage element.
	 * Responsibilities: Retain the supplied creation information and start each reliable channel's outgoing sequence at zero without allocation.
	 * Example:
	 *   FChannel Channel{Information};
	 */
	struct FChannel final
	{
		/**
		 * Motivation: Initializes both channel configuration and reliable runtime state together when a channel is created.
		 * Responsibilities: Copy InInformation and initialize the next outgoing sequence number to zero.
		 */
		explicit FChannel(const FChannelInformation& InInformation) noexcept : Information(InInformation) {}

		/** Motivation: Retains immutable caller-supplied channel configuration beside the state that uses it. */
		FChannelInformation Information{};

		/** Motivation: Supplies the next unique 16-bit sequence number for this channel's reliable wire message. */
		std::uint16_t NextOutgoingSequenceNumber{0};
	};

	/**
	 * Motivation: Keeps one removable local subscription and its liveness metadata in flat system-owned storage.
	 * Responsibilities: Retain occupancy, generation, delivery order, routing criteria, owner liveness, and uniquely owned callable without
	 *   allocating.
	 *
	 * Example:
	 *   FSubscriptionSlot Slot{};
	 */
	struct FSubscriptionSlot final
	{
		/** Motivation: Marks whether this slot currently owns a callable and routing registration. */
		bool bIsOccupied{false};

		/** Motivation: Invalidates handles from earlier occupants whenever this slot is released. */
		std::uint16_t Generation{1};

		/** Motivation: Preserves registration order and excludes registrations added during an active delivery. */
		std::uint32_t SubscriptionSequence{0};

		/** Motivation: Identifies the channel whose local sends can reach this subscriber. */
		FNameId ChannelNameId{};

		/** Motivation: Narrows delivery to one message name, with the unset id accepting every message on the channel. */
		FNameId MessageNameFilter{};

		/** Motivation: Prevents delivery into a callable after its captured owner has died. */
		Core::FWeakOwner Owner{};

		/** Motivation: Owns the inline callable that observes matching local messages. */
		FSubscriberDelegate Subscriber{};
	};

	/**
	 * Motivation: Retains exactly one already-sent reliable frame until its peer acknowledges the frame's channel and sequence number.
	 * Responsibilities: Store explicit occupancy, destination lookup identity, wire bytes, attempt count, and last-attempt time without allocating.
	 * Example:
	 *   FPendingReliableMessage PendingMessage{};
	 */
	struct FPendingReliableMessage final
	{
		/** Motivation: Marks whether this slot is awaiting acknowledgement; false makes the slot free for the next reliable send. */
		bool bAwaitingAcknowledgement{false};

		/** Motivation: Identifies the channel whose device and address resend this stored frame. */
		FNameId ChannelNameId{};

		/** Motivation: Matches an acknowledgement to this one reliable wire frame. */
		std::uint16_t SequenceNumber{0};

		/** Motivation: Retains the already-encoded frame so a resend cannot duplicate framing rules or observe changed caller bytes. */
		std::uint8_t FrameBytes[MaxFrameBytes]{};

		/** Motivation: Records how many leading FrameBytes elements belong to this retained wire frame. */
		std::size_t FrameSize{0};

		/** Motivation: Counts every device send attempt, including the initial attempt that may have reported Full. */
		std::uint8_t SendAttempts{0};

		/** Motivation: Sets the caller-supplied time from which the next retry interval begins. */
		Core::TimePointMilliseconds LastAttemptMilliseconds{0};
	};

	/**
	 * Motivation: Gives channel creation, subscription, send, and receive paths one authoritative lookup for live channel information.
	 * Responsibilities: Return the matching channel state, or null when InChannelNameId names no live channel, without changing channel
	 * storage.
	 */
	FChannel* FindChannel(const FNameId InChannelNameId) noexcept
	{
		for (FChannel& Channel : Channels)
		{
			if (Channel.Information.ChannelNameId == InChannelNameId)
			{
				return &Channel;
			}
		}

		return nullptr;
	}

	/**
	 * Motivation: Gives reliable sends a plain fixed-array free-slot lookup without widening Core's append-only static vector.
	 * Responsibilities: Return the first slot not awaiting acknowledgement, or null when the bounded reliable pending set is full.
	 */
	FPendingReliableMessage* FindFreeReliablePendingMessage() noexcept
	{
		for (FPendingReliableMessage& PendingMessage : ReliablePendingMessages)
		{
			if (!PendingMessage.bAwaitingAcknowledgement)
			{
				return &PendingMessage;
			}
		}

		return nullptr;
	}

	/**
	 * Motivation: Gives acknowledgement processing one exact channel-and-sequence lookup across the fixed pending set.
	 * Responsibilities: Return the occupied slot matching both reliable wire identities, or null when the acknowledgement is duplicate or abandoned.
	 */
	FPendingReliableMessage* FindReliablePendingMessage(const FNameId InChannelNameId, const std::uint16_t InSequenceNumber) noexcept
	{
		for (FPendingReliableMessage& PendingMessage : ReliablePendingMessages)
		{
			if (PendingMessage.bAwaitingAcknowledgement && PendingMessage.ChannelNameId == InChannelNameId
				&& PendingMessage.SequenceNumber == InSequenceNumber)
			{
				return &PendingMessage;
			}
		}

		return nullptr;
	}

	/**
	 * Motivation: Makes one reliable-send reservation retain immutable retry inputs before caller-owned frame storage expires.
	 * Responsibilities: Copy the complete encoded frame and record its initial attempt, channel identity, sequence, and caller-supplied time; the
	 *   caller has already checked that InFrame fits one frame buffer.
	 */
	static void TrackReliableMessage(
		FPendingReliableMessage& OutPendingMessage,
		const FNameId InChannelNameId,
		const std::uint16_t InSequenceNumber,
		const Core::TSpan<const std::uint8_t> InFrame,
		const Core::TimePointMilliseconds InAttemptTimeMilliseconds) noexcept
	{
		OutPendingMessage.ChannelNameId = InChannelNameId;
		OutPendingMessage.SequenceNumber = InSequenceNumber;
		CopyBytes(OutPendingMessage.FrameBytes, InFrame);
		OutPendingMessage.FrameSize = InFrame.Size();
		OutPendingMessage.SendAttempts = 1;
		OutPendingMessage.LastAttemptMilliseconds = InAttemptTimeMilliseconds;
		OutPendingMessage.bAwaitingAcknowledgement = true;
	}

	/**
	 * Motivation: Releases a reliable pending slot without assigning a magic value to any retained message field.
	 * Responsibilities: Mark only the explicit acknowledgement flag false so the slot becomes available for a later reliable send.
	 */
	static void ReleaseReliablePendingMessage(FPendingReliableMessage& InOutPendingMessage) noexcept
	{
		InOutPendingMessage.bAwaitingAcknowledgement = false;
	}

	/**
	 * Motivation: Releases a subscription whose owner died without retaining its callable or validating stale future handles.
	 * Responsibilities: Mark InSlot unoccupied, advance its generation without producing zero, and reset its delegate.
	 */
	static void ReleaseSubscriptionSlot(FSubscriptionSlot& InSlot) noexcept
	{
		InSlot.bIsOccupied = false;
		++InSlot.Generation;
		if (InSlot.Generation == 0)
		{
			InSlot.Generation = 1;
		}

		InSlot.Subscriber.Reset();
	}

	/**
	 * Motivation: Applies the retry interval and attempt budget to one occupied reliable slot without making PostAdvance a policy maze.
	 * Responsibilities: Skip early or backwards time, abandon exhausted or orphaned frames, or resend one retained frame and restamp its attempt.
	 */
	void ProcessReliablePendingMessage(FPendingReliableMessage& InOutPendingMessage, const Core::TimePointMilliseconds InNowMilliseconds) noexcept
	{
		if (!InOutPendingMessage.bAwaitingAcknowledgement)
		{
			return;
		}

		if (InNowMilliseconds < InOutPendingMessage.LastAttemptMilliseconds)
		{
			// Subtracting unsigned non-monotonic time would appear enormous and incorrectly resend every pending frame at once.
			return;
		}

		const Core::TimePointMilliseconds ElapsedMilliseconds = InNowMilliseconds - InOutPendingMessage.LastAttemptMilliseconds;
		if (ElapsedMilliseconds < Information.ReliableRetryIntervalMilliseconds)
		{
			return;
		}

		if (InOutPendingMessage.SendAttempts >= Information.MaxReliableSendAttempts)
		{
			ReleaseReliablePendingMessage(InOutPendingMessage);
			++AbandonedReliableMessageCount;
			return;
		}

		FChannel* const Channel = FindChannel(InOutPendingMessage.ChannelNameId);
		if (Channel == nullptr || Channel->Information.TransportDevice == nullptr)
		{
			ReleaseReliablePendingMessage(InOutPendingMessage);
			++AbandonedReliableMessageCount;
			return;
		}

		// Device refusal is intentionally ignored: each later interval retries until acknowledgement or the bounded attempt budget ends.
		(void)Channel->Information.TransportDevice->TrySend(
			Channel->Information.Address, Core::TSpan<const std::uint8_t>(InOutPendingMessage.FrameBytes, InOutPendingMessage.FrameSize));
		++InOutPendingMessage.SendAttempts;
		InOutPendingMessage.LastAttemptMilliseconds = InNowMilliseconds;
	}

	/**
	 * Motivation: Keeps subscriber routing identical for local sends and decoded inbound messages.
	 * Responsibilities: Reclaim dead owners before routing, then synchronously invoke every matching live subscriber while skipping registrations
	 *   added during this in-flight delivery. Order follows the slot array, which is registration order until a released slot is reused.
	 */
	void DeliverToMatchingSubscribers(const FMessage& InMessage, const FNameId InChannelNameId) noexcept
	{
		const std::uint32_t SequenceAtDispatchStart = NextSubscriptionSequence;
		for (std::size_t SlotIndex = 0; SlotIndex < TTraits::MaxSubscriptions; ++SlotIndex)
		{
			FSubscriptionSlot& Slot = SubscriptionSlots[SlotIndex];
			if (!Slot.bIsOccupied)
			{
				continue;
			}

			if (Slot.SubscriptionSequence >= SequenceAtDispatchStart)
			{
				continue;
			}

			if (!Slot.Owner.IsLive())
			{
				ReleaseSubscriptionSlot(Slot);
				++ReclaimedDeadOwnerSubscriptionCount;
				continue;
			}

			if (Slot.ChannelNameId != InChannelNameId
				|| (Slot.MessageNameFilter != InvalidNameId && Slot.MessageNameFilter != InMessage.GetMessageNameId()))
			{
				continue;
			}

			(void)Slot.Subscriber.Execute(InMessage);
		}
	}

	/**
	 * Motivation: Maps transport acceptance outcomes into the public Messaging result vocabulary in one place.
	 * Responsibilities: Preserve successful sends, report device backpressure as Full, and collapse invalid or unavailable devices into Invalid.
	 */
	static EMessagingResult MapTransportSendResult(const Core::ETransportResult InTransportResult) noexcept
	{
		switch (InTransportResult)
		{
			case Core::ETransportResult::Success:
				return EMessagingResult::Success;
			case Core::ETransportResult::Full:
				return EMessagingResult::Full;
			case Core::ETransportResult::Invalid:
			case Core::ETransportResult::Unavailable:
				return EMessagingResult::Invalid;
		}

		return EMessagingResult::Invalid;
	}

	/**
	 * Motivation: Makes the fixed wire frame contract explicit at the only boundary that writes frame metadata to a device.
	 * Responsibilities: Write channel and message name ids in little-endian order before a caller adds its message or control payload.
	 */
	static void EncodeFrameHeader(const FNameId InChannelNameId, const FNameId InMessageNameId, std::uint8_t* const OutFrameBytes) noexcept
	{
		WriteNameIdLittleEndian(InChannelNameId, &OutFrameBytes[ChannelNameIdByteIndex]);
		WriteNameIdLittleEndian(InMessageNameId, &OutFrameBytes[MessageNameIdByteIndex]);
	}

	/**
	 * Motivation: Lets the inbound turn drain one device without making any channel holding it consume a second time.
	 * Responsibilities: Receive complete packets until the device reports a non-success result, decoding and routing every successful packet, and
	 *   count a packet the device refuses to fit in this system's frame budget.
	 */
	void DrainDevice(Core::ITransportDevice& InTransportDevice) noexcept
	{
		std::uint8_t FrameBytes[MaxFrameBytes]{};
		Core::FDeviceAddress Sender;
		Core::FReceiveResult ReceiveResult;
		for (;;)
		{
			const Core::ETransportResult ReceiveStatus =
				InTransportDevice.TryReceive(Sender, Core::TSpan<std::uint8_t>(FrameBytes, MaxFrameBytes), ReceiveResult);
			if (ReceiveStatus == Core::ETransportResult::Full)
			{
				// The device keeps a packet larger than this system's frame budget, so the same packet is counted again every
				// turn. A count rising with no delivery means a peer sends frames larger than this system's traits allow.
				++DroppedFrameCount;
				return;
			}

			if (ReceiveStatus != Core::ETransportResult::Success)
			{
				return;
			}

			ProcessReceivedFrame(Sender, FrameBytes, ReceiveResult.BytesReceived);
		}
	}

	/**
	 * Motivation: Routes one successfully received device packet through reliable control handling or subscriber delivery.
	 * Responsibilities: Count and discard frames shorter than the header or naming no channel; consume acknowledgement control traffic; otherwise
	 *   decode the channel's best-effort or reliable application payload while the local frame buffer remains valid for the whole call.
	 */
	void ProcessReceivedFrame(const Core::FDeviceAddress& InSender, const std::uint8_t* const InFrameBytes, const std::size_t InFrameSize) noexcept
	{
		if (InFrameSize < FrameHeaderBytes)
		{
			++DroppedFrameCount;
			return;
		}

		const FNameId ChannelNameId = ReadNameIdLittleEndian(&InFrameBytes[ChannelNameIdByteIndex]);
		FChannel* const Channel = FindChannel(ChannelNameId);
		if (Channel == nullptr)
		{
			++DroppedFrameCount;
			return;
		}

		const FNameId MessageNameId = ReadNameIdLittleEndian(&InFrameBytes[MessageNameIdByteIndex]);
		const std::uint8_t* const PayloadBytes = &InFrameBytes[FrameHeaderBytes];
		const std::size_t PayloadSize = InFrameSize - FrameHeaderBytes;
		if (MessageNameId == MessageAcknowledgementNameId)
		{
			ProcessAcknowledgement(ChannelNameId, PayloadBytes, PayloadSize);
			return;
		}

		if (Channel->Information.bIsReliable)
		{
			ProcessReliableMessage(*Channel, InSender, MessageNameId, PayloadBytes, PayloadSize);
			return;
		}

		DeliverReceivedMessage(InSender, ChannelNameId, MessageNameId, PayloadBytes, PayloadSize);
	}

	/**
	 * Motivation: Keeps malformed acknowledgement control traffic observable without exposing it to application subscribers.
	 * Responsibilities: Count only acknowledgements whose payload is not exactly one sequence number; release the matching reliable pending frame
	 *   when it exists, otherwise consume duplicate or late acknowledgement control traffic without counting it as a drop.
	 */
	void ProcessAcknowledgement(const FNameId InChannelNameId, const std::uint8_t* const InPayloadBytes, const std::size_t InPayloadSize) noexcept
	{
		if (InPayloadSize != SequenceNumberBytes)
		{
			++DroppedFrameCount;
			return;
		}

		const std::uint16_t SequenceNumber = static_cast<std::uint16_t>(ReadUnsignedLittleEndian(InPayloadBytes, SequenceNumberBytes));
		FPendingReliableMessage* const PendingMessage = FindReliablePendingMessage(InChannelNameId, SequenceNumber);
		if (PendingMessage != nullptr)
		{
			ReleaseReliablePendingMessage(*PendingMessage);
		}
		// No matching slot is normal: an acknowledgement may be duplicated or may arrive after this bounded system already abandoned its frame.
	}

	/**
	 * Motivation: Separates reliable frame validation and acknowledgement generation from ordinary inbound delivery.
	 * Responsibilities: Count a missing sequence sub-header, deliver only application bytes to subscribers, then send one unsequenced acknowledgement
	 *   for the received sequence number.
	 */
	void ProcessReliableMessage(
		const FChannel& InChannel,
		const Core::FDeviceAddress& InSender,
		const FNameId InMessageNameId,
		const std::uint8_t* const InPayloadBytes,
		const std::size_t InPayloadSize) noexcept
	{
		if (InPayloadSize < SequenceNumberBytes)
		{
			++DroppedFrameCount;
			return;
		}

		const std::uint16_t SequenceNumber = static_cast<std::uint16_t>(ReadUnsignedLittleEndian(InPayloadBytes, SequenceNumberBytes));
		DeliverReceivedMessage(
			InSender,
			InChannel.Information.ChannelNameId,
			InMessageNameId,
			&InPayloadBytes[SequenceNumberBytes],
			InPayloadSize - SequenceNumberBytes);
		SendAcknowledgement(InChannel, SequenceNumber);
	}

	/**
	 * Motivation: Gives best-effort and reliable decoded messages one subscriber-delivery path.
	 * Responsibilities: Build a non-owning message view over InPayloadBytes, retain InSender, and synchronously deliver it only to matching channel
	 * subscribers.
	 */
	void DeliverReceivedMessage(
		const Core::FDeviceAddress& InSender,
		const FNameId InChannelNameId,
		const FNameId InMessageNameId,
		const std::uint8_t* const InPayloadBytes,
		const std::size_t InPayloadSize) noexcept
	{
		FMessage Message;
		Message.SetMessageNameId(InMessageNameId);
		// The decoded payload points into DrainDevice's local frame buffer, so subscribers retaining it must copy it.
		Message.SetPayload(Core::TSpan<const std::uint8_t>(InPayloadBytes, InPayloadSize));
		Message.SetSender(InSender);
		DeliverToMatchingSubscribers(Message, InChannelNameId);
	}

	/**
	 * Motivation: Closes one received reliable message with the protocol acknowledgement that lets its peer stop retrying in B6b.
	 * Responsibilities: Send an unsequenced acknowledgement for InSequenceNumber through InChannel's device and ignore device refusal because the
	 *   sender's later retry policy recovers a lost acknowledgement.
	 *
	 * The acknowledgement goes to the channel's configured address, not to whoever sent the message. That is correct for the point-to-point shape
	 * a reliable channel describes today. A reliable channel hearing several peers would need to answer the actual sender instead, and not every
	 * medium reports one, so that choice belongs with a real medium rather than here.
	 */
	static void SendAcknowledgement(const FChannel& InChannel, const std::uint16_t InSequenceNumber) noexcept
	{
		if (InChannel.Information.TransportDevice == nullptr)
		{
			return;
		}

		std::uint8_t FrameBytes[MaxFrameBytes]{};
		EncodeFrameHeader(InChannel.Information.ChannelNameId, MessageAcknowledgementNameId, FrameBytes);
		WriteUnsignedLittleEndian(InSequenceNumber, &FrameBytes[FrameHeaderBytes], SequenceNumberBytes);
		// Device refusal is intentionally ignored: B6b's sender retry makes a lost acknowledgement recoverable.
		(void)InChannel.Information.TransportDevice->TrySend(
			InChannel.Information.Address, Core::TSpan<const std::uint8_t>(FrameBytes, FrameHeaderBytes + SequenceNumberBytes));
	}

	/**
	 * Motivation: Prevents a shared transport device from being drained twice when several channels use different destination addresses on it.
	 * Responsibilities: Return true only when one channel before InChannelIndex holds the identical transport-device pointer.
	 */
	bool IsDeviceUsedByEarlierChannel(const Core::ITransportDevice* const InTransportDevice, const std::size_t InChannelIndex) const noexcept
	{
		for (std::size_t EarlierChannelIndex = 0; EarlierChannelIndex < InChannelIndex; ++EarlierChannelIndex)
		{
			if (Channels[EarlierChannelIndex].Information.TransportDevice == InTransportDevice)
			{
				return true;
			}
		}

		return false;
	}

	/**
	 * Motivation: Gives every fixed-width wire value one portable, byte-order-independent encoder.
	 * Responsibilities: Write InValue's InByteCount least-significant bytes first at OutBytes.
	 */
	static void WriteUnsignedLittleEndian(const std::uint32_t InValue, std::uint8_t* const OutBytes, const std::size_t InByteCount) noexcept
	{
		for (std::size_t ByteOffset = 0; ByteOffset < InByteCount; ++ByteOffset)
		{
			const std::size_t BitShift = ByteOffset * BitsPerByte;
			OutBytes[ByteOffset] = static_cast<std::uint8_t>(InValue >> BitShift);
		}
	}

	/**
	 * Motivation: Gives every fixed-width wire value one portable, byte-order-independent decoder.
	 * Responsibilities: Read InByteCount least-significant-byte-first bytes from InBytes into one unsigned 32-bit value.
	 */
	static std::uint32_t ReadUnsignedLittleEndian(const std::uint8_t* const InBytes, const std::size_t InByteCount) noexcept
	{
		std::uint32_t Value = 0;
		for (std::size_t ByteOffset = 0; ByteOffset < InByteCount; ++ByteOffset)
		{
			const std::size_t BitShift = ByteOffset * BitsPerByte;
			Value |= static_cast<std::uint32_t>(InBytes[ByteOffset]) << BitShift;
		}

		return Value;
	}

	/**
	 * Motivation: Keeps name-id callers independent of the generic fixed-width wire encoder.
	 * Responsibilities: Write InNameId as four least-significant-byte-first bytes at OutBytes.
	 */
	static void WriteNameIdLittleEndian(const FNameId InNameId, std::uint8_t* const OutBytes) noexcept
	{
		WriteUnsignedLittleEndian(InNameId.Value, OutBytes, NameIdBytes);
	}

	/**
	 * Motivation: Keeps name-id callers independent of the generic fixed-width wire decoder.
	 * Responsibilities: Read four least-significant-byte-first bytes from InBytes into one Messaging name id.
	 */
	static FNameId ReadNameIdLittleEndian(const std::uint8_t* const InBytes) noexcept
	{
		return FNameId{ReadUnsignedLittleEndian(InBytes, NameIdBytes)};
	}

	/**
	 * Motivation: Copies payload bytes into a frame without needing a separate guard for the empty payload a block copy cannot take.
	 * Responsibilities: Copy every byte in InPayload into OutDestination in order, doing nothing for an empty payload.
	 */
	static void CopyBytes(std::uint8_t* const OutDestination, const Core::TSpan<const std::uint8_t> InPayload) noexcept
	{
		for (std::size_t PayloadByteOffset = 0; PayloadByteOffset < InPayload.Size(); ++PayloadByteOffset)
		{
			OutDestination[PayloadByteOffset] = InPayload.Data()[PayloadByteOffset];
		}
	}

	/** Motivation: Retains the reliability policy future Messaging work must consult without a global configuration. */
	FMessagingSystemInformation Information{};

	/** Motivation: Retains the newest caller-supplied turn time for sends between turns; a frame-stale stamp is harmless against retry intervals of
	 * hundreds of milliseconds and keeps Messaging free of a hidden clock. */
	Core::TimePointMilliseconds MostRecentTimeMilliseconds{0};

	/** Motivation: Owns each live channel's configuration and reliable sequence state within the compile-time channel limit. */
	Core::TStaticVector<FChannel, TTraits::MaxChannels> Channels;

	/** Motivation: Owns fixed removable subscription slots; each carries an owner token, sequence stamp, generation, and occupancy flag, so sixteen
	 * default subscriptions cost roughly four hundred bytes more than the former append-only storage. */
	FSubscriptionSlot SubscriptionSlots[TTraits::MaxSubscriptions]{};

	static_assert(
		TTraits::MaxSubscriptions < FSubscriptionHandle::InvalidIndex,
		"MaxSubscriptions must leave InvalidIndex unused, so no live slot index can be mistaken for an empty handle.");

	/** Motivation: Stamps each successful registration so active delivery skips newer subscriptions even when they reuse an earlier slot. */
	std::uint32_t NextSubscriptionSequence{1};

	/** Motivation: Counts dead-owner slots reclaimed before local routing so lifecycle cleanup remains observable. */
	std::uint32_t ReclaimedDeadOwnerSubscriptionCount{0};

	/** Motivation: Owns the largest Messaging allocation: MaxReliablePendingMessages slots, each retaining one MaxFrameBytes encoded frame plus
	 * retry metadata, so increasing pending capacity multiplies the system's frame-sized memory cost. */
	FPendingReliableMessage ReliablePendingMessages[TTraits::MaxReliablePendingMessages]{};

	/** Motivation: Counts inbound frames this Messaging system could not route, whatever the reason, so a misconfigured peer stays observable. */
	std::uint32_t DroppedFrameCount{0};

	/** Motivation: Counts reliable frames whose bounded retry ownership ended without an acknowledgement. */
	std::uint32_t AbandonedReliableMessageCount{0};
};

/** Motivation: Names the default fixed-capacity Messaging system used by engine-facing code. */
using FMessagingSystem = TMessagingSystem<>;

} // namespace MicroWorld::Messaging
