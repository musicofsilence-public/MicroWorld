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

/** Different payload that exposes an accidental overwrite of a queued frame. */
constexpr std::uint8_t ReplacementPayload[] = {0x91, 0x82};

/** Largest permitted payload used to verify the E32 acceptance boundary. */
constexpr std::uint8_t MaximumPayload[E32MaxPayloadBytes]{};

/** Encodes one valid peer frame into caller-owned fixed storage. */
std::size_t EncodePeerFrame(std::uint8_t (&OutFrame)[E32MaxPayloadBytes + FrameOverheadBytes]) noexcept
{
	std::size_t WrittenBytes = 0;
	const ENetResult Result = MicroWorld::EncodeFrame(
		PeerNodeId, TSpan<const std::uint8_t>(Payload, sizeof(Payload)), TSpan<std::uint8_t>(OutFrame, sizeof(OutFrame)), WrittenBytes);
	return Result == ENetResult::Success ? WrittenBytes : 0;
}

/**
 * Scenario: Queue a frame against an invalid destination address.
 * Expected: The invalid destination is rejected as Invalid; the transmit slot remains empty.
 */
MW_TEST_CASE(PicoE32StateRejectsInvalidAddressWithoutOccupyingTransmitSlot)
{
	// Arrange
	FE32LoraTransportState State;
	FNetAddress InvalidAddress{};

	// Act
	const ENetResult Result = State.TryQueueFrame(LocalNodeId, InvalidAddress, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));

	// Assert
	MW_EXPECT_EQ(Test, ENetResult::Invalid, Result, "A non-E32 address must be rejected");
	MW_EXPECT_TRUE(Test, !State.HasPendingTransmit(), "Rejected destination must leave the transmit slot empty");
}

/**
 * Scenario: Queue a frame with a null nonzero-length payload and with an oversize payload.
 * Expected: Both payloads are rejected as Invalid; the transmit slot remains empty.
 */
MW_TEST_CASE(PicoE32StateRejectsInvalidPayloadsWithoutOccupyingTransmitSlot)
{
	// Arrange
	FE32LoraTransportState State;
	const FNetAddress Destination = MakeLoraAddress(PeerNodeId);
	const std::uint8_t OversizeStorage = 0;

	// Act
	const ENetResult NullResult = State.TryQueueFrame(LocalNodeId, Destination, TSpan<const std::uint8_t>(nullptr, 1));
	const ENetResult OversizeResult =
		State.TryQueueFrame(LocalNodeId, Destination, TSpan<const std::uint8_t>(&OversizeStorage, E32MaxPayloadBytes + 1));

	// Assert
	MW_EXPECT_EQ(Test, ENetResult::Invalid, NullResult, "A null payload with nonzero length must be rejected");
	MW_EXPECT_EQ(Test, ENetResult::Invalid, OversizeResult, "An oversize E32 payload must be rejected");
	MW_EXPECT_TRUE(Test, !State.HasPendingTransmit(), "Rejected payloads must leave the transmit slot empty");
}

/**
 * Scenario: Peek the transmit byte on an empty transmit slot.
 * Expected: The peek produces no byte and preserves its output; no pending transmit work is invented.
 */
MW_TEST_CASE(PicoE32StateEmptyTransmitPeekPreservesOutput)
{
	// Arrange
	FE32LoraTransportState State;
	std::uint8_t OutByte = SentinelByte;

	// Act
	const bool bHasByte = State.TryPeekTransmitByte(OutByte);

	// Assert
	MW_EXPECT_TRUE(Test, !bHasByte, "An empty transmit slot must not produce a byte");
	MW_EXPECT_EQ(Test, SentinelByte, OutByte, "An empty transmit peek must preserve its output");
}

/**
 * Scenario: Queue one valid frame, then attempt to queue a second, then peek and commit every encoded byte of the first.
 * Expected: The second frame is rejected as Full while the first is pending; peeks read bytes in order without advancing until commit; only the final
 * committed byte releases the slot for a new frame.
 */
MW_TEST_CASE(PicoE32StateAppliesBackpressureUntilFinalTransmitByte)
{
	// Arrange
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

	// Act
	const ENetResult FirstQueueResult = State.TryQueueFrame(LocalNodeId, Destination, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
	const ENetResult FullResult =
		State.TryQueueFrame(LocalNodeId, Destination, TSpan<const std::uint8_t>(ReplacementPayload, sizeof(ReplacementPayload)));

	// Assert the slot applies immediate backpressure and only releases on the final byte.
	MW_EXPECT_EQ(Test, ENetResult::Success, FirstQueueResult, "An empty slot must accept one complete frame");
	MW_EXPECT_EQ(Test, ENetResult::Full, FullResult, "An occupied slot must reject a second frame with Full");
	for (std::size_t Index = 0; Index < ExpectedFrameBytes; ++Index)
	{
		std::uint8_t FirstPeekedByte = 0;
		std::uint8_t SecondPeekedByte = 0;
		MW_EXPECT_TRUE(Test, State.TryPeekTransmitByte(FirstPeekedByte), "Every queued frame byte must be readable before commit");
		MW_EXPECT_TRUE(Test, State.TryPeekTransmitByte(SecondPeekedByte), "Peeking without commit must retain the current byte");
		MW_EXPECT_EQ(Test, ExpectedFrame[Index], FirstPeekedByte, "Transmit progress must preserve exact frame byte order");
		MW_EXPECT_EQ(Test, FirstPeekedByte, SecondPeekedByte, "Repeated peeks must not advance queued frame progress");
		State.CommitTransmitByte();
		MW_EXPECT_EQ(Test, Index + 1 < ExpectedFrameBytes, State.HasPendingTransmit(), "Only the final committed byte may release the transmit slot");
	}

	// Assert the slot accepts a new frame once the final byte is committed.
	const ENetResult ReuseResult = State.TryQueueFrame(LocalNodeId, Destination, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
	MW_EXPECT_EQ(Test, ENetResult::Success, ReuseResult, "The slot must accept a new frame after final-byte release");
}

/**
 * Scenario: Queue an empty payload, commit its overhead bytes, then queue the largest permitted payload.
 * Expected: The empty payload is accepted and its overhead bytes occupy then release the slot; the maximum payload is accepted and occupies the slot.
 */
MW_TEST_CASE(PicoE32StateAcceptsEmptyAndMaximumPayloads)
{
	// Arrange
	FE32LoraTransportState State;
	const FNetAddress Destination = MakeLoraAddress(PeerNodeId);

	// Act and Assert: an empty payload queues only the framing overhead.
	const ENetResult EmptyResult = State.TryQueueFrame(LocalNodeId, Destination, TSpan<const std::uint8_t>(nullptr, 0));
	MW_EXPECT_EQ(Test, ENetResult::Success, EmptyResult, "An empty payload must be accepted by the E32 framing contract");
	for (std::size_t FrameByteIndex = 0; FrameByteIndex < FrameOverheadBytes; ++FrameByteIndex)
	{
		MW_EXPECT_TRUE(Test, State.HasPendingTransmit(), "Each empty-frame overhead byte must remain pending before commit");
		State.CommitTransmitByte();
	}
	MW_EXPECT_TRUE(Test, !State.HasPendingTransmit(), "Committing the empty-frame overhead must release the transmit slot");

	// Act and Assert: the largest permitted payload is accepted and occupies the slot.
	const ENetResult MaximumResult = State.TryQueueFrame(LocalNodeId, Destination, TSpan<const std::uint8_t>(MaximumPayload, sizeof(MaximumPayload)));
	MW_EXPECT_EQ(Test, ENetResult::Success, MaximumResult, "The documented maximum E32 payload must be accepted");
	MW_EXPECT_TRUE(Test, State.HasPendingTransmit(), "An accepted maximum payload must occupy the transmit slot");
}

/**
 * Scenario: Attempt to deliver a received frame when no frame is held.
 * Expected: The receive reports Unavailable and preserves every caller-owned output: destination bytes, sender address, and byte count.
 */
MW_TEST_CASE(PicoE32StateUnavailableReceivePreservesOutputs)
{
	// Arrange
	FE32LoraTransportState State;
	FNetAddress From = MakeLoraAddress(SentinelByte);
	FNetReceiveResult ReceiveResult{SentinelByteCount};
	std::uint8_t Destination = SentinelByte;

	// Act
	const ENetResult Result = State.TryDeliverReceivedFrame(From, TSpan<std::uint8_t>(&Destination, 1), ReceiveResult);

	// Assert
	MW_EXPECT_EQ(Test, ENetResult::Unavailable, Result, "A state with no held frame must report Unavailable");
	MW_EXPECT_EQ(Test, SentinelByte, Destination, "Unavailable receive must preserve destination bytes");
	MW_EXPECT_EQ(Test, SentinelByte, From.Bytes[0], "Unavailable receive must preserve sender output");
	MW_EXPECT_EQ(Test, SentinelByteCount, ReceiveResult.BytesReceived, "Unavailable receive must preserve byte count");
}

/**
 * Scenario: Push a valid frame, attempt delivery into a too-small destination, then retry into a larger destination.
 * Expected: The short destination reports Full with every caller output preserved and the frame retained; the larger retry delivers the held frame
 * and releases it with the correct sender, size, and bytes.
 */
MW_TEST_CASE(PicoE32StateRetainsReceivedFrameForLargerRetry)
{
	// Arrange
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

	// Act: a too-small destination must report Full.
	std::uint8_t ShortDestination[2] = {SentinelByte, SentinelByte};
	FNetAddress From = MakeLoraAddress(SentinelByte);
	FNetReceiveResult ReceiveResult{SentinelByteCount};
	const ENetResult FullResult = State.TryDeliverReceivedFrame(From, TSpan<std::uint8_t>(ShortDestination, sizeof(ShortDestination)), ReceiveResult);

	// Assert the Full path preserves every caller output and retains the frame.
	MW_EXPECT_EQ(Test, ENetResult::Full, FullResult, "A short destination must report Full");
	MW_EXPECT_TRUE(Test, State.HasReceivedFrame(), "Full must retain the frame for retry");
	MW_EXPECT_EQ(Test, SentinelByte, ShortDestination[0], "Full must preserve destination bytes");
	MW_EXPECT_EQ(Test, SentinelByte, From.Bytes[0], "Full must preserve sender output");
	MW_EXPECT_EQ(Test, SentinelByteCount, ReceiveResult.BytesReceived, "Full must preserve byte count");

	// Act: a larger retry must deliver the held frame transactionally.
	std::uint8_t Destination[sizeof(Payload)]{};
	const ENetResult SuccessResult = State.TryDeliverReceivedFrame(From, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);

	// Assert the retry delivers the frame and releases it.
	MW_EXPECT_EQ(Test, ENetResult::Success, SuccessResult, "A fitting retry must deliver the held frame");
	MW_EXPECT_TRUE(Test, !State.HasReceivedFrame(), "Successful delivery must release the held frame");
	MW_EXPECT_EQ(Test, PeerNodeId, From.Bytes[0], "Successful delivery must report the frame sender");
	MW_EXPECT_EQ(Test, sizeof(Payload), ReceiveResult.BytesReceived, "Successful delivery must report the payload size");
	for (std::size_t Index = 0; Index < sizeof(Payload); ++Index)
	{
		MW_EXPECT_EQ(Test, Payload[Index], Destination[Index], "Successful delivery must preserve every payload byte");
	}
}

/**
 * Scenario: Push a valid frame, then attempt delivery into a null non-empty destination.
 * Expected: The null destination is rejected as Invalid; the already decoded frame is retained for a valid retry; the sender output and byte count
 * are preserved.
 */
MW_TEST_CASE(PicoE32StateRetainsReceivedFrameAfterInvalidNullDestination)
{
	// Arrange
	FE32LoraTransportState State;
	std::uint8_t Frame[E32MaxPayloadBytes + FrameOverheadBytes]{};
	const std::size_t FrameBytes = EncodePeerFrame(Frame);
	for (std::size_t Index = 0; Index < FrameBytes; ++Index)
	{
		State.PushReceivedByte(Frame[Index]);
	}

	FNetAddress From = MakeLoraAddress(SentinelByte);
	FNetReceiveResult ReceiveResult{SentinelByteCount};

	// Act
	const ENetResult InvalidResult = State.TryDeliverReceivedFrame(From, TSpan<std::uint8_t>(nullptr, 1), ReceiveResult);

	// Assert
	MW_EXPECT_EQ(Test, ENetResult::Invalid, InvalidResult, "A null non-empty destination must be rejected");
	MW_EXPECT_TRUE(Test, State.HasReceivedFrame(), "Invalid destination input must retain the decoded frame");
	MW_EXPECT_EQ(Test, SentinelByte, From.Bytes[0], "Invalid input must preserve the sender output");
	MW_EXPECT_EQ(Test, SentinelByteCount, ReceiveResult.BytesReceived, "Invalid input must preserve the byte count");
}

/**
 * Scenario: Push a corrupt frame with a bad checksum, then push the next valid frame.
 * Expected: The corrupt frame is discarded without becoming deliverable; the next valid frame completes after resynchronization.
 */
MW_TEST_CASE(PicoE32StateResynchronizesAfterCorruptFrame)
{
	// Arrange
	FE32LoraTransportState State;
	std::uint8_t Frame[E32MaxPayloadBytes + FrameOverheadBytes]{};
	const std::size_t FrameBytes = EncodePeerFrame(Frame);
	MW_EXPECT_TRUE(Test, FrameBytes != 0, "The test fixture frame must encode");
	Frame[FrameBytes - 1] ^= 0x01u;

	// Act and Assert: the corrupt frame is discarded and does not become deliverable.
	EFrameEvent CorruptEvent = EFrameEvent::None;
	for (std::size_t Index = 0; Index < FrameBytes; ++Index)
	{
		CorruptEvent = State.PushReceivedByte(Frame[Index]);
	}
	MW_EXPECT_EQ(Test, EFrameEvent::Discarded, CorruptEvent, "A bad CRC must discard the candidate frame");
	MW_EXPECT_TRUE(Test, !State.HasReceivedFrame(), "A discarded frame must not become deliverable");

	// Act and Assert: the next valid frame completes after resynchronization.
	const std::size_t ValidFrameBytes = EncodePeerFrame(Frame);
	EFrameEvent ValidEvent = EFrameEvent::None;
	for (std::size_t Index = 0; Index < ValidFrameBytes; ++Index)
	{
		ValidEvent = State.PushReceivedByte(Frame[Index]);
	}
	MW_EXPECT_EQ(Test, EFrameEvent::FrameReady, ValidEvent, "The next valid frame must complete after resynchronization");
}

} // namespace
