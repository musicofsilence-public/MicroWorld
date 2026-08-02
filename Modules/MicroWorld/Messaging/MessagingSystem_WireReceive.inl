// Out-of-class member definitions: subscriber dispatch and the inbound wire
// receive path for TMessagingSystem<TTraits>. Included from MessagingSystem.h after
// the class body closes; never include this file directly. Depends on declarations
// from MessagingSystem_ReliableRetry.inl (FindChannel) and MessagingSystem_FrameCodec.inl
// (EncodeFrameHeader, Read/WriteUnsignedLittleEndian, ReadNameIdLittleEndian).

template<typename TTraits>
void TMessagingSystem<TTraits>::DrainDevice(Core::ITransportDevice& InTransportDevice) noexcept
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

template<typename TTraits>
void TMessagingSystem<TTraits>::ProcessReceivedFrame(
	const Core::FDeviceAddress& InSender, const std::uint8_t* const InFrameBytes, const std::size_t InFrameSize) noexcept
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

template<typename TTraits>
void TMessagingSystem<TTraits>::ProcessAcknowledgement(
	const FNameId InChannelNameId, const std::uint8_t* const InPayloadBytes, const std::size_t InPayloadSize) noexcept
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

template<typename TTraits>
void TMessagingSystem<TTraits>::ProcessReliableMessage(
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
		InSender, InChannel.Information.ChannelNameId, InMessageNameId, &InPayloadBytes[SequenceNumberBytes], InPayloadSize - SequenceNumberBytes);
	SendAcknowledgement(InChannel, SequenceNumber);
}

template<typename TTraits>
void TMessagingSystem<TTraits>::DeliverReceivedMessage(
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

template<typename TTraits>
void TMessagingSystem<TTraits>::SendAcknowledgement(const FChannel& InChannel, const std::uint16_t InSequenceNumber) noexcept
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

template<typename TTraits>
bool TMessagingSystem<TTraits>::IsDeviceUsedByEarlierChannel(
	const Core::ITransportDevice* const InTransportDevice, const std::size_t InChannelIndex) const noexcept
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
