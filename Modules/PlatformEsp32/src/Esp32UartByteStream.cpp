#include <MicroWorld/PlatformEsp32/Detail/Esp32UartByteStream.h>

#include "UartPlatformImplementation.h"

namespace MicroWorld::Detail
{

FEsp32UartByteStream::~FEsp32UartByteStream() noexcept
{
	Close();
}

bool FEsp32UartByteStream::Open(const FEsp32UartByteStreamConfig& InConfig) noexcept
{
	if (bOpen)
	{
		return false;
	}

	const FUartPort Port = AsUartPort(InConfig.UartPort);
	const FOpenedUart Opened = OpenConfiguredUartPort(Port, InConfig.TxGpio, InConfig.RxGpio, InConfig.BaudRate);
	if (!Opened.bOpen)
	{
		return false;
	}

	UartPortNumber = InConfig.UartPort;
	bOpen = true;
	return true;
}

void FEsp32UartByteStream::Close() noexcept
{
	if (!bOpen)
	{
		return;
	}

	CloseUart(AsUartPort(UartPortNumber));
	UartPortNumber = 0;
	bOpen = false;
}

bool FEsp32UartByteStream::IsOpen() const noexcept
{
	return bOpen;
}

EUartByteStreamResult FEsp32UartByteStream::TryWriteByte(const std::uint8_t InByte) noexcept
{
	if (!bOpen)
	{
		return EUartByteStreamResult::Error;
	}

	const EUartWriteOutcome Outcome = TryWriteUartByte(AsUartPort(UartPortNumber), InByte);
	switch (Outcome)
	{
		case EUartWriteOutcome::Sent:
			return EUartByteStreamResult::Success;
		case EUartWriteOutcome::WouldBlock:
			return EUartByteStreamResult::Unavailable;
		case EUartWriteOutcome::Error:
		default:
			return EUartByteStreamResult::Error;
	}
}

EUartByteStreamResult FEsp32UartByteStream::TryReadByte(std::uint8_t& OutByte) noexcept
{
	if (!bOpen)
	{
		return EUartByteStreamResult::Error;
	}

	std::uint8_t CandidateByte = 0;
	const EUartReadStatus Status = ReadUartByte(AsUartPort(UartPortNumber), CandidateByte);
	switch (Status)
	{
		case EUartReadStatus::GotByte:
			OutByte = CandidateByte;
			return EUartByteStreamResult::Success;
		case EUartReadStatus::WouldBlock:
			return EUartByteStreamResult::Unavailable;
		case EUartReadStatus::Error:
		default:
			return EUartByteStreamResult::Error;
	}
}

} // namespace MicroWorld::Detail
