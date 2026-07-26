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

#include <cstddef>
#include <cstdint>

static_assert(__cplusplus >= 201703L);
static_assert(MicroWorld::Version.Major == 0);
static_assert(MicroWorld::Version.Minor == 3);
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

/** Literal byte values used as test inputs across the Net consumer probe. */
inline constexpr std::uint8_t ByteValue01 = 0x01;
inline constexpr std::uint8_t ByteValue02 = 0x02;
inline constexpr std::uint8_t ByteValue03 = 0x03;
inline constexpr std::uint8_t ByteValue04 = 0x04;
inline constexpr std::uint8_t ByteValue10 = 0x10;
inline constexpr std::uint8_t ByteValue20 = 0x20;
inline constexpr std::uint8_t ByteValue30 = 0x30;
inline constexpr std::uint8_t ByteValue40 = 0x40;
inline constexpr std::uint8_t ByteValue55 = 0x55;
inline constexpr std::uint8_t ByteValue66 = 0x66;
inline constexpr std::uint8_t ByteValueAA = 0xAA;
inline constexpr std::uint8_t ByteValueBB = 0xBB;
inline constexpr std::uint8_t ByteValueCC = 0xCC;
inline constexpr std::uint8_t ByteValueDD = 0xDD;
inline constexpr std::uint8_t UntouchedByteMarker = 0xEE;
inline constexpr std::uint8_t FullByteMarker = 0xFF;

/** Sentinel address byte that proves a receive call did not overwrite the caller's address. */
inline constexpr std::uint8_t UntouchedAddressByte = 0x42;

/** Capacities and packet sizes the probe exercises. */
inline constexpr std::size_t SmallWriterByteCount = 4;
inline constexpr std::size_t SmallPacketByteCount = 2;
inline constexpr std::size_t LoopbackDestinationByteCount = 4;
inline constexpr std::size_t TooSmallDestinationByteCount = 1;
inline constexpr std::size_t QueuePacketByteCount = 3;
inline constexpr std::size_t ExpectedReceivedByteCount = 2;
inline constexpr std::size_t EmptyReceiveByteCount = 0;

} // namespace MicroWorldConsumer

/** Exercises representative Core+Net public APIs without platform I/O. */
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
	std::uint8_t WriterStorage[MicroWorldConsumer::SmallWriterByteCount]{};
	MicroWorld::FByteWriter Writer(TSpan<std::uint8_t>(WriterStorage, MicroWorldConsumer::SmallWriterByteCount));
	const bool bAllWritesAccepted = Writer.WriteByte(MicroWorldConsumer::ByteValue01) == ENetResult::Success
		&& Writer.WriteByte(MicroWorldConsumer::ByteValue02) == ENetResult::Success
		&& Writer.WriteByte(MicroWorldConsumer::ByteValue03) == ENetResult::Success
		&& Writer.WriteByte(MicroWorldConsumer::ByteValue04) == ENetResult::Success;
	if (!bAllWritesAccepted)
	{
		return static_cast<int>(ENetConsumerExitCode::ByteWriterOverflowDidNotReturnFull);
	}
	const bool bOverflowReportsFull = Writer.Remaining() == 0 && Writer.WriteByte(MicroWorldConsumer::FullByteMarker) == ENetResult::Full;
	if (!bOverflowReportsFull)
	{
		return static_cast<int>(ENetConsumerExitCode::ByteWriterOverflowDidNotReturnFull);
	}
	const bool bAcceptedPrefixIntact = WriterStorage[0] == MicroWorldConsumer::ByteValue01 && WriterStorage[3] == MicroWorldConsumer::ByteValue04;
	if (!bAcceptedPrefixIntact)
	{
		return static_cast<int>(ENetConsumerExitCode::ByteWriterAcceptedPrefixAltered);
	}

	// Byte reader: consume, observe Unavailable past source, prove output untouched on failure.
	const std::uint8_t ReaderSource[MicroWorldConsumer::SmallWriterByteCount] = {
		MicroWorldConsumer::ByteValue10, MicroWorldConsumer::ByteValue20, MicroWorldConsumer::ByteValue30, MicroWorldConsumer::ByteValue40};
	MicroWorld::FByteReader Reader(TSpan<const std::uint8_t>(ReaderSource, MicroWorldConsumer::SmallWriterByteCount));
	std::uint8_t ReadDestination[MicroWorldConsumer::SmallWriterByteCount]{};
	const ENetResult ReadResult = Reader.Read(TSpan<std::uint8_t>(ReadDestination, MicroWorldConsumer::SmallWriterByteCount));
	if (ReadResult != ENetResult::Success)
	{
		return static_cast<int>(ENetConsumerExitCode::ByteReaderTruncatedDidNotReturnUnavailable);
	}
	std::uint8_t UnusedByte = MicroWorldConsumer::UntouchedByteMarker;
	const ENetResult OverflowReadResult = Reader.ReadByte(UnusedByte);
	const bool bFailureLeavesOutputUntouched = OverflowReadResult == ENetResult::Invalid && UnusedByte == MicroWorldConsumer::UntouchedByteMarker;
	if (!bFailureLeavesOutputUntouched)
	{
		return static_cast<int>(ENetConsumerExitCode::ByteReaderOutputModifiedOnFailure);
	}

	// Loopback: FIFO delivery, full backpressure, empty unavailable, too-small full.
	// A two-port loopback with port 0 sending to its own mailbox reproduces the single-link FIFO.
	THostLoopback<2, 2, 4> Loopback;
	const FNetAddress LoopbackPort0 = MakeLoopbackAddress(0);
	const std::uint8_t FirstPacket[MicroWorldConsumer::SmallPacketByteCount] = {MicroWorldConsumer::ByteValueAA, MicroWorldConsumer::ByteValueBB};
	const std::uint8_t SecondPacket[MicroWorldConsumer::SmallPacketByteCount] = {MicroWorldConsumer::ByteValueCC, MicroWorldConsumer::ByteValueDD};
	const bool bBothSendsAccepted =
		Loopback.Port(0).TrySend(LoopbackPort0, TSpan<const std::uint8_t>(FirstPacket, MicroWorldConsumer::SmallPacketByteCount))
			== ENetResult::Success
		&& Loopback.Port(0).TrySend(LoopbackPort0, TSpan<const std::uint8_t>(SecondPacket, MicroWorldConsumer::SmallPacketByteCount))
			== ENetResult::Success;
	if (!bBothSendsAccepted)
	{
		return static_cast<int>(ENetConsumerExitCode::LoopbackFifoOrderBroken);
	}

	std::uint8_t LoopbackDestination[MicroWorldConsumer::LoopbackDestinationByteCount]{};
	FNetReceiveResult FirstReceive{};
	FNetAddress FirstFrom{MicroWorldConsumer::UntouchedAddressByte};
	const ENetResult FirstReceiveResult = Loopback.Port(0).TryReceive(
		FirstFrom, TSpan<std::uint8_t>(LoopbackDestination, MicroWorldConsumer::LoopbackDestinationByteCount), FirstReceive);
	const bool bFirstPacketIsExpected = FirstReceiveResult == ENetResult::Success
		&& FirstReceive.BytesReceived == MicroWorldConsumer::ExpectedReceivedByteCount && LoopbackDestination[0] == MicroWorldConsumer::ByteValueAA
		&& FirstFrom == LoopbackPort0;
	if (!bFirstPacketIsExpected)
	{
		return static_cast<int>(ENetConsumerExitCode::LoopbackFifoOrderBroken);
	}

	std::uint8_t SecondDestination[MicroWorldConsumer::LoopbackDestinationByteCount]{};
	FNetReceiveResult SecondReceive{};
	FNetAddress SecondFrom{MicroWorldConsumer::UntouchedAddressByte};
	const ENetResult SecondReceiveResult = Loopback.Port(0).TryReceive(
		SecondFrom, TSpan<std::uint8_t>(SecondDestination, MicroWorldConsumer::LoopbackDestinationByteCount), SecondReceive);
	const bool bSecondPacketIsExpected = SecondReceiveResult == ENetResult::Success
		&& SecondReceive.BytesReceived == MicroWorldConsumer::ExpectedReceivedByteCount && SecondDestination[0] == MicroWorldConsumer::ByteValueCC
		&& SecondFrom == LoopbackPort0;
	if (!bSecondPacketIsExpected)
	{
		return static_cast<int>(ENetConsumerExitCode::LoopbackFifoOrderBroken);
	}

	FNetReceiveResult EmptyReceive{};
	FNetAddress EmptyFrom{MicroWorldConsumer::UntouchedAddressByte};
	const ENetResult EmptyReceiveResult = Loopback.Port(0).TryReceive(
		EmptyFrom, TSpan<std::uint8_t>(LoopbackDestination, MicroWorldConsumer::LoopbackDestinationByteCount), EmptyReceive);
	if (EmptyReceiveResult != ENetResult::Unavailable)
	{
		return static_cast<int>(ENetConsumerExitCode::LoopbackEmptyDidNotReturnUnavailable);
	}

	// Full backpressure: a one-slot mailbox must reject the second send and retain the head.
	THostLoopback<2, 1, 4> SingleLoopback;
	const ENetResult SingleFirstSendResult =
		SingleLoopback.Port(0).TrySend(LoopbackPort0, TSpan<const std::uint8_t>(FirstPacket, MicroWorldConsumer::SmallPacketByteCount));
	const ENetResult SingleSecondSendResult =
		SingleLoopback.Port(0).TrySend(LoopbackPort0, TSpan<const std::uint8_t>(SecondPacket, MicroWorldConsumer::SmallPacketByteCount));
	const bool bHeadRetainedUnderBackpressure = SingleFirstSendResult == ENetResult::Success && SingleSecondSendResult == ENetResult::Full;
	if (!bHeadRetainedUnderBackpressure)
	{
		return static_cast<int>(ENetConsumerExitCode::LoopbackFullOverwroteHead);
	}

	// Too-small destination: head packet must be retained for a larger retry.
	std::uint8_t TooSmall[MicroWorldConsumer::TooSmallDestinationByteCount]{};
	FNetReceiveResult TooSmallReceive{};
	FNetAddress TooSmallFrom{MicroWorldConsumer::UntouchedAddressByte};
	const ENetResult TooSmallReceiveResult = SingleLoopback.Port(0).TryReceive(
		TooSmallFrom, TSpan<std::uint8_t>(TooSmall, MicroWorldConsumer::TooSmallDestinationByteCount), TooSmallReceive);
	if (TooSmallReceiveResult != ENetResult::Full)
	{
		return static_cast<int>(ENetConsumerExitCode::LoopbackTooSmallDidNotReturnFull);
	}

	// Manager: queue, advance once (success), observe backpressure retention, recover, receive.
	MicroWorld::TNetPacketStorage<2, 4> ManagerStorage;
	TNetManager<2, 4> Manager(Loopback.Port(0), ManagerStorage);
	const std::uint8_t QueuePacket[MicroWorldConsumer::QueuePacketByteCount] = {
		MicroWorldConsumer::ByteValue01, MicroWorldConsumer::ByteValue02, MicroWorldConsumer::ByteValue03};
	const ENetResult QueueResult = Manager.QueueSend(LoopbackPort0, TSpan<const std::uint8_t>(QueuePacket, MicroWorldConsumer::QueuePacketByteCount));
	if (QueueResult != ENetResult::Success)
	{
		return static_cast<int>(ENetConsumerExitCode::ManagerQueueDidNotAcceptPacket);
	}
	if (Manager.AdvanceSend() != ENetResult::Success)
	{
		return static_cast<int>(ENetConsumerExitCode::ManagerAdvanceDidNotSendHead);
	}

	// Backpressure: fill the driver, then observe the manager retain its head across a Full advance.
	THostLoopback<2, 1, 4> BackpressureDriver;
	MicroWorld::TNetPacketStorage<1, 4> BackpressureStorage;
	TNetManager<1, 4> BackpressureManager(BackpressureDriver.Port(0), BackpressureStorage);
	const std::uint8_t BackpressurePacket[MicroWorldConsumer::SmallPacketByteCount] = {
		MicroWorldConsumer::ByteValue55, MicroWorldConsumer::ByteValue66};
	BackpressureDriver.Port(0).TrySend(LoopbackPort0, TSpan<const std::uint8_t>(BackpressurePacket, MicroWorldConsumer::SmallPacketByteCount));
	BackpressureManager.QueueSend(LoopbackPort0, TSpan<const std::uint8_t>(BackpressurePacket, MicroWorldConsumer::SmallPacketByteCount));
	const ENetResult BackpressureAdvanceResult = BackpressureManager.AdvanceSend();
	const bool bRetainsHeadOnFull = BackpressureAdvanceResult == ENetResult::Full && !BackpressureManager.IsEmpty();
	if (!bRetainsHeadOnFull)
	{
		// The manager must still hold its queued packet when the driver reports Full.
		return static_cast<int>(ENetConsumerExitCode::ManagerDriverFullDidNotRetainHead);
	}
	// Clear backpressure and observe recovery: drain the driver, then advance must succeed.
	BackpressureDriver.Drain(0);
	const ENetResult RecoveryResult = BackpressureManager.AdvanceSend();
	const bool bRecoveredAfterDrain = RecoveryResult == ENetResult::Success && BackpressureManager.IsEmpty();
	if (!bRecoveredAfterDrain)
	{
		return static_cast<int>(ENetConsumerExitCode::ManagerRecoveryDidNotClearBackpressure);
	}

	// Direct receive: the manager must propagate the driver success and byte count.
	std::uint8_t ReceiveDestination[MicroWorldConsumer::LoopbackDestinationByteCount]{};
	FNetReceiveResult ReceiveResult{};
	FNetAddress ReceiveFrom{};
	const ENetResult ManagerReceiveResult =
		Manager.Receive(ReceiveFrom, TSpan<std::uint8_t>(ReceiveDestination, MicroWorldConsumer::LoopbackDestinationByteCount), ReceiveResult);
	const bool bReceivePropagated =
		ManagerReceiveResult == ENetResult::Success && ReceiveResult.BytesReceived != MicroWorldConsumer::EmptyReceiveByteCount;
	if (!bReceivePropagated)
	{
		return static_cast<int>(ENetConsumerExitCode::ManagerReceiveDidNotPropagateSuccess);
	}

	return static_cast<int>(ENetConsumerExitCode::Success);
}
