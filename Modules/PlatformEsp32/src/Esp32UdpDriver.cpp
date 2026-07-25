#include <MicroWorld/PlatformEsp32/Esp32UdpDriver.h>

#include "Esp32SocketPlatformImplementation.h"

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

ENetResult FEsp32UdpDriver::TrySend(const FNetAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept
{
	if (!bOpen)
	{
		return ENetResult::Unavailable;
	}
	// Validate every argument before any syscall so a rejection is truly transactional.
	if (!IsUdpAddress(InTo))
	{
		return ENetResult::Invalid;
	}
	const std::size_t PacketSize = InPacket.Size();
	if (PacketSize > UdpMaxPacketBytes)
	{
		return ENetResult::Invalid;
	}
	if (PacketSize != 0 && InPacket.Data() == nullptr)
	{
		return ENetResult::Invalid;
	}
	const sockaddr_in Destination = Detail::MakeSockAddrIn(InTo.Bytes[0], InTo.Bytes[1], InTo.Bytes[2], InTo.Bytes[3], UdpAddressPort(InTo));
	const Detail::ESendOutcome Outcome = Detail::SendDatagram(Detail::AsSocketHandle(SocketHandle), InPacket.Data(), PacketSize, Destination);
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
	ENetResult ProbeAndClassify(const Detail::FSocketHandle InSocket, const std::size_t InCapacity) noexcept
	{
		const Detail::FPeekProbe Probe = Detail::ProbeReadableDatagram(InSocket);
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
		if (Probe.BytesReady > InCapacity)
		{
			return ENetResult::Full;
		}
		return ENetResult::Success;
	}

} // namespace

ENetResult FEsp32UdpDriver::TryReceive(FNetAddress& OutFrom, TSpan<std::uint8_t> InDestination, FNetReceiveResult& OutResult) noexcept
{
	// Keep the sizing scratch and the advertised max in lockstep; both are 1200.
	static_assert(Detail::PeekScratchBytes == FEsp32UdpDriver::UdpMaxPacketBytes, "Peek scratch must match the advertised packet maximum.");

	if (!bOpen)
	{
		return ENetResult::Unavailable;
	}
	// Reject a null destination with nonzero length before touching the socket.
	const std::size_t Capacity = InDestination.Size();
	if (Capacity != 0 && InDestination.Data() == nullptr)
	{
		return ENetResult::Invalid;
	}
	const ENetResult Classification = ProbeAndClassify(Detail::AsSocketHandle(SocketHandle), Capacity);
	if (Classification != ENetResult::Success)
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
		return ENetResult::Unavailable;
	}
	const std::uint32_t PackedIpv4Address = ntohl(Sender.sin_addr.s_addr);
	OutFrom = MakeUdpAddressFromPackedHostOrder(PackedIpv4Address, ntohs(Sender.sin_port));
	OutResult.BytesReceived = Consumed.BytesReceived;
	return ENetResult::Success;
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
