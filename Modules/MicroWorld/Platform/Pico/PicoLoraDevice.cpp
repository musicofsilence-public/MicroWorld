#include <MicroWorld/Platform/Pico/Internal/PicoUartConfig.h>
#include <MicroWorld/Platform/Pico/PicoE32LoraConfig.h>
#include <MicroWorld/Platform/Pico/PicoLoraDevice.h>

namespace MicroWorld::Platform::Pico
{

FPicoLoraDevice::FPicoLoraDevice(IPicoE32LoraPlatform& InPlatform) noexcept : ByteStream(InPlatform), RadioDevice(ByteStream) {}

FPicoLoraDevice::~FPicoLoraDevice() noexcept = default;

::MicroWorld::Core::ETransportResult FPicoLoraDevice::Initialize(const FPicoE32LoraConfig& InConfig) noexcept
{
	if (IsOpen())
	{
		return ::MicroWorld::Core::ETransportResult::Unavailable;
	}

	const FPicoUartConfig UartConfig{InConfig.UartIndex, InConfig.TxGpio, InConfig.RxGpio, InConfig.BaudRate};
	if (!ByteStream.Open(UartConfig))
	{
		return ::MicroWorld::Core::ETransportResult::Invalid;
	}

	const ::MicroWorld::Core::ETransportResult InitializeResult = RadioDevice.Initialize(InConfig.LocalNodeId);
	if (InitializeResult != ::MicroWorld::Core::ETransportResult::Success)
	{
		ByteStream.Close();
	}

	return InitializeResult;
}

::MicroWorld::Core::ETransportResult FPicoLoraDevice::TrySend(
	const Core::FDeviceAddress& InTo, const Core::TSpan<const std::uint8_t> InPacket) noexcept
{
	return RadioDevice.TrySend(InTo, InPacket);
}

::MicroWorld::Core::ETransportResult FPicoLoraDevice::TryReceive(
	Core::FDeviceAddress& OutFrom, const Core::TSpan<std::uint8_t> InDestination, Core::FReceiveResult& OutResult) noexcept
{
	return RadioDevice.TryReceive(OutFrom, InDestination, OutResult);
}

std::size_t FPicoLoraDevice::MaxPacketBytes() const noexcept
{
	return RadioDevice.MaxPacketBytes();
}

void FPicoLoraDevice::PreAdvance(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	RadioDevice.PreAdvance(InNowMilliseconds);
}

bool FPicoLoraDevice::IsOpen() const noexcept
{
	return ByteStream.IsOpen() && RadioDevice.IsInitialized();
}

} // namespace MicroWorld::Platform::Pico
