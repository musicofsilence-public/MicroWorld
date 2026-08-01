#pragma once

#include <MicroWorld/Core/Containers/StaticVector.h>
#include <MicroWorld/Core/Delegates/Delegate.h>
#include <MicroWorld/Core/PlaySystem.h>
#include <MicroWorld/Core/Time.h>
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

	/**
	 * Motivation: Gives channel subscribers one bounded callable type that keeps local delivery allocation-free.
	 * Responsibilities: Receive a message by const reference and treat its payload span as valid only for the duration of the call; subscribers that
	 * retain bytes must copy them.
	 */
	using FSubscriberDelegate = Core::TDelegate<void(const FMessage&), TTraits::MaxSubscriberCallableBytes>;

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
	EMessagingResult SubscribeToChannel(const FNameId InChannelNameId, FSubscriberDelegate&& InSubscriber) noexcept
	{
		return SubscribeToChannel(InChannelNameId, InvalidNameId, std::move(InSubscriber));
	}

	/**
	 * Motivation: Lets a caller receive only one named kind of message sent locally through one existing channel.
	 * Responsibilities: Validate the channel and bound subscriber before storing a filtered registration within the fixed system-wide subscription
	 * capacity.
	 */
	EMessagingResult SubscribeToChannel(const FNameId InChannelNameId, const FNameId InMessageNameFilter, FSubscriberDelegate&& InSubscriber) noexcept
	{
		if (FindChannel(InChannelNameId) == nullptr)
		{
			return EMessagingResult::NotFound;
		}

		if (!InSubscriber.IsBound())
		{
			return EMessagingResult::Invalid;
		}

		// Capacity exhaustion is the only way Emplace fails, so its result is the whole capacity rule.
		return Subscriptions.Emplace(InChannelNameId, InMessageNameFilter, std::move(InSubscriber)) == Core::ERuntimeResult::Success
			? EMessagingResult::Success
			: EMessagingResult::Full;
	}

	/**
	 * Motivation: Makes locally composed Messaging useful before any transport device exists and extends it with best-effort remote reach.
	 * Responsibilities: Reject an unset message name or missing channel, then synchronously deliver locally once to every matching subscriber before
	 * attempting one complete wire frame through the channel's device, if any; return a transport-capacity or invalid-request result without undoing
	 * that local delivery.
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
			// Wrapping at the 16-bit range is intentional; reusing a number for a different attempted frame is not.
			++Channel->NextOutgoingSequenceNumber;
		}

		return MapTransportSendResult(TransportSendResult);
	}

	/**
	 * Motivation: Pumps inbound best-effort frames before the world advances so this node's subscribers see device input in the current turn.
	 * Responsibilities: Drain each distinct channel device once, route each complete frame by its encoded channel name, and count malformed or
	 * unroutable frames without asserting; leave InNowMilliseconds unused until reliability needs caller-supplied time.
	 */
	void PreAdvance(Core::TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		(void)InNowMilliseconds;

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
	 * Motivation: Reserves the outbound Messaging lifecycle turn required after world advancement.
	 * Responsibilities: Perform no work until later reliability work adds retry processing; never advance a transport device's lifecycle turns.
	 */
	void PostAdvance(Core::TimePointMilliseconds InNowMilliseconds) noexcept override { (void)InNowMilliseconds; }

	/**
	 * Motivation: Lets callers observe inbound frames this system received but could not route to a live channel.
	 * Responsibilities: Return the cumulative count of frames shorter than a header, naming an unknown channel, or too large for this system's frame
	 * budget, without changing system state.
	 */
	std::uint32_t GetDroppedFrameCount() const noexcept { return DroppedFrameCount; }

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
	 * Motivation: Keeps each local subscriber's routing criteria and its bounded callable together in flat system-owned storage.
	 * Responsibilities: Retain one channel identity, optional message-name filter, and uniquely owned callable without allocating.
	 * Example:
	 *   FSubscription Subscription{"Telemetry", InvalidNameId, std::move(Subscriber)};
	 */
	struct FSubscription final
	{
		/**
		 * Motivation: Lets the system publish one fully configured local subscription in a single fixed-storage insertion.
		 * Responsibilities: Retain the supplied channel, optional filter, and uniquely owned bound callable without allocating.
		 */
		FSubscription(const FNameId InChannelNameId, const FNameId InMessageNameFilter, FSubscriberDelegate&& InSubscriber) noexcept
			: ChannelNameId(InChannelNameId), MessageNameFilter(InMessageNameFilter), Subscriber(std::move(InSubscriber))
		{
		}

		/** Motivation: Identifies the channel whose local sends can reach this subscriber. */
		FNameId ChannelNameId{};

		/** Motivation: Narrows delivery to one message name, with the unset id accepting every message on the channel. */
		FNameId MessageNameFilter{};

		/** Motivation: Owns the inline callable that observes matching local messages. */
		FSubscriberDelegate Subscriber{};
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
	 * Motivation: Keeps subscriber routing identical for local sends and decoded inbound messages.
	 * Responsibilities: Synchronously invoke every subscriber matching InChannelNameId and InMessage's optional name filter in registration order;
	 *   capture the subscription count before dispatch so a callback can add a subscriber without changing this in-flight iteration.
	 */
	void DeliverToMatchingSubscribers(const FMessage& InMessage, const FNameId InChannelNameId) noexcept
	{
		// The captured count excludes registrations added by a subscriber during this synchronous delivery.
		const std::size_t SubscriptionCountAtDispatchStart = Subscriptions.Size();
		for (std::size_t SubscriptionIndex = 0; SubscriptionIndex < SubscriptionCountAtDispatchStart; ++SubscriptionIndex)
		{
			FSubscription& Subscription = Subscriptions[SubscriptionIndex];
			if (Subscription.ChannelNameId != InChannelNameId
				|| (Subscription.MessageNameFilter != InvalidNameId && Subscription.MessageNameFilter != InMessage.GetMessageNameId()))
			{
				continue;
			}

			(void)Subscription.Subscriber.Execute(InMessage);
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
			ProcessAcknowledgement(PayloadSize);
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
	 * Responsibilities: Count only acknowledgements whose payload is not exactly one sequence number; consume a well-formed acknowledgement so B6b
	 *   can add its pending-set lookup here.
	 */
	void ProcessAcknowledgement(const std::size_t InPayloadSize) noexcept
	{
		if (InPayloadSize != SequenceNumberBytes)
		{
			++DroppedFrameCount;
			return;
		}

		// A valid acknowledgement is expected control traffic; B6b will match it against the reliable pending set here.
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

	/** Motivation: Owns each live channel's configuration and reliable sequence state within the compile-time channel limit. */
	Core::TStaticVector<FChannel, TTraits::MaxChannels> Channels;

	/** Motivation: Owns every local subscription in registration order within one system-wide fixed capacity. */
	Core::TStaticVector<FSubscription, TTraits::MaxSubscriptions> Subscriptions;

	/** Motivation: Counts inbound frames this Messaging system could not route, whatever the reason, so a misconfigured peer stays observable. */
	std::uint32_t DroppedFrameCount{0};
};

/** Motivation: Names the default fixed-capacity Messaging system used by engine-facing code. */
using FMessagingSystem = TMessagingSystem<>;

} // namespace MicroWorld::Messaging
