#include <MicroWorld/Platform/Host/HostWifiDevice.h>

#include "Internal/UdpSocketPlatformImplementation.h"

#include <cstdint>

namespace MicroWorld::Platform::Host
{

namespace
{

	/**
	 * File-local refcount backing the shared `FWinSockScope` socket-stack lifetime.
	 *
	 * The engine drives the host on one deterministic thread, so this is a plain
	 * scalar rather than an atomic; it exists in this translation unit so the
	 * `FWinSockScope` header stays free of both OS headers and data members.
	 */
	int GWinSockReferenceCount = 0;

} // namespace

FWinSockScope::FWinSockScope() noexcept
{
#ifdef _WIN32
	if (GWinSockReferenceCount == 0)
	{
		WSADATA Data{};
		(void)WSAStartup(MAKEWORD(2, 2), &Data);
	}
#endif
	++GWinSockReferenceCount;
}

FWinSockScope::~FWinSockScope() noexcept
{
	--GWinSockReferenceCount;
#ifdef _WIN32
	if (GWinSockReferenceCount == 0)
	{
		(void)WSACleanup();
	}
#endif
}

FHostWifiDevice::FHostWifiDevice(const std::uint16_t InBindPort) noexcept
{
	const FOpenedSocket Opened = OpenBoundLoopbackUdpSocket(InBindPort);
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

FHostWifiDevice::~FHostWifiDevice() noexcept
{
	if (bOpen)
	{
		CloseSocket(AsSocketHandle(SocketHandle));
	}
}

Transport::ETransportResult FHostWifiDevice::TrySend(
	const Transport::Address::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept
{
	if (!bOpen)
	{
		return Transport::ETransportResult::Unavailable;
	}
	// Validate every argument before any syscall so a rejection is truly transactional.
	if (!Transport::IsUdpAddress(InTo))
	{
		return Transport::ETransportResult::Invalid;
	}
	const std::size_t PacketSize = InPacket.Size();
	if (PacketSize > UdpMaxPacketBytes)
	{
		return Transport::ETransportResult::Invalid;
	}
	if (PacketSize != 0 && InPacket.Data() == nullptr)
	{
		return Transport::ETransportResult::Invalid;
	}
	const sockaddr_in Destination = MakeSockAddrIn(InTo.Bytes[0], InTo.Bytes[1], InTo.Bytes[2], InTo.Bytes[3], Transport::UdpAddressPort(InTo));
	const ESendOutcome Outcome = SendDatagram(AsSocketHandle(SocketHandle), InPacket.Data(), PacketSize, Destination);
	switch (Outcome)
	{
		case ESendOutcome::Success:
			return Transport::ETransportResult::Success;
		case ESendOutcome::WouldBlock:
			return Transport::ETransportResult::Full;
		case ESendOutcome::Error:
		default:
			return Transport::ETransportResult::Invalid;
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
	Transport::ETransportResult ProbeAndClassify(const FSocketHandle InSocket, const std::size_t InCapacity) noexcept
	{
		const FPeekProbe Probe = ProbeReadableDatagram(InSocket);
		switch (Probe.Status)
		{
			case EPeekStatus::WouldBlock:
				return Transport::ETransportResult::Unavailable;
			case EPeekStatus::Error:
				return Transport::ETransportResult::Invalid;
			case EPeekStatus::Ready:
				break;
		}
		// Single fits-vs-Full decision: the caller's destination is untouched on Full.
		if (Probe.BytesReady > InCapacity)
		{
			return Transport::ETransportResult::Full;
		}
		return Transport::ETransportResult::Success;
	}

} // namespace

Transport::ETransportResult FHostWifiDevice::TryReceive(
	Transport::Address::FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, Transport::Device::FReceiveResult& OutResult) noexcept
{
	// Keep the sizing scratch and the advertised max in lockstep; both are 1200.
	static_assert(PeekScratchBytes == FHostWifiDevice::UdpMaxPacketBytes, "Peek scratch must match the advertised packet maximum.");

	if (!bOpen)
	{
		return Transport::ETransportResult::Unavailable;
	}
	// Reject a null destination with nonzero length before touching the socket.
	const std::size_t Capacity = InDestination.Size();
	if (Capacity != 0 && InDestination.Data() == nullptr)
	{
		return Transport::ETransportResult::Invalid;
	}
	const Transport::ETransportResult Classification = ProbeAndClassify(AsSocketHandle(SocketHandle), Capacity);
	if (Classification != Transport::ETransportResult::Success)
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
		return Transport::ETransportResult::Unavailable;
	}
	const std::uint32_t PackedIpv4Address = ntohl(Sender.sin_addr.s_addr);
	OutFrom = Transport::MakeUdpAddressFromPackedHostOrder(PackedIpv4Address, ntohs(Sender.sin_port));
	OutResult.BytesReceived = Consumed.BytesReceived;
	return Transport::ETransportResult::Success;
}

std::size_t FHostWifiDevice::MaxPacketBytes() const noexcept
{
	return UdpMaxPacketBytes;
}

bool FHostWifiDevice::IsOpen() const noexcept
{
	return bOpen;
}

std::uint16_t FHostWifiDevice::BoundPort() const noexcept
{
	return BoundPortValue;
}

bool FHostWifiDevice::PollReadable(const Core::DurationMilliseconds InTimeoutMilliseconds) const noexcept
{
	if (!bOpen)
	{
		return false;
	}
	return WaitForReadable(AsSocketHandle(SocketHandle), InTimeoutMilliseconds);
}

} // namespace MicroWorld::Platform::Host
