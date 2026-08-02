#include "TestSupport.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Transport/TransportManager.h>
#include <MicroWorld/Transport/TransportPacketStorage.h>

#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::Core::ETransportResult;
using MicroWorld::Core::FDeviceAddress;
using MicroWorld::Core::FReceiveResult;
using MicroWorld::Core::ITransportDevice;
using MicroWorld::Core::TSpan;
using MicroWorld::Transport::TTransportManager;
using MicroWorld::Transport::TTransportPacketStorage;

/** Motivation: Sentinel address byte that proves a receive call did not overwrite the caller's address. */
constexpr std::uint8_t UntouchedAddressByte = 0x42;

/** Motivation: Pre-fill marker written into every destination byte before a receive, so a delivery is observable. */
constexpr std::uint8_t DestinationPrefillByte = 0xFF;

/** Motivation: Sentinel value pre-loaded into BytesReceived so an unchanged failed receive is observable. */
constexpr std::size_t UntouchedBytesReceivedSentinel = 0xEE;

/** Motivation: Index of the destination address byte the FIFO and routing cases queue packets to. */
constexpr std::uint8_t DefaultDestIndex = 0;
/** Motivation: Distinct destination indices the per-packet routing case queues packets to. */
constexpr std::uint8_t DestIndexA = 1;
constexpr std::uint8_t DestIndexB = 2;
constexpr std::uint8_t DestIndexC = 3;
/** Motivation: Sender port index the receive-success case stamps into OutFrom. */
constexpr std::uint8_t ReceiveSenderIndex = 7;
/** Motivation: Length of every two-byte packet the capacity and ordering cases thread through the manager. */
constexpr std::size_t TwoBytePacketLength = 2;
/** Motivation: Length of the three-byte packet the FIFO ordering case threads through the manager. */
constexpr std::size_t ThreeBytePacketLength = 3;
/** Motivation: Length of the single-byte packet the FIFO ordering and recovery cases thread through the manager. */
constexpr std::size_t OneBytePacketLength = 1;
/** Motivation: Length of the four-byte packet the oversize-rejection case rejects. */
constexpr std::size_t FourBytePacketLength = 4;
/** Motivation: Byte count a successful forced receive reports in the receive-success case. */
constexpr std::size_t ReceiveFillByteCount = 3;
/** Motivation: Number of wraparound cycles the storage-reuse case drives through the two-slot FIFO. */
constexpr std::size_t WraparoundCycleCount = 6;

/** Motivation: Fill byte the recording device writes into every received byte so a success is observable. */
constexpr std::uint8_t ReceiveFillerByteValue = 0x7C;
/** Motivation: Default fill byte a successful forced receive stamps into destination storage. */
constexpr std::uint8_t DefaultReceiveFillerByte = 0xAB;

/** Motivation: Four-byte packet the oversize-rejection case rejects above the storage capacity. */
constexpr std::uint8_t OversizedPacketData[FourBytePacketLength] = {0x01, 0x02, 0x03, 0x04};
/** Motivation: Two-byte packet the FIFO ordering case queues first. */
constexpr std::uint8_t FifoFirstPacket[TwoBytePacketLength] = {0x10, 0x20};
/** Motivation: Three-byte packet the FIFO ordering case queues second. */
constexpr std::uint8_t FifoSecondPacket[ThreeBytePacketLength] = {0x30, 0x40, 0x50};
/** Motivation: Single-byte packet the FIFO ordering case queues third. */
constexpr std::uint8_t FifoThirdPacket[OneBytePacketLength] = {0x60};
/** Motivation: Two-byte packet the full-FIFO case accepts before the rejected queue. */
constexpr std::uint8_t FullFifoAcceptedPacket[TwoBytePacketLength] = {0xAA, 0xBB};
/** Motivation: Two-byte packet the full-FIFO case rejects as the overflow queue. */
constexpr std::uint8_t FullFifoRejectedPacket[TwoBytePacketLength] = {0xCC, 0xDD};
/** Motivation: Two-byte packet the single-advance case queues as the head. */
constexpr std::uint8_t SingleAdvanceHeadPacket[TwoBytePacketLength] = {0x11, 0x22};
/** Motivation: Three-byte packet the device-Full case queues first. */
constexpr std::uint8_t DeviceFullFirstPacket[ThreeBytePacketLength] = {0x01, 0x02, 0x03};
/** Motivation: Two-byte packet the device-Full case queues second. */
constexpr std::uint8_t DeviceFullSecondPacket[TwoBytePacketLength] = {0x04, 0x05};
/** Motivation: Two-byte packet the device-Unavailable case queues as the retained head. */
constexpr std::uint8_t DeviceUnavailablePacket[TwoBytePacketLength] = {0x55, 0x66};
/** Motivation: Two-byte packet the device-Invalid case queues as the retained head. */
constexpr std::uint8_t DeviceInvalidPacket[TwoBytePacketLength] = {0x07, 0x08};
/** Motivation: Two-byte packet the recovery case queues as the retained head before backpressure clears. */
constexpr std::uint8_t RecoveryHeadPacket[TwoBytePacketLength] = {0x99, 0xAA};
/** Motivation: Single-byte packet the recovery case queues after the retained head. */
constexpr std::uint8_t RecoveryLaterPacket[OneBytePacketLength] = {0xBB};
/** Motivation: Two-byte packet A the storage-reuse case queues each wraparound cycle. */
constexpr std::uint8_t WraparoundCycleAPacket[TwoBytePacketLength] = {0xA0, 0xA1};
/** Motivation: Two-byte packet B the storage-reuse case queues each wraparound cycle. */
constexpr std::uint8_t WraparoundCycleBPacket[TwoBytePacketLength] = {0xB0, 0xB1};
/** Motivation: Two-byte packet A the per-packet routing case queues to DestA. */
constexpr std::uint8_t RoutedPacketA[TwoBytePacketLength] = {0xA0, 0xA1};
/** Motivation: Two-byte packet B the per-packet routing case queues to DestB. */
constexpr std::uint8_t RoutedPacketB[TwoBytePacketLength] = {0xB0, 0xB1};
/** Motivation: Two-byte packet C the per-packet routing case queues to DestC. */
constexpr std::uint8_t RoutedPacketC[TwoBytePacketLength] = {0xC0, 0xC1};

/**
 * Motivation: Builds a 1-byte destination address whose single byte is `InIndex`; keeps queue call sites concise.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
constexpr FDeviceAddress MakeDest(const std::uint8_t InIndex) noexcept
{
	return MicroWorld::Core::MakeLoopbackAddress(InIndex);
}

/**
 * Motivation: Records the exact bytes and destination address the manager passed to every device send so FIFO
 *   order, head retention, recovery, and per-packet routing can be proven across differently sized and
 *   valued packets. The device returns a caller-chosen result on each send attempt and never touches a
 *   real transport, so manager ordering and retention behavior stays deterministic.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FRecordingDevice final : public ITransportDevice
{
public:
	/**
	 * Motivation: The device can live in automatic storage without side effects.
	 * Responsibilities: Defaulted.
	 */
	~FRecordingDevice() noexcept override = default;

	/**
	 * Motivation: FIFO order of delivered packets is provable.
	 * Responsibilities: Counts every attempt and records the destination address and bytes of every successful send.
	 */
	ETransportResult TrySend(const FDeviceAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept override
	{
		++SendCount;
		if (ForcedSendResult == ETransportResult::Success && SuccessfulSendCount < MaxRecordedSends)
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

	/**
	 * Motivation: Returns the forced result, fills the destination on success, and stamps a deterministic sender into
	 *   OutFrom only on success.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	ETransportResult TryReceive(FDeviceAddress& OutFrom, TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept override
	{
		++ReceiveAttemptCount;
		if (ForcedReceiveResult == ETransportResult::Success)
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

	/**
	 * Motivation: Reports a fixed per-packet byte capacity large enough for every test packet in this suite.
	 * Responsibilities: Return the stored value and touch nothing else.
	 */
	std::size_t MaxPacketBytes() const noexcept override { return DeviceMaxPacketBytes; }

	/**
	 * Motivation: Makes this synchronous recording fake's lack of staged bytes explicit at the required pre-advance turn.
	 * Responsibilities: Do no work because TrySend records the packet synchronously and stages nothing.
	 */
	void PreAdvance(MicroWorld::Core::TimePointMilliseconds) noexcept override {}

	/** Motivation: The result the next TrySend call must return, regardless of packet contents. */
	ETransportResult ForcedSendResult{ETransportResult::Success};

	/** Motivation: The result the next TryReceive call must return, regardless of destination. */
	ETransportResult ForcedReceiveResult{ETransportResult::Unavailable};

	/** Motivation: The byte count a successful forced receive reports. */
	std::size_t ReceiveByteCount{0};

	/** Motivation: The byte value written into every received byte so success is observable. */
	std::uint8_t ReceiveFillerByte{DefaultReceiveFillerByte};

	/** Motivation: The sender address a successful forced receive stamps into OutFrom. */
	FDeviceAddress ReceiveSender{};

	/** Motivation: Counts every send attempt, including failures, so backpressure retention is observable. */
	std::size_t SendCount{0};

	/** Motivation: Counts only successful sends so recorded slots map one-to-one to delivered packets. */
	std::size_t SuccessfulSendCount{0};

	/** Motivation: Counts how many times the manager attempted a receive. */
	std::size_t ReceiveAttemptCount{0};

	static constexpr std::size_t MaxRecordedSends = 16;
	static constexpr std::size_t MaxRecordedBytes = 8;
	static constexpr std::size_t DeviceMaxPacketBytes = 64;

	/** Motivation: Records the exact bytes of each send so FIFO order is provable. */
	std::uint8_t RecordedSendBytes[MaxRecordedSends][MaxRecordedBytes]{};

	/** Motivation: Records the exact length of each send alongside its bytes. */
	std::size_t RecordedSendLengths[MaxRecordedSends]{};

	/** Motivation: Records the destination address the manager passed with each send so per-packet routing is provable. */
	FDeviceAddress RecordedSendDestinations[MaxRecordedSends]{};
};

/**
 * Motivation: Construct a manager over a recording device and fixed-capacity packet storage.
 * Responsibilities: The manager reports an empty non-full FIFO with queue capacity and max packet bytes matching the
 *   template parameters and zero queued.
 */
MW_TEST_CASE(TransportManagerStartsEmptyWithFixedConfiguration)
{
	// Arrange
	FRecordingDevice Device;
	TTransportPacketStorage<2, 4> Storage;
	TTransportManager<2, 4> Manager(Device, Storage);

	// Assert
	MW_EXPECT_EQ(Test, true, Manager.IsEmpty(), "A fresh manager must report an empty FIFO");
	MW_EXPECT_EQ(Test, false, Manager.IsFull(), "A fresh manager must not report a full FIFO");
	MW_EXPECT_EQ(Test, TwoBytePacketLength, Manager.QueueCapacity(), "Queue capacity must match the template parameter");
	MW_EXPECT_EQ(Test, FourBytePacketLength, Manager.MaximumPacketBytes(), "Max packet bytes must match the template parameter");
	MW_EXPECT_EQ(Test, std::size_t{0}, Manager.QueuedCount(), "A fresh manager must report zero queued packets");
}

/**
 * Motivation: Attempt to queue a packet larger than MaximumPacketBytes.
 * Responsibilities: The queue returns Invalid and no packet is enqueued.
 */
MW_TEST_CASE(TransportManagerRejectsOversizedPacketTransactionally)
{
	// Arrange
	FRecordingDevice Device;
	TTransportPacketStorage<2, 2> Storage;
	TTransportManager<2, 2> Manager(Device, Storage);

	// Act
	const ETransportResult OversizedResult =
		Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(OversizedPacketData, sizeof(OversizedPacketData)));
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, OversizedResult, "A packet larger than MaximumPacketBytes must return Invalid");
	MW_EXPECT_EQ(Test, true, Manager.IsEmpty(), "Oversized queue must not enqueue a packet");
}

/**
 * Motivation: Attempt to queue a null packet with a nonzero length.
 * Responsibilities: The queue returns Invalid and no packet is enqueued.
 */
MW_TEST_CASE(TransportManagerRejectsNullPacketWithNonzeroLength)
{
	// Arrange
	FRecordingDevice Device;
	TTransportPacketStorage<2, 4> Storage;
	TTransportManager<2, 4> Manager(Device, Storage);

	// Act
	const ETransportResult NullResult = Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(nullptr, TwoBytePacketLength));
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, NullResult, "Null data with nonzero length must return Invalid");
	MW_EXPECT_EQ(Test, true, Manager.IsEmpty(), "Invalid queue must not enqueue a packet");
}

/**
 * Motivation: Queue three differently sized and valued packets, then advance each to the device.
 * Responsibilities: Three advances call the device exactly three times and deliver the packets in FIFO order with
 *   byte-for-byte matching contents, leaving.
 */
MW_TEST_CASE(TransportManagerAdvanceSendsDifferentlySizedPacketsInFifoOrder)
{
	// Arrange
	FRecordingDevice Device;
	TTransportPacketStorage<3, 4> Storage;
	TTransportManager<3, 4> Manager(Device, Storage);

	// Act - queue three packets
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(FifoFirstPacket, sizeof(FifoFirstPacket))),
		"First queue must succeed");
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(FifoSecondPacket, sizeof(FifoSecondPacket))),
		"Second queue must succeed");
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(FifoThirdPacket, sizeof(FifoThirdPacket))),
		"Third queue must succeed");

	// Act - advance each queued packet to the device
	Manager.AdvanceSend();
	Manager.AdvanceSend();
	Manager.AdvanceSend();

	// Assert
	MW_EXPECT_EQ(Test, ThreeBytePacketLength, Device.SendCount, "Three advances must call the device exactly three times");

	// First send: 2 bytes {0x10, 0x20}
	MW_EXPECT_EQ(Test, TwoBytePacketLength, Device.RecordedSendLengths[0], "First send must carry the first packet length");
	MW_EXPECT_EQ(Test, FifoFirstPacket[0], Device.RecordedSendBytes[0][0], "First send must carry the first packet first byte");
	MW_EXPECT_EQ(Test, FifoFirstPacket[1], Device.RecordedSendBytes[0][1], "First send must carry the first packet second byte");

	// Second send: 3 bytes {0x30, 0x40, 0x50}
	MW_EXPECT_EQ(Test, ThreeBytePacketLength, Device.RecordedSendLengths[1], "Second send must carry the second packet length");
	MW_EXPECT_EQ(Test, FifoSecondPacket[0], Device.RecordedSendBytes[1][0], "Second send must carry the second packet first byte");
	MW_EXPECT_EQ(Test, FifoSecondPacket[2], Device.RecordedSendBytes[1][2], "Second send must carry the second packet third byte");

	// Third send: 1 byte {0x60}
	MW_EXPECT_EQ(Test, OneBytePacketLength, Device.RecordedSendLengths[2], "Third send must carry the third packet length");
	MW_EXPECT_EQ(Test, FifoThirdPacket[0], Device.RecordedSendBytes[2][0], "Third send must carry the third packet first byte");

	MW_EXPECT_EQ(Test, true, Manager.IsEmpty(), "Three successful advances must drain a three-packet FIFO");
}

/**
 * Motivation: Fill a one-slot FIFO, attempt to queue an overflow packet, then advance the device.
 * Responsibilities: The overflow queue returns Full without changing the queued count, and the accepted head packet
 *   survives to be advanced intact.
 */
MW_TEST_CASE(TransportManagerFullFifoRejectsFurtherQueue)
{
	// Arrange
	FRecordingDevice Device;
	TTransportPacketStorage<1, 4> Storage;
	TTransportManager<1, 4> Manager(Device, Storage);

	// Act
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(FullFifoAcceptedPacket, sizeof(FullFifoAcceptedPacket))),
		"First queue into an empty FIFO must succeed");
	// Act
	const ETransportResult OverflowResult =
		Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(FullFifoRejectedPacket, sizeof(FullFifoRejectedPacket)));
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Full, OverflowResult, "Queue into a full FIFO must return Full");
	MW_EXPECT_EQ(Test, OneBytePacketLength, Manager.QueuedCount(), "Overflow must not change the queued count");

	// Act / Assert - prove the accepted head survives the rejected queue.
	Manager.AdvanceSend();
	MW_EXPECT_EQ(Test, TwoBytePacketLength, Device.RecordedSendLengths[0], "Retained head must carry the accepted packet length");
	MW_EXPECT_EQ(Test, FullFifoAcceptedPacket[0], Device.RecordedSendBytes[0][0], "Retained head must carry the accepted first byte");
}

/**
 * Motivation: Advance an empty FIFO and observe the recording device's send count.
 * Responsibilities: The advance returns Unavailable and never calls the device.
 */
MW_TEST_CASE(TransportManagerAdvanceEmptyReturnsUnavailableWithoutDeviceCall)
{
	// Arrange
	FRecordingDevice Device;
	TTransportPacketStorage<2, 4> Storage;
	TTransportManager<2, 4> Manager(Device, Storage);

	// Act
	const ETransportResult EmptyAdvanceResult = Manager.AdvanceSend();
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Unavailable, EmptyAdvanceResult, "Advance on an empty FIFO must return Unavailable");
	MW_EXPECT_EQ(Test, std::size_t{0}, Device.SendCount, "Empty advance must not call the device");
}

/**
 * Motivation: Queue one head packet, then advance once against a successful device.
 * Responsibilities: The advance succeeds, calls the device exactly once with the head packet length, and removes the
 *   head from the FIFO.
 */
MW_TEST_CASE(TransportManagerAdvanceAttemptsOneSendAndRemovesHeadOnSuccess)
{
	// Arrange
	FRecordingDevice Device;
	TTransportPacketStorage<2, 4> Storage;
	TTransportManager<2, 4> Manager(Device, Storage);

	Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(SingleAdvanceHeadPacket, sizeof(SingleAdvanceHeadPacket)));

	// Act
	const ETransportResult AdvanceResult = Manager.AdvanceSend();
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, AdvanceResult, "Advance with a successful device must succeed");
	MW_EXPECT_EQ(Test, OneBytePacketLength, Device.SendCount, "Advance must call the device exactly once");
	MW_EXPECT_EQ(Test, TwoBytePacketLength, Device.RecordedSendLengths[0], "Advance must send the head packet length");
	MW_EXPECT_EQ(Test, true, Manager.IsEmpty(), "Successful advance must remove the head packet");
}

/**
 * Motivation: Queue two packets, advance against a device that returns Full, then clear backpressure and advance
 *   twice more.
 * Responsibilities: Device Full propagates as Full and retains all queued packets; once backpressure clears, advances
 *   send the retained first packet ahead of.
 */
MW_TEST_CASE(TransportManagerDeviceFullRetainsExactHeadContents)
{
	// Arrange
	FRecordingDevice Device;
	Device.ForcedSendResult = ETransportResult::Full;
	TTransportPacketStorage<2, 4> Storage;
	TTransportManager<2, 4> Manager(Device, Storage);

	Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(DeviceFullFirstPacket, sizeof(DeviceFullFirstPacket)));
	Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(DeviceFullSecondPacket, sizeof(DeviceFullSecondPacket)));

	// Act
	const ETransportResult AdvanceResult = Manager.AdvanceSend();
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Full, AdvanceResult, "Device Full must propagate as Full");
	MW_EXPECT_EQ(Test, TwoBytePacketLength, Manager.QueuedCount(), "Device Full must retain all queued packets");

	// Clear backpressure: the next advance must send the retained first packet, not the second.
	Device.ForcedSendResult = ETransportResult::Success;
	// Act
	const ETransportResult RecoveryAdvanceResult = Manager.AdvanceSend();
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, RecoveryAdvanceResult, "Recovery advance must succeed after backpressure clears");
	MW_EXPECT_EQ(Test, ThreeBytePacketLength, Device.RecordedSendLengths[0], "Retained head must be the first packet length");
	MW_EXPECT_EQ(Test, DeviceFullFirstPacket[0], Device.RecordedSendBytes[0][0], "Retained head must carry the first packet first byte");
	MW_EXPECT_EQ(Test, DeviceFullFirstPacket[2], Device.RecordedSendBytes[0][2], "Retained head must carry the first packet third byte");

	// Act / Assert - the next advance must send the second packet in FIFO order.
	Manager.AdvanceSend();
	MW_EXPECT_EQ(Test, TwoBytePacketLength, Device.RecordedSendLengths[1], "Second advance must send the second packet length");
	MW_EXPECT_EQ(Test, DeviceFullSecondPacket[0], Device.RecordedSendBytes[1][0], "Second advance must send the second packet first byte");
}

/**
 * Motivation: Queue one packet, advance against a device that returns Unavailable, then switch the device to
 *   success and advance again.
 * Responsibilities: Device Unavailable propagates as Unavailable and retains the head packet; the retry advance sends
 *   the retained head with its original.
 */
MW_TEST_CASE(TransportManagerDeviceUnavailableRetainsExactHead)
{
	// Arrange
	FRecordingDevice Device;
	Device.ForcedSendResult = ETransportResult::Unavailable;
	TTransportPacketStorage<1, 4> Storage;
	TTransportManager<1, 4> Manager(Device, Storage);

	Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(DeviceUnavailablePacket, sizeof(DeviceUnavailablePacket)));

	// Act
	const ETransportResult AdvanceResult = Manager.AdvanceSend();
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Unavailable, AdvanceResult, "Device Unavailable must propagate as Unavailable");
	MW_EXPECT_EQ(Test, OneBytePacketLength, Manager.QueuedCount(), "Device Unavailable must retain the head packet");

	Device.ForcedSendResult = ETransportResult::Success;
	// Act
	Manager.AdvanceSend();
	// Assert
	MW_EXPECT_EQ(Test, TwoBytePacketLength, Device.RecordedSendLengths[0], "Retained head must carry its original length");
	MW_EXPECT_EQ(Test, DeviceUnavailablePacket[0], Device.RecordedSendBytes[0][0], "Retained head must carry its original first byte");
}

/**
 * Motivation: Queue one packet, advance against a device that returns Invalid, then switch the device to success
 *   and advance again.
 * Responsibilities: Device Invalid propagates as Invalid and retains the head packet; the retry advance sends the
 *   retained head with its original length and.
 */
MW_TEST_CASE(TransportManagerDeviceInvalidRetainsExactHead)
{
	// Arrange
	FRecordingDevice Device;
	Device.ForcedSendResult = ETransportResult::Invalid;
	TTransportPacketStorage<1, 4> Storage;
	TTransportManager<1, 4> Manager(Device, Storage);

	Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(DeviceInvalidPacket, sizeof(DeviceInvalidPacket)));

	// Act
	const ETransportResult AdvanceResult = Manager.AdvanceSend();
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, AdvanceResult, "Device Invalid must propagate as Invalid");
	MW_EXPECT_EQ(Test, OneBytePacketLength, Manager.QueuedCount(), "Device Invalid must retain the head packet");

	Device.ForcedSendResult = ETransportResult::Success;
	// Act
	Manager.AdvanceSend();
	// Assert
	MW_EXPECT_EQ(Test, TwoBytePacketLength, Device.RecordedSendLengths[0], "Retained head must carry its original length");
	MW_EXPECT_EQ(Test, DeviceInvalidPacket[1], Device.RecordedSendBytes[0][1], "Retained head must carry its original second byte");
}

/**
 * Motivation: Queue a head and a later packet, advance into a full device, clear backpressure, then advance twice.
 * Responsibilities: Backpressure retains both packets; recovery sends the retained head first, before the later packet,
 *   removing only the head on the first.
 */
MW_TEST_CASE(TransportManagerRecoverySendsRetainedHeadBeforeLaterPackets)
{
	// Arrange
	FRecordingDevice Device;
	Device.ForcedSendResult = ETransportResult::Full;
	TTransportPacketStorage<2, 4> Storage;
	TTransportManager<2, 4> Manager(Device, Storage);

	Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(RecoveryHeadPacket, sizeof(RecoveryHeadPacket)));
	Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(RecoveryLaterPacket, sizeof(RecoveryLaterPacket)));

	// Act / Assert
	MW_EXPECT_EQ(Test, ETransportResult::Full, Manager.AdvanceSend(), "First advance into a full device must return Full");
	MW_EXPECT_EQ(Test, TwoBytePacketLength, Manager.QueuedCount(), "Backpressure must retain both packets");

	Device.ForcedSendResult = ETransportResult::Success;
	// Act
	const ETransportResult FirstRecovery = Manager.AdvanceSend();
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, FirstRecovery, "Recovery advance must succeed");
	MW_EXPECT_EQ(Test, TwoBytePacketLength, Device.RecordedSendLengths[0], "Recovery must send the retained head, not the later packet");
	MW_EXPECT_EQ(Test, RecoveryHeadPacket[0], Device.RecordedSendBytes[0][0], "Recovery must send the retained head first byte");
	MW_EXPECT_EQ(Test, OneBytePacketLength, Manager.QueuedCount(), "Recovery must remove only the head");

	// Act / Assert
	Manager.AdvanceSend();
	MW_EXPECT_EQ(Test, OneBytePacketLength, Device.RecordedSendLengths[1], "Second advance must send the later packet");
	MW_EXPECT_EQ(Test, RecoveryLaterPacket[0], Device.RecordedSendBytes[1][0], "Second advance must send the later packet byte");
}

/**
 * Motivation: Cycle a two-slot FIFO through queue-fill-advance-drain more times than its capacity, recording each
 *   send.
 * Responsibilities: Caller-owned storage is reused across many wraparound cycles, each cycle queues and delivers both
 *   packets in order, and the device is.
 */
MW_TEST_CASE(TransportManagerCallerStorageReusedAfterWraparoundAndDraining)
{
	// Arrange
	FRecordingDevice Device;
	TTransportPacketStorage<2, 2> Storage;
	TTransportManager<2, 2> Manager(Device, Storage);

	// Act / Assert - cycle the FIFO more times than its capacity so head/tail indices wrap around repeatedly.
	for (std::size_t Cycle = 0; Cycle < WraparoundCycleCount; ++Cycle)
	{
		MW_EXPECT_EQ(
			Test,
			ETransportResult::Success,
			Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(WraparoundCycleAPacket, sizeof(WraparoundCycleAPacket))),
			"Queue A must succeed each cycle");
		MW_EXPECT_EQ(
			Test,
			ETransportResult::Success,
			Manager.QueueSend(MakeDest(DefaultDestIndex), TSpan<const std::uint8_t>(WraparoundCycleBPacket, sizeof(WraparoundCycleBPacket))),
			"Queue B must succeed each cycle");
		MW_EXPECT_EQ(Test, true, Manager.IsFull(), "Two queues must fill the two-slot FIFO each cycle");

		Manager.AdvanceSend();
		Manager.AdvanceSend();
		MW_EXPECT_EQ(Test, true, Manager.IsEmpty(), "Two advances must drain the FIFO each cycle");

		const std::size_t SendIndex = Cycle * 2;
		MW_EXPECT_EQ(Test, TwoBytePacketLength, Device.RecordedSendLengths[SendIndex], "Cycle A send must carry two bytes");
		MW_EXPECT_EQ(Test, WraparoundCycleAPacket[0], Device.RecordedSendBytes[SendIndex][0], "Cycle A send must carry the A packet first byte");
		MW_EXPECT_EQ(Test, TwoBytePacketLength, Device.RecordedSendLengths[SendIndex + 1], "Cycle B send must carry two bytes");
		MW_EXPECT_EQ(Test, WraparoundCycleBPacket[1], Device.RecordedSendBytes[SendIndex + 1][1], "Cycle B send must carry the B packet second byte");
	}

	// Assert
	MW_EXPECT_EQ(Test, WraparoundCycleCount * 2, Device.SendCount, "Six cycles of two sends must call the device exactly twelve times");
}

/**
 * Motivation: Queue one packet to each of three distinct destinations, then advance each to the device.
 * Responsibilities: Three advances call the device exactly three times and route each head to its stored destination
 *   address in FIFO order with its original.
 */
MW_TEST_CASE(TransportManagerAdvanceSendsEachHeadToItsStoredDestination)
{
	// Arrange
	FRecordingDevice Device;
	TTransportPacketStorage<3, 4> Storage;
	TTransportManager<3, 4> Manager(Device, Storage);

	const FDeviceAddress DestA = MakeDest(DestIndexA);
	const FDeviceAddress DestB = MakeDest(DestIndexB);
	const FDeviceAddress DestC = MakeDest(DestIndexC);
	// Act - queue one packet to each distinct destination
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Manager.QueueSend(DestA, TSpan<const std::uint8_t>(RoutedPacketA, sizeof(RoutedPacketA))),
		"Queue to DestA must succeed");
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Manager.QueueSend(DestB, TSpan<const std::uint8_t>(RoutedPacketB, sizeof(RoutedPacketB))),
		"Queue to DestB must succeed");
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Manager.QueueSend(DestC, TSpan<const std::uint8_t>(RoutedPacketC, sizeof(RoutedPacketC))),
		"Queue to DestC must succeed");

	// Act - advance each queued packet to the device
	Manager.AdvanceSend();
	Manager.AdvanceSend();
	Manager.AdvanceSend();

	// Assert - each recorded send must carry the exact destination stored with that packet, in FIFO order.
	MW_EXPECT_EQ(Test, ThreeBytePacketLength, Device.SendCount, "Three advances must call the device exactly three times");
	MW_EXPECT_EQ(Test, true, Device.RecordedSendDestinations[0] == DestA, "First advance must send to the first queued destination");
	MW_EXPECT_EQ(Test, true, Device.RecordedSendDestinations[1] == DestB, "Second advance must send to the second queued destination");
	MW_EXPECT_EQ(Test, true, Device.RecordedSendDestinations[2] == DestC, "Third advance must send to the third queued destination");
	MW_EXPECT_EQ(Test, RoutedPacketA[0], Device.RecordedSendBytes[0][0], "First send must still carry the first packet bytes");
	MW_EXPECT_EQ(Test, RoutedPacketC[1], Device.RecordedSendBytes[2][1], "Third send must still carry the third packet bytes");
	MW_EXPECT_EQ(Test, true, Manager.IsEmpty(), "Three successful advances must drain a three-packet FIFO");
}

/**
 * Motivation: Receive against a device that returns Unavailable with pre-filled destination, byte count, and
 *   sender outputs.
 * Responsibilities: Receive performs exactly one direct device receive, propagates Unavailable, and leaves
 *   BytesReceived, the destination, and OutFrom.
 */
MW_TEST_CASE(TransportManagerReceivePerformsOneDirectDeviceReceive)
{
	// Arrange
	FRecordingDevice Device;
	Device.ForcedReceiveResult = ETransportResult::Unavailable;
	TTransportPacketStorage<2, 4> Storage;
	TTransportManager<2, 4> Manager(Device, Storage);

	std::uint8_t Destination[FourBytePacketLength] = {DestinationPrefillByte, DestinationPrefillByte, DestinationPrefillByte, DestinationPrefillByte};
	FReceiveResult ReceiveResult{UntouchedBytesReceivedSentinel};
	FDeviceAddress ReceiveFrom{UntouchedAddressByte};
	// Act
	const ETransportResult UnavailableResult = Manager.Receive(ReceiveFrom, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Unavailable, UnavailableResult, "Receive must propagate the device result");
	MW_EXPECT_EQ(Test, OneBytePacketLength, Device.ReceiveAttemptCount, "Receive must call the device exactly once");
	MW_EXPECT_EQ(Test, UntouchedBytesReceivedSentinel, ReceiveResult.BytesReceived, "Unavailable receive must leave BytesReceived unchanged");
	MW_EXPECT_EQ(Test, DestinationPrefillByte, Destination[0], "Unavailable receive must not modify the destination");
	MW_EXPECT_EQ(Test, UntouchedAddressByte, ReceiveFrom.Bytes[0], "Unavailable receive must leave OutFrom unchanged");
}

/**
 * Motivation: Receive against a device that succeeds with a chosen byte count, fill byte, and sender address.
 * Responsibilities: Receive propagates Success with the device-reported byte count, fills exactly that many destination
 *   bytes without writing past it, and.
 */
MW_TEST_CASE(TransportManagerReceivePropagatesSuccessAndByteCount)
{
	// Arrange
	FRecordingDevice Device;
	Device.ForcedReceiveResult = ETransportResult::Success;
	Device.ReceiveByteCount = ReceiveFillByteCount;
	Device.ReceiveFillerByte = ReceiveFillerByteValue;
	Device.ReceiveSender = MakeDest(ReceiveSenderIndex);
	TTransportPacketStorage<2, 4> Storage;
	TTransportManager<2, 4> Manager(Device, Storage);

	std::uint8_t Destination[FourBytePacketLength] = {0};
	FReceiveResult ReceiveResult{UntouchedBytesReceivedSentinel};
	FDeviceAddress ReceiveFrom{UntouchedAddressByte};
	// Act
	const ETransportResult SuccessResult = Manager.Receive(ReceiveFrom, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, SuccessResult, "Receive must propagate a successful device result");
	MW_EXPECT_EQ(Test, ReceiveFillByteCount, ReceiveResult.BytesReceived, "Receive must propagate the device byte count");
	MW_EXPECT_EQ(Test, ReceiveFillerByteValue, Destination[0], "Receive must propagate the device destination bytes");
	MW_EXPECT_EQ(Test, ReceiveFillerByteValue, Destination[2], "Receive must fill exactly the reported byte count");
	MW_EXPECT_EQ(Test, std::uint8_t{0}, Destination[3], "Receive must not write past the reported byte count");
	MW_EXPECT_EQ(Test, true, ReceiveFrom == Device.ReceiveSender, "Receive must propagate the device-reported sender address");
}

} // namespace
