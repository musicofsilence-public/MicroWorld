#include <MicroWorld/Messaging/MessagingSystem.h>

#include <MicroWorld/Core/IO/ReceiveResult.h>

namespace MicroWorld::Messaging
{

void FMessagingSystem::ProcessDeviceReceiveBudget(Core::ITransportDevice& InTransportDevice, const FMessagingLinkId InLinkId) noexcept
{
	std::uint8_t FrameBytes[MaxFrameBytes]{};
	Core::FDeviceAddress SenderAddress;
	Core::FReceiveResult ReceiveResult;
	for (std::size_t ReceivedFrameCount = 0; ReceivedFrameCount < Information.MaxReceiveFramesPerDevicePerAdvance; ++ReceivedFrameCount)
	{
		const Core::ETransportResult ReceiveStatus =
			InTransportDevice.TryReceive(SenderAddress, Core::TSpan<std::uint8_t>(FrameBytes, MaxFrameBytes), ReceiveResult);
		if (ReceiveStatus == Core::ETransportResult::Full)
		{
			++DroppedFrameCount;
			return;
		}

		if (ReceiveStatus != Core::ETransportResult::Success)
		{
			return;
		}

		ProcessReceivedFrame(FMessagingRoute{InLinkId, SenderAddress}, FrameBytes, ReceiveResult.BytesReceived);
	}
}

void FMessagingSystem::ProcessReceivedFrame(
	const FMessagingRoute& InSenderRoute, const std::uint8_t* const InFrameBytes, const std::size_t InFrameSize) noexcept
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
		ProcessAcknowledgement(ChannelNameId, InSenderRoute, PayloadBytes, PayloadSize);
		return;
	}

	if (Channel->Information.bIsReliable)
	{
		ProcessReliableMessage(*Channel, InSenderRoute, MessageNameId, PayloadBytes, PayloadSize);
		return;
	}

	DeliverReceivedMessage(InSenderRoute, ChannelNameId, MessageNameId, PayloadBytes, PayloadSize);
}

void FMessagingSystem::ProcessAcknowledgement(
	const FNameId InChannelNameId,
	const FMessagingRoute& InSenderRoute,
	const std::uint8_t* const InPayloadBytes,
	const std::size_t InPayloadSize) noexcept
{
	if (InPayloadSize != ReliableMessageIdBytes)
	{
		++DroppedFrameCount;
		return;
	}

	const std::uint64_t ReliableMessageId = ReadUnsignedLittleEndian(InPayloadBytes, ReliableMessageIdBytes);
	FPendingReliableMessage* const PendingMessage = FindReliablePendingMessage(InChannelNameId, InSenderRoute, ReliableMessageId);
	if (PendingMessage != nullptr)
	{
		ReleaseReliablePendingMessage(*PendingMessage);
	}
}

void FMessagingSystem::ProcessReliableMessage(
	const FChannel& InChannel,
	const FMessagingRoute& InSenderRoute,
	const FNameId InMessageNameId,
	const std::uint8_t* const InPayloadBytes,
	const std::size_t InPayloadSize) noexcept
{
	if (InPayloadSize < ReliableMessageIdBytes)
	{
		++DroppedFrameCount;
		return;
	}

	const std::uint64_t ReliableMessageId = ReadUnsignedLittleEndian(InPayloadBytes, ReliableMessageIdBytes);
	DeliverReceivedMessage(
		InSenderRoute,
		InChannel.Information.ChannelNameId,
		InMessageNameId,
		&InPayloadBytes[ReliableMessageIdBytes],
		InPayloadSize - ReliableMessageIdBytes);
	SendAcknowledgement(InChannel.Information.ChannelNameId, InSenderRoute, ReliableMessageId);
}

void FMessagingSystem::DeliverReceivedMessage(
	const FMessagingRoute& InSenderRoute,
	const FNameId InChannelNameId,
	const FNameId InMessageNameId,
	const std::uint8_t* const InPayloadBytes,
	const std::size_t InPayloadSize) noexcept
{
	FMessage Message;
	Message.SetMessageNameId(InMessageNameId);
	Message.SetPayload(Core::TSpan<const std::uint8_t>(InPayloadBytes, InPayloadSize));
	Message.SetSenderContext(InSenderRoute);
	DeliverToMatchingSubscribers(Message, InChannelNameId);
}

void FMessagingSystem::SendAcknowledgement(
	const FNameId InChannelNameId, const FMessagingRoute& InRoute, const std::uint64_t InReliableMessageId) noexcept
{
	Core::ITransportDevice* const TransportDevice = FindLinkDevice(InRoute.LinkId);
	if (TransportDevice == nullptr)
	{
		return;
	}

	std::uint8_t FrameBytes[MaxFrameBytes]{};
	EncodeFrameHeader(InChannelNameId, MessageAcknowledgementNameId, FrameBytes);
	WriteUnsignedLittleEndian(InReliableMessageId, &FrameBytes[FrameHeaderBytes], ReliableMessageIdBytes);
	(void)TransportDevice->TrySend(InRoute.Address, Core::TSpan<const std::uint8_t>(FrameBytes, FrameHeaderBytes + ReliableMessageIdBytes));
}

} // namespace MicroWorld::Messaging
