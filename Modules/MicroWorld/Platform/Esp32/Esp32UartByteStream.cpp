#include <MicroWorld/Platform/Esp32/Internal/Esp32UartByteStream.h>

#include "Internal/UartPlatformImplementation.h"

namespace MicroWorld::Platform::Esp32
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

Core::EUartByteStreamResult FEsp32UartByteStream::TryWriteByte(const std::uint8_t InByte) noexcept
{
	if (!bOpen)
	{
		return Core::EUartByteStreamResult::Error;
	}

	const EUartWriteOutcome Outcome = TryWriteUartByte(AsUartPort(UartPortNumber), InByte);
	switch (Outcome)
	{
		case EUartWriteOutcome::Sent:
			return Core::EUartByteStreamResult::Success;
		case EUartWriteOutcome::WouldBlock:
			return Core::EUartByteStreamResult::Unavailable;
		case EUartWriteOutcome::Error:
		default:
			return Core::EUartByteStreamResult::Error;
	}
}

Core::EUartByteStreamResult FEsp32UartByteStream::TryReadByte(std::uint8_t& OutByte) noexcept
{
	if (!bOpen)
	{
		return Core::EUartByteStreamResult::Error;
	}

	std::uint8_t CandidateByte = 0;
	const EUartReadStatus Status = ReadUartByte(AsUartPort(UartPortNumber), CandidateByte);
	switch (Status)
	{
		case EUartReadStatus::GotByte:
			OutByte = CandidateByte;
			return Core::EUartByteStreamResult::Success;
		case EUartReadStatus::WouldBlock:
			return Core::EUartByteStreamResult::Unavailable;
		case EUartReadStatus::Error:
		default:
			return Core::EUartByteStreamResult::Error;
	}
}

} // namespace MicroWorld::Platform::Esp32
