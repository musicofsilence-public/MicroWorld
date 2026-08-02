#include <MicroWorld/Transport/PacketDropDevice.h>

namespace MicroWorld::Transport
{

FPacketDropDevice::FPacketDropDevice(Core::ITransportDevice& InInnerDevice, const std::uint32_t InDropEveryNthSend) noexcept
	: InnerDevice(InInnerDevice), DropEveryNthSend(InDropEveryNthSend)
{
}

/**
 * Motivation: Anchors the FPacketDropDevice vtable in one translation unit so its destructor entry lives in the Transport archive.
 * Responsibilities: Emit one out-of-line virtual destructor definition without side effects.
 */
FPacketDropDevice::~FPacketDropDevice() noexcept = default;

Core::ETransportResult FPacketDropDevice::TrySend(const Core::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept
{
	++SendCallCount;
	if (DropEveryNthSend != 0 && (SendCallCount % DropEveryNthSend == 0))
	{
		// A dropped send is modeled as having left the wire and been lost: no inner call, no packet inspection.
		++DroppedSendTotal;
		return Core::ETransportResult::Success;
	}
	return InnerDevice.TrySend(InTo, InPacket);
}

Core::ETransportResult FPacketDropDevice::TryReceive(
	Core::FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, Core::FReceiveResult& OutResult) noexcept
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

} // namespace MicroWorld::Transport
