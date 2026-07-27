#include "TestSupport.h"

#include <MicroWorld/Net/E32Lora.h>
#include <MicroWorld/Net/FrameCodec.h>
#include <MicroWorld/Net/NetDriver.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/PlatformPico/Detail/E32LoraTransportState.h>

#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::E32MaxPayloadBytes;
using MicroWorld::EFrameEvent;
using MicroWorld::ENetResult;
using MicroWorld::FNetAddress;
using MicroWorld::FNetReceiveResult;
using MicroWorld::FrameOverheadBytes;
using MicroWorld::MakeLoraAddress;
using MicroWorld::TSpan;
using MicroWorld::Detail::FE32LoraTransportState;

/** Source node id used by deterministic transmit and receive frame fixtures. */
constexpr std::uint8_t LocalNodeId = 7;

/** Peer node id expected in decoded sender addresses. */
constexpr std::uint8_t PeerNodeId = 9;

/** Distinct byte proving failed peek and receive operations preserve caller outputs. */
constexpr std::uint8_t SentinelByte = 0xEE;

/** Distinct byte count proving failed receives preserve `FNetReceiveResult`. */
constexpr std::size_t SentinelByteCount = 123;

/** Three-byte payload used to exercise exact framing and held-frame delivery. */
constexpr std::uint8_t Payload[] = {0x10, 0x20, 0x30};

/** Encodes one valid peer frame into caller-owned fixed storage. */
std::size_t EncodePeerFrame(std::uint8_t (&OutFrame)[E32MaxPayloadBytes + FrameOverheadBytes]) noexcept
{
	std::size_t WrittenBytes = 0;
	const ENetResult Result = MicroWorld::EncodeFrame(
		PeerNodeId, TSpan<const std::uint8_t>(Payload, sizeof(Payload)), TSpan<std::uint8_t>(OutFrame, sizeof(OutFrame)), WrittenBytes);
	return Result == ENetResult::Success ? WrittenBytes : 0;
}

/** Proves an invalid destination is rejected without occupying the transmit slot. */
MW_TEST_CASE(PicoE32StateRejectsInvalidAddressWithoutOccupyingTransmitSlot)
{
	FE32LoraTransportState State;
	FNetAddress InvalidAddress{};

	const ENetResult Result = State.TryQueueFrame(LocalNodeId, InvalidAddress, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));

	MW_EXPECT_EQ(Test, ENetResult::Invalid, Result, "A non-E32 address must be rejected");
	MW_EXPECT_TRUE(Test, !State.HasPendingTransmit(), "Rejected destination must leave the transmit slot empty");
}

/** Proves invalid and oversize spans are rejected before the transmit slot changes. */
MW_TEST_CASE(PicoE32StateRejectsInvalidPayloadsWithoutOccupyingTransmitSlot)
{
	FE32LoraTransportState State;
	const FNetAddress Destination = MakeLoraAddress(PeerNodeId);
	const std::uint8_t OversizeStorage = 0;

	const ENetResult NullResult = State.TryQueueFrame(LocalNodeId, Destination, TSpan<const std::uint8_t>(nullptr, 1));
	const ENetResult OversizeResult =
		State.TryQueueFrame(LocalNodeId, Destination, TSpan<const std::uint8_t>(&OversizeStorage, E32MaxPayloadBytes + 1));

	MW_EXPECT_EQ(Test, ENetResult::Invalid, NullResult, "A null payload with nonzero length must be rejected");
	MW_EXPECT_EQ(Test, ENetResult::Invalid, OversizeResult, "An oversize E32 payload must be rejected");
	MW_EXPECT_TRUE(Test, !State.HasPendingTransmit(), "Rejected payloads must leave the transmit slot empty");
}

/** Proves failed peeks preserve their output and do not invent pending work. */
MW_TEST_CASE(PicoE32StateEmptyTransmitPeekPreservesOutput)
{
	FE32LoraTransportState State;
	std::uint8_t OutByte = SentinelByte;

	const bool bHasByte = State.TryPeekTransmitByte(OutByte);

	MW_EXPECT_TRUE(Test, !bHasByte, "An empty transmit slot must not produce a byte");
	MW_EXPECT_EQ(Test, SentinelByte, OutByte, "An empty transmit peek must preserve its output");
}

/** Proves one accepted frame applies backpressure until every encoded byte is committed. */
MW_TEST_CASE(PicoE32StateAppliesBackpressureUntilFinalTransmitByte)
{
	FE32LoraTransportState State;
	const FNetAddress Destination = MakeLoraAddress(PeerNodeId);
	std::uint8_t ExpectedFrame[E32MaxPayloadBytes + FrameOverheadBytes]{};
	std::size_t ExpectedFrameBytes = 0;
	const ENetResult EncodeResult = MicroWorld::EncodeFrame(
		LocalNodeId,
		TSpan<const std::uint8_t>(Payload, sizeof(Payload)),
		TSpan<std::uint8_t>(ExpectedFrame, sizeof(ExpectedFrame)),
		ExpectedFrameBytes);
	MW_EXPECT_EQ(Test, ENetResult::Success, EncodeResult, "The test fixture frame must encode");

	const ENetResult FirstQueueResult = State.TryQueueFrame(LocalNodeId, Destination, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
	const ENetResult FullResult = State.TryQueueFrame(LocalNodeId, Destination, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));

	MW_EXPECT_EQ(Test, ENetResult::Success, FirstQueueResult, "An empty slot must accept one complete frame");
	MW_EXPECT_EQ(Test, ENetResult::Full, FullResult, "An occupied slot must reject a second frame with Full");
	for (std::size_t Index = 0; Index < ExpectedFrameBytes; ++Index)
	{
		std::uint8_t NextByte = 0;
		MW_EXPECT_TRUE(Test, State.TryPeekTransmitByte(NextByte), "Every queued frame byte must be readable before commit");
		MW_EXPECT_EQ(Test, ExpectedFrame[Index], NextByte, "Transmit progress must preserve exact frame byte order");
		State.CommitTransmitByte();
		MW_EXPECT_EQ(Test, Index + 1 < ExpectedFrameBytes, State.HasPendingTransmit(), "Only the final committed byte may release the transmit slot");
	}

	const ENetResult ReuseResult = State.TryQueueFrame(LocalNodeId, Destination, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
	MW_EXPECT_EQ(Test, ENetResult::Success, ReuseResult, "The slot must accept a new frame after final-byte release");
}

/** Proves an unavailable receive preserves every caller-owned output. */
MW_TEST_CASE(PicoE32StateUnavailableReceivePreservesOutputs)
{
	FE32LoraTransportState State;
	FNetAddress From = MakeLoraAddress(SentinelByte);
	FNetReceiveResult ReceiveResult{SentinelByteCount};
	std::uint8_t Destination = SentinelByte;

	const ENetResult Result = State.TryDeliverReceivedFrame(From, TSpan<std::uint8_t>(&Destination, 1), ReceiveResult);

	MW_EXPECT_EQ(Test, ENetResult::Unavailable, Result, "A state with no held frame must report Unavailable");
	MW_EXPECT_EQ(Test, SentinelByte, Destination, "Unavailable receive must preserve destination bytes");
	MW_EXPECT_EQ(Test, SentinelByte, From.Bytes[0], "Unavailable receive must preserve sender output");
	MW_EXPECT_EQ(Test, SentinelByteCount, ReceiveResult.BytesReceived, "Unavailable receive must preserve byte count");
}

/** Proves a short receive retains the frame and a larger retry delivers it transactionally. */
MW_TEST_CASE(PicoE32StateRetainsReceivedFrameForLargerRetry)
{
	FE32LoraTransportState State;
	std::uint8_t Frame[E32MaxPayloadBytes + FrameOverheadBytes]{};
	const std::size_t FrameBytes = EncodePeerFrame(Frame);
	MW_EXPECT_TRUE(Test, FrameBytes != 0, "The test fixture frame must encode");
	EFrameEvent FinalEvent = EFrameEvent::None;
	for (std::size_t Index = 0; Index < FrameBytes; ++Index)
	{
		FinalEvent = State.PushReceivedByte(Frame[Index]);
	}
	MW_EXPECT_EQ(Test, EFrameEvent::FrameReady, FinalEvent, "The final valid frame byte must report FrameReady");

	std::uint8_t ShortDestination[2] = {SentinelByte, SentinelByte};
	FNetAddress From = MakeLoraAddress(SentinelByte);
	FNetReceiveResult ReceiveResult{SentinelByteCount};
	const ENetResult FullResult = State.TryDeliverReceivedFrame(From, TSpan<std::uint8_t>(ShortDestination, sizeof(ShortDestination)), ReceiveResult);

	MW_EXPECT_EQ(Test, ENetResult::Full, FullResult, "A short destination must report Full");
	MW_EXPECT_TRUE(Test, State.HasReceivedFrame(), "Full must retain the frame for retry");
	MW_EXPECT_EQ(Test, SentinelByte, ShortDestination[0], "Full must preserve destination bytes");
	MW_EXPECT_EQ(Test, SentinelByte, From.Bytes[0], "Full must preserve sender output");
	MW_EXPECT_EQ(Test, SentinelByteCount, ReceiveResult.BytesReceived, "Full must preserve byte count");

	std::uint8_t Destination[sizeof(Payload)]{};
	const ENetResult SuccessResult = State.TryDeliverReceivedFrame(From, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);

	MW_EXPECT_EQ(Test, ENetResult::Success, SuccessResult, "A fitting retry must deliver the held frame");
	MW_EXPECT_TRUE(Test, !State.HasReceivedFrame(), "Successful delivery must release the held frame");
	MW_EXPECT_EQ(Test, PeerNodeId, From.Bytes[0], "Successful delivery must report the frame sender");
	MW_EXPECT_EQ(Test, sizeof(Payload), ReceiveResult.BytesReceived, "Successful delivery must report the payload size");
	for (std::size_t Index = 0; Index < sizeof(Payload); ++Index)
	{
		MW_EXPECT_EQ(Test, Payload[Index], Destination[Index], "Successful delivery must preserve every payload byte");
	}
}

/** Proves a discarded corrupt frame does not prevent the next valid frame from completing. */
MW_TEST_CASE(PicoE32StateResynchronizesAfterCorruptFrame)
{
	FE32LoraTransportState State;
	std::uint8_t Frame[E32MaxPayloadBytes + FrameOverheadBytes]{};
	const std::size_t FrameBytes = EncodePeerFrame(Frame);
	MW_EXPECT_TRUE(Test, FrameBytes != 0, "The test fixture frame must encode");
	Frame[FrameBytes - 1] ^= 0x01u;

	EFrameEvent CorruptEvent = EFrameEvent::None;
	for (std::size_t Index = 0; Index < FrameBytes; ++Index)
	{
		CorruptEvent = State.PushReceivedByte(Frame[Index]);
	}
	MW_EXPECT_EQ(Test, EFrameEvent::Discarded, CorruptEvent, "A bad CRC must discard the candidate frame");
	MW_EXPECT_TRUE(Test, !State.HasReceivedFrame(), "A discarded frame must not become deliverable");

	const std::size_t ValidFrameBytes = EncodePeerFrame(Frame);
	EFrameEvent ValidEvent = EFrameEvent::None;
	for (std::size_t Index = 0; Index < ValidFrameBytes; ++Index)
	{
		ValidEvent = State.PushReceivedByte(Frame[Index]);
	}
	MW_EXPECT_EQ(Test, EFrameEvent::FrameReady, ValidEvent, "The next valid frame must complete after resynchronization");
}

} // namespace
