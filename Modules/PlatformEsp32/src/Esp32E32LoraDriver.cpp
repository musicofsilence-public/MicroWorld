#include <MicroWorld/PlatformEsp32/Esp32E32LoraDriver.h>

#include "E32UartGlue.h"

#include <MicroWorld/Log.h>
#include <MicroWorld/Net/FrameCodec.h>

#include <cstdint>
#include <cstring>

namespace MicroWorld
{

FEsp32E32LoraDriver::FEsp32E32LoraDriver(const FEsp32E32LoraConfig& Config) noexcept
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

FEsp32E32LoraDriver::~FEsp32E32LoraDriver() noexcept
{
	if (bOpen)
	{
		Detail::CloseUart(Detail::AsUartPort(UartPortNumber));
	}
}

ENetResult FEsp32E32LoraDriver::TrySend(const FNetAddress& To, TSpan<const std::uint8_t> Packet) noexcept
{
	if (!bOpen)
	{
		return ENetResult::Unavailable;
	}
	// Validate every argument before any syscall so a rejection is truly transactional.
	if (!IsLoraAddress(To))
	{
		return ENetResult::Invalid;
	}
	const std::size_t PacketSize = Packet.Size();
	if (PacketSize > E32MaxPayloadBytes)
	{
		return ENetResult::Invalid;
	}
	if (PacketSize != 0 && Packet.Data() == nullptr)
	{
		return ENetResult::Invalid;
	}
	// Encode the full frame into a stack buffer; the codec is transactional on failure.
	std::uint8_t Frame[E32MaxPayloadBytes + FrameOverheadBytes];
	std::size_t Written = 0;
	const ENetResult EncodeResult = EncodeFrame(LocalNodeIdValue, Packet, TSpan<std::uint8_t>(Frame, sizeof(Frame)), Written);
	if (EncodeResult != ENetResult::Success)
	{
		return EncodeResult;
	}
	const Detail::EUartWriteOutcome Outcome = Detail::WriteUart(Detail::AsUartPort(UartPortNumber), Frame, Written);
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

ENetResult FEsp32E32LoraDriver::TryReceive(FNetAddress& OutFrom, TSpan<std::uint8_t> Destination, FNetReceiveResult& OutResult) noexcept
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
		const std::size_t HeldLength = Decoder.FramePayload().Size();
		if (HeldLength > Capacity)
		{
			return ENetResult::Full;
		}
		std::memcpy(Destination.Data(), Decoder.FramePayload().Data(), HeldLength);
		OutFrom = MakeLoraAddress(Decoder.FrameNodeId());
		OutResult.BytesReceived = HeldLength;
		Decoder.ClearFrame();
		return ENetResult::Success;
	}
	// Pump available UART bytes one at a time, bounded so a flood cannot starve the caller.
	const std::size_t PumpByteCap = 2u * (E32MaxPayloadBytes + FrameOverheadBytes);
	const Detail::FUartPort Port = Detail::AsUartPort(UartPortNumber);
	for (std::size_t Pumped = 0; Pumped < PumpByteCap; ++Pumped)
	{
		std::uint8_t Byte = 0;
		const Detail::EUartReadStatus Status = Detail::ReadUartByte(Port, Byte);
		if (Status == Detail::EUartReadStatus::WouldBlock)
		{
			break;
		}
		if (Status == Detail::EUartReadStatus::Error)
		{
			return ENetResult::Invalid;
		}
		const EFrameEvent Event = Decoder.PushByte(Byte);
		if (Event == EFrameEvent::FrameReady)
		{
			// Deliver the held frame into the caller's destination, honoring the transactional Full contract.
			const std::size_t HeldLength = Decoder.FramePayload().Size();
			if (HeldLength > Capacity)
			{
				return ENetResult::Full;
			}
			std::memcpy(Destination.Data(), Decoder.FramePayload().Data(), HeldLength);
			OutFrom = MakeLoraAddress(Decoder.FrameNodeId());
			OutResult.BytesReceived = HeldLength;
			Decoder.ClearFrame();
			return ENetResult::Success;
		}
		if (Event == EFrameEvent::Discarded)
		{
			MW_LOG_MSG(Warning, "E32Lora", "decoder discarded a candidate frame (bad length or CRC)");
		}
	}
	return ENetResult::Unavailable;
}

std::size_t FEsp32E32LoraDriver::MaxPacketBytes() const noexcept
{
	return E32MaxPayloadBytes;
}

bool FEsp32E32LoraDriver::IsOpen() const noexcept
{
	return bOpen;
}

} // namespace MicroWorld
