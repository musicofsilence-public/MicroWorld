#include "TestSupport.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetDriver.h>
#include <MicroWorld/Net/NetManager.h>
#include <MicroWorld/Net/NetPacketStorage.h>
#include <MicroWorld/Net/NetResult.h>

#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::ENetResult;
using MicroWorld::FNetAddress;
using MicroWorld::FNetReceiveResult;
using MicroWorld::INetDriver;
using MicroWorld::TNetManager;
using MicroWorld::TNetPacketStorage;
using MicroWorld::TSpan;

/** Sentinel address byte that proves a receive call did not overwrite the caller's address. */
constexpr std::uint8_t UntouchedAddressByte = 0x42;

/** Pre-fill marker written into every destination byte before a receive, so a delivery is observable. */
constexpr std::uint8_t DestinationPrefillByte = 0xFF;

/** Sentinel value pre-loaded into BytesReceived so an unchanged failed receive is observable. */
constexpr std::size_t UntouchedBytesReceivedSentinel = 0xEE;

/** Index of the destination address byte the FIFO and routing cases queue packets to. */
constexpr std::uint8_t DefaultDestIndex = 0;
/** Distinct destination indices the per-packet routing case queues packets to. */
constexpr std::uint8_t DestIndexA = 1;
constexpr std::uint8_t DestIndexB = 2;
constexpr std::uint8_t DestIndexC = 3;
/** Sender port index the receive-success case stamps into OutFrom. */
constexpr std::uint8_t ReceiveSenderIndex = 7;
/** Length of every two-byte packet the capacity and ordering cases thread through the manager. */
constexpr std::size_t TwoBytePacketLength = 2;
/** Length of the three-byte packet the FIFO ordering case threads through the manager. */
constexpr std::size_t ThreeBytePacketLength = 3;
/** Length of the single-byte packet the FIFO ordering and recovery cases thread through the manager. */
constexpr std::size_t OneBytePacketLength = 1;
/** Length of the four-byte packet the oversize-rejection case rejects. */
constexpr std::size_t FourBytePacketLength = 4;
/** Byte count a successful forced receive reports in the receive-success case. */
constexpr std::size_t ReceiveFillByteCount = 3;
/** Number of wraparound cycles the storage-reuse case drives through the two-slot FIFO. */
constexpr std::size_t WraparoundCycleCount = 6;

/** Fill byte the recording driver writes into every received byte so a success is observable. */
constexpr std::uint8_t ReceiveFillerByteValue = 0x7C;
/** Default fill byte a successful forced receive stamps into destination storage. */
constexpr std::uint8_t DefaultReceiveFillerByte = 0xAB;

/** Four-byte packet the oversize-rejection case rejects above the storage capacity. */
constexpr std::uint8_t OversizedPacketData[FourBytePacketLength] = {0x01, 0x02, 0x03, 0x04};
/** Two-byte packet the FIFO ordering case queues first. */
constexpr std::uint8_t FifoFirstPacket[TwoBytePacketLength] = {0x10, 0x20};
/** Three-byte packet the FIFO ordering case queues second. */
constexpr std::uint8_t FifoSecondPacket[ThreeBytePacketLength] = {0x30, 0x40, 0x50};
/** Single-byte packet the FIFO ordering case queues third. */
constexpr std::uint8_t FifoThirdPacket[OneBytePacketLength] = {0x60};
/** Two-byte packet the full-FIFO case accepts before the rejected queue. */
constexpr std::uint8_t FullFifoAcceptedPacket[TwoBytePacketLength] = {0xAA, 0xBB};
/** Two-byte packet the full-FIFO case rejects as the overflow queue. */
constexpr std::uint8_t FullFifoRejectedPacket[TwoBytePacketLength] = {0xCC, 0xDD};
/** Two-byte packet the single-advance case queues as the head. */
constexpr std::uint8_t SingleAdvanceHeadPacket[TwoBytePacketLength] = {0x11, 0x22};
/** Three-byte packet the driver-Full case queues first. */
constexpr std::uint8_t DriverFullFirstPacket[ThreeBytePacketLength] = {0x01, 0x02, 0x03};
/** Two-byte packet the driver-Full case queues second. */
constexpr std::uint8_t DriverFullSecondPacket[TwoBytePacketLength] = {0x04, 0x05};
/** Two-byte packet the driver-Unavailable case queues as the retained head. */
constexpr std::uint8_t DriverUnavailablePacket[TwoBytePacketLength] = {0x55, 0x66};
/** Two-byte packet the driver-Invalid case queues as the retained head. */
constexpr std::uint8_t DriverInvalidPacket[TwoBytePacketLength] = {0x07, 0x08};
/** Two-byte packet the recovery case queues as the retained head before backpressure clears. */
constexpr std::uint8_t RecoveryHeadPacket[TwoBytePacketLength] = {0x99, 0xAA};
/** Single-byte packet the recovery case queues after the retained head. */
constexpr std::uint8_t RecoveryLaterPacket[OneBytePacketLength] = {0xBB};
/** Two-byte packet A the storage-reuse case queues each wraparound cycle. */
constexpr std::uint8_t WraparoundCycleAPacket[TwoBytePacketLength] = {0xA0, 0xA1};
/** Two-byte packet B the storage-reuse case queues each wraparound cycle. */
constexpr std::uint8_t WraparoundCycleBPacket[TwoBytePacketLength] = {0xB0, 0xB1};
/** Two-byte packet A the per-packet routing case queues to DestA. */
constexpr std::uint8_t RoutedPacketA[TwoBytePacketLength] = {0xA0, 0xA1};
/** Two-byte packet B the per-packet routing case queues to DestB. */
constexpr std::uint8_t RoutedPacketB[TwoBytePacketLength] = {0xB0, 0xB1};
/** Two-byte packet C the per-packet routing case queues to DestC. */
constexpr std::uint8_t RoutedPacketC[TwoBytePacketLength] = {0xC0, 0xC1};

/** Builds a 1-byte destination address whose single byte is `InIndex`; keeps queue call sites concise. */
constexpr FNetAddress MakeDest(const std::uint8_t InIndex) noexcept
{
	return MicroWorld::MakeLoopbackAddress(InIndex);
}

/**
 * Records the exact bytes and destination address the manager passed to every driver send so FIFO order,
 * head retention, recovery, and per-packet routing can be proven across differently sized and valued packets.
 *
 * The driver returns a caller-chosen result on each send attempt and never touches a real transport, so
 * manager ordering and retention behavior stays deterministic.
 */
class FRecordingDriver final : public INetDriver
{
public:
	/** Defaulted so the driver can live in automatic storage without side effects. */
	~FRecordingDriver() noexcept override = default;

	/** Counts every attempt and records the destination address and bytes of every successful send so FIFO order of delivered packets is provable. */
	ENetResult TrySend(const FNetAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept override
	{
		++SendCount;
		if (ForcedSendResult == ENetResult::Success && SuccessfulSendCount < MaxRecordedSends)
		{
			const std::size_t CopyLength = InPacket.Size() <= MaxRecordedBytes ? InPacket.Size() : MaxRecordedBytes;
			for (std::size_t Index = 0; Index < CopyLength; ++Index)
			{
				RecordedSendBytes[SuccessfulSendCount][Index] = InPacket[Index];
			}
			RecordedSendLengths[SuccessfulSendCount] = InPacket.Size();
			RecordedSendDestinations[SuccessfulSendCount] = InTo;
			++SuccessfulSendCount;
		}
		return ForcedSendResult;
	}

	/** Returns the forced result, fills the destination on success, and stamps a deterministic sender into OutFrom only on success. */
	ENetResult TryReceive(FNetAddress& OutFrom, TSpan<std::uint8_t> InDestination, FNetReceiveResult& OutResult) noexcept override
	{
		++ReceiveAttemptCount;
		if (ForcedReceiveResult == ENetResult::Success)
		{
			const std::size_t CopyLength = ReceiveByteCount <= InDestination.Size() ? ReceiveByteCount : InDestination.Size();
			for (std::size_t Index = 0; Index < CopyLength; ++Index)
			{
				InDestination[Index] = ReceiveFillerByte;
			}
			OutResult.BytesReceived = ReceiveByteCount;
			OutFrom = ReceiveSender;
		}
		return ForcedReceiveResult;
	}

	/** Reports a fixed per-packet byte capacity large enough for every test packet in this suite. */
	std::size_t MaxPacketBytes() const noexcept override { return DriverMaxPacketBytes; }

	/** The result the next TrySend call must return, regardless of packet contents. */
	ENetResult ForcedSendResult{ENetResult::Success};

	/** The result the next TryReceive call must return, regardless of destination. */
	ENetResult ForcedReceiveResult{ENetResult::Unavailable};

	/** The byte count a successful forced receive reports. */
	std::size_t ReceiveByteCount{0};

	/** The byte value written into every received byte so success is observable. */
	std::uint8_t ReceiveFillerByte{DefaultReceiveFillerByte};

	/** The sender address a successful forced receive stamps into OutFrom. */
	FNetAddress ReceiveSender{};

	/** Counts every send attempt, including failures, so backpressure retention is observable. */
	std::size_t SendCount{0};

	/** Counts only successful sends so recorded slots map one-to-one to delivered packets. */
	std::size_t SuccessfulSendCount{0};

	/** Counts how many times the manager attempted a receive. */
	std::size_t ReceiveAttemptCount{0};

	static constexpr std::size_t MaxRecordedSends = 16;
	static constexpr std::size_t MaxRecordedBytes = 8;
	static constexpr std::size_t DriverMaxPacketBytes = 64;

	/** Records the exact bytes of each send so FIFO order is provable. */
	std::uint8_t RecordedSendBytes[MaxRecordedSends][MaxRecordedBytes]{};

	/** Records the exact length of each send alongside its bytes. */
	std::size_t RecordedSendLengths[MaxRecordedSends]{};

	/** Records the destination address the manager passed with each send so per-packet routing is provable. */
	FNetAddress RecordedSendDestinations[MaxRecordedSends]{};
};

/**
 * Scenario: Construct a manager over a recording driver and fixed-capacity packet storage.
 * Expected: The manager reports an empty non-full FIFO with queue capacity and max packet bytes matching the template parameters and zero queued
 * packets.
 */
MW_TEST_CASE(NetManagerStartsEmptyWithFixedConfiguration)
{
	// Arrange
	FRecordingDriver Driver;
	TNetPacketStorage<2, 4> Storage;
	TNetManager<2, 4> Manager(Driver, Storage);

	// Assert
	MW_EXPECT_EQ(Test, true, Manager.IsEmpty(), "A fresh manager must report an empty FIFO");
	MW_EXPECT_EQ(Test, false, Manager.IsFull(), "A fresh manager must not report a full FIFO");
	MW_EXPECT_EQ(Test, TwoBytePacketLength, Manager.QueueCapacity(), "Queue capacity must match the template parameter");
	MW_EXPECT_EQ(Test, FourBytePacketLength, Manager.MaximumPacketBytes(), "Max packet bytes must match the template parameter");
	MW_EXPECT_EQ(Test, std::size_t{0}, Manager.QueuedCount(), "A fresh manager must report zero queued packets");
}

/**
 * Scenario: Attempt to queue a packet larger than MaximumPacketBytes.
 * Expected: The queue returns Invalid and no packet is enqueued.
 */
MW_TEST_CASE(NetManagerRejectsOversizedPacketTransactionally)
{
	// Arrange
	FRecordingDriver Driver;
	TNetPacketStorage<2, 2> Storage;
	TNetManager<2, 2> Manager(Driver, Storage);

	// Act
	const ENetResult OversizedResult =
		Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(OversizedPacketData, sizeof(OversizedPacketData)));
	// Assert
	MW_EXPECT_EQ(Test, ENetResult::Invalid, OversizedResult, "A packet larger than MaximumPacketBytes must return Invalid");
	MW_EXPECT_EQ(Test, true, Manager.IsEmpty(), "Oversized queue must not enqueue a packet");
}

/**
 * Scenario: Attempt to queue a null packet with a nonzero length.
 * Expected: The queue returns Invalid and no packet is enqueued.
 */
MW_TEST_CASE(NetManagerRejectsNullPacketWithNonzeroLength)
{
	// Arrange
	FRecordingDriver Driver;
	TNetPacketStorage<2, 4> Storage;
	TNetManager<2, 4> Manager(Driver, Storage);

	// Act
	const ENetResult NullResult = Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(nullptr, TwoBytePacketLength));
	// Assert
	MW_EXPECT_EQ(Test, ENetResult::Invalid, NullResult, "Null data with nonzero length must return Invalid");
	MW_EXPECT_EQ(Test, true, Manager.IsEmpty(), "Invalid queue must not enqueue a packet");
}

/**
 * Scenario: Queue three differently sized and valued packets, then advance each to the driver.
 * Expected: Three advances call the driver exactly three times and deliver the packets in FIFO order with byte-for-byte matching contents, leaving
 * the FIFO empty.
 */
MW_TEST_CASE(NetManagerAdvanceSendsDifferentlySizedPacketsInFifoOrder)
{
	// Arrange
	FRecordingDriver Driver;
	TNetPacketStorage<3, 4> Storage;
	TNetManager<3, 4> Manager(Driver, Storage);

	// Act - queue three packets
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(FifoFirstPacket, sizeof(FifoFirstPacket))),
		"First queue must succeed");
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(FifoSecondPacket, sizeof(FifoSecondPacket))),
		"Second queue must succeed");
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(FifoThirdPacket, sizeof(FifoThirdPacket))),
		"Third queue must succeed");

	// Act - advance each queued packet to the driver
	Manager.AdvanceSend();
	Manager.AdvanceSend();
	Manager.AdvanceSend();

	// Assert
	MW_EXPECT_EQ(Test, ThreeBytePacketLength, Driver.SendCount, "Three advances must call the driver exactly three times");

	// First send: 2 bytes {0x10, 0x20}
	MW_EXPECT_EQ(Test, TwoBytePacketLength, Driver.RecordedSendLengths[0], "First send must carry the first packet length");
	MW_EXPECT_EQ(Test, FifoFirstPacket[0], Driver.RecordedSendBytes[0][0], "First send must carry the first packet first byte");
	MW_EXPECT_EQ(Test, FifoFirstPacket[1], Driver.RecordedSendBytes[0][1], "First send must carry the first packet second byte");

	// Second send: 3 bytes {0x30, 0x40, 0x50}
	MW_EXPECT_EQ(Test, ThreeBytePacketLength, Driver.RecordedSendLengths[1], "Second send must carry the second packet length");
	MW_EXPECT_EQ(Test, FifoSecondPacket[0], Driver.RecordedSendBytes[1][0], "Second send must carry the second packet first byte");
	MW_EXPECT_EQ(Test, FifoSecondPacket[2], Driver.RecordedSendBytes[1][2], "Second send must carry the second packet third byte");

	// Third send: 1 byte {0x60}
	MW_EXPECT_EQ(Test, OneBytePacketLength, Driver.RecordedSendLengths[2], "Third send must carry the third packet length");
	MW_EXPECT_EQ(Test, FifoThirdPacket[0], Driver.RecordedSendBytes[2][0], "Third send must carry the third packet first byte");

	MW_EXPECT_EQ(Test, true, Manager.IsEmpty(), "Three successful advances must drain a three-packet FIFO");
}

/**
 * Scenario: Fill a one-slot FIFO, attempt to queue an overflow packet, then advance the driver.
 * Expected: The overflow queue returns Full without changing the queued count, and the accepted head packet survives to be advanced intact.
 */
MW_TEST_CASE(NetManagerFullFifoRejectsFurtherQueue)
{
	// Arrange
	FRecordingDriver Driver;
	TNetPacketStorage<1, 4> Storage;
	TNetManager<1, 4> Manager(Driver, Storage);

	// Act
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(FullFifoAcceptedPacket, sizeof(FullFifoAcceptedPacket))),
		"First queue into an empty FIFO must succeed");
	// Act
	const ENetResult OverflowResult =
		Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(FullFifoRejectedPacket, sizeof(FullFifoRejectedPacket)));
	// Assert
	MW_EXPECT_EQ(Test, ENetResult::Full, OverflowResult, "Queue into a full FIFO must return Full");
	MW_EXPECT_EQ(Test, OneBytePacketLength, Manager.QueuedCount(), "Overflow must not change the queued count");

	// Act / Assert - prove the accepted head survives the rejected queue.
	Manager.AdvanceSend();
	MW_EXPECT_EQ(Test, TwoBytePacketLength, Driver.RecordedSendLengths[0], "Retained head must carry the accepted packet length");
	MW_EXPECT_EQ(Test, FullFifoAcceptedPacket[0], Driver.RecordedSendBytes[0][0], "Retained head must carry the accepted first byte");
}

/**
 * Scenario: Advance an empty FIFO and observe the recording driver's send count.
 * Expected: The advance returns Unavailable and never calls the driver.
 */
MW_TEST_CASE(NetManagerAdvanceEmptyReturnsUnavailableWithoutDriverCall)
{
	// Arrange
	FRecordingDriver Driver;
	TNetPacketStorage<2, 4> Storage;
	TNetManager<2, 4> Manager(Driver, Storage);

	// Act
	const ENetResult EmptyAdvanceResult = Manager.AdvanceSend();
	// Assert
	MW_EXPECT_EQ(Test, ENetResult::Unavailable, EmptyAdvanceResult, "Advance on an empty FIFO must return Unavailable");
	MW_EXPECT_EQ(Test, std::size_t{0}, Driver.SendCount, "Empty advance must not call the driver");
}

/**
 * Scenario: Queue one head packet, then advance once against a successful driver.
 * Expected: The advance succeeds, calls the driver exactly once with the head packet length, and removes the head from the FIFO.
 */
MW_TEST_CASE(NetManagerAdvanceAttemptsOneSendAndRemovesHeadOnSuccess)
{
	// Arrange
	FRecordingDriver Driver;
	TNetPacketStorage<2, 4> Storage;
	TNetManager<2, 4> Manager(Driver, Storage);

	Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(SingleAdvanceHeadPacket, sizeof(SingleAdvanceHeadPacket)));

	// Act
	const ENetResult AdvanceResult = Manager.AdvanceSend();
	// Assert
	MW_EXPECT_EQ(Test, ENetResult::Success, AdvanceResult, "Advance with a successful driver must succeed");
	MW_EXPECT_EQ(Test, OneBytePacketLength, Driver.SendCount, "Advance must call the driver exactly once");
	MW_EXPECT_EQ(Test, TwoBytePacketLength, Driver.RecordedSendLengths[0], "Advance must send the head packet length");
	MW_EXPECT_EQ(Test, true, Manager.IsEmpty(), "Successful advance must remove the head packet");
}

/**
 * Scenario: Queue two packets, advance against a driver that returns Full, then clear backpressure and advance twice more.
 * Expected: Driver Full propagates as Full and retains all queued packets; once backpressure clears, advances send the retained first packet ahead of
 * the second in FIFO order with byte-for-byte matching contents.
 */
MW_TEST_CASE(NetManagerDriverFullRetainsExactHeadContents)
{
	// Arrange
	FRecordingDriver Driver;
	Driver.ForcedSendResult = ENetResult::Full;
	TNetPacketStorage<2, 4> Storage;
	TNetManager<2, 4> Manager(Driver, Storage);

	Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(DriverFullFirstPacket, sizeof(DriverFullFirstPacket)));
	Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(DriverFullSecondPacket, sizeof(DriverFullSecondPacket)));

	// Act
	const ENetResult AdvanceResult = Manager.AdvanceSend();
	// Assert
	MW_EXPECT_EQ(Test, ENetResult::Full, AdvanceResult, "Driver Full must propagate as Full");
	MW_EXPECT_EQ(Test, TwoBytePacketLength, Manager.QueuedCount(), "Driver Full must retain all queued packets");

	// Clear backpressure: the next advance must send the retained first packet, not the second.
	Driver.ForcedSendResult = ENetResult::Success;
	// Act
	const ENetResult RecoveryAdvanceResult = Manager.AdvanceSend();
	// Assert
	MW_EXPECT_EQ(Test, ENetResult::Success, RecoveryAdvanceResult, "Recovery advance must succeed after backpressure clears");
	MW_EXPECT_EQ(Test, ThreeBytePacketLength, Driver.RecordedSendLengths[0], "Retained head must be the first packet length");
	MW_EXPECT_EQ(Test, DriverFullFirstPacket[0], Driver.RecordedSendBytes[0][0], "Retained head must carry the first packet first byte");
	MW_EXPECT_EQ(Test, DriverFullFirstPacket[2], Driver.RecordedSendBytes[0][2], "Retained head must carry the first packet third byte");

	// Act / Assert - the next advance must send the second packet in FIFO order.
	Manager.AdvanceSend();
	MW_EXPECT_EQ(Test, TwoBytePacketLength, Driver.RecordedSendLengths[1], "Second advance must send the second packet length");
	MW_EXPECT_EQ(Test, DriverFullSecondPacket[0], Driver.RecordedSendBytes[1][0], "Second advance must send the second packet first byte");
}

/**
 * Scenario: Queue one packet, advance against a driver that returns Unavailable, then switch the driver to success and advance again.
 * Expected: Driver Unavailable propagates as Unavailable and retains the head packet; the retry advance sends the retained head with its original
 * length and bytes.
 */
MW_TEST_CASE(NetManagerDriverUnavailableRetainsExactHead)
{
	// Arrange
	FRecordingDriver Driver;
	Driver.ForcedSendResult = ENetResult::Unavailable;
	TNetPacketStorage<1, 4> Storage;
	TNetManager<1, 4> Manager(Driver, Storage);

	Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(DriverUnavailablePacket, sizeof(DriverUnavailablePacket)));

	// Act
	const ENetResult AdvanceResult = Manager.AdvanceSend();
	// Assert
	MW_EXPECT_EQ(Test, ENetResult::Unavailable, AdvanceResult, "Driver Unavailable must propagate as Unavailable");
	MW_EXPECT_EQ(Test, OneBytePacketLength, Manager.QueuedCount(), "Driver Unavailable must retain the head packet");

	Driver.ForcedSendResult = ENetResult::Success;
	// Act
	Manager.AdvanceSend();
	// Assert
	MW_EXPECT_EQ(Test, TwoBytePacketLength, Driver.RecordedSendLengths[0], "Retained head must carry its original length");
	MW_EXPECT_EQ(Test, DriverUnavailablePacket[0], Driver.RecordedSendBytes[0][0], "Retained head must carry its original first byte");
}

/**
 * Scenario: Queue one packet, advance against a driver that returns Invalid, then switch the driver to success and advance again.
 * Expected: Driver Invalid propagates as Invalid and retains the head packet; the retry advance sends the retained head with its original length and
 * bytes.
 */
MW_TEST_CASE(NetManagerDriverInvalidRetainsExactHead)
{
	// Arrange
	FRecordingDriver Driver;
	Driver.ForcedSendResult = ENetResult::Invalid;
	TNetPacketStorage<1, 4> Storage;
	TNetManager<1, 4> Manager(Driver, Storage);

	Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(DriverInvalidPacket, sizeof(DriverInvalidPacket)));

	// Act
	const ENetResult AdvanceResult = Manager.AdvanceSend();
	// Assert
	MW_EXPECT_EQ(Test, ENetResult::Invalid, AdvanceResult, "Driver Invalid must propagate as Invalid");
	MW_EXPECT_EQ(Test, OneBytePacketLength, Manager.QueuedCount(), "Driver Invalid must retain the head packet");

	Driver.ForcedSendResult = ENetResult::Success;
	// Act
	Manager.AdvanceSend();
	// Assert
	MW_EXPECT_EQ(Test, TwoBytePacketLength, Driver.RecordedSendLengths[0], "Retained head must carry its original length");
	MW_EXPECT_EQ(Test, DriverInvalidPacket[1], Driver.RecordedSendBytes[0][1], "Retained head must carry its original second byte");
}

/**
 * Scenario: Queue a head and a later packet, advance into a full driver, clear backpressure, then advance twice.
 * Expected: Backpressure retains both packets; recovery sends the retained head first, before the later packet, removing only the head on the first
 * advance.
 */
MW_TEST_CASE(NetManagerRecoverySendsRetainedHeadBeforeLaterPackets)
{
	// Arrange
	FRecordingDriver Driver;
	Driver.ForcedSendResult = ENetResult::Full;
	TNetPacketStorage<2, 4> Storage;
	TNetManager<2, 4> Manager(Driver, Storage);

	Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(RecoveryHeadPacket, sizeof(RecoveryHeadPacket)));
	Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(RecoveryLaterPacket, sizeof(RecoveryLaterPacket)));

	// Act / Assert
	MW_EXPECT_EQ(Test, ENetResult::Full, Manager.AdvanceSend(), "First advance into a full driver must return Full");
	MW_EXPECT_EQ(Test, TwoBytePacketLength, Manager.QueuedCount(), "Backpressure must retain both packets");

	Driver.ForcedSendResult = ENetResult::Success;
	// Act
	const ENetResult FirstRecovery = Manager.AdvanceSend();
	// Assert
	MW_EXPECT_EQ(Test, ENetResult::Success, FirstRecovery, "Recovery advance must succeed");
	MW_EXPECT_EQ(Test, TwoBytePacketLength, Driver.RecordedSendLengths[0], "Recovery must send the retained head, not the later packet");
	MW_EXPECT_EQ(Test, RecoveryHeadPacket[0], Driver.RecordedSendBytes[0][0], "Recovery must send the retained head first byte");
	MW_EXPECT_EQ(Test, OneBytePacketLength, Manager.QueuedCount(), "Recovery must remove only the head");

	// Act / Assert
	Manager.AdvanceSend();
	MW_EXPECT_EQ(Test, OneBytePacketLength, Driver.RecordedSendLengths[1], "Second advance must send the later packet");
	MW_EXPECT_EQ(Test, RecoveryLaterPacket[0], Driver.RecordedSendBytes[1][0], "Second advance must send the later packet byte");
}

/**
 * Scenario: Cycle a two-slot FIFO through queue-fill-advance-drain more times than its capacity, recording each send.
 * Expected: Caller-owned storage is reused across many wraparound cycles, each cycle queues and delivers both packets in order, and the driver is
 * called exactly twice per cycle.
 */
MW_TEST_CASE(NetManagerCallerStorageReusedAfterWraparoundAndDraining)
{
	// Arrange
	FRecordingDriver Driver;
	TNetPacketStorage<2, 2> Storage;
	TNetManager<2, 2> Manager(Driver, Storage);

	// Act / Assert - cycle the FIFO more times than its capacity so head/tail indices wrap around repeatedly.
	for (std::size_t Cycle = 0; Cycle < WraparoundCycleCount; ++Cycle)
	{
		MW_EXPECT_EQ(
			Test,
			ENetResult::Success,
			Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(WraparoundCycleAPacket, sizeof(WraparoundCycleAPacket))),
			"Queue A must succeed each cycle");
		MW_EXPECT_EQ(
			Test,
			ENetResult::Success,
			Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(WraparoundCycleBPacket, sizeof(WraparoundCycleBPacket))),
			"Queue B must succeed each cycle");
		MW_EXPECT_EQ(Test, true, Manager.IsFull(), "Two queues must fill the two-slot FIFO each cycle");

		Manager.AdvanceSend();
		Manager.AdvanceSend();
		MW_EXPECT_EQ(Test, true, Manager.IsEmpty(), "Two advances must drain the FIFO each cycle");

		const std::size_t SendIndex = Cycle * 2;
		MW_EXPECT_EQ(Test, TwoBytePacketLength, Driver.RecordedSendLengths[SendIndex], "Cycle A send must carry two bytes");
		MW_EXPECT_EQ(Test, WraparoundCycleAPacket[0], Driver.RecordedSendBytes[SendIndex][0], "Cycle A send must carry the A packet first byte");
		MW_EXPECT_EQ(Test, TwoBytePacketLength, Driver.RecordedSendLengths[SendIndex + 1], "Cycle B send must carry two bytes");
		MW_EXPECT_EQ(Test, WraparoundCycleBPacket[1], Driver.RecordedSendBytes[SendIndex + 1][1], "Cycle B send must carry the B packet second byte");
	}

	// Assert
	MW_EXPECT_EQ(Test, WraparoundCycleCount * 2, Driver.SendCount, "Six cycles of two sends must call the driver exactly twelve times");
}

/**
 * Scenario: Queue one packet to each of three distinct destinations, then advance each to the driver.
 * Expected: Three advances call the driver exactly three times and route each head to its stored destination address in FIFO order with its original
 * bytes, leaving the FIFO empty.
 */
MW_TEST_CASE(NetManagerAdvanceSendsEachHeadToItsStoredDestination)
{
	// Arrange
	FRecordingDriver Driver;
	TNetPacketStorage<3, 4> Storage;
	TNetManager<3, 4> Manager(Driver, Storage);

	const FNetAddress DestA = MakeDest(DestIndexA);
	const FNetAddress DestB = MakeDest(DestIndexB);
	const FNetAddress DestC = MakeDest(DestIndexC);
	// Act - queue one packet to each distinct destination
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Manager.QueueSend(DestA, TSpan<const std::uint8_t>(RoutedPacketA, sizeof(RoutedPacketA))),
		"Queue to DestA must succeed");
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Manager.QueueSend(DestB, TSpan<const std::uint8_t>(RoutedPacketB, sizeof(RoutedPacketB))),
		"Queue to DestB must succeed");
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Manager.QueueSend(DestC, TSpan<const std::uint8_t>(RoutedPacketC, sizeof(RoutedPacketC))),
		"Queue to DestC must succeed");

	// Act - advance each queued packet to the driver
	Manager.AdvanceSend();
	Manager.AdvanceSend();
	Manager.AdvanceSend();

	// Assert - each recorded send must carry the exact destination stored with that packet, in FIFO order.
	MW_EXPECT_EQ(Test, ThreeBytePacketLength, Driver.SendCount, "Three advances must call the driver exactly three times");
	MW_EXPECT_EQ(Test, true, Driver.RecordedSendDestinations[0] == DestA, "First advance must send to the first queued destination");
	MW_EXPECT_EQ(Test, true, Driver.RecordedSendDestinations[1] == DestB, "Second advance must send to the second queued destination");
	MW_EXPECT_EQ(Test, true, Driver.RecordedSendDestinations[2] == DestC, "Third advance must send to the third queued destination");
	MW_EXPECT_EQ(Test, RoutedPacketA[0], Driver.RecordedSendBytes[0][0], "First send must still carry the first packet bytes");
	MW_EXPECT_EQ(Test, RoutedPacketC[1], Driver.RecordedSendBytes[2][1], "Third send must still carry the third packet bytes");
	MW_EXPECT_EQ(Test, true, Manager.IsEmpty(), "Three successful advances must drain a three-packet FIFO");
}

/**
 * Scenario: Receive against a driver that returns Unavailable with pre-filled destination, byte count, and sender outputs.
 * Expected: Receive performs exactly one direct driver receive, propagates Unavailable, and leaves BytesReceived, the destination, and OutFrom
 * unchanged on failure.
 */
MW_TEST_CASE(NetManagerReceivePerformsOneDirectDriverReceive)
{
	// Arrange
	FRecordingDriver Driver;
	Driver.ForcedReceiveResult = ENetResult::Unavailable;
	TNetPacketStorage<2, 4> Storage;
	TNetManager<2, 4> Manager(Driver, Storage);

	std::uint8_t Destination[FourBytePacketLength] = {DestinationPrefillByte, DestinationPrefillByte, DestinationPrefillByte, DestinationPrefillByte};
	FNetReceiveResult ReceiveResult{UntouchedBytesReceivedSentinel};
	FNetAddress ReceiveFrom{UntouchedAddressByte};
	// Act
	const ENetResult UnavailableResult = Manager.Receive(ReceiveFrom, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);
	// Assert
	MW_EXPECT_EQ(Test, ENetResult::Unavailable, UnavailableResult, "Receive must propagate the driver result");
	MW_EXPECT_EQ(Test, OneBytePacketLength, Driver.ReceiveAttemptCount, "Receive must call the driver exactly once");
	MW_EXPECT_EQ(Test, UntouchedBytesReceivedSentinel, ReceiveResult.BytesReceived, "Unavailable receive must leave BytesReceived unchanged");
	MW_EXPECT_EQ(Test, DestinationPrefillByte, Destination[0], "Unavailable receive must not modify the destination");
	MW_EXPECT_EQ(Test, UntouchedAddressByte, ReceiveFrom.Bytes[0], "Unavailable receive must leave OutFrom unchanged");
}

/**
 * Scenario: Receive against a driver that succeeds with a chosen byte count, fill byte, and sender address.
 * Expected: Receive propagates Success with the driver-reported byte count, fills exactly that many destination bytes without writing past it, and
 * propagates the driver-reported sender address.
 */
MW_TEST_CASE(NetManagerReceivePropagatesSuccessAndByteCount)
{
	// Arrange
	FRecordingDriver Driver;
	Driver.ForcedReceiveResult = ENetResult::Success;
	Driver.ReceiveByteCount = ReceiveFillByteCount;
	Driver.ReceiveFillerByte = ReceiveFillerByteValue;
	Driver.ReceiveSender = MakeDest(ReceiveSenderIndex);
	TNetPacketStorage<2, 4> Storage;
	TNetManager<2, 4> Manager(Driver, Storage);

	std::uint8_t Destination[FourBytePacketLength] = {0};
	FNetReceiveResult ReceiveResult{UntouchedBytesReceivedSentinel};
	FNetAddress ReceiveFrom{UntouchedAddressByte};
	// Act
	const ENetResult SuccessResult = Manager.Receive(ReceiveFrom, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);
	// Assert
	MW_EXPECT_EQ(Test, ENetResult::Success, SuccessResult, "Receive must propagate a successful driver result");
	MW_EXPECT_EQ(Test, ReceiveFillByteCount, ReceiveResult.BytesReceived, "Receive must propagate the driver byte count");
	MW_EXPECT_EQ(Test, ReceiveFillerByteValue, Destination[0], "Receive must propagate the driver destination bytes");
	MW_EXPECT_EQ(Test, ReceiveFillerByteValue, Destination[2], "Receive must fill exactly the reported byte count");
	MW_EXPECT_EQ(Test, std::uint8_t{0}, Destination[3], "Receive must not write past the reported byte count");
	MW_EXPECT_EQ(Test, true, ReceiveFrom == Driver.ReceiveSender, "Receive must propagate the driver-reported sender address");
}

} // namespace
