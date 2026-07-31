#include <MicroWorld/Platform/Pico/PicoE32LoraDriver.h>

namespace MicroWorld
{

FPicoE32LoraDriver::FPicoE32LoraDriver(Detail::IPicoE32LoraPlatform& InPlatform) noexcept : ByteStream(InPlatform), RadioDriver(ByteStream) {}

FPicoE32LoraDriver::~FPicoE32LoraDriver() noexcept = default;

ETransportResult FPicoE32LoraDriver::Initialize(const FPicoE32LoraConfig& InConfig) noexcept
{
	if (IsOpen())
	{
		return ETransportResult::Unavailable;
	}

	const Detail::FPicoUartConfig UartConfig{InConfig.UartIndex, InConfig.TxGpio, InConfig.RxGpio, InConfig.BaudRate};
	if (!ByteStream.Open(UartConfig))
	{
		return ETransportResult::Invalid;
	}

	const ETransportResult InitializeResult = RadioDriver.Initialize(InConfig.LocalNodeId);
	if (InitializeResult != ETransportResult::Success)
	{
		ByteStream.Close();
	}

	return InitializeResult;
}

ETransportResult FPicoE32LoraDriver::TrySend(const FDeviceAddress& InTo, const TSpan<const std::uint8_t> InPacket) noexcept
{
	return RadioDriver.TrySend(InTo, InPacket);
}

ETransportResult FPicoE32LoraDriver::TryReceive(FDeviceAddress& OutFrom, const TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept
{
	return RadioDriver.TryReceive(OutFrom, InDestination, OutResult);
}

std::size_t FPicoE32LoraDriver::MaxPacketBytes() const noexcept
{
	return RadioDriver.MaxPacketBytes();
}

void FPicoE32LoraDriver::AdvanceTransmit() noexcept
{
	RadioDriver.AdvanceTransmit();
}

bool FPicoE32LoraDriver::IsOpen() const noexcept
{
	return ByteStream.IsOpen() && RadioDriver.IsInitialized();
}

} // namespace MicroWorld
