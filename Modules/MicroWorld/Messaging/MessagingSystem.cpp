#include <MicroWorld/Messaging/MessagingSystem.h>

#include <MicroWorld/Core/RuntimeResult.h>

#include <limits>
#include <utility>

namespace MicroWorld::Messaging
{

FMessagingSystem::FChannel* FMessagingSystem::FindChannel(const FNameId InChannelNameId) noexcept
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

const FMessagingSystem::FChannel* FMessagingSystem::FindChannel(const FNameId InChannelNameId) const noexcept
{
	for (const FChannel& Channel : Channels)
	{
		if (Channel.Information.ChannelNameId == InChannelNameId)
		{
			return &Channel;
		}
	}

	return nullptr;
}

Core::ITransportDevice* FMessagingSystem::FindLinkDevice(const FMessagingLinkId InLinkId) noexcept
{
	if (!InLinkId.IsValid() || InLinkId.Index >= MaxLinks)
	{
		return nullptr;
	}

	return Links[InLinkId.Index];
}

FMessagingLinkId FMessagingSystem::FindLinkId(const Core::ITransportDevice& InTransportDevice) const noexcept
{
	for (std::size_t LinkIndex = 0; LinkIndex < MaxLinks; ++LinkIndex)
	{
		if (Links[LinkIndex] == &InTransportDevice)
		{
			return FMessagingLinkId{static_cast<std::uint8_t>(LinkIndex)};
		}
	}

	return {};
}

FMessagingSystem::FPendingReliableMessage* FMessagingSystem::FindFreeReliablePendingMessage() noexcept
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

FMessagingSystem::FPendingReliableMessage* FMessagingSystem::FindReliablePendingMessage(
	const FNameId InChannelNameId, const FMessagingRoute& InRoute, const std::uint64_t InReliableMessageId) noexcept
{
	for (FPendingReliableMessage& PendingMessage : ReliablePendingMessages)
	{
		if (PendingMessage.bAwaitingAcknowledgement && PendingMessage.ChannelNameId == InChannelNameId && PendingMessage.Route == InRoute
			&& PendingMessage.ReliableMessageId == InReliableMessageId)
		{
			return &PendingMessage;
		}
	}

	return nullptr;
}

void FMessagingSystem::TrackReliableMessage(
	FPendingReliableMessage& OutPendingMessage,
	const FNameId InChannelNameId,
	const FMessagingRoute& InRoute,
	const std::uint64_t InReliableMessageId,
	const Core::TSpan<const std::uint8_t> InFrame,
	const Core::TimePointMilliseconds InAttemptTimeMilliseconds) noexcept
{
	OutPendingMessage.ChannelNameId = InChannelNameId;
	OutPendingMessage.Route = InRoute;
	OutPendingMessage.ReliableMessageId = InReliableMessageId;
	CopyBytes(OutPendingMessage.FrameBytes, InFrame);
	OutPendingMessage.FrameSize = InFrame.Size();
	OutPendingMessage.SendAttempts = 1;
	OutPendingMessage.LastAttemptMilliseconds = InAttemptTimeMilliseconds;
	OutPendingMessage.bAwaitingAcknowledgement = true;
}

void FMessagingSystem::ReleaseReliablePendingMessage(FPendingReliableMessage& InOutPendingMessage) noexcept
{
	InOutPendingMessage.bAwaitingAcknowledgement = false;
}

void FMessagingSystem::ReleaseSubscriptionSlot(FSubscriptionSlot& InSlot) noexcept
{
	InSlot.bIsOccupied = false;
	++InSlot.Generation;
	if (InSlot.Generation == 0)
	{
		InSlot.Generation = 1;
	}
}

FMessagingSystem::FSubscriptionSlot* FMessagingSystem::FindFreeSubscriptionSlot() noexcept
{
	for (FSubscriptionSlot& Slot : SubscriptionSlots)
	{
		if (!Slot.bIsOccupied && !Slot.bIsBeingDispatched)
		{
			return &Slot;
		}
	}

	return nullptr;
}

void FMessagingSystem::ReclaimDeadOwnerSubscriptionSlot(FSubscriptionSlot& InSlot) noexcept
{
	ReleaseSubscriptionSlot(InSlot);
	++ReclaimedDeadOwnerSubscriptionCount;
}

void FMessagingSystem::ProcessReliablePendingMessage(
	FPendingReliableMessage& InOutPendingMessage, const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	if (!InOutPendingMessage.bAwaitingAcknowledgement || InNowMilliseconds < InOutPendingMessage.LastAttemptMilliseconds)
	{
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

	Core::ITransportDevice* const TransportDevice = FindLinkDevice(InOutPendingMessage.Route.LinkId);
	if (TransportDevice == nullptr)
	{
		ReleaseReliablePendingMessage(InOutPendingMessage);
		++AbandonedReliableMessageCount;
		return;
	}

	// Device refusal is intentionally ignored: each later interval retries until acknowledgement or the bounded attempt budget ends.
	(void)TransportDevice->TrySend(
		InOutPendingMessage.Route.Address, Core::TSpan<const std::uint8_t>(InOutPendingMessage.FrameBytes, InOutPendingMessage.FrameSize));
	++InOutPendingMessage.SendAttempts;
	InOutPendingMessage.LastAttemptMilliseconds = InNowMilliseconds;
}

FMessagingSystem::FMessagingSystem(const FMessagingSystemInformation& InInformation) noexcept : Information(InInformation) {}

EMessagingResult FMessagingSystem::RegisterLink(Core::ITransportDevice& InTransportDevice, FMessagingLinkId& OutLinkId) noexcept
{
	const FMessagingLinkId ExistingLinkId = FindLinkId(InTransportDevice);
	if (ExistingLinkId.IsValid())
	{
		OutLinkId = ExistingLinkId;
		return EMessagingResult::Success;
	}

	for (std::size_t LinkIndex = 0; LinkIndex < MaxLinks; ++LinkIndex)
	{
		if (Links[LinkIndex] == nullptr)
		{
			Links[LinkIndex] = &InTransportDevice;
			OutLinkId = FMessagingLinkId{static_cast<std::uint8_t>(LinkIndex)};
			return EMessagingResult::Success;
		}
	}

	return EMessagingResult::Full;
}

EMessagingResult FMessagingSystem::CreateChannel(const FChannelInformation& InChannelInformation) noexcept
{
	if (InChannelInformation.ChannelNameId == InvalidNameId)
	{
		return EMessagingResult::Invalid;
	}

	if (FindChannel(InChannelInformation.ChannelNameId) != nullptr)
	{
		return EMessagingResult::Duplicate;
	}

	if (Channels.IsFull())
	{
		return EMessagingResult::Full;
	}

	FMessagingRoute DefaultRoute{};
	if (InChannelInformation.TransportDevice != nullptr)
	{
		const EMessagingResult RegistrationResult = RegisterLink(*InChannelInformation.TransportDevice, DefaultRoute.LinkId);
		if (RegistrationResult != EMessagingResult::Success)
		{
			return RegistrationResult;
		}
		DefaultRoute.Address = InChannelInformation.Address;
	}

	(void)Channels.Emplace(InChannelInformation, DefaultRoute);
	return EMessagingResult::Success;
}

EMessagingResult FMessagingSystem::DestroyChannel(const FNameId InChannelNameId) noexcept
{
	std::size_t ChannelIndex = Channels.Size();
	for (std::size_t CandidateIndex = 0; CandidateIndex < Channels.Size(); ++CandidateIndex)
	{
		if (Channels[CandidateIndex].Information.ChannelNameId == InChannelNameId)
		{
			ChannelIndex = CandidateIndex;
			break;
		}
	}

	if (ChannelIndex == Channels.Size())
	{
		return EMessagingResult::NotFound;
	}

	for (const FSubscriptionSlot& Slot : SubscriptionSlots)
	{
		if (Slot.bIsOccupied && Slot.ChannelNameId == InChannelNameId)
		{
			return EMessagingResult::Busy;
		}
	}

	for (const FPendingReliableMessage& PendingMessage : ReliablePendingMessages)
	{
		if (PendingMessage.bAwaitingAcknowledgement && PendingMessage.ChannelNameId == InChannelNameId)
		{
			return EMessagingResult::Busy;
		}
	}

	FChannel RemainingChannels[MaxChannels]{};
	std::size_t RemainingChannelCount = 0;
	for (std::size_t ExistingIndex = 0; ExistingIndex < Channels.Size(); ++ExistingIndex)
	{
		if (ExistingIndex != ChannelIndex)
		{
			RemainingChannels[RemainingChannelCount] = Channels[ExistingIndex];
			++RemainingChannelCount;
		}
	}

	Channels.Clear();
	for (std::size_t RemainingIndex = 0; RemainingIndex < RemainingChannelCount; ++RemainingIndex)
	{
		(void)Channels.Add(RemainingChannels[RemainingIndex]);
	}

	return EMessagingResult::Success;
}

EMessagingResult FMessagingSystem::CancelReliableMessagesForChannel(const FNameId InChannelNameId) noexcept
{
	if (FindChannel(InChannelNameId) == nullptr)
	{
		return EMessagingResult::NotFound;
	}

	for (FPendingReliableMessage& PendingMessage : ReliablePendingMessages)
	{
		if (PendingMessage.bAwaitingAcknowledgement && PendingMessage.ChannelNameId == InChannelNameId)
		{
			ReleaseReliablePendingMessage(PendingMessage);
		}
	}

	return EMessagingResult::Success;
}

EMessagingResult FMessagingSystem::GetChannelTraits(const FNameId InChannelNameId, FChannelTraits& OutTraits) const noexcept
{
	const FChannel* const Channel = FindChannel(InChannelNameId);
	if (Channel == nullptr)
	{
		return EMessagingResult::NotFound;
	}

	OutTraits.bIsReliable = Channel->Information.bIsReliable;
	OutTraits.bHasDefaultRoute = Channel->DefaultRoute.IsValid();
	return EMessagingResult::Success;
}

EMessagingResult FMessagingSystem::SubscribeToChannel(
	const FNameId InChannelNameId, FSubscriberDelegate&& InSubscriber, const Core::FWeakOwner InOwner, FSubscriptionHandle* const OutHandle) noexcept
{
	return SubscribeToChannel(InChannelNameId, InvalidNameId, std::move(InSubscriber), InOwner, OutHandle);
}

EMessagingResult FMessagingSystem::SubscribeToChannel(
	const FNameId InChannelNameId,
	const FNameId InMessageNameFilter,
	FSubscriberDelegate&& InSubscriber,
	const Core::FWeakOwner InOwner,
	FSubscriptionHandle* const OutHandle) noexcept
{
	if (FindChannel(InChannelNameId) == nullptr)
	{
		return EMessagingResult::NotFound;
	}

	if (!InSubscriber.IsBound() || !InOwner.IsLive())
	{
		return EMessagingResult::Invalid;
	}

	FSubscriptionSlot* Slot = FindFreeSubscriptionSlot();
	if (Slot == nullptr)
	{
		for (FSubscriptionSlot& CandidateSlot : SubscriptionSlots)
		{
			if (CandidateSlot.bIsOccupied && !CandidateSlot.Owner.IsLive())
			{
				ReclaimDeadOwnerSubscriptionSlot(CandidateSlot);
			}
		}
		Slot = FindFreeSubscriptionSlot();
	}

	if (Slot == nullptr)
	{
		return EMessagingResult::Full;
	}

	Slot->SubscriptionSequence = NextSubscriptionSequence;
	++NextSubscriptionSequence;
	Slot->ChannelNameId = InChannelNameId;
	Slot->MessageNameFilter = InMessageNameFilter;
	Slot->Owner = InOwner;
	Slot->Subscriber = std::move(InSubscriber);
	Slot->bIsOccupied = true;
	if (OutHandle != nullptr)
	{
		OutHandle->Index = static_cast<std::uint16_t>(Slot - SubscriptionSlots);
		OutHandle->Generation = Slot->Generation;
	}

	return EMessagingResult::Success;
}

EMessagingResult FMessagingSystem::Unsubscribe(const FSubscriptionHandle InHandle) noexcept
{
	if (InHandle.Index >= MaxSubscriptions)
	{
		return EMessagingResult::NotFound;
	}

	FSubscriptionSlot& Slot = SubscriptionSlots[InHandle.Index];
	if (!Slot.bIsOccupied || InHandle.Generation != Slot.Generation)
	{
		return EMessagingResult::NotFound;
	}

	ReleaseSubscriptionSlot(Slot);
	return EMessagingResult::Success;
}

void FMessagingSystem::UnsubscribeAll(const Core::FWeakOwner InOwner) noexcept
{
	for (FSubscriptionSlot& Slot : SubscriptionSlots)
	{
		if (Slot.bIsOccupied && Slot.Owner.IsSameOwner(InOwner))
		{
			ReleaseSubscriptionSlot(Slot);
		}
	}
}

void FMessagingSystem::DeliverToMatchingSubscribers(const FMessage& InMessage, const FNameId InChannelNameId) noexcept
{
	const std::uint32_t SequenceAtDispatchStart = NextSubscriptionSequence;
	for (std::size_t SlotIndex = 0; SlotIndex < MaxSubscriptions; ++SlotIndex)
	{
		FSubscriptionSlot& Slot = SubscriptionSlots[SlotIndex];
		if (!Slot.bIsOccupied || Slot.bIsBeingDispatched || Slot.SubscriptionSequence >= SequenceAtDispatchStart)
		{
			continue;
		}

		if (!Slot.Owner.IsLive())
		{
			ReclaimDeadOwnerSubscriptionSlot(Slot);
			continue;
		}

		if (Slot.ChannelNameId != InChannelNameId
			|| (Slot.MessageNameFilter != InvalidNameId && Slot.MessageNameFilter != InMessage.GetMessageNameId()))
		{
			continue;
		}

		Slot.bIsBeingDispatched = true;
		(void)Slot.Subscriber.Execute(InMessage);
		Slot.bIsBeingDispatched = false;
	}
}

bool FMessagingSystem::IsValidApplicationMessage(const FMessage& InMessage) noexcept
{
	return InMessage.GetMessageNameId() != InvalidNameId && InMessage.GetMessageNameId() != MessageAcknowledgementNameId;
}

EMessagingResult FMessagingSystem::DeliverMessageLocally(const FMessage& InMessage, const FNameId InChannelNameId) noexcept
{
	if (!IsValidApplicationMessage(InMessage))
	{
		return EMessagingResult::Invalid;
	}

	if (FindChannel(InChannelNameId) == nullptr)
	{
		return EMessagingResult::NotFound;
	}

	DeliverToMatchingSubscribers(InMessage, InChannelNameId);
	return EMessagingResult::Success;
}

EMessagingResult FMessagingSystem::SendMessageToRoute(const FMessage& InMessage, FChannel& InChannel, const FMessagingRoute& InRoute) noexcept
{
	Core::ITransportDevice* const TransportDevice = FindLinkDevice(InRoute.LinkId);
	if (TransportDevice == nullptr)
	{
		return EMessagingResult::Invalid;
	}

	const Core::TSpan<const std::uint8_t> Payload = InMessage.GetPayload();
	const std::size_t ReliableHeaderBytes = InChannel.Information.bIsReliable ? ReliableMessageIdBytes : 0;
	const std::size_t FrameSize = FrameHeaderBytes + ReliableHeaderBytes + Payload.Size();
	if (FrameSize > MaxFrameBytes || FrameSize > TransportDevice->MaxPacketBytes())
	{
		return EMessagingResult::Full;
	}

	FPendingReliableMessage* PendingMessage = nullptr;
	if (InChannel.Information.bIsReliable)
	{
		if (NextReliableMessageId == std::numeric_limits<std::uint64_t>::max())
		{
			return EMessagingResult::Full;
		}
		PendingMessage = FindFreeReliablePendingMessage();
		if (PendingMessage == nullptr)
		{
			return EMessagingResult::Full;
		}
	}

	std::uint8_t FrameBytes[MaxFrameBytes]{};
	EncodeFrameHeader(InChannel.Information.ChannelNameId, InMessage.GetMessageNameId(), FrameBytes);
	if (InChannel.Information.bIsReliable)
	{
		WriteUnsignedLittleEndian(NextReliableMessageId, &FrameBytes[FrameHeaderBytes], ReliableMessageIdBytes);
	}
	CopyBytes(&FrameBytes[FrameHeaderBytes + ReliableHeaderBytes], Payload);

	const Core::ETransportResult TransportSendResult =
		TransportDevice->TrySend(InRoute.Address, Core::TSpan<const std::uint8_t>(FrameBytes, FrameSize));
	if (InChannel.Information.bIsReliable)
	{
		TrackReliableMessage(
			*PendingMessage,
			InChannel.Information.ChannelNameId,
			InRoute,
			NextReliableMessageId,
			Core::TSpan<const std::uint8_t>(FrameBytes, FrameSize),
			MostRecentTimeMilliseconds);
		++NextReliableMessageId;
	}

	return MapTransportSendResult(TransportSendResult);
}

EMessagingResult FMessagingSystem::SendMessageToRemoteChannel(
	const FMessage& InMessage, const FNameId InChannelNameId, const FMessagingRoute& InRoute) noexcept
{
	if (!IsValidApplicationMessage(InMessage) || !InRoute.IsValid())
	{
		return EMessagingResult::Invalid;
	}

	FChannel* const Channel = FindChannel(InChannelNameId);
	if (Channel == nullptr)
	{
		return EMessagingResult::NotFound;
	}

	return SendMessageToRoute(InMessage, *Channel, InRoute);
}

EMessagingResult FMessagingSystem::SendMessageToChannel(const FMessage& InMessage, const FNameId InChannelNameId) noexcept
{
	const EMessagingResult LocalResult = DeliverMessageLocally(InMessage, InChannelNameId);
	if (LocalResult != EMessagingResult::Success)
	{
		return LocalResult;
	}

	FChannel* const Channel = FindChannel(InChannelNameId);
	if (!Channel->DefaultRoute.IsValid())
	{
		return EMessagingResult::Success;
	}

	return SendMessageToRoute(InMessage, *Channel, Channel->DefaultRoute);
}

void FMessagingSystem::PreAdvance(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	MostRecentTimeMilliseconds = InNowMilliseconds;
	for (std::size_t LinkIndex = 0; LinkIndex < MaxLinks; ++LinkIndex)
	{
		if (Links[LinkIndex] != nullptr)
		{
			ProcessDeviceReceiveBudget(*Links[LinkIndex], FMessagingLinkId{static_cast<std::uint8_t>(LinkIndex)});
		}
	}
}

void FMessagingSystem::PostAdvance(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	MostRecentTimeMilliseconds = InNowMilliseconds;
	for (FPendingReliableMessage& PendingMessage : ReliablePendingMessages)
	{
		ProcessReliablePendingMessage(PendingMessage, InNowMilliseconds);
	}
}

} // namespace MicroWorld::Messaging
