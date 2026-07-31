#pragma once

#include "MemoryConsumerProbe.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Transport/ByteReader.h>
#include <MicroWorld/Transport/ByteWriter.h>
#include <MicroWorld/Transport/HostLoopback.h>
#include <MicroWorld/Transport/DeviceAddress.h>
#include <MicroWorld/Transport/Device.h>
#include <MicroWorld/Transport/TransportManager.h>
#include <MicroWorld/Transport/TransportPacketStorage.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Core/Version.h>

#include <cstddef>
#include <cstdint>

static_assert(__cplusplus >= 201703L);
static_assert(MicroWorld::Version.Major == 0);
static_assert(MicroWorld::Version.Minor == 4);
static_assert(MicroWorld::Version.Patch == 0);

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#error "The MicroWorld Transport consumer must compile with exceptions disabled."
#endif

#if defined(__GXX_RTTI) || defined(_CPPRTTI)
#error "The MicroWorld Transport consumer must compile with RTTI disabled."
#endif

namespace MicroWorldConsumer
{

/** Stable process exit codes that identify the exact Transport public-API probe failure. */
enum class ETransportConsumerExitCode : int
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
	ManagerDeviceFullDidNotRetainHead = 11,
	ManagerReceiveDidNotPropagateSuccess = 12,
	ManagerRecoveryDidNotClearBackpressure = 13,
	MemoryProfileFailureOffset = 100,
};

/** Literal byte values used as test inputs across the Transport consumer probe. */
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

/** Exercises representative Core+Transport public APIs without platform I/O. */
inline int RunTransportConsumerProbe() noexcept
{
	using namespace MicroWorld;
	using MicroWorldConsumer::ETransportConsumerExitCode;

	const int MemoryProfileResult = RunMemoryConsumerProbe();
	if (MemoryProfileResult != 0)
	{
		return static_cast<int>(ETransportConsumerExitCode::MemoryProfileFailureOffset) + MemoryProfileResult;
	}

	// Byte writer: fill, observe Full past capacity, prove accepted bytes survive.
	std::uint8_t WriterStorage[MicroWorldConsumer::SmallWriterByteCount]{};
	MicroWorld::FByteWriter Writer(TSpan<std::uint8_t>(WriterStorage, MicroWorldConsumer::SmallWriterByteCount));
	const bool bAllWritesAccepted = Writer.WriteByte(MicroWorldConsumer::ByteValue01) == ETransportResult::Success
		&& Writer.WriteByte(MicroWorldConsumer::ByteValue02) == ETransportResult::Success
		&& Writer.WriteByte(MicroWorldConsumer::ByteValue03) == ETransportResult::Success
		&& Writer.WriteByte(MicroWorldConsumer::ByteValue04) == ETransportResult::Success;
	if (!bAllWritesAccepted)
	{
		return static_cast<int>(ETransportConsumerExitCode::ByteWriterOverflowDidNotReturnFull);
	}
	const bool bOverflowReportsFull = Writer.Remaining() == 0 && Writer.WriteByte(MicroWorldConsumer::FullByteMarker) == ETransportResult::Full;
	if (!bOverflowReportsFull)
	{
		return static_cast<int>(ETransportConsumerExitCode::ByteWriterOverflowDidNotReturnFull);
	}
	const bool bAcceptedPrefixIntact = WriterStorage[0] == MicroWorldConsumer::ByteValue01 && WriterStorage[3] == MicroWorldConsumer::ByteValue04;
	if (!bAcceptedPrefixIntact)
	{
		return static_cast<int>(ETransportConsumerExitCode::ByteWriterAcceptedPrefixAltered);
	}

	// Byte reader: consume, observe Unavailable past source, prove output untouched on failure.
	const std::uint8_t ReaderSource[MicroWorldConsumer::SmallWriterByteCount] = {
		MicroWorldConsumer::ByteValue10, MicroWorldConsumer::ByteValue20, MicroWorldConsumer::ByteValue30, MicroWorldConsumer::ByteValue40};
	MicroWorld::FByteReader Reader(TSpan<const std::uint8_t>(ReaderSource, MicroWorldConsumer::SmallWriterByteCount));
	std::uint8_t ReadDestination[MicroWorldConsumer::SmallWriterByteCount]{};
	const ETransportResult ReadResult = Reader.Read(TSpan<std::uint8_t>(ReadDestination, MicroWorldConsumer::SmallWriterByteCount));
	if (ReadResult != ETransportResult::Success)
	{
		return static_cast<int>(ETransportConsumerExitCode::ByteReaderTruncatedDidNotReturnUnavailable);
	}
	std::uint8_t UnusedByte = MicroWorldConsumer::UntouchedByteMarker;
	const ETransportResult OverflowReadResult = Reader.ReadByte(UnusedByte);
	const bool bFailureLeavesOutputUntouched =
		OverflowReadResult == ETransportResult::Invalid && UnusedByte == MicroWorldConsumer::UntouchedByteMarker;
	if (!bFailureLeavesOutputUntouched)
	{
		return static_cast<int>(ETransportConsumerExitCode::ByteReaderOutputModifiedOnFailure);
	}

	// Loopback: FIFO delivery, full backpressure, empty unavailable, too-small full.
	// A two-port loopback with port 0 sending to its own mailbox reproduces the single-link FIFO.
	THostLoopback<2, 2, 4> Loopback;
	const FDeviceAddress LoopbackPort0 = MakeLoopbackAddress(0);
	const std::uint8_t FirstPacket[MicroWorldConsumer::SmallPacketByteCount] = {MicroWorldConsumer::ByteValueAA, MicroWorldConsumer::ByteValueBB};
	const std::uint8_t SecondPacket[MicroWorldConsumer::SmallPacketByteCount] = {MicroWorldConsumer::ByteValueCC, MicroWorldConsumer::ByteValueDD};
	const bool bBothSendsAccepted =
		Loopback.Port(0).TrySend(LoopbackPort0, TSpan<const std::uint8_t>(FirstPacket, MicroWorldConsumer::SmallPacketByteCount))
			== ETransportResult::Success
		&& Loopback.Port(0).TrySend(LoopbackPort0, TSpan<const std::uint8_t>(SecondPacket, MicroWorldConsumer::SmallPacketByteCount))
			== ETransportResult::Success;
	if (!bBothSendsAccepted)
	{
		return static_cast<int>(ETransportConsumerExitCode::LoopbackFifoOrderBroken);
	}

	std::uint8_t LoopbackDestination[MicroWorldConsumer::LoopbackDestinationByteCount]{};
	FReceiveResult FirstReceive{};
	FDeviceAddress FirstFrom{MicroWorldConsumer::UntouchedAddressByte};
	const ETransportResult FirstReceiveResult = Loopback.Port(0).TryReceive(
		FirstFrom, TSpan<std::uint8_t>(LoopbackDestination, MicroWorldConsumer::LoopbackDestinationByteCount), FirstReceive);
	const bool bFirstPacketIsExpected = FirstReceiveResult == ETransportResult::Success
		&& FirstReceive.BytesReceived == MicroWorldConsumer::ExpectedReceivedByteCount && LoopbackDestination[0] == MicroWorldConsumer::ByteValueAA
		&& FirstFrom == LoopbackPort0;
	if (!bFirstPacketIsExpected)
	{
		return static_cast<int>(ETransportConsumerExitCode::LoopbackFifoOrderBroken);
	}

	std::uint8_t SecondDestination[MicroWorldConsumer::LoopbackDestinationByteCount]{};
	FReceiveResult SecondReceive{};
	FDeviceAddress SecondFrom{MicroWorldConsumer::UntouchedAddressByte};
	const ETransportResult SecondReceiveResult = Loopback.Port(0).TryReceive(
		SecondFrom, TSpan<std::uint8_t>(SecondDestination, MicroWorldConsumer::LoopbackDestinationByteCount), SecondReceive);
	const bool bSecondPacketIsExpected = SecondReceiveResult == ETransportResult::Success
		&& SecondReceive.BytesReceived == MicroWorldConsumer::ExpectedReceivedByteCount && SecondDestination[0] == MicroWorldConsumer::ByteValueCC
		&& SecondFrom == LoopbackPort0;
	if (!bSecondPacketIsExpected)
	{
		return static_cast<int>(ETransportConsumerExitCode::LoopbackFifoOrderBroken);
	}

	FReceiveResult EmptyReceive{};
	FDeviceAddress EmptyFrom{MicroWorldConsumer::UntouchedAddressByte};
	const ETransportResult EmptyReceiveResult = Loopback.Port(0).TryReceive(
		EmptyFrom, TSpan<std::uint8_t>(LoopbackDestination, MicroWorldConsumer::LoopbackDestinationByteCount), EmptyReceive);
	if (EmptyReceiveResult != ETransportResult::Unavailable)
	{
		return static_cast<int>(ETransportConsumerExitCode::LoopbackEmptyDidNotReturnUnavailable);
	}

	// Full backpressure: a one-slot mailbox must reject the second send and retain the head.
	THostLoopback<2, 1, 4> SingleLoopback;
	const ETransportResult SingleFirstSendResult =
		SingleLoopback.Port(0).TrySend(LoopbackPort0, TSpan<const std::uint8_t>(FirstPacket, MicroWorldConsumer::SmallPacketByteCount));
	const ETransportResult SingleSecondSendResult =
		SingleLoopback.Port(0).TrySend(LoopbackPort0, TSpan<const std::uint8_t>(SecondPacket, MicroWorldConsumer::SmallPacketByteCount));
	const bool bHeadRetainedUnderBackpressure =
		SingleFirstSendResult == ETransportResult::Success && SingleSecondSendResult == ETransportResult::Full;
	if (!bHeadRetainedUnderBackpressure)
	{
		return static_cast<int>(ETransportConsumerExitCode::LoopbackFullOverwroteHead);
	}

	// Too-small destination: head packet must be retained for a larger retry.
	std::uint8_t TooSmall[MicroWorldConsumer::TooSmallDestinationByteCount]{};
	FReceiveResult TooSmallReceive{};
	FDeviceAddress TooSmallFrom{MicroWorldConsumer::UntouchedAddressByte};
	const ETransportResult TooSmallReceiveResult = SingleLoopback.Port(0).TryReceive(
		TooSmallFrom, TSpan<std::uint8_t>(TooSmall, MicroWorldConsumer::TooSmallDestinationByteCount), TooSmallReceive);
	if (TooSmallReceiveResult != ETransportResult::Full)
	{
		return static_cast<int>(ETransportConsumerExitCode::LoopbackTooSmallDidNotReturnFull);
	}

	// Manager: queue, advance once (success), observe backpressure retention, recover, receive.
	MicroWorld::TTransportPacketStorage<2, 4> ManagerStorage;
	TTransportManager<2, 4> Manager(Loopback.Port(0), ManagerStorage);
	const std::uint8_t QueuePacket[MicroWorldConsumer::QueuePacketByteCount] = {
		MicroWorldConsumer::ByteValue01, MicroWorldConsumer::ByteValue02, MicroWorldConsumer::ByteValue03};
	const ETransportResult QueueResult =
		Manager.QueueSend(LoopbackPort0, TSpan<const std::uint8_t>(QueuePacket, MicroWorldConsumer::QueuePacketByteCount));
	if (QueueResult != ETransportResult::Success)
	{
		return static_cast<int>(ETransportConsumerExitCode::ManagerQueueDidNotAcceptPacket);
	}
	if (Manager.AdvanceSend() != ETransportResult::Success)
	{
		return static_cast<int>(ETransportConsumerExitCode::ManagerAdvanceDidNotSendHead);
	}

	// Backpressure: fill the device, then observe the manager retain its head across a Full advance.
	THostLoopback<2, 1, 4> BackpressureDevice;
	MicroWorld::TTransportPacketStorage<1, 4> BackpressureStorage;
	TTransportManager<1, 4> BackpressureManager(BackpressureDevice.Port(0), BackpressureStorage);
	const std::uint8_t BackpressurePacket[MicroWorldConsumer::SmallPacketByteCount] = {
		MicroWorldConsumer::ByteValue55, MicroWorldConsumer::ByteValue66};
	BackpressureDevice.Port(0).TrySend(LoopbackPort0, TSpan<const std::uint8_t>(BackpressurePacket, MicroWorldConsumer::SmallPacketByteCount));
	BackpressureManager.QueueSend(LoopbackPort0, TSpan<const std::uint8_t>(BackpressurePacket, MicroWorldConsumer::SmallPacketByteCount));
	const ETransportResult BackpressureAdvanceResult = BackpressureManager.AdvanceSend();
	const bool bRetainsHeadOnFull = BackpressureAdvanceResult == ETransportResult::Full && !BackpressureManager.IsEmpty();
	if (!bRetainsHeadOnFull)
	{
		// The manager must still hold its queued packet when the device reports Full.
		return static_cast<int>(ETransportConsumerExitCode::ManagerDeviceFullDidNotRetainHead);
	}
	// Clear backpressure and observe recovery: drain the device, then advance must succeed.
	BackpressureDevice.Drain(0);
	const ETransportResult RecoveryResult = BackpressureManager.AdvanceSend();
	const bool bRecoveredAfterDrain = RecoveryResult == ETransportResult::Success && BackpressureManager.IsEmpty();
	if (!bRecoveredAfterDrain)
	{
		return static_cast<int>(ETransportConsumerExitCode::ManagerRecoveryDidNotClearBackpressure);
	}

	// Direct receive: the manager must propagate the device success and byte count.
	std::uint8_t ReceiveDestination[MicroWorldConsumer::LoopbackDestinationByteCount]{};
	FReceiveResult ReceiveResult{};
	FDeviceAddress ReceiveFrom{};
	const ETransportResult ManagerReceiveResult =
		Manager.Receive(ReceiveFrom, TSpan<std::uint8_t>(ReceiveDestination, MicroWorldConsumer::LoopbackDestinationByteCount), ReceiveResult);
	const bool bReceivePropagated =
		ManagerReceiveResult == ETransportResult::Success && ReceiveResult.BytesReceived != MicroWorldConsumer::EmptyReceiveByteCount;
	if (!bReceivePropagated)
	{
		return static_cast<int>(ETransportConsumerExitCode::ManagerReceiveDidNotPropagateSuccess);
	}

	return static_cast<int>(ETransportConsumerExitCode::Success);
}
