#include "TestSupport.h"

#include "TransportAllocationCounters.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Transport/FrameCodec.h>
#include <MicroWorld/Transport/TransportResult.h>

#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::Core::TSpan;
using MicroWorld::Transport::ETransportResult;
using MicroWorld::Transport::FrameCodec::ComputeCrc16Ccitt;
using MicroWorld::Transport::FrameCodec::EFrameEvent;
using MicroWorld::Transport::FrameCodec::EncodeFrame;
using MicroWorld::Transport::FrameCodec::FrameMagicByte;
using MicroWorld::Transport::FrameCodec::FrameOverheadBytes;
using MicroWorld::Transport::FrameCodec::TFrameDecoder;

/** Decoder payload capacity shared by every case so a declared length of nine exercises the oversize path. */
constexpr std::size_t DecoderMaxPayload = 8;

/** One framed payload buffer's capacity: the maximum payload plus the codec's per-frame overhead. */
constexpr std::size_t FrameBufferCapacity = DecoderMaxPayload + FrameOverheadBytes;

/** Capacity for a stream holding two back-to-back framed payloads. */
constexpr std::size_t DoubleFrameBufferCapacity = FrameBufferCapacity * 2;

/** Number of leading non-magic bytes the resync case drops before a valid frame. */
constexpr std::size_t GarbageByteCount = 3;

/** Number of CRC-16/CCITT-FALSE check-vector input bytes (ASCII "123456789"). */
constexpr std::size_t CrcCheckInputCount = 9;

/** Number of hand-assembled bytes in the oversize-declared-length candidate. */
constexpr std::size_t BadLengthCandidateCount = 4;

/** Number of hand-assembled bytes in the truncated candidate. */
constexpr std::size_t TruncatedCandidateCount = 6;

/** Payload byte index whose value equals the frame magic, proving it is not misread as a boundary. */
constexpr std::size_t MagicPayloadIndex = 1;

/** Payload sizes the round-trip case exercises: empty, single-byte, and the decoder maximum. */
constexpr std::size_t EmptyPayloadSize = 0;
constexpr std::size_t SingleBytePayloadSize = 1;
constexpr std::size_t RoundTripSizeCount = 3;

/** Span length larger than any 16-bit length field can encode, so the size guard rejects it before any byte is read. */
constexpr std::size_t OversizePayloadLength = 0x10000;

/** Nonzero destination length paired with a null pointer to exercise the null-destination rejection. */
constexpr std::size_t NullDestinationLength = 4;

/** Base added to each round-trip payload index so every non-magic byte is distinct and observable. */
constexpr std::uint8_t PayloadBaseByte = 0x10;

/** Mask XORed into the final CRC byte to force a validation failure on the candidate's last byte. */
constexpr std::uint8_t CrcCorruptionMask = 0xFF;

/** Sentinel pre-loaded into OutWritten so a rejection that leaves it unchanged is observable. */
constexpr std::size_t UntouchedWrittenSentinel = 0xDEAD;

/** The check value CRC-16/CCITT-FALSE produces for ASCII "123456789". */
constexpr std::uint16_t Crc16CcittFalseCheckValue = 0x29B1;

/** Distinct source node ids the encode/decode cases thread through the codec. */
constexpr std::uint8_t NodeId01 = 0x01;
constexpr std::uint8_t NodeId02 = 0x02;
constexpr std::uint8_t NodeId03 = 0x03;
constexpr std::uint8_t NodeId04 = 0x04;
constexpr std::uint8_t NodeId06 = 0x06;
constexpr std::uint8_t NodeId07 = 0x07;
constexpr std::uint8_t NodeId09 = 0x09;

/** ASCII "123456789" - the canonical CRC-16/CCITT-FALSE check-vector input. */
constexpr std::uint8_t CrcCheckInput[CrcCheckInputCount] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};

/** Two-byte payload the encode-rejects case threads through a too-small destination. */
constexpr std::uint8_t EncodeRejectsPayload[2] = {0x01, 0x02};

/** Single-byte payload the leading-garbage case delivers after the dropped prefix. */
constexpr std::uint8_t LeadingGarbagePayload[1] = {0x55};

/** Two-byte first payload the back-to-back case encodes ahead of a second frame. */
constexpr std::uint8_t FirstBackToBackPayload[2] = {0x10, 0x20};

/** Three-byte second payload the back-to-back case encodes after the first frame. */
constexpr std::uint8_t SecondBackToBackPayload[3] = {0x30, 0x40, 0x50};

/** Two-byte payload whose CRC the corrupted-CRC case invalidates. */
constexpr std::uint8_t CorruptedCrcPayload[2] = {0x11, 0x22};

/** Single-byte good payload the corrupted-CRC case decodes after the discard. */
constexpr std::uint8_t CorruptedCrcGoodPayload[1] = {0x77};

/** Single-byte good payload the bad-length case decodes after the discard. */
constexpr std::uint8_t BadLengthGoodPayload[1] = {0x66};

/** Two-byte payload A the truncated-resync case encodes after the truncated candidate. */
constexpr std::uint8_t TruncatedPayloadA[2] = {0x11, 0x22};

/** Single-byte payload B the truncated-resync case decodes as the surviving frame. */
constexpr std::uint8_t TruncatedPayloadB[1] = {0x33};

/** Eight distinct payload bytes the steady-state allocation case round-trips. */
constexpr std::uint8_t SteadyStatePayload[DecoderMaxPayload] = {0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87};

/** Non-magic leading bytes the decoder must drop while waiting for a frame start. */
constexpr std::uint8_t LeadingGarbageBytes[GarbageByteCount] = {0x00, 0xFF, 0x42};

/** Hand-assembled candidate whose declared length (nine) exceeds the decoder capacity (eight). */
constexpr std::uint8_t BadLengthCandidate[BadLengthCandidateCount] = {FrameMagicByte, 0x05, 0x00, 0x09};

/** Hand-assembled truncated candidate: magic, node, declared length five, but only two payload bytes. */
constexpr std::uint8_t TruncatedCandidate[TruncatedCandidateCount] = {FrameMagicByte, 0x08, 0x00, 0x05, 0xAA, 0xBB};

/** Convenient alias so each case names one concrete decoder type without repeating the capacity. */
using FDecoder = TFrameDecoder<DecoderMaxPayload>;

/** Feeds a byte sequence through a decoder and returns the event produced by the final byte. */
EFrameEvent FeedBytes(FDecoder& InDecoder, const std::uint8_t* const InBytes, const std::size_t InCount) noexcept
{
	EFrameEvent Last = EFrameEvent::None;
	for (std::size_t Index = 0; Index < InCount; ++Index)
	{
		Last = InDecoder.PushByte(InBytes[Index]);
	}
	return Last;
}

/** Reports whether the decoder's held payload equals the expected bytes and length. */
bool PayloadMatches(const FDecoder& InDecoder, const std::uint8_t* const InExpected, const std::size_t InCount) noexcept
{
	if (InDecoder.FramePayload().Size() != InCount)
	{
		return false;
	}
	const std::uint8_t* const Data = InDecoder.FramePayload().Data();
	for (std::size_t Index = 0; Index < InCount; ++Index)
	{
		if (Data[Index] != InExpected[Index])
		{
			return false;
		}
	}
	return true;
}

/**
 * Scenario: Compute the CRC-16/CCITT-FALSE primitive over the canonical ASCII "123456789" check vector.
 * Expected: The result equals the canonical check value 0x29B1.
 */
MW_TEST_CASE(FrameCodec_Crc16CcittFalseCheckValueIs29B1)
{
	// Act
	const std::uint16_t Check = ComputeCrc16Ccitt(TSpan<const std::uint8_t>(CrcCheckInput, sizeof(CrcCheckInput)));

	// Assert
	MW_EXPECT_EQ(Test, Crc16CcittFalseCheckValue, Check, "CRC-16/CCITT-FALSE of ASCII 123456789 must be 0x29B1");
}

/**
 * Scenario: Encode payloads of size zero, one, and the decoder maximum, then feed each frame byte-by-byte into a decoder and clear the held frame.
 * Expected: Each frame completes with the encoded source node id and a byte-for-byte matching payload, including a 0xA5 magic-valued payload byte;
 * ClearFrame releases the held frame.
 */
MW_TEST_CASE(FrameCodec_RoundTripsPayloadSizesZeroOneAndMax)
{
	// Arrange
	const std::size_t Sizes[RoundTripSizeCount] = {EmptyPayloadSize, SingleBytePayloadSize, DecoderMaxPayload};

	for (std::size_t SizeIndex = 0; SizeIndex < RoundTripSizeCount; ++SizeIndex)
	{
		const std::size_t PayloadSize = Sizes[SizeIndex];

		std::uint8_t Payload[DecoderMaxPayload] = {};
		for (std::size_t Index = 0; Index < PayloadSize; ++Index)
		{
			// A payload byte equal to the magic at index 1 proves it is not misread as a frame boundary.
			Payload[Index] = (Index == MagicPayloadIndex) ? FrameMagicByte : static_cast<std::uint8_t>(PayloadBaseByte + Index);
		}

		std::uint8_t Frame[FrameBufferCapacity] = {};
		std::size_t Written = 0;
		// Act
		const ETransportResult EncodeResult =
			EncodeFrame(NodeId07, TSpan<const std::uint8_t>(Payload, PayloadSize), TSpan<std::uint8_t>(Frame, sizeof(Frame)), Written);
		// Assert
		MW_EXPECT_EQ(Test, ETransportResult::Success, EncodeResult, "Encode must succeed for every in-capacity payload size");
		MW_EXPECT_EQ(Test, PayloadSize + FrameOverheadBytes, Written, "Written must equal payload plus overhead");

		FDecoder Decoder;
		const EFrameEvent Last = FeedBytes(Decoder, Frame, Written);
		MW_EXPECT_EQ(Test, EFrameEvent::FrameReady, Last, "The final frame byte must complete a frame");
		MW_EXPECT_EQ(Test, true, Decoder.HasFrame(), "A completed frame must be held");
		MW_EXPECT_EQ(Test, NodeId07, Decoder.FrameNodeId(), "The held frame must carry the encoded source node id");
		MW_EXPECT_EQ(Test, true, PayloadMatches(Decoder, Payload, PayloadSize), "The held payload must match the encoded bytes byte-for-byte");

		// Act
		Decoder.ClearFrame();
		// Assert
		MW_EXPECT_EQ(Test, false, Decoder.HasFrame(), "ClearFrame must release the held frame");
	}
}

/**
 * Scenario: Attempt EncodeFrame with a null nonzero payload, a too-small destination, a null nonzero destination, and a payload larger than the
 * 16-bit length field. Expected: Each rejection returns Invalid or Full as appropriate and leaves OutWritten unchanged.
 */
MW_TEST_CASE(FrameCodec_EncodeRejectsInvalidAndFullCases)
{
	// Arrange
	std::uint8_t Frame[FrameBufferCapacity] = {};

	// Null payload with nonzero length must return Invalid without touching outputs.
	std::size_t Written = UntouchedWrittenSentinel;
	// Act
	ETransportResult Result =
		EncodeFrame(NodeId01, TSpan<const std::uint8_t>(nullptr, sizeof(EncodeRejectsPayload)), TSpan<std::uint8_t>(Frame, sizeof(Frame)), Written);
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, Result, "A null payload with nonzero length must return Invalid");
	MW_EXPECT_EQ(Test, UntouchedWrittenSentinel, Written, "Invalid must leave OutWritten unchanged");

	// A destination too small for the framed payload must return Full without touching outputs.
	std::uint8_t TooSmall[2] = {};
	Written = UntouchedWrittenSentinel;
	// Act
	Result = EncodeFrame(
		NodeId01,
		TSpan<const std::uint8_t>(EncodeRejectsPayload, sizeof(EncodeRejectsPayload)),
		TSpan<std::uint8_t>(TooSmall, sizeof(TooSmall)),
		Written);
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Full, Result, "A destination smaller than payload plus overhead must return Full");
	MW_EXPECT_EQ(Test, UntouchedWrittenSentinel, Written, "Full must leave OutWritten unchanged");

	// A null destination with nonzero length must return Invalid without touching outputs.
	Written = UntouchedWrittenSentinel;
	// Act
	Result = EncodeFrame(
		NodeId01,
		TSpan<const std::uint8_t>(EncodeRejectsPayload, sizeof(EncodeRejectsPayload)),
		TSpan<std::uint8_t>(nullptr, NullDestinationLength),
		Written);
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, Result, "A null destination with nonzero length must return Invalid");
	MW_EXPECT_EQ(Test, UntouchedWrittenSentinel, Written, "Invalid must leave OutWritten unchanged");

	// A payload larger than the 16-bit length field can never fit any frame, so it is Invalid, not Full (D7).
	// The size guard returns before any payload byte is read, so a size-only span needs no real backing.
	std::uint8_t OversizePayloadByte{};
	Written = UntouchedWrittenSentinel;
	// Act
	Result = EncodeFrame(
		NodeId01, TSpan<const std::uint8_t>(&OversizePayloadByte, OversizePayloadLength), TSpan<std::uint8_t>(Frame, sizeof(Frame)), Written);
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, Result, "A payload larger than the 16-bit length field must return Invalid");
	MW_EXPECT_EQ(Test, UntouchedWrittenSentinel, Written, "Invalid must leave OutWritten unchanged");
}

/**
 * Scenario: Prepend three non-magic garbage bytes to a valid frame and feed the stream into a decoder.
 * Expected: The garbage is dropped while the decoder waits for a frame start, and the following frame still completes with the correct source node id
 * and payload.
 */
MW_TEST_CASE(FrameCodec_LeadingGarbageThenValidFrameDecodes)
{
	// Arrange
	std::uint8_t Frame[FrameBufferCapacity] = {};
	std::size_t Written = 0;
	EncodeFrame(
		NodeId09,
		TSpan<const std::uint8_t>(LeadingGarbagePayload, sizeof(LeadingGarbagePayload)),
		TSpan<std::uint8_t>(Frame, sizeof(Frame)),
		Written);

	// Prepend bytes that are not the magic so the decoder must drop them while waiting for a frame start.
	std::uint8_t Stream[GarbageByteCount + FrameBufferCapacity] = {};
	for (std::size_t Index = 0; Index < GarbageByteCount; ++Index)
	{
		Stream[Index] = LeadingGarbageBytes[Index];
	}
	for (std::size_t Index = 0; Index < Written; ++Index)
	{
		Stream[GarbageByteCount + Index] = Frame[Index];
	}

	FDecoder Decoder;
	// Act
	const EFrameEvent Last = FeedBytes(Decoder, Stream, GarbageByteCount + Written);
	// Assert
	MW_EXPECT_EQ(Test, EFrameEvent::FrameReady, Last, "The valid frame following garbage must complete");
	MW_EXPECT_EQ(Test, NodeId09, Decoder.FrameNodeId(), "The decoded frame must carry the encoded source node id");
	MW_EXPECT_EQ(
		Test,
		true,
		PayloadMatches(Decoder, LeadingGarbagePayload, sizeof(LeadingGarbagePayload)),
		"The decoded payload must match the encoded bytes");
}

/**
 * Scenario: Feed two valid frames back-to-back into a decoder, clearing the held frame between them.
 * Expected: Both frames complete in order, each carrying its encoded source node id and payload.
 */
MW_TEST_CASE(FrameCodec_TwoBackToBackFramesDecodeInOrder)
{
	// Arrange
	std::uint8_t FirstFrame[FrameBufferCapacity] = {};
	std::uint8_t SecondFrame[FrameBufferCapacity] = {};
	std::size_t FirstWritten = 0;
	std::size_t SecondWritten = 0;
	EncodeFrame(
		NodeId01,
		TSpan<const std::uint8_t>(FirstBackToBackPayload, sizeof(FirstBackToBackPayload)),
		TSpan<std::uint8_t>(FirstFrame, sizeof(FirstFrame)),
		FirstWritten);
	EncodeFrame(
		NodeId02,
		TSpan<const std::uint8_t>(SecondBackToBackPayload, sizeof(SecondBackToBackPayload)),
		TSpan<std::uint8_t>(SecondFrame, sizeof(SecondFrame)),
		SecondWritten);

	std::uint8_t Stream[DoubleFrameBufferCapacity] = {};
	for (std::size_t Index = 0; Index < FirstWritten; ++Index)
	{
		Stream[Index] = FirstFrame[Index];
	}
	for (std::size_t Index = 0; Index < SecondWritten; ++Index)
	{
		Stream[FirstWritten + Index] = SecondFrame[Index];
	}

	FDecoder Decoder;
	// Feed the first frame, drain it, then feed the second so each held frame is observed before the next begins.
	EFrameEvent Last = EFrameEvent::None;
	// Act - first frame
	for (std::size_t Index = 0; Index < FirstWritten; ++Index)
	{
		Last = Decoder.PushByte(Stream[Index]);
	}
	// Assert
	MW_EXPECT_EQ(Test, EFrameEvent::FrameReady, Last, "The first back-to-back frame must complete");
	MW_EXPECT_EQ(Test, NodeId01, Decoder.FrameNodeId(), "The first decoded frame must carry node id one");
	MW_EXPECT_EQ(Test, true, PayloadMatches(Decoder, FirstBackToBackPayload, sizeof(FirstBackToBackPayload)), "The first decoded payload must match");
	Decoder.ClearFrame();

	// Act - second frame
	for (std::size_t Index = 0; Index < SecondWritten; ++Index)
	{
		Last = Decoder.PushByte(Stream[FirstWritten + Index]);
	}
	// Assert
	MW_EXPECT_EQ(Test, EFrameEvent::FrameReady, Last, "The second back-to-back frame must complete");
	MW_EXPECT_EQ(Test, NodeId02, Decoder.FrameNodeId(), "The second decoded frame must carry node id two");
	MW_EXPECT_EQ(
		Test, true, PayloadMatches(Decoder, SecondBackToBackPayload, sizeof(SecondBackToBackPayload)), "The second decoded payload must match");
}

/**
 * Scenario: Feed a frame whose final CRC byte is corrupted followed by a valid frame into a decoder.
 * Expected: The corrupted candidate is discarded, and the valid frame completes afterward with the correct source node id and payload.
 */
MW_TEST_CASE(FrameCodec_CorruptedCrcDiscardsThenNextFrameDecodes)
{
	// Arrange
	std::uint8_t BadFrame[FrameBufferCapacity] = {};
	std::size_t BadWritten = 0;
	EncodeFrame(
		NodeId03,
		TSpan<const std::uint8_t>(CorruptedCrcPayload, sizeof(CorruptedCrcPayload)),
		TSpan<std::uint8_t>(BadFrame, sizeof(BadFrame)),
		BadWritten);
	// Flip the final CRC byte so the candidate fails validation on its last byte.
	BadFrame[BadWritten - 1] = static_cast<std::uint8_t>(BadFrame[BadWritten - 1] ^ CrcCorruptionMask);

	std::uint8_t GoodFrame[FrameBufferCapacity] = {};
	std::size_t GoodWritten = 0;
	EncodeFrame(
		NodeId04,
		TSpan<const std::uint8_t>(CorruptedCrcGoodPayload, sizeof(CorruptedCrcGoodPayload)),
		TSpan<std::uint8_t>(GoodFrame, sizeof(GoodFrame)),
		GoodWritten);

	std::uint8_t Stream[DoubleFrameBufferCapacity] = {};
	for (std::size_t Index = 0; Index < BadWritten; ++Index)
	{
		Stream[Index] = BadFrame[Index];
	}
	for (std::size_t Index = 0; Index < GoodWritten; ++Index)
	{
		Stream[BadWritten + Index] = GoodFrame[Index];
	}

	FDecoder Decoder;
	bool bSawDiscarded = false;
	bool bSawFrameReady = false;
	std::size_t FrameReadyOffset = 0;
	// Act
	for (std::size_t Index = 0; Index < BadWritten + GoodWritten; ++Index)
	{
		const EFrameEvent Event = Decoder.PushByte(Stream[Index]);
		if (Event == EFrameEvent::Discarded)
		{
			bSawDiscarded = true;
		}
		else if (Event == EFrameEvent::FrameReady)
		{
			bSawFrameReady = true;
			FrameReadyOffset = Index;
		}
	}
	// Assert
	MW_EXPECT_EQ(Test, true, bSawDiscarded, "The corrupted-CRC candidate must be discarded");
	MW_EXPECT_EQ(Test, true, bSawFrameReady, "A valid frame after the discard must complete");
	// The surviving frame must be the good one, decoded after the bad candidate was rejected.
	MW_EXPECT_EQ(Test, true, FrameReadyOffset >= BadWritten, "The surviving frame must complete after the corrupted candidate ends");
	MW_EXPECT_EQ(Test, NodeId04, Decoder.FrameNodeId(), "The surviving frame must carry the good source node id");
	MW_EXPECT_EQ(
		Test,
		true,
		PayloadMatches(Decoder, CorruptedCrcGoodPayload, sizeof(CorruptedCrcGoodPayload)),
		"The surviving payload must match the good bytes");
}

/**
 * Scenario: Feed a hand-assembled candidate whose declared length exceeds the decoder capacity followed by a valid frame into a decoder.
 * Expected: The oversize-declared candidate is discarded, and the valid frame completes afterward with the correct source node id and payload.
 */
MW_TEST_CASE(FrameCodec_BadLengthDiscardsThenNextFrameDecodes)
{
	// Arrange
	std::uint8_t GoodFrame[FrameBufferCapacity] = {};
	std::size_t GoodWritten = 0;
	EncodeFrame(
		NodeId06,
		TSpan<const std::uint8_t>(BadLengthGoodPayload, sizeof(BadLengthGoodPayload)),
		TSpan<std::uint8_t>(GoodFrame, sizeof(GoodFrame)),
		GoodWritten);

	std::uint8_t Stream[BadLengthCandidateCount + FrameBufferCapacity] = {};
	for (std::size_t Index = 0; Index < BadLengthCandidateCount; ++Index)
	{
		Stream[Index] = BadLengthCandidate[Index];
	}
	for (std::size_t Index = 0; Index < GoodWritten; ++Index)
	{
		Stream[BadLengthCandidateCount + Index] = GoodFrame[Index];
	}

	FDecoder Decoder;
	bool bSawDiscarded = false;
	EFrameEvent Last = EFrameEvent::None;
	// Act
	for (std::size_t Index = 0; Index < BadLengthCandidateCount + GoodWritten; ++Index)
	{
		Last = Decoder.PushByte(Stream[Index]);
		if (Last == EFrameEvent::Discarded)
		{
			bSawDiscarded = true;
		}
	}
	// Assert
	MW_EXPECT_EQ(Test, true, bSawDiscarded, "An oversize declared length must be discarded");
	MW_EXPECT_EQ(Test, EFrameEvent::FrameReady, Last, "A valid frame after the discard must complete");
	MW_EXPECT_EQ(Test, NodeId06, Decoder.FrameNodeId(), "The surviving frame must carry the good source node id");
	MW_EXPECT_EQ(
		Test, true, PayloadMatches(Decoder, BadLengthGoodPayload, sizeof(BadLengthGoodPayload)), "The surviving payload must match the good bytes");
}

/**
 * Scenario: Feed a truncated candidate, a first valid frame, and a second valid frame into a decoder and capture the last completed frame.
 * Expected: The decoder resyncs after the truncated frame and decodes a later well-formed frame, with the surviving frame carrying the later frame's
 * node id and payload.
 */
MW_TEST_CASE(FrameCodec_TruncatedFrameResyncsOnSubsequentValidFrame)
{
	// Arrange - keep all payload bytes clear of the magic so no stray resync interferes with the documented behavior.
	std::uint8_t FrameA[FrameBufferCapacity] = {};
	std::uint8_t FrameB[FrameBufferCapacity] = {};
	std::size_t WrittenA = 0;
	std::size_t WrittenB = 0;
	EncodeFrame(
		NodeId01, TSpan<const std::uint8_t>(TruncatedPayloadA, sizeof(TruncatedPayloadA)), TSpan<std::uint8_t>(FrameA, sizeof(FrameA)), WrittenA);
	EncodeFrame(
		NodeId02, TSpan<const std::uint8_t>(TruncatedPayloadB, sizeof(TruncatedPayloadB)), TSpan<std::uint8_t>(FrameB, sizeof(FrameB)), WrittenB);

	std::uint8_t Stream[TruncatedCandidateCount + DoubleFrameBufferCapacity] = {};
	std::size_t Offset = 0;
	for (std::size_t Index = 0; Index < TruncatedCandidateCount; ++Index)
	{
		Stream[Offset++] = TruncatedCandidate[Index];
	}
	for (std::size_t Index = 0; Index < WrittenA; ++Index)
	{
		Stream[Offset++] = FrameA[Index];
	}
	for (std::size_t Index = 0; Index < WrittenB; ++Index)
	{
		Stream[Offset++] = FrameB[Index];
	}
	const std::size_t StreamLength = Offset;

	FDecoder Decoder;
	bool bSawFrameReady = false;
	std::uint8_t FinalNode = 0;
	std::uint8_t FinalPayload[DecoderMaxPayload] = {};
	std::size_t FinalLength = 0;
	// Act
	for (std::size_t Index = 0; Index < StreamLength; ++Index)
	{
		const EFrameEvent Event = Decoder.PushByte(Stream[Index]);
		if (Event == EFrameEvent::FrameReady)
		{
			bSawFrameReady = true;
			FinalNode = Decoder.FrameNodeId();
			FinalLength = Decoder.FramePayload().Size();
			const std::uint8_t* const PayloadData = Decoder.FramePayload().Data();
			for (std::size_t PayloadIndex = 0; PayloadIndex < FinalLength; ++PayloadIndex)
			{
				FinalPayload[PayloadIndex] = PayloadData[PayloadIndex];
			}
		}
	}
	// Assert - per the documented contract, the frame immediately after a truncated frame may be consumed and lost,
	// but the decoder must resync and decode a later well-formed frame (here, frame B).
	MW_EXPECT_EQ(Test, true, bSawFrameReady, "A subsequent valid frame must decode after a truncated frame resyncs");
	MW_EXPECT_EQ(Test, NodeId02, FinalNode, "The last decoded frame must be the later valid frame B");
	MW_EXPECT_EQ(Test, SingleBytePayloadSize, FinalLength, "The last decoded frame must carry frame B's payload length");
	MW_EXPECT_EQ(Test, TruncatedPayloadB[0], FinalPayload[0], "The last decoded frame must carry frame B's payload byte");
}

/**
 * Scenario: Warm up the codec once, capture the allocation counter, then run one steady-state encode and decode round trip.
 * Expected: The steady-state round trip performs no heap allocation.
 */
MW_TEST_CASE(FrameCodec_RoundTripDoesNotAllocate)
{
	// Arrange
	FDecoder Decoder;
	std::uint8_t Frame[FrameBufferCapacity] = {};
	std::size_t Written = 0;

	// Warm up once so any one-time lazy allocation is excluded from the steady-state measurement.
	EncodeFrame(
		NodeId01, TSpan<const std::uint8_t>(SteadyStatePayload, sizeof(SteadyStatePayload)), TSpan<std::uint8_t>(Frame, sizeof(Frame)), Written);
	FeedBytes(Decoder, Frame, Written);
	Decoder.ClearFrame();

	const std::uint32_t AllocationsBefore = MicroWorld::Tests::GlobalAllocationCount;

	// Act
	EncodeFrame(
		NodeId01, TSpan<const std::uint8_t>(SteadyStatePayload, sizeof(SteadyStatePayload)), TSpan<std::uint8_t>(Frame, sizeof(Frame)), Written);
	FeedBytes(Decoder, Frame, Written);
	Decoder.ClearFrame();

	const std::uint32_t AllocationsAfter = MicroWorld::Tests::GlobalAllocationCount;
	// Assert
	MW_EXPECT_EQ(Test, AllocationsBefore, AllocationsAfter, "A steady-state encode plus decode round trip must not allocate");
}

} // namespace
