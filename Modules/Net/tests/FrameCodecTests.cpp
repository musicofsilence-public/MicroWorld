#include "TestSupport.h"

#include "NetAllocationCounters.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Net/FrameCodec.h>
#include <MicroWorld/Net/NetResult.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::ComputeCrc16Ccitt;
using MicroWorld::EFrameEvent;
using MicroWorld::EncodeFrame;
using MicroWorld::ENetResult;
using MicroWorld::FrameMagicByte;
using MicroWorld::FrameOverheadBytes;
using MicroWorld::TFrameDecoder;
using MicroWorld::TSpan;

/** Decoder payload capacity shared by every case so a declared length of nine exercises the oversize path. */
constexpr std::size_t DecoderMaxPayload = 8;

/** Convenient alias so each case names one concrete decoder type without repeating the capacity. */
using FDecoder = TFrameDecoder<DecoderMaxPayload>;

/** Feeds a byte sequence through a decoder and returns the event produced by the final byte. */
EFrameEvent FeedBytes(FDecoder& Decoder, const std::uint8_t* const Bytes, const std::size_t Count) noexcept
{
	EFrameEvent Last = EFrameEvent::None;
	for (std::size_t Index = 0; Index < Count; ++Index)
	{
		Last = Decoder.PushByte(Bytes[Index]);
	}
	return Last;
}

/** Reports whether the decoder's held payload equals the expected bytes and length. */
bool PayloadMatches(const FDecoder& Decoder, const std::uint8_t* const Expected, const std::size_t Count) noexcept
{
	if (Decoder.FramePayload().Size() != Count)
	{
		return false;
	}
	const std::uint8_t* const Data = Decoder.FramePayload().Data();
	for (std::size_t Index = 0; Index < Count; ++Index)
	{
		if (Data[Index] != Expected[Index])
		{
			return false;
		}
	}
	return true;
}

/** Proves the CRC primitive matches the canonical CRC-16/CCITT-FALSE check value. */
MW_TEST_CASE(FrameCodec_Crc16CcittFalseCheckValueIs29B1)
{
	const std::array<std::uint8_t, 9> CheckInput{0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};

	const std::uint16_t Check = ComputeCrc16Ccitt(TSpan<const std::uint8_t>(CheckInput.data(), CheckInput.size()));

	MW_EXPECT_EQ(Test, static_cast<std::uint16_t>(0x29B1u), Check, "CRC-16/CCITT-FALSE of ASCII 123456789 must be 0x29B1");
}

/** Proves encode then byte-by-byte decode round-trips payloads of size zero, one, and the maximum, including a 0xA5 payload byte. */
MW_TEST_CASE(FrameCodec_RoundTripsPayloadSizesZeroOneAndMax)
{
	const std::array<std::size_t, 3> Sizes{0, 1, DecoderMaxPayload};

	for (std::size_t SizeIndex = 0; SizeIndex < Sizes.size(); ++SizeIndex)
	{
		const std::size_t PayloadSize = Sizes[SizeIndex];

		std::array<std::uint8_t, DecoderMaxPayload> Payload{};
		for (std::size_t Index = 0; Index < PayloadSize; ++Index)
		{
			// 0xA5 at index 1 proves a payload byte equal to the magic is not misread as a frame boundary.
			Payload[Index] = (Index == 1) ? FrameMagicByte : static_cast<std::uint8_t>(0x10u + Index);
		}

		std::array<std::uint8_t, DecoderMaxPayload + FrameOverheadBytes> Frame{};
		std::size_t Written = 0;
		const ENetResult EncodeResult =
			EncodeFrame(0x07, TSpan<const std::uint8_t>(Payload.data(), PayloadSize), TSpan<std::uint8_t>(Frame.data(), Frame.size()), Written);
		MW_EXPECT_EQ(Test, ENetResult::Success, EncodeResult, "Encode must succeed for every in-capacity payload size");
		MW_EXPECT_EQ(Test, PayloadSize + FrameOverheadBytes, Written, "Written must equal payload plus overhead");

		FDecoder Decoder;
		const EFrameEvent Last = FeedBytes(Decoder, Frame.data(), Written);
		MW_EXPECT_EQ(Test, EFrameEvent::FrameReady, Last, "The final frame byte must complete a frame");
		MW_EXPECT_EQ(Test, true, Decoder.HasFrame(), "A completed frame must be held");
		MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x07), Decoder.FrameNodeId(), "The held frame must carry the encoded source node id");
		MW_EXPECT_EQ(Test, true, PayloadMatches(Decoder, Payload.data(), PayloadSize), "The held payload must match the encoded bytes byte-for-byte");

		Decoder.ClearFrame();
		MW_EXPECT_EQ(Test, false, Decoder.HasFrame(), "ClearFrame must release the held frame");
	}
}

/** Proves EncodeFrame rejects invalid inputs and oversize destinations while leaving OutWritten unchanged. */
MW_TEST_CASE(FrameCodec_EncodeRejectsInvalidAndFullCases)
{
	const std::array<std::uint8_t, 2> Payload{0x01, 0x02};

	// Null payload with nonzero length must return Invalid without touching outputs.
	std::array<std::uint8_t, DecoderMaxPayload + FrameOverheadBytes> Frame{};
	std::size_t Written = 0xDEAD;
	ENetResult Result = EncodeFrame(0x01, TSpan<const std::uint8_t>(nullptr, 2), TSpan<std::uint8_t>(Frame.data(), Frame.size()), Written);
	MW_EXPECT_EQ(Test, ENetResult::Invalid, Result, "A null payload with nonzero length must return Invalid");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0xDEAD), Written, "Invalid must leave OutWritten unchanged");

	// A destination too small for the framed payload must return Full without touching outputs.
	std::array<std::uint8_t, 2> TooSmall{};
	Written = 0xDEAD;
	Result =
		EncodeFrame(0x01, TSpan<const std::uint8_t>(Payload.data(), Payload.size()), TSpan<std::uint8_t>(TooSmall.data(), TooSmall.size()), Written);
	MW_EXPECT_EQ(Test, ENetResult::Full, Result, "A destination smaller than payload plus overhead must return Full");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0xDEAD), Written, "Full must leave OutWritten unchanged");

	// A null destination with nonzero length must return Invalid without touching outputs.
	Written = 0xDEAD;
	Result = EncodeFrame(0x01, TSpan<const std::uint8_t>(Payload.data(), Payload.size()), TSpan<std::uint8_t>(nullptr, 4), Written);
	MW_EXPECT_EQ(Test, ENetResult::Invalid, Result, "A null destination with nonzero length must return Invalid");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0xDEAD), Written, "Invalid must leave OutWritten unchanged");
}

/** Proves leading non-magic garbage is dropped and the following valid frame still decodes with the correct node. */
MW_TEST_CASE(FrameCodec_LeadingGarbageThenValidFrameDecodes)
{
	std::array<std::uint8_t, 1> Payload{0x55};
	std::array<std::uint8_t, DecoderMaxPayload + FrameOverheadBytes> Frame{};
	std::size_t Written = 0;
	EncodeFrame(0x09, TSpan<const std::uint8_t>(Payload.data(), Payload.size()), TSpan<std::uint8_t>(Frame.data(), Frame.size()), Written);

	// Prepend bytes that are not the magic so the decoder must drop them while waiting for a frame start.
	std::array<std::uint8_t, 3> Garbage{0x00, 0xFF, 0x42};
	std::array<std::uint8_t, Garbage.size() + DecoderMaxPayload + FrameOverheadBytes> Stream{};
	for (std::size_t Index = 0; Index < Garbage.size(); ++Index)
	{
		Stream[Index] = Garbage[Index];
	}
	for (std::size_t Index = 0; Index < Written; ++Index)
	{
		Stream[Garbage.size() + Index] = Frame[Index];
	}

	FDecoder Decoder;
	const EFrameEvent Last = FeedBytes(Decoder, Stream.data(), Garbage.size() + Written);
	MW_EXPECT_EQ(Test, EFrameEvent::FrameReady, Last, "The valid frame following garbage must complete");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x09), Decoder.FrameNodeId(), "The decoded frame must carry the encoded source node id");
	MW_EXPECT_EQ(Test, true, PayloadMatches(Decoder, Payload.data(), Payload.size()), "The decoded payload must match the encoded bytes");
}

/** Proves two valid frames sent back-to-back both decode in order when the held frame is cleared between them. */
MW_TEST_CASE(FrameCodec_TwoBackToBackFramesDecodeInOrder)
{
	const std::array<std::uint8_t, 2> FirstPayload{0x10, 0x20};
	const std::array<std::uint8_t, 3> SecondPayload{0x30, 0x40, 0x50};

	std::array<std::uint8_t, DecoderMaxPayload + FrameOverheadBytes> FirstFrame{};
	std::array<std::uint8_t, DecoderMaxPayload + FrameOverheadBytes> SecondFrame{};
	std::size_t FirstWritten = 0;
	std::size_t SecondWritten = 0;
	EncodeFrame(
		0x01,
		TSpan<const std::uint8_t>(FirstPayload.data(), FirstPayload.size()),
		TSpan<std::uint8_t>(FirstFrame.data(), FirstFrame.size()),
		FirstWritten);
	EncodeFrame(
		0x02,
		TSpan<const std::uint8_t>(SecondPayload.data(), SecondPayload.size()),
		TSpan<std::uint8_t>(SecondFrame.data(), SecondFrame.size()),
		SecondWritten);

	std::array<std::uint8_t, (DecoderMaxPayload + FrameOverheadBytes) * 2> Stream{};
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
	for (std::size_t Index = 0; Index < FirstWritten; ++Index)
	{
		Last = Decoder.PushByte(Stream[Index]);
	}
	MW_EXPECT_EQ(Test, EFrameEvent::FrameReady, Last, "The first back-to-back frame must complete");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x01), Decoder.FrameNodeId(), "The first decoded frame must carry node id one");
	MW_EXPECT_EQ(Test, true, PayloadMatches(Decoder, FirstPayload.data(), FirstPayload.size()), "The first decoded payload must match");
	Decoder.ClearFrame();

	for (std::size_t Index = 0; Index < SecondWritten; ++Index)
	{
		Last = Decoder.PushByte(Stream[FirstWritten + Index]);
	}
	MW_EXPECT_EQ(Test, EFrameEvent::FrameReady, Last, "The second back-to-back frame must complete");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x02), Decoder.FrameNodeId(), "The second decoded frame must carry node id two");
	MW_EXPECT_EQ(Test, true, PayloadMatches(Decoder, SecondPayload.data(), SecondPayload.size()), "The second decoded payload must match");
}

/** Proves a corrupted CRC byte discards the candidate and a following valid frame still decodes. */
MW_TEST_CASE(FrameCodec_CorruptedCrcDiscardsThenNextFrameDecodes)
{
	const std::array<std::uint8_t, 2> Payload{0x11, 0x22};
	std::array<std::uint8_t, DecoderMaxPayload + FrameOverheadBytes> BadFrame{};
	std::size_t BadWritten = 0;
	EncodeFrame(0x03, TSpan<const std::uint8_t>(Payload.data(), Payload.size()), TSpan<std::uint8_t>(BadFrame.data(), BadFrame.size()), BadWritten);
	// Flip the final CRC byte so the candidate fails validation on its last byte.
	BadFrame[BadWritten - 1] = static_cast<std::uint8_t>(BadFrame[BadWritten - 1] ^ 0xFFu);

	const std::array<std::uint8_t, 1> GoodPayload{0x77};
	std::array<std::uint8_t, DecoderMaxPayload + FrameOverheadBytes> GoodFrame{};
	std::size_t GoodWritten = 0;
	EncodeFrame(
		0x04,
		TSpan<const std::uint8_t>(GoodPayload.data(), GoodPayload.size()),
		TSpan<std::uint8_t>(GoodFrame.data(), GoodFrame.size()),
		GoodWritten);

	std::array<std::uint8_t, (DecoderMaxPayload + FrameOverheadBytes) * 2> Stream{};
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
	MW_EXPECT_EQ(Test, true, bSawDiscarded, "The corrupted-CRC candidate must be discarded");
	MW_EXPECT_EQ(Test, true, bSawFrameReady, "A valid frame after the discard must complete");
	// The surviving frame must be the good one, decoded after the bad candidate was rejected.
	MW_EXPECT_EQ(Test, true, FrameReadyOffset >= BadWritten, "The surviving frame must complete after the corrupted candidate ends");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x04), Decoder.FrameNodeId(), "The surviving frame must carry the good source node id");
	MW_EXPECT_EQ(Test, true, PayloadMatches(Decoder, GoodPayload.data(), GoodPayload.size()), "The surviving payload must match the good bytes");
}

/** Proves a declared length above the decoder capacity is discarded and a following valid frame still decodes. */
MW_TEST_CASE(FrameCodec_BadLengthDiscardsThenNextFrameDecodes)
{
	// Hand-assemble a candidate whose declared length (nine) exceeds the decoder capacity (eight).
	const std::array<std::uint8_t, 4> BadCandidate{FrameMagicByte, 0x05, 0x00, 0x09};

	const std::array<std::uint8_t, 1> GoodPayload{0x66};
	std::array<std::uint8_t, DecoderMaxPayload + FrameOverheadBytes> GoodFrame{};
	std::size_t GoodWritten = 0;
	EncodeFrame(
		0x06,
		TSpan<const std::uint8_t>(GoodPayload.data(), GoodPayload.size()),
		TSpan<std::uint8_t>(GoodFrame.data(), GoodFrame.size()),
		GoodWritten);

	std::array<std::uint8_t, BadCandidate.size() + DecoderMaxPayload + FrameOverheadBytes> Stream{};
	for (std::size_t Index = 0; Index < BadCandidate.size(); ++Index)
	{
		Stream[Index] = BadCandidate[Index];
	}
	for (std::size_t Index = 0; Index < GoodWritten; ++Index)
	{
		Stream[BadCandidate.size() + Index] = GoodFrame[Index];
	}

	FDecoder Decoder;
	bool bSawDiscarded = false;
	EFrameEvent Last = EFrameEvent::None;
	for (std::size_t Index = 0; Index < BadCandidate.size() + GoodWritten; ++Index)
	{
		Last = Decoder.PushByte(Stream[Index]);
		if (Last == EFrameEvent::Discarded)
		{
			bSawDiscarded = true;
		}
	}
	MW_EXPECT_EQ(Test, true, bSawDiscarded, "An oversize declared length must be discarded");
	MW_EXPECT_EQ(Test, EFrameEvent::FrameReady, Last, "A valid frame after the discard must complete");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x06), Decoder.FrameNodeId(), "The surviving frame must carry the good source node id");
	MW_EXPECT_EQ(Test, true, PayloadMatches(Decoder, GoodPayload.data(), GoodPayload.size()), "The surviving payload must match the good bytes");
}

/** Proves the documented resync guarantee after a truncated frame: a later valid frame decodes once the machine resyncs. */
MW_TEST_CASE(FrameCodec_TruncatedFrameResyncsOnSubsequentValidFrame)
{
	// Hand-assemble a truncated candidate: magic, node, declared length five, but only two payload bytes.
	const std::array<std::uint8_t, 6> Truncated{FrameMagicByte, 0x08, 0x00, 0x05, 0xAA, 0xBB};
	// Keep all payload bytes clear of the magic so no stray resync interferes with the documented behavior.
	const std::array<std::uint8_t, 2> PayloadA{0x11, 0x22};
	const std::array<std::uint8_t, 1> PayloadB{0x33};
	std::array<std::uint8_t, DecoderMaxPayload + FrameOverheadBytes> FrameA{};
	std::array<std::uint8_t, DecoderMaxPayload + FrameOverheadBytes> FrameB{};
	std::size_t WrittenA = 0;
	std::size_t WrittenB = 0;
	EncodeFrame(0x01, TSpan<const std::uint8_t>(PayloadA.data(), PayloadA.size()), TSpan<std::uint8_t>(FrameA.data(), FrameA.size()), WrittenA);
	EncodeFrame(0x02, TSpan<const std::uint8_t>(PayloadB.data(), PayloadB.size()), TSpan<std::uint8_t>(FrameB.data(), FrameB.size()), WrittenB);

	std::array<std::uint8_t, Truncated.size() + (DecoderMaxPayload + FrameOverheadBytes) * 2> Stream{};
	std::size_t Offset = 0;
	for (std::size_t Index = 0; Index < Truncated.size(); ++Index)
	{
		Stream[Offset++] = Truncated[Index];
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
	std::array<std::uint8_t, DecoderMaxPayload> FinalPayload{};
	std::size_t FinalLength = 0;
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
	// Per the documented contract, the frame immediately after a truncated frame may be consumed and lost,
	// but the decoder must resync and decode a later well-formed frame (here, frame B).
	MW_EXPECT_EQ(Test, true, bSawFrameReady, "A subsequent valid frame must decode after a truncated frame resyncs");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x02), FinalNode, "The last decoded frame must be the later valid frame B");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), FinalLength, "The last decoded frame must carry frame B's payload length");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x33), FinalPayload[0], "The last decoded frame must carry frame B's payload byte");
}

/** Proves a steady-state encode plus decode round trip performs no heap allocation. */
MW_TEST_CASE(FrameCodec_RoundTripDoesNotAllocate)
{
	FDecoder Decoder;
	std::array<std::uint8_t, DecoderMaxPayload> Payload{0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87};
	std::array<std::uint8_t, DecoderMaxPayload + FrameOverheadBytes> Frame{};
	std::size_t Written = 0;

	// Warm up once so any one-time lazy allocation is excluded from the steady-state measurement.
	EncodeFrame(0x01, TSpan<const std::uint8_t>(Payload.data(), Payload.size()), TSpan<std::uint8_t>(Frame.data(), Frame.size()), Written);
	FeedBytes(Decoder, Frame.data(), Written);
	Decoder.ClearFrame();

	const std::uint32_t AllocationsBefore = MicroWorld::Tests::GlobalAllocationCount;

	EncodeFrame(0x01, TSpan<const std::uint8_t>(Payload.data(), Payload.size()), TSpan<std::uint8_t>(Frame.data(), Frame.size()), Written);
	FeedBytes(Decoder, Frame.data(), Written);
	Decoder.ClearFrame();

	const std::uint32_t AllocationsAfter = MicroWorld::Tests::GlobalAllocationCount;
	MW_EXPECT_EQ(Test, AllocationsBefore, AllocationsAfter, "A steady-state encode plus decode round trip must not allocate");
}

} // namespace
