#include <MicroWorld/PlatformHost/HostUdpDriver.h>

#include "UdpSocketGlue.h"

#include <cstdint>

namespace MicroWorld
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

FHostUdpDriver::FHostUdpDriver(const std::uint16_t BindPort) noexcept
{
	const Detail::FOpenedSocket Opened = Detail::OpenBoundLoopbackUdpSocket(BindPort);
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

FHostUdpDriver::~FHostUdpDriver() noexcept
{
	if (bOpen)
	{
		Detail::CloseSocket(Detail::AsSocketHandle(SocketHandle));
	}
}

ENetResult FHostUdpDriver::TrySend(const FNetAddress& To, TSpan<const std::uint8_t> Packet) noexcept
{
	if (!bOpen)
	{
		return ENetResult::Unavailable;
	}
	// Validate every argument before any syscall so a rejection is truly transactional.
	if (!IsUdpAddress(To))
	{
		return ENetResult::Invalid;
	}
	const std::size_t PacketSize = Packet.Size();
	if (PacketSize > UdpMaxPacketBytes)
	{
		return ENetResult::Invalid;
	}
	if (PacketSize != 0 && Packet.Data() == nullptr)
	{
		return ENetResult::Invalid;
	}
	const sockaddr_in Destination = Detail::MakeSockAddrIn(To.Bytes[0], To.Bytes[1], To.Bytes[2], To.Bytes[3], UdpAddressPort(To));
	const Detail::ESendOutcome Outcome = Detail::SendDatagram(Detail::AsSocketHandle(SocketHandle), Packet.Data(), PacketSize, Destination);
	switch (Outcome)
	{
		case Detail::ESendOutcome::Success:
			return ENetResult::Success;
		case Detail::ESendOutcome::WouldBlock:
			return ENetResult::Full;
		case Detail::ESendOutcome::Error:
		default:
			return ENetResult::Invalid;
	}
}

ENetResult FHostUdpDriver::TryReceive(FNetAddress& OutFrom, TSpan<std::uint8_t> Destination, FNetReceiveResult& OutResult) noexcept
{
	// Keep the sizing scratch and the advertised max in lockstep; both are 1200.
	static_assert(Detail::PeekScratchBytes == FHostUdpDriver::UdpMaxPacketBytes, "Peek scratch must match the advertised packet maximum.");

	if (!bOpen)
	{
		return ENetResult::Unavailable;
	}
	// Reject a null destination with nonzero length before touching the socket.
	const std::size_t Capacity = Destination.Size();
	if (Capacity != 0 && Destination.Data() == nullptr)
	{
		return ENetResult::Invalid;
	}
	// Size the head datagram without consuming or touching the caller's destination.
	const Detail::FPeekProbe Probe = Detail::ProbeReadableDatagram(Detail::AsSocketHandle(SocketHandle));
	switch (Probe.Status)
	{
		case Detail::EPeekStatus::WouldBlock:
			return ENetResult::Unavailable;
		case Detail::EPeekStatus::Error:
			return ENetResult::Invalid;
		case Detail::EPeekStatus::Ready:
			break;
	}
	// Single fits-vs-Full decision: the caller's destination is untouched on Full.
	if (Probe.BytesReady > Capacity)
	{
		return ENetResult::Full;
	}
	// The datagram fits: perform one consuming read to deliver the bytes and the sender.
	sockaddr_in Sender{};
	const Detail::FConsumeResult Consumed = Detail::ConsumeDatagram(Detail::AsSocketHandle(SocketHandle), Destination.Data(), Capacity, Sender);
	if (!Consumed.bSuccess)
	{
		// A peer may have evicted the probed datagram; treat that race as transient.
		return ENetResult::Unavailable;
	}
	const std::uint32_t Packed = ntohl(Sender.sin_addr.s_addr);
	OutFrom = MakeUdpAddress(
		static_cast<std::uint8_t>(Packed >> 24),
		static_cast<std::uint8_t>(Packed >> 16),
		static_cast<std::uint8_t>(Packed >> 8),
		static_cast<std::uint8_t>(Packed),
		ntohs(Sender.sin_port));
	OutResult.BytesReceived = Consumed.BytesReceived;
	return ENetResult::Success;
}

std::size_t FHostUdpDriver::MaxPacketBytes() const noexcept
{
	return UdpMaxPacketBytes;
}

bool FHostUdpDriver::IsOpen() const noexcept
{
	return bOpen;
}

std::uint16_t FHostUdpDriver::BoundPort() const noexcept
{
	return BoundPortValue;
}

bool FHostUdpDriver::PollReadable(const DurationMilliseconds TimeoutMilliseconds) const noexcept
{
	if (!bOpen)
	{
		return false;
	}
	return Detail::WaitForReadable(Detail::AsSocketHandle(SocketHandle), TimeoutMilliseconds);
}

} // namespace MicroWorld
