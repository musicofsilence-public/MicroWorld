#include <MicroWorld/Platform/Esp32/Esp32WifiDevice.h>

#include "Internal/Esp32SocketPlatformImplementation.h"

#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

FEsp32WifiDevice::FEsp32WifiDevice(const std::uint16_t InBindPort) noexcept
{
	const FOpenedSocket Opened = OpenBoundUdpSocket(InBindPort);
	if (!Opened.bOpen)
	{
		SocketHandle = 0;
		BoundPortValue = 0;
		bOpen = false;
		return;
	}
	SocketHandle = AsOpaqueHandle(Opened.Handle);
	BoundPortValue = Opened.BoundPort;
	bOpen = true;
}

FEsp32WifiDevice::~FEsp32WifiDevice() noexcept
{
	if (bOpen)
	{
		CloseSocket(AsSocketHandle(SocketHandle));
	}
}

Core::ETransportResult FEsp32WifiDevice::TrySend(const Core::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept
{
	if (!bOpen)
	{
		return Core::ETransportResult::Unavailable;
	}
	// Validate every argument before any syscall so a rejection is truly transactional.
	if (!Transport::IsUdpAddress(InTo))
	{
		return Core::ETransportResult::Invalid;
	}
	const std::size_t PacketSize = InPacket.Size();
	if (PacketSize > UdpMaxPacketBytes)
	{
		return Core::ETransportResult::Invalid;
	}
	if (PacketSize != 0 && InPacket.Data() == nullptr)
	{
		return Core::ETransportResult::Invalid;
	}
	const sockaddr_in Destination = MakeSockAddrIn(InTo.Bytes[0], InTo.Bytes[1], InTo.Bytes[2], InTo.Bytes[3], Transport::UdpAddressPort(InTo));
	const ESendOutcome Outcome = SendDatagram(AsSocketHandle(SocketHandle), InPacket.Data(), PacketSize, Destination);
	switch (Outcome)
	{
		case ESendOutcome::Success:
			return Core::ETransportResult::Success;
		case ESendOutcome::WouldBlock:
			return Core::ETransportResult::Full;
		case ESendOutcome::Error:
		default:
			return Core::ETransportResult::Invalid;
	}
}

namespace
{

	/**
	 * Motivation: Sizes the head datagram and folds every pre-consume verdict into one result so the consuming
	 *   read stays transactional.
	 * Responsibilities: Return Unavailable when nothing is queued, Invalid on a socket error, Full when the head
	 *   datagram cannot fit the caller's capacity (left unconsumed), or Success when a datagram is ready and fits.
	 */
	Core::ETransportResult ProbeAndClassify(const FSocketHandle InSocket, const std::size_t InCapacity) noexcept
	{
		const FPeekProbe Probe = ProbeReadableDatagram(InSocket);
		switch (Probe.Status)
		{
			case EPeekStatus::WouldBlock:
				return Core::ETransportResult::Unavailable;
			case EPeekStatus::Error:
				return Core::ETransportResult::Invalid;
			case EPeekStatus::Ready:
				break;
		}
		// Single fits-vs-Full decision: the caller's destination is untouched on Full.
		if (Probe.BytesReady > InCapacity)
		{
			return Core::ETransportResult::Full;
		}
		return Core::ETransportResult::Success;
	}

} // namespace

Core::ETransportResult FEsp32WifiDevice::TryReceive(
	Core::FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, Core::FReceiveResult& OutResult) noexcept
{
	// Keep the sizing scratch and the advertised max in lockstep; both are 1200.
	static_assert(PeekScratchBytes == FEsp32WifiDevice::UdpMaxPacketBytes, "Peek scratch must match the advertised packet maximum.");

	if (!bOpen)
	{
		return Core::ETransportResult::Unavailable;
	}
	// Reject a null destination with nonzero length before touching the socket.
	const std::size_t Capacity = InDestination.Size();
	if (Capacity != 0 && InDestination.Data() == nullptr)
	{
		return Core::ETransportResult::Invalid;
	}
	const Core::ETransportResult Classification = ProbeAndClassify(AsSocketHandle(SocketHandle), Capacity);
	if (Classification != Core::ETransportResult::Success)
	{
		return Classification;
	}
	// The fits check already passed on the peeked head datagram; this consuming
	// read removes exactly that datagram.
	sockaddr_in Sender{};
	const FConsumeResult Consumed = ConsumeDatagram(AsSocketHandle(SocketHandle), InDestination.Data(), Capacity, Sender);
	if (!Consumed.bSuccess)
	{
		// A peer may have evicted the probed datagram; treat that race as transient.
		return Core::ETransportResult::Unavailable;
	}
	const std::uint32_t PackedIpv4Address = ntohl(Sender.sin_addr.s_addr);
	OutFrom = Transport::MakeUdpAddressFromPackedHostOrder(PackedIpv4Address, ntohs(Sender.sin_port));
	OutResult.BytesReceived = Consumed.BytesReceived;
	return Core::ETransportResult::Success;
}

std::size_t FEsp32WifiDevice::MaxPacketBytes() const noexcept
{
	return UdpMaxPacketBytes;
}

bool FEsp32WifiDevice::IsOpen() const noexcept
{
	return bOpen;
}

std::uint16_t FEsp32WifiDevice::BoundPort() const noexcept
{
	return BoundPortValue;
}

bool FEsp32WifiDevice::PollReadable(const Core::DurationMilliseconds InTimeoutMilliseconds) const noexcept
{
	if (!bOpen)
	{
		return false;
	}
	return WaitForReadable(AsSocketHandle(SocketHandle), InTimeoutMilliseconds);
}

} // namespace MicroWorld::Platform::Esp32
