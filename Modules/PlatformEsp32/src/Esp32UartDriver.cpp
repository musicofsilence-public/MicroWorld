#include <MicroWorld/PlatformEsp32/Esp32UartDriver.h>

#include "E32UartPlatformImplementation.h"

#include <MicroWorld/Log.h>
#include <MicroWorld/Net/FrameCodec.h>

#include <cstdint>
#include <cstring>

namespace MicroWorld
{

FEsp32UartDriver::FEsp32UartDriver(const FEsp32UartConfig& Config) noexcept
{
	const Detail::FUartPort Port = Detail::AsUartPort(Config.UartPort);
	const Detail::FOpenedUart Opened = Detail::OpenConfiguredUartPort(Port, Config.TxGpio, Config.RxGpio, Config.BaudRate);
	if (!Opened.bOpen)
	{
		UartPortNumber = 0;
		LocalNodeIdValue = 0;
		bOpen = false;
		return;
	}
	UartPortNumber = Config.UartPort;
	LocalNodeIdValue = Config.LocalNodeId;
	bOpen = true;
}

FEsp32UartDriver::~FEsp32UartDriver() noexcept
{
	if (bOpen)
	{
		Detail::CloseUart(Detail::AsUartPort(UartPortNumber));
	}
}

namespace
{

	/** Maps one UART write outcome to the shared driver result. */
	ENetResult MapUartWriteOutcome(const Detail::EUartWriteOutcome Outcome) noexcept
	{
		switch (Outcome)
		{
			case Detail::EUartWriteOutcome::Sent:
				return ENetResult::Success;
			case Detail::EUartWriteOutcome::WouldBlock:
				return ENetResult::Full;
			case Detail::EUartWriteOutcome::Error:
			default:
				return ENetResult::Invalid;
		}
	}

} // namespace

ENetResult FEsp32UartDriver::TrySend(const FNetAddress& To, TSpan<const std::uint8_t> Packet) noexcept
{
	if (!bOpen)
	{
		return ENetResult::Unavailable;
	}
	const ENetResult Validation = ValidateOutgoingPacket(To, Packet);
	if (Validation != ENetResult::Success)
	{
		return Validation;
	}
	// The codec is transactional on failure.
	std::uint8_t Frame[UartMaxPayloadBytes + FrameOverheadBytes];
	std::size_t Written = 0;
	const ENetResult EncodeResult = EncodeFrame(LocalNodeIdValue, Packet, TSpan<std::uint8_t>(Frame, sizeof(Frame)), Written);
	if (EncodeResult != ENetResult::Success)
	{
		return EncodeResult;
	}
	const Detail::EUartWriteOutcome Outcome = Detail::WriteUart(Detail::AsUartPort(UartPortNumber), Frame, Written);
	return MapUartWriteOutcome(Outcome);
}

ENetResult FEsp32UartDriver::ValidateOutgoingPacket(const FNetAddress& To, const TSpan<const std::uint8_t> Packet) const noexcept
{
	// Validate every argument before any syscall so a rejection is truly transactional.
	if (!IsUartAddress(To))
	{
		return ENetResult::Invalid;
	}
	const std::size_t PacketSize = Packet.Size();
	if (PacketSize > UartMaxPayloadBytes)
	{
		return ENetResult::Invalid;
	}
	if (PacketSize != 0 && Packet.Data() == nullptr)
	{
		return ENetResult::Invalid;
	}
	return ENetResult::Success;
}

ENetResult FEsp32UartDriver::TryReceive(FNetAddress& OutFrom, TSpan<std::uint8_t> Destination, FNetReceiveResult& OutResult) noexcept
{
	// Reject a null destination with nonzero length before any UART read.
	const std::size_t Capacity = Destination.Size();
	if (Capacity != 0 && Destination.Data() == nullptr)
	{
		return ENetResult::Invalid;
	}
	if (!bOpen)
	{
		return ENetResult::Unavailable;
	}
	// A frame held from a prior Full is delivered first so the decoder precondition is honored.
	if (Decoder.HasFrame())
	{
		return DeliverFrameToDestination(Destination, OutFrom, OutResult);
	}
	return PumpDecoderForFrame(Destination, OutFrom, OutResult);
}

ENetResult FEsp32UartDriver::DeliverFrameToDestination(TSpan<std::uint8_t> Destination, FNetAddress& OutFrom, FNetReceiveResult& OutResult) noexcept
{
	// On Full the destination is untouched and the frame stays held for the next
	// call, so a receive that cannot fit is transactional.
	const std::size_t HeldLength = Decoder.FramePayload().Size();
	if (HeldLength > Destination.Size())
	{
		return ENetResult::Full;
	}
	std::memcpy(Destination.Data(), Decoder.FramePayload().Data(), HeldLength);
	OutFrom = MakeUartAddress(Decoder.FrameNodeId());
	OutResult.BytesReceived = HeldLength;
	Decoder.ClearFrame();
	return ENetResult::Success;
}

ENetResult FEsp32UartDriver::PumpDecoderForFrame(TSpan<std::uint8_t> Destination, FNetAddress& OutFrom, FNetReceiveResult& OutResult) noexcept
{
	// Pump available UART bytes one at a time, bounded so a flood cannot starve the caller.
	const std::size_t PumpByteCap = 2u * (UartMaxPayloadBytes + FrameOverheadBytes);
	const Detail::FUartPort Port = Detail::AsUartPort(UartPortNumber);
	for (std::size_t Pumped = 0; Pumped < PumpByteCap; ++Pumped)
	{
		std::uint8_t IncomingByte = 0;
		const Detail::EUartReadStatus Status = Detail::ReadUartByte(Port, IncomingByte);
		if (Status == Detail::EUartReadStatus::WouldBlock)
		{
			break;
		}
		if (Status == Detail::EUartReadStatus::Error)
		{
			return ENetResult::Invalid;
		}
		const EFrameEvent Event = Decoder.PushByte(IncomingByte);
		if (Event == EFrameEvent::FrameReady)
		{
			// A completed frame is delivered immediately; Full keeps it held for the next call.
			return DeliverFrameToDestination(Destination, OutFrom, OutResult);
		}
		if (Event == EFrameEvent::Discarded)
		{
			MW_LOG_MSG(Warning, "Uart", "decoder discarded a candidate frame (bad length or CRC)");
		}
	}
	return ENetResult::Unavailable;
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
