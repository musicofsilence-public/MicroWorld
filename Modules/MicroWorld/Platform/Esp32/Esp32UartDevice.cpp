#include <MicroWorld/Platform/Esp32/Esp32UartConfig.h>
#include <MicroWorld/Platform/Esp32/Esp32UartDevice.h>
#include <MicroWorld/Platform/Esp32/UartAddress.h>

#include "Internal/OpenedUart.h"
#include "Internal/UartPort.h"
#include "Internal/UartReadStatus.h"
#include "Internal/UartWriteOutcome.h"

#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/EFrameEvent.h>
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

	/**
	 * Motivation: Translates one ESP-IDF-normalized UART write outcome into the shared device result so the device
	 *   body never inspects platform codes.
	 * Responsibilities: Map Sent to Success, WouldBlock to Full, and anything else to Invalid.
	 */
	Core::ETransportResult MapUartWriteOutcome(const EUartWriteOutcome InOutcome) noexcept
	{
		switch (InOutcome)
		{
			case EUartWriteOutcome::Sent:
				return Core::ETransportResult::Success;
			case EUartWriteOutcome::WouldBlock:
				return Core::ETransportResult::Full;
			case EUartWriteOutcome::Error:
			default:
				return Core::ETransportResult::Invalid;
		}
	}

} // namespace

Core::ETransportResult FEsp32UartDevice::TrySend(const Core::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept
{
	if (!bOpen)
	{
		return Core::ETransportResult::Unavailable;
	}
	const Core::ETransportResult Validation = ValidateOutgoingPacket(InTo, InPacket);
	if (Validation != Core::ETransportResult::Success)
	{
		return Validation;
	}
	// The codec is transactional on failure.
	std::uint8_t Frame[UartMaxPayloadBytes + Transport::FrameCodec::FrameOverheadBytes];
	std::size_t Written = 0;
	const Core::ETransportResult EncodeResult =
		Transport::FrameCodec::EncodeFrame(LocalNodeIdValue, InPacket, Core::TSpan<std::uint8_t>(Frame, sizeof(Frame)), Written);
	if (EncodeResult != Core::ETransportResult::Success)
	{
		return EncodeResult;
	}
	const EUartWriteOutcome Outcome = WriteUart(AsUartPort(UartPortNumber), Frame, Written);
	return MapUartWriteOutcome(Outcome);
}

Core::ETransportResult FEsp32UartDevice::ValidateOutgoingPacket(
	const Core::FDeviceAddress& InTo, const Core::TSpan<const std::uint8_t> InPacket) const noexcept
{
	// Validate every argument before any syscall so a rejection is truly transactional.
	if (!IsUartAddress(InTo))
	{
		return Core::ETransportResult::Invalid;
	}
	const std::size_t PacketSize = InPacket.Size();
	if (PacketSize > UartMaxPayloadBytes)
	{
		return Core::ETransportResult::Invalid;
	}
	if (PacketSize != 0 && InPacket.Data() == nullptr)
	{
		return Core::ETransportResult::Invalid;
	}
	return Core::ETransportResult::Success;
}

Core::ETransportResult FEsp32UartDevice::TryReceive(
	Core::FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, Core::FReceiveResult& OutResult) noexcept
{
	// Reject a null destination with nonzero length before any UART read.
	const std::size_t Capacity = InDestination.Size();
	if (Capacity != 0 && InDestination.Data() == nullptr)
	{
		return Core::ETransportResult::Invalid;
	}
	if (!bOpen)
	{
		return Core::ETransportResult::Unavailable;
	}
	// A frame held from a prior Full is delivered first so the decoder precondition is honored.
	if (Decoder.HasFrame())
	{
		return DeliverFrameToDestination(InDestination, OutFrom, OutResult);
	}
	return PumpDecoderForFrame(InDestination, OutFrom, OutResult);
}

Core::ETransportResult FEsp32UartDevice::DeliverFrameToDestination(
	Core::TSpan<std::uint8_t> InDestination, Core::FDeviceAddress& OutFrom, Core::FReceiveResult& OutResult) noexcept
{
	// On Full the destination is untouched and the frame stays held for the next
	// call, so a receive that cannot fit is transactional.
	const std::size_t HeldLength = Decoder.FramePayload().Size();
	if (HeldLength > InDestination.Size())
	{
		return Core::ETransportResult::Full;
	}
	std::memcpy(InDestination.Data(), Decoder.FramePayload().Data(), HeldLength);
	OutFrom = MakeUartAddress(Decoder.FrameNodeId());
	OutResult.BytesReceived = HeldLength;
	Decoder.ClearFrame();
	return Core::ETransportResult::Success;
}

Core::ETransportResult FEsp32UartDevice::PumpDecoderForFrame(
	Core::TSpan<std::uint8_t> InDestination, Core::FDeviceAddress& OutFrom, Core::FReceiveResult& OutResult) noexcept
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
			return Core::ETransportResult::Invalid;
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
	return Core::ETransportResult::Unavailable;
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
