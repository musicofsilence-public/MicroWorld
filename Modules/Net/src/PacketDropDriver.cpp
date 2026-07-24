#include <MicroWorld/Net/PacketDropDriver.h>

namespace MicroWorld
{

FPacketDropDriver::FPacketDropDriver(INetDriver& InnerDriver, const std::uint32_t DropEveryNthSend) noexcept
	: InnerDriver(InnerDriver), DropEveryNthSend(DropEveryNthSend)
{
}

/** Defines the destructor out of line so one vtable entry lives in the Net archive. */
FPacketDropDriver::~FPacketDropDriver() noexcept = default;

ENetResult FPacketDropDriver::TrySend(const FNetAddress& To, TSpan<const std::uint8_t> Packet) noexcept
{
	++SendCallCount;
	if (DropEveryNthSend != 0 && (SendCallCount % DropEveryNthSend == 0))
	{
		// A dropped send is modeled as having left the wire and been lost: no inner call, no packet inspection.
		++DroppedSendTotal;
		return ENetResult::Success;
	}
	return InnerDriver.TrySend(To, Packet);
}

ENetResult FPacketDropDriver::TryReceive(FNetAddress& OutFrom, TSpan<std::uint8_t> Destination, FNetReceiveResult& OutResult) noexcept
{
	return InnerDriver.TryReceive(OutFrom, Destination, OutResult);
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
