#include "TestSupport.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Transport/HostLoopback.h>
#include <MicroWorld/Transport/DeviceAddress.h>
#include <MicroWorld/Transport/Device.h>
#include <MicroWorld/Transport/TransportResult.h>

#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::ETransportResult;
using MicroWorld::FDeviceAddress;
using MicroWorld::FReceiveResult;
using MicroWorld::IDevice;
using MicroWorld::MakeLoopbackAddress;
using MicroWorld::THostLoopback;
using MicroWorld::TSpan;

/** Sentinel address byte that proves a receive call did not overwrite the caller's address. */
constexpr std::uint8_t UntouchedAddressByte = 0x42;

/** Pre-fill marker written into every destination byte before a receive, so a delivery is observable. */
constexpr std::uint8_t DestinationPrefillByte = 0xFF;

/** Value written into destination storage before each multi-port receive so an overwrite is provable. */
constexpr std::uint8_t DestinationResetMarker = 0xEE;

/** Sentinel value pre-loaded into BytesReceived so an unchanged failed receive is observable. */
constexpr std::size_t UntouchedBytesReceivedSentinel = 0xEE;

/** Loopback template parameter: one mailbox (start, multi-port cases raise it). */
constexpr std::size_t SinglePortCount = 1;
/** Loopback template parameter: four mailboxes for the multi-port routing cases. */
constexpr std::size_t FourPortCount = 4;
/** Loopback template parameter: two mailbox slots. */
constexpr std::size_t TwoSlotMailbox = 2;
/** Loopback template parameter: one mailbox slot (forces Full after a single send). */
constexpr std::size_t OneSlotMailbox = 1;
/** Loopback template parameter: a four-byte per-packet capacity. */
constexpr std::size_t FourBytePacketCapacity = 4;
/** Loopback template parameter: a two-byte per-packet capacity. */
constexpr std::size_t TwoBytePacketCapacity = 2;
/** Loopback port index that owns the outbound side in the single-port cases. */
constexpr std::uint8_t SourcePort = 0;
/** Number of target ports the multi-port routing case delivers distinct packets to. */
constexpr std::uint8_t TargetPortCount = 3;
/** First target port index the multi-port routing case sends to. */
constexpr std::uint8_t FirstTargetPort = 1;
/** Out-of-range port index the unroutable-address case must reject. */
constexpr std::uint8_t OverRangePortIndex = 99;
/** Driver port index queried for MaxPacketBytes in the capacity-report case. */
constexpr std::uint8_t ReportedDriverPort = 2;

/** Two-byte packet the FIFO case delivers as the head packet. */
constexpr std::uint8_t FifoFirstPacket[2] = {0x10, 0x20};
/** Three-byte packet the FIFO case delivers after the head packet. */
constexpr std::uint8_t FifoSecondPacket[3] = {0x30, 0x40, 0x50};
/** Two-byte packet the full-queue case accepts before the overflow send. */
constexpr std::uint8_t FullQueueAcceptedPacket[2] = {0xAA, 0xBB};
/** Two-byte packet the full-queue case rejects as the overflow send. */
constexpr std::uint8_t FullQueueRejectedPacket[2] = {0xCC, 0xDD};
/** Three-byte packet the too-small-destination case retains as the head. */
constexpr std::uint8_t TooSmallHeadPacket[3] = {0x01, 0x02, 0x03};
/** Two-byte packet the drain case sends first to fill the mailbox. */
constexpr std::uint8_t DrainFirstPacket[2] = {0x11, 0x22};
/** Two-byte packet the drain case sends second to fill the mailbox. */
constexpr std::uint8_t DrainSecondPacket[2] = {0x33, 0x44};
/** Two-byte packet the drain case re-queues after the mailbox is emptied. */
constexpr std::uint8_t DrainReusePacket[2] = {0x55, 0x66};
/** Four-byte oversized packet the oversized-rejection case must reject. */
constexpr std::uint8_t OversizedPacket[4] = {0x01, 0x02, 0x03, 0x04};
/** Two-byte packet the null-destination-retains-head case queues as the head. */
constexpr std::uint8_t NullDestHeadPacket[2] = {0x11, 0x22};
/** Two-byte packet the IDevice interface case threads through the loopback. */
constexpr std::uint8_t InterfacePacket[2] = {0x07, 0x08};
/** Single-byte packet the multi-port routing case delivers to port 1. */
constexpr std::uint8_t ToPort1Packet[1] = {0x01};
/** Single-byte packet the multi-port routing case delivers to port 2. */
constexpr std::uint8_t ToPort2Packet[1] = {0x02};
/** Single-byte packet the multi-port routing case delivers to port 3. */
constexpr std::uint8_t ToPort3Packet[1] = {0x03};
/** Two-byte request packet the two-way-reply case sends from port 0 to port 1. */
constexpr std::uint8_t TwoWayRequestPacket[2] = {0xA0, 0xA1};
/** Two-byte reply packet the two-way-reply case sends from port 1 back to port 0. */
constexpr std::uint8_t TwoWayReplyPacket[2] = {0xB0, 0xB1};
/** Two-byte packet the unroutable-address case tries to send to invalid destinations. */
constexpr std::uint8_t UnroutablePacket[2] = {0x77, 0x88};
/** Three-byte packet the capacity-report case retains on a too-small receive. */
constexpr std::uint8_t CapacityReportHeadPacket[3] = {0x10, 0x20, 0x30};

/**
 * Scenario: Construct a fresh host loopback with a fixed mailbox depth and per-packet capacity.
 * Expected: The mailbox reports empty, not full, matching template capacities and zero queued packets.
 */
MW_TEST_CASE(HostLoopbackStartsEmptyWithFixedCapacities)
{
	// Arrange
	THostLoopback<SinglePortCount, TwoSlotMailbox, FourBytePacketCapacity> Loopback;

	// Assert
	MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(SourcePort), "A fresh loopback mailbox must be empty");
	MW_EXPECT_EQ(Test, false, Loopback.IsFull(SourcePort), "A fresh loopback mailbox must not be full");
	MW_EXPECT_EQ(Test, TwoSlotMailbox, Loopback.MailboxCapacityValue(), "Mailbox capacity must match the template parameter");
	MW_EXPECT_EQ(Test, FourBytePacketCapacity, Loopback.MaximumPacketBytes(), "Max packet bytes must match the template parameter");
	MW_EXPECT_EQ(Test, std::size_t{0}, Loopback.QueuedCount(SourcePort), "A fresh loopback must report zero queued packets");
}

/**
 * Scenario: Queue two differently sized packets into a single port, then receive both in turn.
 * Expected: Each receive delivers the queued bytes in FIFO order, reports the sender as port 0, and leaves the mailbox empty after both are drained.
 */
MW_TEST_CASE(HostLoopbackDeliversPacketsInFifoOrder)
{
	// Arrange
	THostLoopback<SinglePortCount, TwoSlotMailbox, FourBytePacketCapacity> Loopback;
	const FDeviceAddress Port0 = MakeLoopbackAddress(SourcePort);

	// Act - queue two packets
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Loopback.Port(SourcePort).TrySend(Port0, TSpan<const std::uint8_t>(FifoFirstPacket, sizeof(FifoFirstPacket))),
		"First send must succeed");
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Loopback.Port(SourcePort).TrySend(Port0, TSpan<const std::uint8_t>(FifoSecondPacket, sizeof(FifoSecondPacket))),
		"Second send must succeed");
	// Assert
	MW_EXPECT_EQ(Test, TwoSlotMailbox, Loopback.QueuedCount(SourcePort), "Two sends must queue two packets");

	std::uint8_t Destination[FourBytePacketCapacity] = {};
	FReceiveResult FirstReceive{};
	FDeviceAddress FirstFrom{UntouchedAddressByte};
	// Act - receive the head packet
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Loopback.Port(SourcePort).TryReceive(FirstFrom, TSpan<std::uint8_t>(Destination, sizeof(Destination)), FirstReceive),
		"First receive must succeed");
	// Assert
	MW_EXPECT_EQ(Test, TwoSlotMailbox, FirstReceive.BytesReceived, "First receive must report the head packet length");
	MW_EXPECT_EQ(Test, FifoFirstPacket[0], Destination[0], "First receive must deliver the first head byte");
	MW_EXPECT_EQ(Test, FifoFirstPacket[1], Destination[1], "First receive must deliver the second head byte");
	MW_EXPECT_EQ(Test, true, FirstFrom == Port0, "First receive must report the sender as port 0");

	std::uint8_t SecondDestination[FourBytePacketCapacity] = {};
	FReceiveResult SecondReceive{};
	FDeviceAddress SecondFrom{UntouchedAddressByte};
	// Act - receive the next packet
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Loopback.Port(SourcePort).TryReceive(SecondFrom, TSpan<std::uint8_t>(SecondDestination, sizeof(SecondDestination)), SecondReceive),
		"Second receive must succeed");
	// Assert
	MW_EXPECT_EQ(Test, sizeof(FifoSecondPacket), SecondReceive.BytesReceived, "Second receive must report the next head packet length");
	MW_EXPECT_EQ(Test, FifoSecondPacket[0], SecondDestination[0], "Second receive must deliver the second packet first byte");
	MW_EXPECT_EQ(Test, true, SecondFrom == Port0, "Second receive must report the sender as port 0");
	MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(SourcePort), "Loopback must be empty after draining both packets");
}

/**
 * Scenario: Fill a one-slot queue, attempt an overflow send, then drain the head.
 * Expected: The overflow send returns Full without changing the queued count, and the accepted head packet survives unmodified.
 */
MW_TEST_CASE(HostLoopbackFullQueueDoesNotOverwriteAcceptedPackets)
{
	// Arrange
	THostLoopback<SinglePortCount, OneSlotMailbox, FourBytePacketCapacity> Loopback;
	const FDeviceAddress Port0 = MakeLoopbackAddress(SourcePort);

	// Act
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Loopback.Port(SourcePort).TrySend(Port0, TSpan<const std::uint8_t>(FullQueueAcceptedPacket, sizeof(FullQueueAcceptedPacket))),
		"Send into an empty queue must succeed");
	// Assert
	MW_EXPECT_EQ(Test, true, Loopback.IsFull(SourcePort), "A one-slot queue must be full after one send");

	// Act
	const ETransportResult OverflowResult =
		Loopback.Port(SourcePort).TrySend(Port0, TSpan<const std::uint8_t>(FullQueueRejectedPacket, sizeof(FullQueueRejectedPacket)));
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Full, OverflowResult, "Send into a full queue must return Full");
	MW_EXPECT_EQ(Test, OneSlotMailbox, Loopback.QueuedCount(SourcePort), "Overflow must not change the queued count");

	std::uint8_t Destination[FourBytePacketCapacity] = {};
	FReceiveResult ReceiveResult{};
	FDeviceAddress ReceiveFrom{};
	// Act - drain to prove the accepted head survived
	Loopback.Port(SourcePort).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);
	// Assert
	MW_EXPECT_EQ(Test, FullQueueAcceptedPacket[0], Destination[0], "Overflow must not overwrite the accepted head packet");
	MW_EXPECT_EQ(Test, FullQueueAcceptedPacket[1], Destination[1], "Overflow must not overwrite the accepted head packet");
}

/**
 * Scenario: Attempt a receive on an empty mailbox with pre-filled destination, byte count, and sender outputs.
 * Expected: The receive returns Unavailable and leaves the destination, BytesReceived, and OutFrom untouched.
 */
MW_TEST_CASE(HostLoopbackEmptyReceiveReturnsUnavailable)
{
	// Arrange
	THostLoopback<SinglePortCount, TwoSlotMailbox, FourBytePacketCapacity> Loopback;

	std::uint8_t Destination[FourBytePacketCapacity] = {
		DestinationPrefillByte, DestinationPrefillByte, DestinationPrefillByte, DestinationPrefillByte};
	FReceiveResult ReceiveResult{UntouchedBytesReceivedSentinel};
	FDeviceAddress ReceiveFrom{UntouchedAddressByte};
	// Act
	const ETransportResult EmptyResult =
		Loopback.Port(SourcePort).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Unavailable, EmptyResult, "Receive from an empty loopback must return Unavailable");
	MW_EXPECT_EQ(Test, UntouchedBytesReceivedSentinel, ReceiveResult.BytesReceived, "Failed receive must leave BytesReceived unchanged");
	MW_EXPECT_EQ(Test, DestinationPrefillByte, Destination[0], "Failed receive must not modify the destination");
	MW_EXPECT_EQ(Test, UntouchedAddressByte, ReceiveFrom.Bytes[0], "Failed receive must leave OutFrom unchanged");
}

/**
 * Scenario: Send a three-byte head packet, attempt a receive into a too-small destination, then retry with a large enough destination.
 * Expected: The too-small receive returns Full and leaves the head packet and outputs intact; the retry succeeds and delivers the retained head with
 * its original bytes and sender.
 */
MW_TEST_CASE(HostLoopbackTooSmallDestinationRetainsHeadPacket)
{
	// Arrange
	THostLoopback<SinglePortCount, OneSlotMailbox, FourBytePacketCapacity> Loopback;
	const FDeviceAddress Port0 = MakeLoopbackAddress(SourcePort);

	Loopback.Port(SourcePort).TrySend(Port0, TSpan<const std::uint8_t>(TooSmallHeadPacket, sizeof(TooSmallHeadPacket)));

	std::uint8_t TooSmall[2] = {DestinationPrefillByte, DestinationPrefillByte};
	FReceiveResult ReceiveResult{UntouchedBytesReceivedSentinel};
	FDeviceAddress ReceiveFrom{UntouchedAddressByte};
	// Act
	const ETransportResult SmallResult =
		Loopback.Port(SourcePort).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(TooSmall, sizeof(TooSmall)), ReceiveResult);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Full, SmallResult, "A destination too small for the head must return Full");
	MW_EXPECT_EQ(Test, UntouchedBytesReceivedSentinel, ReceiveResult.BytesReceived, "Failed receive must leave BytesReceived unchanged");
	MW_EXPECT_EQ(Test, DestinationPrefillByte, TooSmall[0], "Failed receive must not modify the destination");
	MW_EXPECT_EQ(Test, UntouchedAddressByte, ReceiveFrom.Bytes[0], "Failed receive must leave OutFrom unchanged");
	MW_EXPECT_EQ(Test, OneSlotMailbox, Loopback.QueuedCount(SourcePort), "Too-small receive must retain the head packet");

	std::uint8_t LargeDestination[FourBytePacketCapacity] = {};
	FReceiveResult RetryResult{UntouchedBytesReceivedSentinel};
	FDeviceAddress RetryFrom{UntouchedAddressByte};
	// Act - retry with a large enough destination
	const ETransportResult RetrySendResult =
		Loopback.Port(SourcePort).TryReceive(RetryFrom, TSpan<std::uint8_t>(LargeDestination, sizeof(LargeDestination)), RetryResult);
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, RetrySendResult, "Retry with a larger destination must succeed");
	MW_EXPECT_EQ(Test, sizeof(TooSmallHeadPacket), RetryResult.BytesReceived, "Retained head must deliver its original length");
	MW_EXPECT_EQ(Test, TooSmallHeadPacket[0], LargeDestination[0], "Retained head must deliver its original bytes");
	MW_EXPECT_EQ(Test, true, RetryFrom == Port0, "Retained head receive must report the sender as port 0");
}

/**
 * Scenario: Fill a two-slot mailbox, drain it, then send a fresh packet.
 * Expected: Drain empties the mailbox and resets the queued count, and the fresh send reuses the freed capacity successfully.
 */
MW_TEST_CASE(HostLoopbackDrainRestoresCapacityForReuse)
{
	// Arrange
	THostLoopback<SinglePortCount, TwoSlotMailbox, TwoBytePacketCapacity> Loopback;
	const FDeviceAddress Port0 = MakeLoopbackAddress(SourcePort);

	// Act - fill the mailbox
	Loopback.Port(SourcePort).TrySend(Port0, TSpan<const std::uint8_t>(DrainFirstPacket, sizeof(DrainFirstPacket)));
	Loopback.Port(SourcePort).TrySend(Port0, TSpan<const std::uint8_t>(DrainSecondPacket, sizeof(DrainSecondPacket)));
	// Assert
	MW_EXPECT_EQ(Test, true, Loopback.IsFull(SourcePort), "Two sends must fill the two-slot mailbox");

	// Act
	Loopback.Drain(SourcePort);
	// Assert
	MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(SourcePort), "Drain must empty the mailbox");
	MW_EXPECT_EQ(Test, std::size_t{0}, Loopback.QueuedCount(SourcePort), "Drain must reset the queued count");

	// Act / Assert
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Loopback.Port(SourcePort).TrySend(Port0, TSpan<const std::uint8_t>(DrainReusePacket, sizeof(DrainReusePacket))),
		"Send after drain must reuse the freed capacity");
}

/**
 * Scenario: Send a zero-length packet, then receive it into a pre-filled destination.
 * Expected: The zero-length send succeeds but still occupies one slot, and the receive succeeds reporting zero bytes without modifying the
 * destination and still reports the sender as port 0.
 */
MW_TEST_CASE(HostLoopbackAcceptsZeroLengthPacketRoundTrip)
{
	// Arrange
	THostLoopback<SinglePortCount, OneSlotMailbox, TwoBytePacketCapacity> Loopback;
	const FDeviceAddress Port0 = MakeLoopbackAddress(SourcePort);

	// Act
	const ETransportResult ZeroSendResult = Loopback.Port(SourcePort).TrySend(Port0, TSpan<const std::uint8_t>(nullptr, 0));
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, ZeroSendResult, "A zero-length send must succeed as a valid no-op");
	MW_EXPECT_EQ(Test, OneSlotMailbox, Loopback.QueuedCount(SourcePort), "Zero-length send must still occupy one slot");

	std::uint8_t Destination[2] = {DestinationPrefillByte, DestinationPrefillByte};
	FReceiveResult ReceiveResult{};
	FDeviceAddress ReceiveFrom{UntouchedAddressByte};
	// Act
	const ETransportResult ZeroReceiveResult =
		Loopback.Port(SourcePort).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, ZeroReceiveResult, "Receive of a queued zero-length packet must succeed");
	MW_EXPECT_EQ(Test, std::size_t{0}, ReceiveResult.BytesReceived, "Zero-length receive must report zero bytes");
	MW_EXPECT_EQ(Test, DestinationPrefillByte, Destination[0], "Zero-length receive must not modify the destination");
	MW_EXPECT_EQ(Test, true, ReceiveFrom == Port0, "Zero-length receive must still report the sender as port 0");
}

/**
 * Scenario: Attempt to send a packet larger than the loopback's maximum packet bytes.
 * Expected: The send returns Invalid and no packet is queued.
 */
MW_TEST_CASE(HostLoopbackRejectsOversizedPacket)
{
	// Arrange
	THostLoopback<SinglePortCount, TwoSlotMailbox, TwoBytePacketCapacity> Loopback;
	const FDeviceAddress Port0 = MakeLoopbackAddress(SourcePort);

	// Act
	const ETransportResult OversizedResult =
		Loopback.Port(SourcePort).TrySend(Port0, TSpan<const std::uint8_t>(OversizedPacket, sizeof(OversizedPacket)));
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, OversizedResult, "A packet larger than MaximumPacketBytes must return Invalid");
	MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(SourcePort), "Oversized send must not queue a packet");
}

/**
 * Scenario: Attempt to send a null packet with a nonzero length.
 * Expected: The send returns Invalid and no packet is queued.
 */
MW_TEST_CASE(HostLoopbackRejectsNullPacketWithNonzeroLength)
{
	// Arrange
	THostLoopback<SinglePortCount, TwoSlotMailbox, FourBytePacketCapacity> Loopback;
	const FDeviceAddress Port0 = MakeLoopbackAddress(SourcePort);

	// Act
	const ETransportResult NullResult = Loopback.Port(SourcePort).TrySend(Port0, TSpan<const std::uint8_t>(nullptr, TwoBytePacketCapacity));
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, NullResult, "Null data with nonzero length must return Invalid");
	MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(SourcePort), "Invalid send must not queue a packet");
}

/**
 * Scenario: Attempt a receive with a null destination and nonzero length into pre-filled outputs on an empty loopback.
 * Expected: The receive returns Invalid and leaves the destination, BytesReceived, OutFrom, and queue state unchanged.
 */
MW_TEST_CASE(HostLoopbackEmptyReceiveNullDestinationReturnsInvalid)
{
	// Arrange
	THostLoopback<SinglePortCount, TwoSlotMailbox, FourBytePacketCapacity> Loopback;
	MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(SourcePort), "Precondition: the loopback mailbox must start empty");

	// Sentinel output bytes, BytesReceived, and OutFrom so an unchanged failure is provable.
	std::uint8_t Destination[FourBytePacketCapacity] = {
		DestinationPrefillByte, DestinationPrefillByte, DestinationPrefillByte, DestinationPrefillByte};
	FReceiveResult ReceiveResult{UntouchedBytesReceivedSentinel};
	FDeviceAddress ReceiveFrom{UntouchedAddressByte};

	// Act
	const ETransportResult NullResult =
		Loopback.Port(SourcePort).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(nullptr, FourBytePacketCapacity), ReceiveResult);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, NullResult, "A null destination with nonzero length must return Invalid even on an empty loopback");
	MW_EXPECT_EQ(Test, UntouchedBytesReceivedSentinel, ReceiveResult.BytesReceived, "Invalid receive must leave BytesReceived unchanged");
	MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(SourcePort), "Invalid receive must not change the queue state");
	MW_EXPECT_EQ(Test, std::size_t{0}, Loopback.QueuedCount(SourcePort), "Invalid receive must not change the queued count");

	// The caller-supplied sentinel destination storage must be untouched even though the loopback owns no packet to copy.
	MW_EXPECT_EQ(Test, DestinationPrefillByte, Destination[0], "Invalid receive must not modify the destination");
	MW_EXPECT_EQ(Test, DestinationPrefillByte, Destination[3], "Invalid receive must not modify the destination tail");
	MW_EXPECT_EQ(Test, UntouchedAddressByte, ReceiveFrom.Bytes[0], "Invalid receive must leave OutFrom unchanged");
}

/**
 * Scenario: Queue a head packet, attempt a receive with a null destination and nonzero length, then retry with a valid destination.
 * Expected: The null-destination receive returns Invalid and leaves the destination, BytesReceived, OutFrom, and queue count unchanged; the retry
 * delivers the retained head with its original bytes and sender.
 */
MW_TEST_CASE(HostLoopbackNullDestinationRetainsHeadPacketAndOutputs)
{
	// Arrange
	THostLoopback<SinglePortCount, OneSlotMailbox, FourBytePacketCapacity> Loopback;
	const FDeviceAddress Port0 = MakeLoopbackAddress(SourcePort);

	Loopback.Port(SourcePort).TrySend(Port0, TSpan<const std::uint8_t>(NullDestHeadPacket, sizeof(NullDestHeadPacket)));
	MW_EXPECT_EQ(Test, OneSlotMailbox, Loopback.QueuedCount(SourcePort), "Precondition: the head packet must be queued");

	std::uint8_t Destination[FourBytePacketCapacity] = {
		DestinationPrefillByte, DestinationPrefillByte, DestinationPrefillByte, DestinationPrefillByte};
	FReceiveResult ReceiveResult{UntouchedBytesReceivedSentinel};
	FDeviceAddress ReceiveFrom{UntouchedAddressByte};

	// Act
	const ETransportResult NullResult =
		Loopback.Port(SourcePort).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(nullptr, FourBytePacketCapacity), ReceiveResult);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, NullResult, "A null destination with nonzero length must return Invalid even with a queued head");
	MW_EXPECT_EQ(Test, UntouchedBytesReceivedSentinel, ReceiveResult.BytesReceived, "Invalid receive must leave BytesReceived unchanged");
	MW_EXPECT_EQ(Test, DestinationPrefillByte, Destination[0], "Invalid receive must not modify the destination");
	MW_EXPECT_EQ(Test, UntouchedAddressByte, ReceiveFrom.Bytes[0], "Invalid receive must leave OutFrom unchanged");
	MW_EXPECT_EQ(Test, OneSlotMailbox, Loopback.QueuedCount(SourcePort), "Invalid receive must retain the head packet");

	// Act - the retained head must still be deliverable to a valid destination.
	std::uint8_t RetryDestination[FourBytePacketCapacity] = {0};
	FReceiveResult RetryResult{UntouchedBytesReceivedSentinel};
	FDeviceAddress RetryFrom{UntouchedAddressByte};
	const ETransportResult RetryResultValue =
		Loopback.Port(SourcePort).TryReceive(RetryFrom, TSpan<std::uint8_t>(RetryDestination, sizeof(RetryDestination)), RetryResult);
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, RetryResultValue, "Retained head must be deliverable to a valid destination");
	MW_EXPECT_EQ(Test, sizeof(NullDestHeadPacket), RetryResult.BytesReceived, "Retained head must deliver its original length");
	MW_EXPECT_EQ(Test, NullDestHeadPacket[0], RetryDestination[0], "Retained head must deliver its original first byte");
	MW_EXPECT_EQ(Test, true, RetryFrom == Port0, "Retained head receive must report the sender as port 0");
}

/**
 * Scenario: Send and receive through an IDevice reference bound to a loopback port.
 * Expected: The interface send and receive route to the loopback mailbox, delivering the head packet length and the correct sender.
 */
MW_TEST_CASE(HostLoopbackSatisfiesIDeviceInterface)
{
	// Arrange
	THostLoopback<SinglePortCount, OneSlotMailbox, FourBytePacketCapacity> Loopback;
	const FDeviceAddress Port0 = MakeLoopbackAddress(SourcePort);
	IDevice& Driver = Loopback.Port(SourcePort);

	// Act
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Driver.TrySend(Port0, TSpan<const std::uint8_t>(InterfacePacket, sizeof(InterfacePacket))),
		"Interface send must route to the loopback mailbox");

	std::uint8_t Destination[FourBytePacketCapacity] = {};
	FReceiveResult ReceiveResult{};
	FDeviceAddress ReceiveFrom{UntouchedAddressByte};
	// Act / Assert
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Driver.TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult),
		"Interface receive must route to the loopback mailbox");
	MW_EXPECT_EQ(Test, sizeof(InterfacePacket), ReceiveResult.BytesReceived, "Interface receive must report the head packet length");
	MW_EXPECT_EQ(Test, true, ReceiveFrom == Port0, "Interface receive must report the sender as port 0");
}

/**
 * Scenario: From port 0, send a distinct single-byte packet to each of ports 1, 2, and 3, then receive on each target.
 * Expected: The sender's own mailbox stays empty and each target receives exactly its own packet with OutFrom equal to port 0 exactly once
 * (multi-port isolation).
 */
MW_TEST_CASE(HostLoopbackRoutesDistinctPacketsToEachTargetPort)
{
	// Arrange
	THostLoopback<FourPortCount, TwoSlotMailbox, FourBytePacketCapacity> Loopback;
	const FDeviceAddress Port0 = MakeLoopbackAddress(SourcePort);

	// Act - port 0 sends a distinct 1-byte packet to each of ports 1, 2, and 3.
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Loopback.Port(SourcePort).TrySend(MakeLoopbackAddress(1), TSpan<const std::uint8_t>(ToPort1Packet, sizeof(ToPort1Packet))),
		"Send to port 1 must succeed");
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Loopback.Port(SourcePort).TrySend(MakeLoopbackAddress(2), TSpan<const std::uint8_t>(ToPort2Packet, sizeof(ToPort2Packet))),
		"Send to port 2 must succeed");
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Loopback.Port(SourcePort).TrySend(MakeLoopbackAddress(3), TSpan<const std::uint8_t>(ToPort3Packet, sizeof(ToPort3Packet))),
		"Send to port 3 must succeed");

	// Assert - the sender's own mailbox and the still-silent port 0 must remain empty.
	MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(SourcePort), "Port 0's mailbox must stay empty after only outbound traffic");

	// Act / Assert - each target receives exactly its own packet, with OutFrom == port 0, exactly once.
	std::uint8_t Destination[FourBytePacketCapacity] = {};
	const std::uint8_t DistinctPackets[TargetPortCount] = {ToPort1Packet[0], ToPort2Packet[0], ToPort3Packet[0]};
	for (std::uint8_t Target = FirstTargetPort; Target < FirstTargetPort + TargetPortCount; ++Target)
	{
		FReceiveResult ReceiveResult{};
		FDeviceAddress ReceiveFrom{UntouchedAddressByte};
		Destination[0] = DestinationResetMarker;
		const ETransportResult Result =
			Loopback.Port(Target).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);
		MW_EXPECT_EQ(Test, ETransportResult::Success, Result, "Each target must deliver its queued packet");
		MW_EXPECT_EQ(Test, OneSlotMailbox, ReceiveResult.BytesReceived, "Each target must report one received byte");
		MW_EXPECT_EQ(Test, DistinctPackets[Target - FirstTargetPort], Destination[0], "Each target must receive exactly its own packet");
		MW_EXPECT_EQ(Test, true, ReceiveFrom == Port0, "Each target must report the sender as port 0");
		MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(Target), "Each target mailbox must be empty after one receive");
	}
}

/**
 * Scenario: Port 0 sends a request to port 1, port 1 receives it and replies back to port 0, then port 0 receives the reply.
 * Expected: Each receive reports the correct sender as the other port, and the reply bytes are delivered intact.
 */
MW_TEST_CASE(HostLoopbackSupportsTwoWayReplyWithCorrectSender)
{
	// Arrange
	THostLoopback<FourPortCount, TwoSlotMailbox, FourBytePacketCapacity> Loopback;
	const FDeviceAddress Port0 = MakeLoopbackAddress(SourcePort);
	const FDeviceAddress Port1 = MakeLoopbackAddress(FirstTargetPort);

	// Act - port 0 sends to port 1; port 1 receives and then replies back to port 0.
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Loopback.Port(SourcePort).TrySend(Port1, TSpan<const std::uint8_t>(TwoWayRequestPacket, sizeof(TwoWayRequestPacket))),
		"Port 0 sending to port 1 must succeed");

	std::uint8_t RequestDestination[FourBytePacketCapacity] = {};
	FReceiveResult RequestReceive{};
	FDeviceAddress RequestFrom{UntouchedAddressByte};
	// Act / Assert
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Loopback.Port(FirstTargetPort).TryReceive(RequestFrom, TSpan<std::uint8_t>(RequestDestination, sizeof(RequestDestination)), RequestReceive),
		"Port 1 must receive the request");
	MW_EXPECT_EQ(Test, true, RequestFrom == Port0, "Port 1 must see the request sender as port 0");

	// Act
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Loopback.Port(FirstTargetPort).TrySend(Port0, TSpan<const std::uint8_t>(TwoWayReplyPacket, sizeof(TwoWayReplyPacket))),
		"Port 1 replying to port 0 must succeed");

	std::uint8_t ReplyDestination[FourBytePacketCapacity] = {};
	FReceiveResult ReplyReceive{};
	FDeviceAddress ReplyFrom{UntouchedAddressByte};
	// Act / Assert
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Loopback.Port(SourcePort).TryReceive(ReplyFrom, TSpan<std::uint8_t>(ReplyDestination, sizeof(ReplyDestination)), ReplyReceive),
		"Port 0 must receive the reply");
	MW_EXPECT_EQ(Test, TwoWayReplyPacket[0], ReplyDestination[0], "Port 0 must receive the reply bytes");
	MW_EXPECT_EQ(Test, true, ReplyFrom == Port1, "Port 0 must see the reply sender as port 1");
}

/**
 * Scenario: Attempt to send to an out-of-range port index, a zero-size address, and a two-byte address, then check every mailbox.
 * Expected: Each unroutable destination is rejected as Invalid and no mailbox absorbs any packet.
 */
MW_TEST_CASE(HostLoopbackRejectsUnroutableDestinationAddress)
{
	// Arrange
	THostLoopback<FourPortCount, TwoSlotMailbox, FourBytePacketCapacity> Loopback;

	// Act / Assert - out-of-range port index (the loopback exposes only ports 0..3).
	const ETransportResult OverRangeResult =
		Loopback.Port(SourcePort)
			.TrySend(MakeLoopbackAddress(OverRangePortIndex), TSpan<const std::uint8_t>(UnroutablePacket, sizeof(UnroutablePacket)));
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, OverRangeResult, "A destination port index above MaxPorts-1 must return Invalid");

	// A zero-size address carries no port index at all.
	FDeviceAddress EmptyAddress{};
	EmptyAddress.Size = 0;
	// Act
	const ETransportResult EmptySizeResult =
		Loopback.Port(SourcePort).TrySend(EmptyAddress, TSpan<const std::uint8_t>(UnroutablePacket, sizeof(UnroutablePacket)));
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, EmptySizeResult, "A zero-size address must return Invalid");

	// A two-byte address is the wrong encoding for this driver.
	FDeviceAddress TwoByteAddress{};
	TwoByteAddress.Bytes[0] = 1;
	TwoByteAddress.Bytes[1] = 0;
	TwoByteAddress.Size = 2;
	// Act
	const ETransportResult TwoByteResult =
		Loopback.Port(SourcePort).TrySend(TwoByteAddress, TSpan<const std::uint8_t>(UnroutablePacket, sizeof(UnroutablePacket)));
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, TwoByteResult, "A two-byte address must return Invalid");

	// Assert - no mailbox may have absorbed any packet from the rejected sends.
	for (std::uint8_t Port = 0; Port < FourPortCount; ++Port)
	{
		MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(Port), "An unroutable destination must not enqueue a packet on any mailbox");
	}
}

/**
 * Scenario: Query the per-port driver and loopback maximum packet bytes, send a three-byte head, then attempt a too-small receive.
 * Expected: MaxPacketBytes reports the template packet capacity; the too-small receive returns Full, leaves BytesReceived and OutFrom unchanged, and
 * retains the head packet.
 */
MW_TEST_CASE(HostLoopbackReportsMaxPacketBytesAndRetainsHeadOnTooSmallReceive)
{
	// Arrange
	THostLoopback<FourPortCount, OneSlotMailbox, FourBytePacketCapacity> Loopback;
	const FDeviceAddress Port0 = MakeLoopbackAddress(SourcePort);

	// Assert - the per-port driver reports the network's per-packet byte capacity.
	IDevice& Driver = Loopback.Port(ReportedDriverPort);
	MW_EXPECT_EQ(Test, FourBytePacketCapacity, Driver.MaxPacketBytes(), "MaxPacketBytes must report the template packet byte capacity");
	MW_EXPECT_EQ(Test, FourBytePacketCapacity, Loopback.MaximumPacketBytes(), "MaximumPacketBytes must report the template packet byte capacity");

	// A too-small receive must retain the head packet and leave OutFrom at its caller sentinel.
	Loopback.Port(SourcePort).TrySend(Port0, TSpan<const std::uint8_t>(CapacityReportHeadPacket, sizeof(CapacityReportHeadPacket)));

	std::uint8_t TooSmall[2] = {DestinationPrefillByte, DestinationPrefillByte};
	FReceiveResult TooSmallReceive{UntouchedBytesReceivedSentinel};
	FDeviceAddress TooSmallFrom{UntouchedAddressByte};
	// Act
	const ETransportResult TooSmallResult =
		Loopback.Port(SourcePort).TryReceive(TooSmallFrom, TSpan<std::uint8_t>(TooSmall, sizeof(TooSmall)), TooSmallReceive);
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Full, TooSmallResult, "A destination too small for the head must return Full");
	MW_EXPECT_EQ(Test, UntouchedBytesReceivedSentinel, TooSmallReceive.BytesReceived, "Failed receive must leave BytesReceived unchanged");
	MW_EXPECT_EQ(Test, UntouchedAddressByte, TooSmallFrom.Bytes[0], "Failed receive must leave OutFrom unchanged");
	MW_EXPECT_EQ(Test, OneSlotMailbox, Loopback.QueuedCount(SourcePort), "Too-small receive must retain the head packet");
}

} // namespace
