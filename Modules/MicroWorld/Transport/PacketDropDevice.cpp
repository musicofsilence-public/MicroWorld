#include <MicroWorld/Transport/PacketDropDevice.h>

namespace MicroWorld
{

FPacketDropDevice::FPacketDropDevice(IDevice& InInnerDevice, const std::uint32_t InDropEveryNthSend) noexcept
	: InnerDevice(InInnerDevice), DropEveryNthSend(InDropEveryNthSend)
{
}

/** Defines the destructor out of line so one vtable entry lives in the Transport archive. */
FPacketDropDevice::~FPacketDropDevice() noexcept = default;

ETransportResult FPacketDropDevice::TrySend(const FDeviceAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept
{
	++SendCallCount;
	if (DropEveryNthSend != 0 && (SendCallCount % DropEveryNthSend == 0))
	{
		// A dropped send is modeled as having left the wire and been lost: no inner call, no packet inspection.
		++DroppedSendTotal;
		return ETransportResult::Success;
	}
	return InnerDevice.TrySend(InTo, InPacket);
}

ETransportResult FPacketDropDevice::TryReceive(FDeviceAddress& OutFrom, TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept
{
	return InnerDevice.TryReceive(OutFrom, InDestination, OutResult);
}

std::size_t FPacketDropDevice::MaxPacketBytes() const noexcept
{
	return InnerDevice.MaxPacketBytes();
}

std::uint32_t FPacketDropDevice::DroppedSendCount() const noexcept
{
	return DroppedSendTotal;
}

} // namespace MicroWorld
