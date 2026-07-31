#include <MicroWorld/Transport/Lora/E32LoraDevice.h>

#include <MicroWorld/Transport/Lora/E32Lora.h>
#include <MicroWorld/Transport/FrameCodec.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Transport
{

namespace
{

	/** Motivation: Limits receive work so a UART flood cannot monopolize one caller iteration. */
	constexpr std::size_t ReceivePumpByteCap = 2u * (E32MaxPayloadBytes + ::MicroWorld::Transport::FrameCodec::FrameOverheadBytes);

	/** Motivation: Bounds one transmit progress call to one fixed encoded E32 frame. */
	constexpr std::size_t TransmitProgressByteCap = E32MaxPayloadBytes + ::MicroWorld::Transport::FrameCodec::FrameOverheadBytes;

} // namespace

FE32LoraDevice::FE32LoraDevice(Core::IUartByteStream& InByteStream) noexcept : ByteStream(InByteStream) {}

ETransportResult FE32LoraDevice::Initialize(const std::uint8_t InLocalNodeId) noexcept
{
	if (bInitialized)
	{
		return ETransportResult::Unavailable;
	}

	LocalNodeIdValue = InLocalNodeId;
	bInitialized = true;
	return ETransportResult::Success;
}

ETransportResult FE32LoraDevice::TrySend(
	const ::MicroWorld::Transport::Address::FDeviceAddress& InTo, const Core::TSpan<const std::uint8_t> InPacket) noexcept
{
	if (!bInitialized)
	{
		return ETransportResult::Unavailable;
	}

	return TransportState.TryQueueFrame(LocalNodeIdValue, InTo, InPacket);
}

ETransportResult FE32LoraDevice::TryReceive(
	::MicroWorld::Transport::Address::FDeviceAddress& OutFrom,
	const Core::TSpan<std::uint8_t> InDestination,
	::MicroWorld::Transport::Device::FReceiveResult& OutResult) noexcept
{
	if (InDestination.Size() != 0 && InDestination.Data() == nullptr)
	{
		return ETransportResult::Invalid;
	}
	if (!bInitialized)
	{
		return ETransportResult::Unavailable;
	}
	if (TransportState.HasReceivedFrame())
	{
		return TransportState.TryDeliverReceivedFrame(OutFrom, InDestination, OutResult);
	}

	for (std::size_t PumpedBytes = 0; PumpedBytes < ReceivePumpByteCap; ++PumpedBytes)
	{
		std::uint8_t ReceivedByte = 0;
		const Core::EUartByteStreamResult ReadResult = ByteStream.TryReadByte(ReceivedByte);
		if (ReadResult == Core::EUartByteStreamResult::Unavailable)
		{
			return ETransportResult::Unavailable;
		}
		if (ReadResult == Core::EUartByteStreamResult::Error)
		{
			return ETransportResult::Invalid;
		}

		const ::MicroWorld::Transport::FrameCodec::EFrameEvent Event = TransportState.PushReceivedByte(ReceivedByte);
		if (Event == ::MicroWorld::Transport::FrameCodec::EFrameEvent::FrameReady)
		{
			return TransportState.TryDeliverReceivedFrame(OutFrom, InDestination, OutResult);
		}
	}

	return ETransportResult::Unavailable;
}

std::size_t FE32LoraDevice::MaxPacketBytes() const noexcept
{
	return E32MaxPayloadBytes;
}

void FE32LoraDevice::AdvanceTransmit() noexcept
{
	if (!bInitialized)
	{
		return;
	}

	for (std::size_t Attempt = 0; Attempt < TransmitProgressByteCap; ++Attempt)
	{
		std::uint8_t NextByte = 0;
		if (!TransportState.TryPeekTransmitByte(NextByte))
		{
			return;
		}

		const Core::EUartByteStreamResult WriteResult = ByteStream.TryWriteByte(NextByte);
		if (WriteResult == Core::EUartByteStreamResult::Unavailable)
		{
			return;
		}
		if (WriteResult == Core::EUartByteStreamResult::Error)
		{
			TransportState.DiscardTransmitFrame();
			return;
		}

		TransportState.CommitTransmitByte();
	}
}

bool FE32LoraDevice::IsInitialized() const noexcept
{
	return bInitialized;
}

} // namespace MicroWorld::Transport
