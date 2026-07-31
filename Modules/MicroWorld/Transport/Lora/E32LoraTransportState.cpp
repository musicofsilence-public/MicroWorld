#include <MicroWorld/Transport/Lora/Internal/E32LoraTransportState.h>

#include <cstring>

namespace MicroWorld::Transport
{

ETransportResult FE32LoraTransportState::TryQueueFrame(
	const std::uint8_t InLocalNodeId, const ::MicroWorld::Transport::Address::FDeviceAddress& InTo, const TSpan<const std::uint8_t> InPacket) noexcept
{
	if (!IsLoraAddress(InTo))
	{
		return ETransportResult::Invalid;
	}
	const std::size_t PacketSize = InPacket.Size();
	if (PacketSize > E32MaxPayloadBytes || (PacketSize != 0 && InPacket.Data() == nullptr))
	{
		return ETransportResult::Invalid;
	}
	if (HasPendingTransmit())
	{
		return ETransportResult::Full;
	}

	std::size_t WrittenBytes = 0;
	const ETransportResult EncodeResult = ::MicroWorld::Transport::FrameCodec::EncodeFrame(
		InLocalNodeId, InPacket, TSpan<std::uint8_t>(TransmitFrame, sizeof(TransmitFrame)), WrittenBytes);
	if (EncodeResult != ETransportResult::Success)
	{
		return EncodeResult;
	}

	TransmitFrameLength = WrittenBytes;
	NextTransmitByteIndex = 0;
	return ETransportResult::Success;
}

bool FE32LoraTransportState::TryPeekTransmitByte(std::uint8_t& OutByte) const noexcept
{
	if (!HasPendingTransmit())
	{
		return false;
	}
	OutByte = TransmitFrame[NextTransmitByteIndex];
	return true;
}

void FE32LoraTransportState::CommitTransmitByte() noexcept
{
	if (!HasPendingTransmit())
	{
		return;
	}

	++NextTransmitByteIndex;
	if (NextTransmitByteIndex == TransmitFrameLength)
	{
		TransmitFrameLength = 0;
		NextTransmitByteIndex = 0;
	}
}

void FE32LoraTransportState::DiscardTransmitFrame() noexcept
{
	TransmitFrameLength = 0;
	NextTransmitByteIndex = 0;
}

bool FE32LoraTransportState::HasPendingTransmit() const noexcept
{
	return TransmitFrameLength != 0;
}

::MicroWorld::Transport::FrameCodec::EFrameEvent FE32LoraTransportState::PushReceivedByte(const std::uint8_t InByte) noexcept
{
	if (Decoder.HasFrame())
	{
		return ::MicroWorld::Transport::FrameCodec::EFrameEvent::FrameReady;
	}
	return Decoder.PushByte(InByte);
}

bool FE32LoraTransportState::HasReceivedFrame() const noexcept
{
	return Decoder.HasFrame();
}

ETransportResult FE32LoraTransportState::TryDeliverReceivedFrame(
	::MicroWorld::Transport::Address::FDeviceAddress& OutFrom,
	const TSpan<std::uint8_t> InDestination,
	::MicroWorld::Transport::Device::FReceiveResult& OutResult) noexcept
{
	if (InDestination.Size() != 0 && InDestination.Data() == nullptr)
	{
		return ETransportResult::Invalid;
	}
	if (!Decoder.HasFrame())
	{
		return ETransportResult::Unavailable;
	}

	const TSpan<const std::uint8_t> Payload = Decoder.FramePayload();
	const std::size_t PayloadBytes = Payload.Size();
	if (PayloadBytes > InDestination.Size())
	{
		return ETransportResult::Full;
	}

	const ::MicroWorld::Transport::Address::FDeviceAddress Sender = MakeLoraAddress(Decoder.FrameNodeId());
	if (PayloadBytes != 0)
	{
		std::memcpy(InDestination.Data(), Payload.Data(), PayloadBytes);
	}
	OutFrom = Sender;
	OutResult.BytesReceived = PayloadBytes;
	Decoder.ClearFrame();
	return ETransportResult::Success;
}

} // namespace MicroWorld::Transport
