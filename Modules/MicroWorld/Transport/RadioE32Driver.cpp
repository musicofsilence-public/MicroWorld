#include <MicroWorld/Transport/RadioE32Driver.h>

#include <MicroWorld/Transport/E32Lora.h>
#include <MicroWorld/Transport/FrameCodec.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld
{

namespace
{

	/** Limits receive work so a UART flood cannot monopolize one caller iteration. */
	constexpr std::size_t ReceivePumpByteCap = 2u * (E32MaxPayloadBytes + FrameOverheadBytes);

	/** Bounds one transmit progress call to one fixed encoded E32 frame. */
	constexpr std::size_t TransmitProgressByteCap = E32MaxPayloadBytes + FrameOverheadBytes;

} // namespace

FRadioE32Driver::FRadioE32Driver(IUartByteStream& InByteStream) noexcept : ByteStream(InByteStream) {}

ETransportResult FRadioE32Driver::Initialize(const std::uint8_t InLocalNodeId) noexcept
{
	if (bInitialized)
	{
		return ETransportResult::Unavailable;
	}

	LocalNodeIdValue = InLocalNodeId;
	bInitialized = true;
	return ETransportResult::Success;
}

ETransportResult FRadioE32Driver::TrySend(const FDeviceAddress& InTo, const TSpan<const std::uint8_t> InPacket) noexcept
{
	if (!bInitialized)
	{
		return ETransportResult::Unavailable;
	}

	return TransportState.TryQueueFrame(LocalNodeIdValue, InTo, InPacket);
}

ETransportResult FRadioE32Driver::TryReceive(FDeviceAddress& OutFrom, const TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept
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
		const EUartByteStreamResult ReadResult = ByteStream.TryReadByte(ReceivedByte);
		if (ReadResult == EUartByteStreamResult::Unavailable)
		{
			return ETransportResult::Unavailable;
		}
		if (ReadResult == EUartByteStreamResult::Error)
		{
			return ETransportResult::Invalid;
		}

		const EFrameEvent Event = TransportState.PushReceivedByte(ReceivedByte);
		if (Event == EFrameEvent::FrameReady)
		{
			return TransportState.TryDeliverReceivedFrame(OutFrom, InDestination, OutResult);
		}
	}

	return ETransportResult::Unavailable;
}

std::size_t FRadioE32Driver::MaxPacketBytes() const noexcept
{
	return E32MaxPayloadBytes;
}

void FRadioE32Driver::AdvanceTransmit() noexcept
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

		const EUartByteStreamResult WriteResult = ByteStream.TryWriteByte(NextByte);
		if (WriteResult == EUartByteStreamResult::Unavailable)
		{
			return;
		}
		if (WriteResult == EUartByteStreamResult::Error)
		{
			TransportState.DiscardTransmitFrame();
			return;
		}

		TransportState.CommitTransmitByte();
	}
}

bool FRadioE32Driver::IsInitialized() const noexcept
{
	return bInitialized;
}

} // namespace MicroWorld
