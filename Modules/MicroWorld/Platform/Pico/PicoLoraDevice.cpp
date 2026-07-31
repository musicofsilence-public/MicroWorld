#include <MicroWorld/Platform/Pico/PicoLoraDevice.h>

namespace MicroWorld
{

FPicoLoraDevice::FPicoLoraDevice(IPicoE32LoraPlatform& InPlatform) noexcept : ByteStream(InPlatform), RadioDevice(ByteStream) {}

FPicoLoraDevice::~FPicoLoraDevice() noexcept = default;

::MicroWorld::Transport::ETransportResult FPicoLoraDevice::Initialize(const FPicoE32LoraConfig& InConfig) noexcept
{
	if (IsOpen())
	{
		return ::MicroWorld::Transport::ETransportResult::Unavailable;
	}

	const FPicoUartConfig UartConfig{InConfig.UartIndex, InConfig.TxGpio, InConfig.RxGpio, InConfig.BaudRate};
	if (!ByteStream.Open(UartConfig))
	{
		return ::MicroWorld::Transport::ETransportResult::Invalid;
	}

	const ::MicroWorld::Transport::ETransportResult InitializeResult = RadioDevice.Initialize(InConfig.LocalNodeId);
	if (InitializeResult != ::MicroWorld::Transport::ETransportResult::Success)
	{
		ByteStream.Close();
	}

	return InitializeResult;
}

::MicroWorld::Transport::ETransportResult FPicoLoraDevice::TrySend(
	const ::MicroWorld::Transport::Address::FDeviceAddress& InTo, const TSpan<const std::uint8_t> InPacket) noexcept
{
	return RadioDevice.TrySend(InTo, InPacket);
}

::MicroWorld::Transport::ETransportResult FPicoLoraDevice::TryReceive(
	::MicroWorld::Transport::Address::FDeviceAddress& OutFrom,
	const TSpan<std::uint8_t> InDestination,
	::MicroWorld::Transport::Device::FReceiveResult& OutResult) noexcept
{
	return RadioDevice.TryReceive(OutFrom, InDestination, OutResult);
}

std::size_t FPicoLoraDevice::MaxPacketBytes() const noexcept
{
	return RadioDevice.MaxPacketBytes();
}

void FPicoLoraDevice::AdvanceTransmit() noexcept
{
	RadioDevice.AdvanceTransmit();
}

bool FPicoLoraDevice::IsOpen() const noexcept
{
	return ByteStream.IsOpen() && RadioDevice.IsInitialized();
}

} // namespace MicroWorld
