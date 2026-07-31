#include <MicroWorld/Platform/Esp32/Esp32UartDriver.h>

#include "Internal/UartPlatformImplementation.h"

#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/FrameCodec.h>

#include <cstdint>
#include <cstring>

namespace MicroWorld
{

FEsp32UartDriver::FEsp32UartDriver(const FEsp32UartConfig& InConfig) noexcept
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

FEsp32UartDriver::~FEsp32UartDriver() noexcept
{
	if (bOpen)
	{
		CloseUart(AsUartPort(UartPortNumber));
	}
}

namespace
{

	/** Maps one UART write outcome to the shared driver result. */
	ETransportResult MapUartWriteOutcome(const EUartWriteOutcome InOutcome) noexcept
	{
		switch (InOutcome)
		{
			case EUartWriteOutcome::Sent:
				return ETransportResult::Success;
			case EUartWriteOutcome::WouldBlock:
				return ETransportResult::Full;
			case EUartWriteOutcome::Error:
			default:
				return ETransportResult::Invalid;
		}
	}

} // namespace

ETransportResult FEsp32UartDriver::TrySend(const FDeviceAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept
{
	if (!bOpen)
	{
		return ETransportResult::Unavailable;
	}
	const ETransportResult Validation = ValidateOutgoingPacket(InTo, InPacket);
	if (Validation != ETransportResult::Success)
	{
		return Validation;
	}
	// The codec is transactional on failure.
	std::uint8_t Frame[UartMaxPayloadBytes + FrameOverheadBytes];
	std::size_t Written = 0;
	const ETransportResult EncodeResult = EncodeFrame(LocalNodeIdValue, InPacket, TSpan<std::uint8_t>(Frame, sizeof(Frame)), Written);
	if (EncodeResult != ETransportResult::Success)
	{
		return EncodeResult;
	}
	const EUartWriteOutcome Outcome = WriteUart(AsUartPort(UartPortNumber), Frame, Written);
	return MapUartWriteOutcome(Outcome);
}

ETransportResult FEsp32UartDriver::ValidateOutgoingPacket(const FDeviceAddress& InTo, const TSpan<const std::uint8_t> InPacket) const noexcept
{
	// Validate every argument before any syscall so a rejection is truly transactional.
	if (!IsUartAddress(InTo))
	{
		return ETransportResult::Invalid;
	}
	const std::size_t PacketSize = InPacket.Size();
	if (PacketSize > UartMaxPayloadBytes)
	{
		return ETransportResult::Invalid;
	}
	if (PacketSize != 0 && InPacket.Data() == nullptr)
	{
		return ETransportResult::Invalid;
	}
	return ETransportResult::Success;
}

ETransportResult FEsp32UartDriver::TryReceive(FDeviceAddress& OutFrom, TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept
{
	// Reject a null destination with nonzero length before any UART read.
	const std::size_t Capacity = InDestination.Size();
	if (Capacity != 0 && InDestination.Data() == nullptr)
	{
		return ETransportResult::Invalid;
	}
	if (!bOpen)
	{
		return ETransportResult::Unavailable;
	}
	// A frame held from a prior Full is delivered first so the decoder precondition is honored.
	if (Decoder.HasFrame())
	{
		return DeliverFrameToDestination(InDestination, OutFrom, OutResult);
	}
	return PumpDecoderForFrame(InDestination, OutFrom, OutResult);
}

ETransportResult FEsp32UartDriver::DeliverFrameToDestination(
	TSpan<std::uint8_t> InDestination, FDeviceAddress& OutFrom, FReceiveResult& OutResult) noexcept
{
	// On Full the destination is untouched and the frame stays held for the next
	// call, so a receive that cannot fit is transactional.
	const std::size_t HeldLength = Decoder.FramePayload().Size();
	if (HeldLength > InDestination.Size())
	{
		return ETransportResult::Full;
	}
	std::memcpy(InDestination.Data(), Decoder.FramePayload().Data(), HeldLength);
	OutFrom = MakeUartAddress(Decoder.FrameNodeId());
	OutResult.BytesReceived = HeldLength;
	Decoder.ClearFrame();
	return ETransportResult::Success;
}

ETransportResult FEsp32UartDriver::PumpDecoderForFrame(TSpan<std::uint8_t> InDestination, FDeviceAddress& OutFrom, FReceiveResult& OutResult) noexcept
{
	// Pump available UART bytes one at a time, bounded so a flood cannot starve the caller.
	const std::size_t PumpByteCap = 2u * (UartMaxPayloadBytes + FrameOverheadBytes);
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
			return ETransportResult::Invalid;
		}
		const EFrameEvent Event = Decoder.PushByte(IncomingByte);
		if (Event == EFrameEvent::FrameReady)
		{
			// A completed frame is delivered immediately; Full keeps it held for the next call.
			return DeliverFrameToDestination(InDestination, OutFrom, OutResult);
		}
		if (Event == EFrameEvent::Discarded)
		{
			MW_LOG_MSG(Warning, "Uart", "decoder discarded a candidate frame (bad length or CRC)");
		}
	}
	return ETransportResult::Unavailable;
}

std::size_t FEsp32UartDriver::MaxPacketBytes() const noexcept
{
	return UartMaxPayloadBytes;
}

bool FEsp32UartDriver::IsOpen() const noexcept
{
	return bOpen;
}

} // namespace MicroWorld
