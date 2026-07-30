#include <MicroWorld/Platform/Pico/Detail/PicoUartByteStream.h>

#include <cstdint>

namespace MicroWorld::Detail
{

FPicoUartByteStream::FPicoUartByteStream(IPicoUartPlatform& InPlatform) noexcept : Platform(InPlatform) {}

FPicoUartByteStream::~FPicoUartByteStream() noexcept
{
	Close();
}

bool FPicoUartByteStream::Open(const FPicoUartConfig& InConfig) noexcept
{
	if (bOpen)
	{
		return false;
	}
	if (InConfig.BaudRate == 0 || !IsValidTransmitPin(InConfig.UartIndex, InConfig.TxGpio) || !IsValidReceivePin(InConfig.UartIndex, InConfig.RxGpio))
	{
		return false;
	}

	const std::uint32_t AchievedBaudRate = Platform.OpenUart(InConfig.UartIndex, InConfig.TxGpio, InConfig.RxGpio, InConfig.BaudRate);
	if (AchievedBaudRate != InConfig.BaudRate)
	{
		Platform.CloseUart(InConfig.UartIndex);
		return false;
	}

	UartIndexValue = InConfig.UartIndex;
	bOpen = true;
	return true;
}

void FPicoUartByteStream::Close() noexcept
{
	if (!bOpen)
	{
		return;
	}

	Platform.CloseUart(UartIndexValue);
	UartIndexValue = 0;
	bOpen = false;
}

bool FPicoUartByteStream::IsOpen() const noexcept
{
	return bOpen;
}

EUartByteStreamResult FPicoUartByteStream::TryWriteByte(const std::uint8_t InByte) noexcept
{
	if (!bOpen)
	{
		return EUartByteStreamResult::Error;
	}
	if (!Platform.IsUartWritable(UartIndexValue))
	{
		return EUartByteStreamResult::Unavailable;
	}

	Platform.WriteUartByte(UartIndexValue, InByte);
	return EUartByteStreamResult::Success;
}

EUartByteStreamResult FPicoUartByteStream::TryReadByte(std::uint8_t& OutByte) noexcept
{
	if (!bOpen)
	{
		return EUartByteStreamResult::Error;
	}

	std::uint8_t CandidateByte = 0;
	if (!Platform.TryReadUartByte(UartIndexValue, CandidateByte))
	{
		return EUartByteStreamResult::Unavailable;
	}

	OutByte = CandidateByte;
	return EUartByteStreamResult::Success;
}

bool FPicoUartByteStream::IsValidTransmitPin(const std::uint8_t InUartIndex, const unsigned int InPin) noexcept
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

bool FPicoUartByteStream::IsValidReceivePin(const std::uint8_t InUartIndex, const unsigned int InPin) noexcept
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

} // namespace MicroWorld::Detail
