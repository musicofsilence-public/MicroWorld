#pragma once

#include "MemoryConsumerProbe.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Net/ByteReader.h>
#include <MicroWorld/Net/ByteWriter.h>
#include <MicroWorld/Net/HostLoopback.h>
#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetDriver.h>
#include <MicroWorld/Net/NetManager.h>
#include <MicroWorld/Net/NetPacketStorage.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/Version.h>

#include <array>
#include <cstddef>
#include <cstdint>

static_assert(__cplusplus >= 201703L);
static_assert(MicroWorld::Version.Major == 0);
static_assert(MicroWorld::Version.Minor == 2);
static_assert(MicroWorld::Version.Patch == 0);

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#error "The MicroWorld Net consumer must compile with exceptions disabled."
#endif

#if defined(__GXX_RTTI) || defined(_CPPRTTI)
#error "The MicroWorld Net consumer must compile with RTTI disabled."
#endif

namespace MicroWorldConsumer
{

/** Stable process exit codes that identify the exact Net public-API probe failure. */
enum class ENetConsumerExitCode : int
{
	Success = 0,
	ByteWriterOverflowDidNotReturnFull = 1,
	ByteWriterAcceptedPrefixAltered = 2,
	ByteReaderTruncatedDidNotReturnUnavailable = 3,
	ByteReaderOutputModifiedOnFailure = 4,
	LoopbackFifoOrderBroken = 5,
	LoopbackFullOverwroteHead = 6,
	LoopbackEmptyDidNotReturnUnavailable = 7,
	LoopbackTooSmallDidNotReturnFull = 8,
	ManagerQueueDidNotAcceptPacket = 9,
	ManagerAdvanceDidNotSendHead = 10,
	ManagerDriverFullDidNotRetainHead = 11,
	ManagerReceiveDidNotPropagateSuccess = 12,
	ManagerRecoveryDidNotClearBackpressure = 13,
	MemoryProfileFailureOffset = 100,
};

} // namespace MicroWorldConsumer

/** Exercises representative Core+Memory+Net public APIs without platform I/O. */
inline int RunNetConsumerProbe() noexcept
{
	using namespace MicroWorld;
	using MicroWorldConsumer::ENetConsumerExitCode;

	const int MemoryProfileResult = RunMemoryConsumerProbe();
	if (MemoryProfileResult != 0)
	{
		return static_cast<int>(ENetConsumerExitCode::MemoryProfileFailureOffset) + MemoryProfileResult;
	}

	// Byte writer: fill, observe Full past capacity, prove accepted bytes survive.
	std::array<std::uint8_t, 4> WriterStorage{};
	FByteWriter Writer(TSpan<std::uint8_t>(WriterStorage.data(), WriterStorage.size()));
	if (Writer.WriteByte(0x01) != ENetResult::Success || Writer.WriteByte(0x02) != ENetResult::Success
		|| Writer.WriteByte(0x03) != ENetResult::Success || Writer.WriteByte(0x04) != ENetResult::Success)
	{
		return static_cast<int>(ENetConsumerExitCode::ByteWriterOverflowDidNotReturnFull);
	}
	if (Writer.Remaining() != 0 || Writer.WriteByte(0xFF) != ENetResult::Full)
	{
		return static_cast<int>(ENetConsumerExitCode::ByteWriterOverflowDidNotReturnFull);
	}
	if (WriterStorage[0] != 0x01 || WriterStorage[3] != 0x04)
	{
		return static_cast<int>(ENetConsumerExitCode::ByteWriterAcceptedPrefixAltered);
	}

	// Byte reader: consume, observe Unavailable past source, prove output untouched on failure.
	const std::array<std::uint8_t, 4> Source{0x10, 0x20, 0x30, 0x40};
	FByteReader Reader(TSpan<const std::uint8_t>(Source.data(), Source.size()));
	std::array<std::uint8_t, 4> ReadDestination{};
	if (Reader.Read(TSpan<std::uint8_t>(ReadDestination.data(), ReadDestination.size())) != ENetResult::Success)
	{
		return static_cast<int>(ENetConsumerExitCode::ByteReaderTruncatedDidNotReturnUnavailable);
	}
	std::uint8_t UnusedByte = 0xEE;
	if (Reader.ReadByte(UnusedByte) != ENetResult::Invalid || UnusedByte != 0xEE)
	{
		return static_cast<int>(ENetConsumerExitCode::ByteReaderOutputModifiedOnFailure);
	}

	// Loopback: FIFO delivery, full backpressure, empty unavailable, too-small full.
	// A two-port loopback with port 0 sending to its own mailbox reproduces the single-link FIFO.
	FHostLoopback<2, 2, 4> Loopback;
	const FNetAddress LoopbackPort0 = MakeLoopbackAddress(0);
	const std::array<std::uint8_t, 2> FirstPacket{0xAA, 0xBB};
	const std::array<std::uint8_t, 2> SecondPacket{0xCC, 0xDD};
	if (Loopback.Port(0).TrySend(LoopbackPort0, TSpan<const std::uint8_t>(FirstPacket.data(), FirstPacket.size())) != ENetResult::Success
		|| Loopback.Port(0).TrySend(LoopbackPort0, TSpan<const std::uint8_t>(SecondPacket.data(), SecondPacket.size())) != ENetResult::Success)
	{
		return static_cast<int>(ENetConsumerExitCode::LoopbackFifoOrderBroken);
	}

	std::array<std::uint8_t, 4> LoopbackDestination{};
	FNetReceiveResult FirstReceive{};
	FNetAddress FirstFrom{0x42};
	if (Loopback.Port(0).TryReceive(FirstFrom, TSpan<std::uint8_t>(LoopbackDestination.data(), LoopbackDestination.size()), FirstReceive)
			!= ENetResult::Success
		|| FirstReceive.BytesReceived != 2 || LoopbackDestination[0] != 0xAA || !(FirstFrom == LoopbackPort0))
	{
		return static_cast<int>(ENetConsumerExitCode::LoopbackFifoOrderBroken);
	}

	std::array<std::uint8_t, 4> SecondDestination{};
	FNetReceiveResult SecondReceive{};
	FNetAddress SecondFrom{0x42};
	if (Loopback.Port(0).TryReceive(SecondFrom, TSpan<std::uint8_t>(SecondDestination.data(), SecondDestination.size()), SecondReceive)
			!= ENetResult::Success
		|| SecondReceive.BytesReceived != 2 || SecondDestination[0] != 0xCC || !(SecondFrom == LoopbackPort0))
	{
		return static_cast<int>(ENetConsumerExitCode::LoopbackFifoOrderBroken);
	}

	FNetReceiveResult EmptyReceive{};
	FNetAddress EmptyFrom{0x42};
	if (Loopback.Port(0).TryReceive(EmptyFrom, TSpan<std::uint8_t>(LoopbackDestination.data(), LoopbackDestination.size()), EmptyReceive)
		!= ENetResult::Unavailable)
	{
		return static_cast<int>(ENetConsumerExitCode::LoopbackEmptyDidNotReturnUnavailable);
	}

	// Full backpressure: a one-slot mailbox must reject the second send and retain the head.
	FHostLoopback<2, 1, 4> SingleLoopback;
	if (SingleLoopback.Port(0).TrySend(LoopbackPort0, TSpan<const std::uint8_t>(FirstPacket.data(), FirstPacket.size())) != ENetResult::Success
		|| SingleLoopback.Port(0).TrySend(LoopbackPort0, TSpan<const std::uint8_t>(SecondPacket.data(), SecondPacket.size())) != ENetResult::Full)
	{
		return static_cast<int>(ENetConsumerExitCode::LoopbackFullOverwroteHead);
	}

	// Too-small destination: head packet must be retained for a larger retry.
	std::array<std::uint8_t, 1> TooSmall{};
	FNetReceiveResult TooSmallReceive{};
	FNetAddress TooSmallFrom{0x42};
	if (SingleLoopback.Port(0).TryReceive(TooSmallFrom, TSpan<std::uint8_t>(TooSmall.data(), TooSmall.size()), TooSmallReceive) != ENetResult::Full)
	{
		return static_cast<int>(ENetConsumerExitCode::LoopbackTooSmallDidNotReturnFull);
	}

	// Manager: queue, advance once (success), observe backpressure retention, recover, receive.
	MicroWorld::FNetPacketStorage<2, 4> ManagerStorage;
	FNetManager<2, 4> Manager(Loopback.Port(0), ManagerStorage);
	const std::array<std::uint8_t, 3> QueuePacket{0x01, 0x02, 0x03};
	if (Manager.QueueSend(LoopbackPort0, TSpan<const std::uint8_t>(QueuePacket.data(), QueuePacket.size())) != ENetResult::Success)
	{
		return static_cast<int>(ENetConsumerExitCode::ManagerQueueDidNotAcceptPacket);
	}
	if (Manager.AdvanceSend() != ENetResult::Success)
	{
		return static_cast<int>(ENetConsumerExitCode::ManagerAdvanceDidNotSendHead);
	}

	// Backpressure: fill the driver, then observe the manager retain its head across a Full advance.
	FHostLoopback<2, 1, 4> BackpressureDriver;
	MicroWorld::FNetPacketStorage<1, 4> BackpressureStorage;
	FNetManager<1, 4> BackpressureManager(BackpressureDriver.Port(0), BackpressureStorage);
	const std::array<std::uint8_t, 2> BackpressurePacket{0x55, 0x66};
	BackpressureDriver.Port(0).TrySend(LoopbackPort0, TSpan<const std::uint8_t>(BackpressurePacket.data(), BackpressurePacket.size()));
	BackpressureManager.QueueSend(LoopbackPort0, TSpan<const std::uint8_t>(BackpressurePacket.data(), BackpressurePacket.size()));
	if (BackpressureManager.AdvanceSend() != ENetResult::Full || BackpressureManager.IsEmpty())
	{
		// The manager must still hold its queued packet when the driver reports Full.
		return static_cast<int>(ENetConsumerExitCode::ManagerDriverFullDidNotRetainHead);
	}
	// Clear backpressure and observe recovery: drain the driver, then advance must succeed.
	BackpressureDriver.Drain(0);
	if (BackpressureManager.AdvanceSend() != ENetResult::Success || !BackpressureManager.IsEmpty())
	{
		return static_cast<int>(ENetConsumerExitCode::ManagerRecoveryDidNotClearBackpressure);
	}

	// Direct receive: the manager must propagate the driver success and byte count.
	std::array<std::uint8_t, 4> ReceiveDestination{};
	FNetReceiveResult ReceiveResult{};
	FNetAddress ReceiveFrom{};
	if (Manager.Receive(ReceiveFrom, TSpan<std::uint8_t>(ReceiveDestination.data(), ReceiveDestination.size()), ReceiveResult) != ENetResult::Success
		|| ReceiveResult.BytesReceived == 0)
	{
		return static_cast<int>(ENetConsumerExitCode::ManagerReceiveDidNotPropagateSuccess);
	}

	return static_cast<int>(ENetConsumerExitCode::Success);
}
