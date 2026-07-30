#include <MicroWorld/Transport/Detail/E32LoraTransportState.h>

#include <cstring>

namespace MicroWorld::Detail
{

ENetResult FE32LoraTransportState::TryQueueFrame(
	const std::uint8_t InLocalNodeId, const FNetAddress& InTo, const TSpan<const std::uint8_t> InPacket) noexcept
{
	if (!IsLoraAddress(InTo))
	{
		return ENetResult::Invalid;
	}
	const std::size_t PacketSize = InPacket.Size();
	if (PacketSize > E32MaxPayloadBytes || (PacketSize != 0 && InPacket.Data() == nullptr))
	{
		return ENetResult::Invalid;
	}
	if (HasPendingTransmit())
	{
		return ENetResult::Full;
	}

	std::size_t WrittenBytes = 0;
	const ENetResult EncodeResult = EncodeFrame(InLocalNodeId, InPacket, TSpan<std::uint8_t>(TransmitFrame, sizeof(TransmitFrame)), WrittenBytes);
	if (EncodeResult != ENetResult::Success)
	{
		return EncodeResult;
	}

	TransmitFrameLength = WrittenBytes;
	NextTransmitByteIndex = 0;
	return ENetResult::Success;
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

EFrameEvent FE32LoraTransportState::PushReceivedByte(const std::uint8_t InByte) noexcept
{
	if (Decoder.HasFrame())
	{
		return EFrameEvent::FrameReady;
	}
	return Decoder.PushByte(InByte);
}

bool FE32LoraTransportState::HasReceivedFrame() const noexcept
{
	return Decoder.HasFrame();
}

ENetResult FE32LoraTransportState::TryDeliverReceivedFrame(
	FNetAddress& OutFrom, const TSpan<std::uint8_t> InDestination, FNetReceiveResult& OutResult) noexcept
{
	if (InDestination.Size() != 0 && InDestination.Data() == nullptr)
	{
		return ENetResult::Invalid;
	}
	if (!Decoder.HasFrame())
	{
		return ENetResult::Unavailable;
	}

	const TSpan<const std::uint8_t> Payload = Decoder.FramePayload();
	const std::size_t PayloadBytes = Payload.Size();
	if (PayloadBytes > InDestination.Size())
	{
		return ENetResult::Full;
	}

	const FNetAddress Sender = MakeLoraAddress(Decoder.FrameNodeId());
	if (PayloadBytes != 0)
	{
		std::memcpy(InDestination.Data(), Payload.Data(), PayloadBytes);
	}
	OutFrom = Sender;
	OutResult.BytesReceived = PayloadBytes;
	Decoder.ClearFrame();
	return ENetResult::Success;
}

} // namespace MicroWorld::Detail
