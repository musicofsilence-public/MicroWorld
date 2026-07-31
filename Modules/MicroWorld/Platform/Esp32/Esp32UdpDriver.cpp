#include <MicroWorld/Platform/Esp32/Esp32UdpDriver.h>

#include "Detail/Esp32SocketPlatformImplementation.h"

#include <cstdint>

namespace MicroWorld
{

FEsp32UdpDriver::FEsp32UdpDriver(const std::uint16_t InBindPort) noexcept
{
	const Detail::FOpenedSocket Opened = Detail::OpenBoundUdpSocket(InBindPort);
	if (!Opened.bOpen)
	{
		SocketHandle = 0;
		BoundPortValue = 0;
		bOpen = false;
		return;
	}
	SocketHandle = Detail::AsOpaqueHandle(Opened.Handle);
	BoundPortValue = Opened.BoundPort;
	bOpen = true;
}

FEsp32UdpDriver::~FEsp32UdpDriver() noexcept
{
	if (bOpen)
	{
		Detail::CloseSocket(Detail::AsSocketHandle(SocketHandle));
	}
}

ETransportResult FEsp32UdpDriver::TrySend(const FDeviceAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept
{
	if (!bOpen)
	{
		return ETransportResult::Unavailable;
	}
	// Validate every argument before any syscall so a rejection is truly transactional.
	if (!IsUdpAddress(InTo))
	{
		return ETransportResult::Invalid;
	}
	const std::size_t PacketSize = InPacket.Size();
	if (PacketSize > UdpMaxPacketBytes)
	{
		return ETransportResult::Invalid;
	}
	if (PacketSize != 0 && InPacket.Data() == nullptr)
	{
		return ETransportResult::Invalid;
	}
	const sockaddr_in Destination = Detail::MakeSockAddrIn(InTo.Bytes[0], InTo.Bytes[1], InTo.Bytes[2], InTo.Bytes[3], UdpAddressPort(InTo));
	const Detail::ESendOutcome Outcome = Detail::SendDatagram(Detail::AsSocketHandle(SocketHandle), InPacket.Data(), PacketSize, Destination);
	switch (Outcome)
	{
		case Detail::ESendOutcome::Success:
			return ETransportResult::Success;
		case Detail::ESendOutcome::WouldBlock:
			return ETransportResult::Full;
		case Detail::ESendOutcome::Error:
		default:
			return ETransportResult::Invalid;
	}
}

namespace
{

	/**
	 * Sizes the head datagram and folds every pre-consume verdict into one result.
	 *
	 * Returns `Unavailable` when nothing is queued, `Invalid` on a socket error,
	 * and `Full` when the head datagram cannot fit the caller's capacity (the
	 * datagram is left unconsumed so the receive stays transactional). `Success`
	 * means a datagram is ready and fits, so the caller should consume it next.
	 */
	ETransportResult ProbeAndClassify(const Detail::FSocketHandle InSocket, const std::size_t InCapacity) noexcept
	{
		const Detail::FPeekProbe Probe = Detail::ProbeReadableDatagram(InSocket);
		switch (Probe.Status)
		{
			case Detail::EPeekStatus::WouldBlock:
				return ETransportResult::Unavailable;
			case Detail::EPeekStatus::Error:
				return ETransportResult::Invalid;
			case Detail::EPeekStatus::Ready:
				break;
		}
		// Single fits-vs-Full decision: the caller's destination is untouched on Full.
		if (Probe.BytesReady > InCapacity)
		{
			return ETransportResult::Full;
		}
		return ETransportResult::Success;
	}

} // namespace

ETransportResult FEsp32UdpDriver::TryReceive(FDeviceAddress& OutFrom, TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept
{
	// Keep the sizing scratch and the advertised max in lockstep; both are 1200.
	static_assert(Detail::PeekScratchBytes == FEsp32UdpDriver::UdpMaxPacketBytes, "Peek scratch must match the advertised packet maximum.");

	if (!bOpen)
	{
		return ETransportResult::Unavailable;
	}
	// Reject a null destination with nonzero length before touching the socket.
	const std::size_t Capacity = InDestination.Size();
	if (Capacity != 0 && InDestination.Data() == nullptr)
	{
		return ETransportResult::Invalid;
	}
	const ETransportResult Classification = ProbeAndClassify(Detail::AsSocketHandle(SocketHandle), Capacity);
	if (Classification != ETransportResult::Success)
	{
		return Classification;
	}
	// The fits check already passed on the peeked head datagram; this consuming
	// read removes exactly that datagram.
	sockaddr_in Sender{};
	const Detail::FConsumeResult Consumed = Detail::ConsumeDatagram(Detail::AsSocketHandle(SocketHandle), InDestination.Data(), Capacity, Sender);
	if (!Consumed.bSuccess)
	{
		// A peer may have evicted the probed datagram; treat that race as transient.
		return ETransportResult::Unavailable;
	}
	const std::uint32_t PackedIpv4Address = ntohl(Sender.sin_addr.s_addr);
	OutFrom = MakeUdpAddressFromPackedHostOrder(PackedIpv4Address, ntohs(Sender.sin_port));
	OutResult.BytesReceived = Consumed.BytesReceived;
	return ETransportResult::Success;
}

std::size_t FEsp32UdpDriver::MaxPacketBytes() const noexcept
{
	return UdpMaxPacketBytes;
}

bool FEsp32UdpDriver::IsOpen() const noexcept
{
	return bOpen;
}

std::uint16_t FEsp32UdpDriver::BoundPort() const noexcept
{
	return BoundPortValue;
}

bool FEsp32UdpDriver::PollReadable(const DurationMilliseconds InTimeoutMilliseconds) const noexcept
{
	if (!bOpen)
	{
		return false;
	}
	return Detail::WaitForReadable(Detail::AsSocketHandle(SocketHandle), InTimeoutMilliseconds);
}

} // namespace MicroWorld
