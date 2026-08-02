// Out-of-class member definitions: reliable-delivery tracking and retry policy for
// TMessagingSystem<TTraits>. Included from MessagingSystem.h after the class body
// closes; never include this file directly.

template<typename TTraits>
typename TMessagingSystem<TTraits>::FChannel* TMessagingSystem<TTraits>::FindChannel(const FNameId InChannelNameId) noexcept
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

template<typename TTraits>
typename TMessagingSystem<TTraits>::FPendingReliableMessage* TMessagingSystem<TTraits>::FindFreeReliablePendingMessage() noexcept
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

template<typename TTraits>
typename TMessagingSystem<TTraits>::FPendingReliableMessage* TMessagingSystem<TTraits>::FindReliablePendingMessage(
	const FNameId InChannelNameId, const std::uint16_t InSequenceNumber) noexcept
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

template<typename TTraits>
void TMessagingSystem<TTraits>::TrackReliableMessage(
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

template<typename TTraits>
void TMessagingSystem<TTraits>::ReleaseReliablePendingMessage(FPendingReliableMessage& InOutPendingMessage) noexcept
{
	InOutPendingMessage.bAwaitingAcknowledgement = false;
}

template<typename TTraits>
void TMessagingSystem<TTraits>::ReleaseSubscriptionSlot(FSubscriptionSlot& InSlot) noexcept
{
	InSlot.bIsOccupied = false;
	++InSlot.Generation;
	if (InSlot.Generation == 0)
	{
		InSlot.Generation = 1;
	}
}

template<typename TTraits>
typename TMessagingSystem<TTraits>::FSubscriptionSlot* TMessagingSystem<TTraits>::FindFreeSubscriptionSlot() noexcept
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

template<typename TTraits>
void TMessagingSystem<TTraits>::ReclaimDeadOwnerSubscriptionSlot(FSubscriptionSlot& InSlot) noexcept
{
	ReleaseSubscriptionSlot(InSlot);
	++ReclaimedDeadOwnerSubscriptionCount;
}

template<typename TTraits>
void TMessagingSystem<TTraits>::ProcessReliablePendingMessage(
	FPendingReliableMessage& InOutPendingMessage, const Core::TimePointMilliseconds InNowMilliseconds) noexcept
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

template<typename TTraits>
TMessagingSystem<TTraits>::TMessagingSystem(const FMessagingSystemInformation& InInformation) noexcept : Information(InInformation)
{
}

template<typename TTraits>
EMessagingResult TMessagingSystem<TTraits>::CreateChannel(const FChannelInformation& InChannelInformation) noexcept
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

template<typename TTraits>
EMessagingResult TMessagingSystem<TTraits>::SubscribeToChannel(
	const FNameId InChannelNameId, FSubscriberDelegate&& InSubscriber, Core::FWeakOwner InOwner, FSubscriptionHandle* OutHandle) noexcept
{
	return SubscribeToChannel(InChannelNameId, InvalidNameId, std::move(InSubscriber), InOwner, OutHandle);
}

template<typename TTraits>
EMessagingResult TMessagingSystem<TTraits>::SubscribeToChannel(
	const FNameId InChannelNameId,
	const FNameId InMessageNameFilter,
	FSubscriberDelegate&& InSubscriber,
	Core::FWeakOwner InOwner,
	FSubscriptionHandle* OutHandle) noexcept
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

template<typename TTraits>
EMessagingResult TMessagingSystem<TTraits>::Unsubscribe(const FSubscriptionHandle InHandle) noexcept
{
	if (InHandle.Index >= TTraits::MaxSubscriptions)
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

template<typename TTraits>
void TMessagingSystem<TTraits>::UnsubscribeAll(const Core::FWeakOwner InOwner) noexcept
{
	for (FSubscriptionSlot& Slot : SubscriptionSlots)
	{
		if (Slot.bIsOccupied && Slot.Owner.IsSameOwner(InOwner))
		{
			ReleaseSubscriptionSlot(Slot);
		}
	}
}

template<typename TTraits>
void TMessagingSystem<TTraits>::DeliverToMatchingSubscribers(const FMessage& InMessage, const FNameId InChannelNameId) noexcept
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

template<typename TTraits>
EMessagingResult TMessagingSystem<TTraits>::SendMessageToChannel(const FMessage& InMessage, const FNameId InChannelNameId) noexcept
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

template<typename TTraits>
void TMessagingSystem<TTraits>::PreAdvance(Core::TimePointMilliseconds InNowMilliseconds) noexcept
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

template<typename TTraits>
void TMessagingSystem<TTraits>::PostAdvance(Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	MostRecentTimeMilliseconds = InNowMilliseconds;
	for (FPendingReliableMessage& PendingMessage : ReliablePendingMessages)
	{
		ProcessReliablePendingMessage(PendingMessage, InNowMilliseconds);
	}
}
