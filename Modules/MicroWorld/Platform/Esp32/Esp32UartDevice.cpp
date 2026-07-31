#include <MicroWorld/Platform/Esp32/Esp32UartDevice.h>

#include "Internal/UartPlatformImplementation.h"

#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/FrameCodec.h>

#include <cstdint>
#include <cstring>

namespace MicroWorld::Platform::Esp32
{

FEsp32UartDevice::FEsp32UartDevice(const FEsp32UartConfig& InConfig) noexcept
{
	const FUartPort Port = AsUartPort(InConfig.UartPort);
	const FOpenedUart Opened = OpenConfiguredUartPort(Port, InConfig.TxGpio, InConfig.RxGpio, InConfig.BaudRate);
	if (!Opened.bOpen)
	{
		UartPortNumber = 0;
		LocalNodeIdValue = 0;
		bOpen = false;
		return;
	}
	UartPortNumber = InConfig.UartPort;
	LocalNodeIdValue = InConfig.LocalNodeId;
	bOpen = true;
}

FEsp32UartDevice::~FEsp32UartDevice() noexcept
{
	if (bOpen)
	{
		CloseUart(AsUartPort(UartPortNumber));
	}
}

namespace
{

	/** Maps one UART write outcome to the shared device result. */
	Transport::ETransportResult MapUartWriteOutcome(const EUartWriteOutcome InOutcome) noexcept
	{
		switch (InOutcome)
		{
			case EUartWriteOutcome::Sent:
				return Transport::ETransportResult::Success;
			case EUartWriteOutcome::WouldBlock:
				return Transport::ETransportResult::Full;
			case EUartWriteOutcome::Error:
			default:
				return Transport::ETransportResult::Invalid;
		}
	}

} // namespace

Transport::ETransportResult FEsp32UartDevice::TrySend(
	const Transport::Address::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept
{
	if (!bOpen)
	{
		return Transport::ETransportResult::Unavailable;
	}
	const Transport::ETransportResult Validation = ValidateOutgoingPacket(InTo, InPacket);
	if (Validation != Transport::ETransportResult::Success)
	{
		return Validation;
	}
	// The codec is transactional on failure.
	std::uint8_t Frame[UartMaxPayloadBytes + Transport::FrameCodec::FrameOverheadBytes];
	std::size_t Written = 0;
	const Transport::ETransportResult EncodeResult =
		Transport::FrameCodec::EncodeFrame(LocalNodeIdValue, InPacket, Core::TSpan<std::uint8_t>(Frame, sizeof(Frame)), Written);
	if (EncodeResult != Transport::ETransportResult::Success)
	{
		return EncodeResult;
	}
	const EUartWriteOutcome Outcome = WriteUart(AsUartPort(UartPortNumber), Frame, Written);
	return MapUartWriteOutcome(Outcome);
}

Transport::ETransportResult FEsp32UartDevice::ValidateOutgoingPacket(
	const Transport::Address::FDeviceAddress& InTo, const Core::TSpan<const std::uint8_t> InPacket) const noexcept
{
	// Validate every argument before any syscall so a rejection is truly transactional.
	if (!IsUartAddress(InTo))
	{
		return Transport::ETransportResult::Invalid;
	}
	const std::size_t PacketSize = InPacket.Size();
	if (PacketSize > UartMaxPayloadBytes)
	{
		return Transport::ETransportResult::Invalid;
	}
	if (PacketSize != 0 && InPacket.Data() == nullptr)
	{
		return Transport::ETransportResult::Invalid;
	}
	return Transport::ETransportResult::Success;
}

Transport::ETransportResult FEsp32UartDevice::TryReceive(
	Transport::Address::FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, Transport::Device::FReceiveResult& OutResult) noexcept
{
	// Reject a null destination with nonzero length before any UART read.
	const std::size_t Capacity = InDestination.Size();
	if (Capacity != 0 && InDestination.Data() == nullptr)
	{
		return Transport::ETransportResult::Invalid;
	}
	if (!bOpen)
	{
		return Transport::ETransportResult::Unavailable;
	}
	// A frame held from a prior Full is delivered first so the decoder precondition is honored.
	if (Decoder.HasFrame())
	{
		return DeliverFrameToDestination(InDestination, OutFrom, OutResult);
	}
	return PumpDecoderForFrame(InDestination, OutFrom, OutResult);
}

Transport::ETransportResult FEsp32UartDevice::DeliverFrameToDestination(
	Core::TSpan<std::uint8_t> InDestination, Transport::Address::FDeviceAddress& OutFrom, Transport::Device::FReceiveResult& OutResult) noexcept
{
	// On Full the destination is untouched and the frame stays held for the next
	// call, so a receive that cannot fit is transactional.
	const std::size_t HeldLength = Decoder.FramePayload().Size();
	if (HeldLength > InDestination.Size())
	{
		return Transport::ETransportResult::Full;
	}
	std::memcpy(InDestination.Data(), Decoder.FramePayload().Data(), HeldLength);
	OutFrom = MakeUartAddress(Decoder.FrameNodeId());
	OutResult.BytesReceived = HeldLength;
	Decoder.ClearFrame();
	return Transport::ETransportResult::Success;
}

Transport::ETransportResult FEsp32UartDevice::PumpDecoderForFrame(
	Core::TSpan<std::uint8_t> InDestination, Transport::Address::FDeviceAddress& OutFrom, Transport::Device::FReceiveResult& OutResult) noexcept
{
	// Pump available UART bytes one at a time, bounded so a flood cannot starve the caller.
	const std::size_t PumpByteCap = 2u * (UartMaxPayloadBytes + Transport::FrameCodec::FrameOverheadBytes);
	const FUartPort Port = AsUartPort(UartPortNumber);
	for (std::size_t Pumped = 0; Pumped < PumpByteCap; ++Pumped)
	{
		std::uint8_t IncomingByte = 0;
		const EUartReadStatus Status = ReadUartByte(Port, IncomingByte);
		if (Status == EUartReadStatus::WouldBlock)
		{
			break;
		}
		if (Status == EUartReadStatus::Error)
		{
			return Transport::ETransportResult::Invalid;
		}
		const Transport::FrameCodec::EFrameEvent Event = Decoder.PushByte(IncomingByte);
		if (Event == Transport::FrameCodec::EFrameEvent::FrameReady)
		{
			// A completed frame is delivered immediately; Full keeps it held for the next call.
			return DeliverFrameToDestination(InDestination, OutFrom, OutResult);
		}
		if (Event == Transport::FrameCodec::EFrameEvent::Discarded)
		{
			MW_LOG_MSG(Warning, "Uart", "decoder discarded a candidate frame (bad length or CRC)");
		}
	}
	return Transport::ETransportResult::Unavailable;
}

std::size_t FEsp32UartDevice::MaxPacketBytes() const noexcept
{
	return UartMaxPayloadBytes;
}

bool FEsp32UartDevice::IsOpen() const noexcept
{
	return bOpen;
}

} // namespace MicroWorld::Platform::Esp32
