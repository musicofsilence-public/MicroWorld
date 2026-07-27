#include <MicroWorld/PlatformPico/PicoE32LoraDriver.h>

#include <MicroWorld/Net/E32Lora.h>
#include <MicroWorld/Net/FrameCodec.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld
{

namespace
{

	/** Limits receive work so a UART flood cannot monopolize one caller iteration. */
	constexpr std::size_t ReceivePumpByteCap = 2u * (E32MaxPayloadBytes + FrameOverheadBytes);

	/** Reports whether a GPIO can carry TX for the selected RP2040 UART. */
	bool IsValidTransmitPin(const std::uint8_t InUartIndex, const unsigned int InPin) noexcept
	{
		if (InUartIndex == 0)
		{
			return InPin == 0 || InPin == 12 || InPin == 16 || InPin == 28;
		}
		if (InUartIndex == 1)
		{
			return InPin == 4 || InPin == 8 || InPin == 20 || InPin == 24;
		}
		return false;
	}

	/** Reports whether a GPIO can carry RX for the selected RP2040 UART. */
	bool IsValidReceivePin(const std::uint8_t InUartIndex, const unsigned int InPin) noexcept
	{
		if (InUartIndex == 0)
		{
			return InPin == 1 || InPin == 13 || InPin == 17 || InPin == 29;
		}
		if (InUartIndex == 1)
		{
			return InPin == 5 || InPin == 9 || InPin == 21 || InPin == 25;
		}
		return false;
	}

} // namespace

FPicoE32LoraDriver::FPicoE32LoraDriver(Detail::IPicoE32LoraPlatform& InPlatform) noexcept : Platform(InPlatform) {}

FPicoE32LoraDriver::~FPicoE32LoraDriver() noexcept
{
	if (bOpen)
	{
		Platform.CloseUart(UartIndexValue);
	}
}

ENetResult FPicoE32LoraDriver::Initialize(const FPicoE32LoraConfig& InConfig) noexcept
{
	if (bOpen)
	{
		return ENetResult::Unavailable;
	}

	if (InConfig.BaudRate == 0 || !IsValidTransmitPin(InConfig.UartIndex, InConfig.TxGpio) || !IsValidReceivePin(InConfig.UartIndex, InConfig.RxGpio))
	{
		return ENetResult::Invalid;
	}

	const std::uint32_t AchievedBaudRate = Platform.OpenUart(InConfig.UartIndex, InConfig.TxGpio, InConfig.RxGpio, InConfig.BaudRate);
	if (AchievedBaudRate != InConfig.BaudRate)
	{
		Platform.CloseUart(InConfig.UartIndex);
		return ENetResult::Invalid;
	}

	UartIndexValue = InConfig.UartIndex;
	LocalNodeIdValue = InConfig.LocalNodeId;
	bOpen = true;
	return ENetResult::Success;
}

ENetResult FPicoE32LoraDriver::TrySend(const FNetAddress& InTo, const TSpan<const std::uint8_t> InPacket) noexcept
{
	if (!bOpen)
	{
		return ENetResult::Unavailable;
	}
	return TransportState.TryQueueFrame(LocalNodeIdValue, InTo, InPacket);
}

ENetResult FPicoE32LoraDriver::TryReceive(FNetAddress& OutFrom, const TSpan<std::uint8_t> InDestination, FNetReceiveResult& OutResult) noexcept
{
	if (InDestination.Size() != 0 && InDestination.Data() == nullptr)
	{
		return ENetResult::Invalid;
	}
	if (!bOpen)
	{
		return ENetResult::Unavailable;
	}
	if (TransportState.HasReceivedFrame())
	{
		return TransportState.TryDeliverReceivedFrame(OutFrom, InDestination, OutResult);
	}
	return PumpReceive(OutFrom, InDestination, OutResult);
}

std::size_t FPicoE32LoraDriver::MaxPacketBytes() const noexcept
{
	return E32MaxPayloadBytes;
}

void FPicoE32LoraDriver::AdvanceTransmit() noexcept
{
	if (!bOpen)
	{
		return;
	}

	std::uint8_t NextByte = 0;
	if (!TransportState.TryPeekTransmitByte(NextByte))
	{
		return;
	}

	if (!Platform.IsUartWritable(UartIndexValue))
	{
		return;
	}
	Platform.WriteUartByte(UartIndexValue, NextByte);
	TransportState.CommitTransmitByte();
}

bool FPicoE32LoraDriver::IsOpen() const noexcept
{
	return bOpen;
}

ENetResult FPicoE32LoraDriver::PumpReceive(FNetAddress& OutFrom, const TSpan<std::uint8_t> InDestination, FNetReceiveResult& OutResult) noexcept
{
	for (std::size_t PumpedBytes = 0; PumpedBytes < ReceivePumpByteCap; ++PumpedBytes)
	{
		std::uint8_t ReceivedByte = 0;
		if (!Platform.TryReadUartByte(UartIndexValue, ReceivedByte))
		{
			break;
		}

		const EFrameEvent Event = TransportState.PushReceivedByte(ReceivedByte);
		if (Event == EFrameEvent::FrameReady)
		{
			return TransportState.TryDeliverReceivedFrame(OutFrom, InDestination, OutResult);
		}
	}
	return ENetResult::Unavailable;
}

} // namespace MicroWorld
