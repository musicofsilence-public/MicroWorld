#include "TestSupport.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Net/HostLoopback.h>
#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetDriver.h>
#include <MicroWorld/Net/NetResult.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::ENetResult;
using MicroWorld::FHostLoopback;
using MicroWorld::FNetAddress;
using MicroWorld::FNetReceiveResult;
using MicroWorld::INetDriver;
using MicroWorld::MakeLoopbackAddress;
using MicroWorld::TSpan;

/** Proves a fresh loopback is empty and reports its fixed capacities. */
MW_TEST_CASE(HostLoopbackStartsEmptyWithFixedCapacities)
{
	FHostLoopback<1, 2, 4> Loopback;

	MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(0), "A fresh loopback mailbox must be empty");
	MW_EXPECT_EQ(Test, false, Loopback.IsFull(0), "A fresh loopback mailbox must not be full");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Loopback.MailboxCapacityValue(), "Mailbox capacity must match the template parameter");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(4), Loopback.MaximumPacketBytes(), "Max packet bytes must match the template parameter");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Loopback.QueuedCount(0), "A fresh loopback must report zero queued packets");
}

/** Proves send followed by receive delivers the same bytes in FIFO order. */
MW_TEST_CASE(HostLoopbackDeliversPacketsInFifoOrder)
{
	FHostLoopback<1, 2, 4> Loopback;
	const FNetAddress Port0 = MakeLoopbackAddress(0);

	const std::array<std::uint8_t, 2> FirstPacket{0x10, 0x20};
	const std::array<std::uint8_t, 3> SecondPacket{0x30, 0x40, 0x50};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Loopback.Port(0).TrySend(Port0, TSpan<const std::uint8_t>(FirstPacket.data(), FirstPacket.size())),
		"First send must succeed");
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Loopback.Port(0).TrySend(Port0, TSpan<const std::uint8_t>(SecondPacket.data(), SecondPacket.size())),
		"Second send must succeed");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Loopback.QueuedCount(0), "Two sends must queue two packets");

	std::array<std::uint8_t, 4> Destination{};
	FNetReceiveResult FirstReceive{};
	FNetAddress FirstFrom{0x42};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Loopback.Port(0).TryReceive(FirstFrom, TSpan<std::uint8_t>(Destination.data(), Destination.size()), FirstReceive),
		"First receive must succeed");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), FirstReceive.BytesReceived, "First receive must report the head packet length");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x10), Destination[0], "First receive must deliver the first head byte");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x20), Destination[1], "First receive must deliver the second head byte");
	MW_EXPECT_EQ(Test, true, FirstFrom == Port0, "First receive must report the sender as port 0");

	std::array<std::uint8_t, 4> SecondDestination{};
	FNetReceiveResult SecondReceive{};
	FNetAddress SecondFrom{0x42};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Loopback.Port(0).TryReceive(SecondFrom, TSpan<std::uint8_t>(SecondDestination.data(), SecondDestination.size()), SecondReceive),
		"Second receive must succeed");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(3), SecondReceive.BytesReceived, "Second receive must report the next head packet length");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x30), SecondDestination[0], "Second receive must deliver the second packet first byte");
	MW_EXPECT_EQ(Test, true, SecondFrom == Port0, "Second receive must report the sender as port 0");
	MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(0), "Loopback must be empty after draining both packets");
}

/** Proves a full queue rejects further sends without overwriting accepted packets. */
MW_TEST_CASE(HostLoopbackFullQueueDoesNotOverwriteAcceptedPackets)
{
	FHostLoopback<1, 1, 4> Loopback;
	const FNetAddress Port0 = MakeLoopbackAddress(0);

	const std::array<std::uint8_t, 2> Accepted{0xAA, 0xBB};
	const std::array<std::uint8_t, 2> Rejected{0xCC, 0xDD};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Loopback.Port(0).TrySend(Port0, TSpan<const std::uint8_t>(Accepted.data(), Accepted.size())),
		"Send into an empty queue must succeed");
	MW_EXPECT_EQ(Test, true, Loopback.IsFull(0), "A one-slot queue must be full after one send");

	const ENetResult OverflowResult = Loopback.Port(0).TrySend(Port0, TSpan<const std::uint8_t>(Rejected.data(), Rejected.size()));
	MW_EXPECT_EQ(Test, ENetResult::Full, OverflowResult, "Send into a full queue must return Full");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Loopback.QueuedCount(0), "Overflow must not change the queued count");

	std::array<std::uint8_t, 4> Destination{};
	FNetReceiveResult ReceiveResult{};
	FNetAddress ReceiveFrom{};
	Loopback.Port(0).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination.data(), Destination.size()), ReceiveResult);
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xAA), Destination[0], "Overflow must not overwrite the accepted head packet");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xBB), Destination[1], "Overflow must not overwrite the accepted head packet");
}

/** Proves an empty receive returns Unavailable without touching its destination, byte count, or sender output. */
MW_TEST_CASE(HostLoopbackEmptyReceiveReturnsUnavailable)
{
	FHostLoopback<1, 2, 4> Loopback;

	std::array<std::uint8_t, 4> Destination{0xFF, 0xFF, 0xFF, 0xFF};
	FNetReceiveResult ReceiveResult{std::size_t{0xEE}};
	FNetAddress ReceiveFrom{0x42};
	const ENetResult EmptyResult =
		Loopback.Port(0).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination.data(), Destination.size()), ReceiveResult);

	MW_EXPECT_EQ(Test, ENetResult::Unavailable, EmptyResult, "Receive from an empty loopback must return Unavailable");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0xEE), ReceiveResult.BytesReceived, "Failed receive must leave BytesReceived unchanged");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xFF), Destination[0], "Failed receive must not modify the destination");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x42), ReceiveFrom.Bytes[0], "Failed receive must leave OutFrom unchanged");
}

/** Proves a too-small destination returns Full and leaves the head packet and outputs intact. */
MW_TEST_CASE(HostLoopbackTooSmallDestinationRetainsHeadPacket)
{
	FHostLoopback<1, 1, 4> Loopback;
	const FNetAddress Port0 = MakeLoopbackAddress(0);

	const std::array<std::uint8_t, 3> HeadPacket{0x01, 0x02, 0x03};
	Loopback.Port(0).TrySend(Port0, TSpan<const std::uint8_t>(HeadPacket.data(), HeadPacket.size()));

	std::array<std::uint8_t, 2> TooSmall{0xFF, 0xFF};
	FNetReceiveResult ReceiveResult{std::size_t{0xEE}};
	FNetAddress ReceiveFrom{0x42};
	const ENetResult SmallResult = Loopback.Port(0).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(TooSmall.data(), TooSmall.size()), ReceiveResult);

	MW_EXPECT_EQ(Test, ENetResult::Full, SmallResult, "A destination too small for the head must return Full");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0xEE), ReceiveResult.BytesReceived, "Failed receive must leave BytesReceived unchanged");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xFF), TooSmall[0], "Failed receive must not modify the destination");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x42), ReceiveFrom.Bytes[0], "Failed receive must leave OutFrom unchanged");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Loopback.QueuedCount(0), "Too-small receive must retain the head packet");

	std::array<std::uint8_t, 4> LargeDestination{};
	FNetReceiveResult RetryResult{std::size_t{0xEE}};
	FNetAddress RetryFrom{0x42};
	const ENetResult RetrySendResult =
		Loopback.Port(0).TryReceive(RetryFrom, TSpan<std::uint8_t>(LargeDestination.data(), LargeDestination.size()), RetryResult);
	MW_EXPECT_EQ(Test, ENetResult::Success, RetrySendResult, "Retry with a larger destination must succeed");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(3), RetryResult.BytesReceived, "Retained head must deliver its original length");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x01), LargeDestination[0], "Retained head must deliver its original bytes");
	MW_EXPECT_EQ(Test, true, RetryFrom == Port0, "Retained head receive must report the sender as port 0");
}

/** Proves Drain empties the mailbox so capacity can be reused. */
MW_TEST_CASE(HostLoopbackDrainRestoresCapacityForReuse)
{
	FHostLoopback<1, 2, 2> Loopback;
	const FNetAddress Port0 = MakeLoopbackAddress(0);

	const std::array<std::uint8_t, 2> FirstPacket{0x11, 0x22};
	const std::array<std::uint8_t, 2> SecondPacket{0x33, 0x44};
	Loopback.Port(0).TrySend(Port0, TSpan<const std::uint8_t>(FirstPacket.data(), FirstPacket.size()));
	Loopback.Port(0).TrySend(Port0, TSpan<const std::uint8_t>(SecondPacket.data(), SecondPacket.size()));
	MW_EXPECT_EQ(Test, true, Loopback.IsFull(0), "Two sends must fill the two-slot mailbox");

	Loopback.Drain(0);
	MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(0), "Drain must empty the mailbox");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Loopback.QueuedCount(0), "Drain must reset the queued count");

	const std::array<std::uint8_t, 2> ReusePacket{0x55, 0x66};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Loopback.Port(0).TrySend(Port0, TSpan<const std::uint8_t>(ReusePacket.data(), ReusePacket.size())),
		"Send after drain must reuse the freed capacity");
}

/** Proves a zero-length packet is enqueued and delivered as a zero-byte receive. */
MW_TEST_CASE(HostLoopbackAcceptsZeroLengthPacketRoundTrip)
{
	FHostLoopback<1, 1, 2> Loopback;
	const FNetAddress Port0 = MakeLoopbackAddress(0);

	const ENetResult ZeroSendResult = Loopback.Port(0).TrySend(Port0, TSpan<const std::uint8_t>(nullptr, 0));
	MW_EXPECT_EQ(Test, ENetResult::Success, ZeroSendResult, "A zero-length send must succeed as a valid no-op");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Loopback.QueuedCount(0), "Zero-length send must still occupy one slot");

	std::array<std::uint8_t, 2> Destination{0xFF, 0xFF};
	FNetReceiveResult ReceiveResult{};
	FNetAddress ReceiveFrom{0x42};
	const ENetResult ZeroReceiveResult =
		Loopback.Port(0).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination.data(), Destination.size()), ReceiveResult);
	MW_EXPECT_EQ(Test, ENetResult::Success, ZeroReceiveResult, "Receive of a queued zero-length packet must succeed");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), ReceiveResult.BytesReceived, "Zero-length receive must report zero bytes");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xFF), Destination[0], "Zero-length receive must not modify the destination");
	MW_EXPECT_EQ(Test, true, ReceiveFrom == Port0, "Zero-length receive must still report the sender as port 0");
}

/** Proves an oversized packet is rejected as Invalid without queueing. */
MW_TEST_CASE(HostLoopbackRejectsOversizedPacket)
{
	FHostLoopback<1, 2, 2> Loopback;
	const FNetAddress Port0 = MakeLoopbackAddress(0);

	const std::array<std::uint8_t, 4> Oversized{0x01, 0x02, 0x03, 0x04};
	const ENetResult OversizedResult = Loopback.Port(0).TrySend(Port0, TSpan<const std::uint8_t>(Oversized.data(), Oversized.size()));
	MW_EXPECT_EQ(Test, ENetResult::Invalid, OversizedResult, "A packet larger than MaximumPacketBytes must return Invalid");
	MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(0), "Oversized send must not queue a packet");
}

/** Proves a null packet with nonzero length is rejected as Invalid without queueing. */
MW_TEST_CASE(HostLoopbackRejectsNullPacketWithNonzeroLength)
{
	FHostLoopback<1, 2, 4> Loopback;
	const FNetAddress Port0 = MakeLoopbackAddress(0);

	const ENetResult NullResult = Loopback.Port(0).TrySend(Port0, TSpan<const std::uint8_t>(nullptr, 2));
	MW_EXPECT_EQ(Test, ENetResult::Invalid, NullResult, "Null data with nonzero length must return Invalid");
	MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(0), "Invalid send must not queue a packet");
}

/**
 * Proves a null destination with nonzero length returns Invalid even when the loopback is empty,
 * and that this transactional rejection leaves the destination, BytesReceived, OutFrom, and queue state unchanged.
 */
MW_TEST_CASE(HostLoopbackEmptyReceiveNullDestinationReturnsInvalid)
{
	FHostLoopback<1, 2, 4> Loopback;
	MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(0), "Precondition: the loopback mailbox must start empty");

	// Sentinel output bytes, BytesReceived, and OutFrom so an unchanged failure is provable.
	std::array<std::uint8_t, 4> Destination{0xFF, 0xFF, 0xFF, 0xFF};
	FNetReceiveResult ReceiveResult{std::size_t{0xEE}};
	FNetAddress ReceiveFrom{0x42};

	const ENetResult NullResult = Loopback.Port(0).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(nullptr, 4), ReceiveResult);

	MW_EXPECT_EQ(Test, ENetResult::Invalid, NullResult, "A null destination with nonzero length must return Invalid even on an empty loopback");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0xEE), ReceiveResult.BytesReceived, "Invalid receive must leave BytesReceived unchanged");
	MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(0), "Invalid receive must not change the queue state");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Loopback.QueuedCount(0), "Invalid receive must not change the queued count");

	// The caller-supplied sentinel destination storage must be untouched even though the loopback owns no packet to copy.
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xFF), Destination[0], "Invalid receive must not modify the destination");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xFF), Destination[3], "Invalid receive must not modify the destination tail");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x42), ReceiveFrom.Bytes[0], "Invalid receive must leave OutFrom unchanged");
}

/**
 * Proves a null destination with nonzero length returns Invalid even when the loopback has a queued head,
 * and that the head packet survives for a later valid retry.
 */
MW_TEST_CASE(HostLoopbackNullDestinationRetainsHeadPacketAndOutputs)
{
	FHostLoopback<1, 1, 4> Loopback;
	const FNetAddress Port0 = MakeLoopbackAddress(0);

	const std::array<std::uint8_t, 2> HeadPacket{0x11, 0x22};
	Loopback.Port(0).TrySend(Port0, TSpan<const std::uint8_t>(HeadPacket.data(), HeadPacket.size()));
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Loopback.QueuedCount(0), "Precondition: the head packet must be queued");

	std::array<std::uint8_t, 4> Destination{0xFF, 0xFF, 0xFF, 0xFF};
	FNetReceiveResult ReceiveResult{std::size_t{0xEE}};
	FNetAddress ReceiveFrom{0x42};

	const ENetResult NullResult = Loopback.Port(0).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(nullptr, 4), ReceiveResult);

	MW_EXPECT_EQ(Test, ENetResult::Invalid, NullResult, "A null destination with nonzero length must return Invalid even with a queued head");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0xEE), ReceiveResult.BytesReceived, "Invalid receive must leave BytesReceived unchanged");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xFF), Destination[0], "Invalid receive must not modify the destination");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x42), ReceiveFrom.Bytes[0], "Invalid receive must leave OutFrom unchanged");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Loopback.QueuedCount(0), "Invalid receive must retain the head packet");

	// The retained head must still be deliverable to a valid destination.
	std::array<std::uint8_t, 4> RetryDestination{0};
	FNetReceiveResult RetryResult{std::size_t{0xEE}};
	FNetAddress RetryFrom{0x42};
	const ENetResult RetryResultValue =
		Loopback.Port(0).TryReceive(RetryFrom, TSpan<std::uint8_t>(RetryDestination.data(), RetryDestination.size()), RetryResult);
	MW_EXPECT_EQ(Test, ENetResult::Success, RetryResultValue, "Retained head must be deliverable to a valid destination");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), RetryResult.BytesReceived, "Retained head must deliver its original length");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x11), RetryDestination[0], "Retained head must deliver its original first byte");
	MW_EXPECT_EQ(Test, true, RetryFrom == Port0, "Retained head receive must report the sender as port 0");
}

/** Proves the loopback port satisfies the INetDriver interface so a driver reference is usable. */
MW_TEST_CASE(HostLoopbackSatisfiesINetDriverInterface)
{
	FHostLoopback<1, 1, 4> Loopback;
	const FNetAddress Port0 = MakeLoopbackAddress(0);
	INetDriver& Driver = Loopback.Port(0);

	const std::array<std::uint8_t, 2> Packet{0x07, 0x08};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Driver.TrySend(Port0, TSpan<const std::uint8_t>(Packet.data(), Packet.size())),
		"Interface send must route to the loopback mailbox");

	std::array<std::uint8_t, 4> Destination{};
	FNetReceiveResult ReceiveResult{};
	FNetAddress ReceiveFrom{0x42};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Driver.TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination.data(), Destination.size()), ReceiveResult),
		"Interface receive must route to the loopback mailbox");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), ReceiveResult.BytesReceived, "Interface receive must report the head packet length");
	MW_EXPECT_EQ(Test, true, ReceiveFrom == Port0, "Interface receive must report the sender as port 0");
}

/**
 * Proves one port can deliver distinct packets to three other ports and each target receives exactly
 * its own packet with the correct sender; non-target mailboxes stay empty (multi-port isolation).
 */
MW_TEST_CASE(HostLoopbackRoutesDistinctPacketsToEachTargetPort)
{
	FHostLoopback<4, 2, 4> Loopback;
	const FNetAddress Port0 = MakeLoopbackAddress(0);

	// Port 0 sends a distinct 1-byte packet to each of ports 1, 2, and 3.
	const std::array<std::uint8_t, 1> ToPort1{0x01};
	const std::array<std::uint8_t, 1> ToPort2{0x02};
	const std::array<std::uint8_t, 1> ToPort3{0x03};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Loopback.Port(0).TrySend(MakeLoopbackAddress(1), TSpan<const std::uint8_t>(ToPort1.data(), ToPort1.size())),
		"Send to port 1 must succeed");
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Loopback.Port(0).TrySend(MakeLoopbackAddress(2), TSpan<const std::uint8_t>(ToPort2.data(), ToPort2.size())),
		"Send to port 2 must succeed");
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Loopback.Port(0).TrySend(MakeLoopbackAddress(3), TSpan<const std::uint8_t>(ToPort3.data(), ToPort3.size())),
		"Send to port 3 must succeed");

	// The sender's own mailbox and the still-silent port 0 must remain empty.
	MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(0), "Port 0's mailbox must stay empty after only outbound traffic");

	// Each target receives exactly its own packet, with OutFrom == port 0, exactly once.
	std::array<std::uint8_t, 4> Destination{};
	for (std::uint8_t Target = 1; Target <= 3; ++Target)
	{
		FNetReceiveResult ReceiveResult{};
		FNetAddress ReceiveFrom{0x42};
		Destination.fill(0xEE);
		const ENetResult Result =
			Loopback.Port(Target).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination.data(), Destination.size()), ReceiveResult);
		MW_EXPECT_EQ(Test, ENetResult::Success, Result, "Each target must deliver its queued packet");
		MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), ReceiveResult.BytesReceived, "Each target must report one received byte");
		MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(Target), Destination[0], "Each target must receive exactly its own packet");
		MW_EXPECT_EQ(Test, true, ReceiveFrom == Port0, "Each target must report the sender as port 0");
		MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(Target), "Each target mailbox must be empty after one receive");
	}
}

/** Proves a target can reply to the original sender, which then receives it with OutFrom == the replier. */
MW_TEST_CASE(HostLoopbackSupportsTwoWayReplyWithCorrectSender)
{
	FHostLoopback<4, 2, 4> Loopback;
	const FNetAddress Port0 = MakeLoopbackAddress(0);
	const FNetAddress Port1 = MakeLoopbackAddress(1);

	// Port 0 sends to port 1; port 1 receives and then replies back to port 0.
	const std::array<std::uint8_t, 2> Request{0xA0, 0xA1};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Loopback.Port(0).TrySend(Port1, TSpan<const std::uint8_t>(Request.data(), Request.size())),
		"Port 0 sending to port 1 must succeed");

	std::array<std::uint8_t, 4> RequestDestination{};
	FNetReceiveResult RequestReceive{};
	FNetAddress RequestFrom{0x42};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Loopback.Port(1).TryReceive(RequestFrom, TSpan<std::uint8_t>(RequestDestination.data(), RequestDestination.size()), RequestReceive),
		"Port 1 must receive the request");
	MW_EXPECT_EQ(Test, true, RequestFrom == Port0, "Port 1 must see the request sender as port 0");

	const std::array<std::uint8_t, 2> Reply{0xB0, 0xB1};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Loopback.Port(1).TrySend(Port0, TSpan<const std::uint8_t>(Reply.data(), Reply.size())),
		"Port 1 replying to port 0 must succeed");

	std::array<std::uint8_t, 4> ReplyDestination{};
	FNetReceiveResult ReplyReceive{};
	FNetAddress ReplyFrom{0x42};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Loopback.Port(0).TryReceive(ReplyFrom, TSpan<std::uint8_t>(ReplyDestination.data(), ReplyDestination.size()), ReplyReceive),
		"Port 0 must receive the reply");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xB0), ReplyDestination[0], "Port 0 must receive the reply bytes");
	MW_EXPECT_EQ(Test, true, ReplyFrom == Port1, "Port 0 must see the reply sender as port 1");
}

/**
 * Proves a destination address that is out of range, zero-size, or two-byte is rejected as Invalid
 * and that nothing is enqueued on any mailbox.
 */
MW_TEST_CASE(HostLoopbackRejectsUnroutableDestinationAddress)
{
	FHostLoopback<4, 2, 4> Loopback;

	const std::array<std::uint8_t, 2> Packet{0x77, 0x88};

	// Out-of-range port index (the loopback exposes only ports 0..3).
	const ENetResult OverRangeResult = Loopback.Port(0).TrySend(MakeLoopbackAddress(99), TSpan<const std::uint8_t>(Packet.data(), Packet.size()));
	MW_EXPECT_EQ(Test, ENetResult::Invalid, OverRangeResult, "A destination port index above MaxPorts-1 must return Invalid");

	// A zero-size address carries no port index at all.
	FNetAddress EmptyAddress{};
	EmptyAddress.Size = 0;
	const ENetResult EmptySizeResult = Loopback.Port(0).TrySend(EmptyAddress, TSpan<const std::uint8_t>(Packet.data(), Packet.size()));
	MW_EXPECT_EQ(Test, ENetResult::Invalid, EmptySizeResult, "A zero-size address must return Invalid");

	// A two-byte address is the wrong encoding for this driver.
	FNetAddress TwoByteAddress{};
	TwoByteAddress.Bytes[0] = 1;
	TwoByteAddress.Bytes[1] = 0;
	TwoByteAddress.Size = 2;
	const ENetResult TwoByteResult = Loopback.Port(0).TrySend(TwoByteAddress, TSpan<const std::uint8_t>(Packet.data(), Packet.size()));
	MW_EXPECT_EQ(Test, ENetResult::Invalid, TwoByteResult, "A two-byte address must return Invalid");

	// No mailbox may have absorbed any packet from the rejected sends.
	for (std::uint8_t Port = 0; Port < 4; ++Port)
	{
		MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(Port), "An unroutable destination must not enqueue a packet on any mailbox");
	}
}

/**
 * Proves MaxPacketBytes reports the template capacity and that a too-small receive still returns Full,
 * retaining the head packet and leaving OutFrom unchanged.
 */
MW_TEST_CASE(HostLoopbackReportsMaxPacketBytesAndRetainsHeadOnTooSmallReceive)
{
	FHostLoopback<4, 1, 4> Loopback;
	const FNetAddress Port0 = MakeLoopbackAddress(0);

	// The per-port driver reports the network's per-packet byte capacity.
	INetDriver& Driver = Loopback.Port(2);
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(4), Driver.MaxPacketBytes(), "MaxPacketBytes must report the template packet byte capacity");
	MW_EXPECT_EQ(
		Test, static_cast<std::size_t>(4), Loopback.MaximumPacketBytes(), "MaximumPacketBytes must report the template packet byte capacity");

	// A too-small receive must retain the head packet and leave OutFrom at its caller sentinel.
	const std::array<std::uint8_t, 3> HeadPacket{0x10, 0x20, 0x30};
	Loopback.Port(0).TrySend(Port0, TSpan<const std::uint8_t>(HeadPacket.data(), HeadPacket.size()));

	std::array<std::uint8_t, 2> TooSmall{0xFF, 0xFF};
	FNetReceiveResult TooSmallReceive{std::size_t{0xEE}};
	FNetAddress TooSmallFrom{0x42};
	const ENetResult TooSmallResult =
		Loopback.Port(0).TryReceive(TooSmallFrom, TSpan<std::uint8_t>(TooSmall.data(), TooSmall.size()), TooSmallReceive);
	MW_EXPECT_EQ(Test, ENetResult::Full, TooSmallResult, "A destination too small for the head must return Full");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0xEE), TooSmallReceive.BytesReceived, "Failed receive must leave BytesReceived unchanged");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x42), TooSmallFrom.Bytes[0], "Failed receive must leave OutFrom unchanged");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Loopback.QueuedCount(0), "Too-small receive must retain the head packet");
}

} // namespace
