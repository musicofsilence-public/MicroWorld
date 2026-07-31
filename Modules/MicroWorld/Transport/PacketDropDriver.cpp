#include <MicroWorld/Transport/PacketDropDriver.h>

namespace MicroWorld
{

FPacketDropDriver::FPacketDropDriver(IDevice& InInnerDriver, const std::uint32_t InDropEveryNthSend) noexcept
	: InnerDriver(InInnerDriver), DropEveryNthSend(InDropEveryNthSend)
{
}

/** Defines the destructor out of line so one vtable entry lives in the Transport archive. */
FPacketDropDriver::~FPacketDropDriver() noexcept = default;

ETransportResult FPacketDropDriver::TrySend(const FDeviceAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept
{
	++SendCallCount;
	if (DropEveryNthSend != 0 && (SendCallCount % DropEveryNthSend == 0))
	{
		// A dropped send is modeled as having left the wire and been lost: no inner call, no packet inspection.
		++DroppedSendTotal;
		return ETransportResult::Success;
	}
	return InnerDriver.TrySend(InTo, InPacket);
}

ETransportResult FPacketDropDriver::TryReceive(FDeviceAddress& OutFrom, TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept
{
	return InnerDriver.TryReceive(OutFrom, InDestination, OutResult);
}

std::size_t FPacketDropDriver::MaxPacketBytes() const noexcept
{
	return InnerDriver.MaxPacketBytes();
}

std::uint32_t FPacketDropDriver::DroppedSendCount() const noexcept
{
	return DroppedSendTotal;
}

} // namespace MicroWorld
