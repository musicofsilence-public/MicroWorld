#include "EngineAllocationCounters.h"
#include "TestSupport.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Engine/Message.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{
using MicroWorld::ActorMessageHeaderBytes;
using MicroWorld::DecodeActorMessage;
using MicroWorld::EMessageResult;
using MicroWorld::EncodeActorMessage;
using MicroWorld::FActorMessageHeader;
using MicroWorld::TSpan;
using MicroWorld::Tests::GlobalAllocationCount;

/** Fills a buffer with a recognizable sentinel so a transactional failure leaving it untouched is observable. */
template<std::size_t BufferSize>
void FillWithSentinel(std::array<std::uint8_t, BufferSize>& Buffer, const std::uint8_t SentinelByte) noexcept
{
	for (std::size_t Index = 0; Index < BufferSize; ++Index)
	{
		Buffer[Index] = SentinelByte;
	}
}

/** Proves encode then decode round-trips a nonempty payload and reports the correct written byte count. */
MW_TEST_CASE(EngineMessageCodec_RoundTripsHeaderAndPayload)
{
	const FActorMessageHeader Header{0x1234, 0x0042, 0x0007};
	const std::array<std::uint8_t, 3> Payload{0xAA, 0xBB, 0xCC};
	std::array<std::uint8_t, ActorMessageHeaderBytes + 3> Encoded{};
	std::size_t WrittenBytes = 0;

	const EMessageResult EncodeResult = EncodeActorMessage(
		Header, TSpan<const std::uint8_t>(Payload.data(), Payload.size()), TSpan<std::uint8_t>(Encoded.data(), Encoded.size()), WrittenBytes);
	MW_EXPECT_EQ(Test, EMessageResult::Success, EncodeResult, "Encoding a valid header and payload must succeed");
	MW_EXPECT_EQ(Test, ActorMessageHeaderBytes + Payload.size(), WrittenBytes, "Written bytes must equal header plus payload size");

	FActorMessageHeader DecodedHeader{};
	TSpan<const std::uint8_t> DecodedPayload;
	const EMessageResult DecodeResult = DecodeActorMessage(TSpan<const std::uint8_t>(Encoded.data(), WrittenBytes), DecodedHeader, DecodedPayload);
	MW_EXPECT_EQ(Test, EMessageResult::Success, DecodeResult, "Decoding a freshly encoded message must succeed");
	MW_EXPECT_EQ(Test, Header.MessageTypeId, DecodedHeader.MessageTypeId, "Decoded MessageTypeId must match the encoded value");
	MW_EXPECT_EQ(Test, Header.TargetActorId, DecodedHeader.TargetActorId, "Decoded TargetActorId must match the encoded value");
	MW_EXPECT_EQ(Test, Header.SenderActorId, DecodedHeader.SenderActorId, "Decoded SenderActorId must match the encoded value");
	MW_EXPECT_EQ(Test, Payload.size(), DecodedPayload.Size(), "Decoded payload size must match the encoded payload size");
	for (std::size_t Index = 0; Index < Payload.size(); ++Index)
	{
		MW_EXPECT_EQ(Test, Payload[Index], DecodedPayload.Data()[Index], "Decoded payload bytes must match the encoded bytes byte-for-byte");
	}
}

/** Proves encode produces the exact little-endian byte layout for a known header and payload. */
MW_TEST_CASE(EngineMessageCodec_EncodeProducesExactLittleEndianByteLayout)
{
	const FActorMessageHeader Header{0x0102, 0x0304, 0x0506};
	const std::array<std::uint8_t, 2> Payload{0xDE, 0xAD};
	std::array<std::uint8_t, ActorMessageHeaderBytes + 2> Encoded{};
	std::size_t WrittenBytes = 0;

	const EMessageResult EncodeResult = EncodeActorMessage(
		Header, TSpan<const std::uint8_t>(Payload.data(), Payload.size()), TSpan<std::uint8_t>(Encoded.data(), Encoded.size()), WrittenBytes);
	MW_EXPECT_EQ(Test, EMessageResult::Success, EncodeResult, "Encoding a valid header and payload must succeed");

	// [u16 MessageTypeId=0x0102][u16 TargetActorId=0x0304][u16 SenderActorId=0x0506][Payload], all little-endian.
	const std::array<std::uint8_t, ActorMessageHeaderBytes + 2> Expected{0x02, 0x01, 0x04, 0x03, 0x06, 0x05, 0xDE, 0xAD};
	MW_EXPECT_EQ(Test, Expected.size(), WrittenBytes, "Written bytes must equal the expected encoded length");
	for (std::size_t Index = 0; Index < Expected.size(); ++Index)
	{
		MW_EXPECT_EQ(Test, Expected[Index], Encoded[Index], "Each encoded byte must match the expected little-endian layout");
	}
}

/** Proves a zero-length payload round-trips as a valid six-byte encoded message. */
MW_TEST_CASE(EngineMessageCodec_ZeroLengthPayloadRoundTrips)
{
	const FActorMessageHeader Header{0x0001, 0x0000, 0x0000};
	std::array<std::uint8_t, ActorMessageHeaderBytes> Encoded{};
	std::size_t WrittenBytes = 0;

	const EMessageResult EncodeResult =
		EncodeActorMessage(Header, TSpan<const std::uint8_t>(nullptr, 0), TSpan<std::uint8_t>(Encoded.data(), Encoded.size()), WrittenBytes);
	MW_EXPECT_EQ(Test, EMessageResult::Success, EncodeResult, "Encoding a zero-length payload must succeed");
	MW_EXPECT_EQ(Test, ActorMessageHeaderBytes, WrittenBytes, "A zero-length payload must write exactly the header size");

	FActorMessageHeader DecodedHeader{};
	TSpan<const std::uint8_t> DecodedPayload;
	const EMessageResult DecodeResult = DecodeActorMessage(TSpan<const std::uint8_t>(Encoded.data(), WrittenBytes), DecodedHeader, DecodedPayload);
	MW_EXPECT_EQ(Test, EMessageResult::Success, DecodeResult, "Decoding a zero-length-payload message must succeed");
	MW_EXPECT_EQ(Test, Header.MessageTypeId, DecodedHeader.MessageTypeId, "Decoded MessageTypeId must match the encoded value");
	MW_EXPECT_EQ(Test, std::size_t{0}, DecodedPayload.Size(), "Decoded payload must be empty");
}

/** Proves encode rejects a zero MessageTypeId as InvalidType and leaves the destination and OutWrittenBytes untouched. */
MW_TEST_CASE(EngineMessageCodec_EncodeRejectsZeroMessageTypeIdTransactionally)
{
	const FActorMessageHeader InvalidHeader{0x0000, 0x0001, 0x0002};
	const std::array<std::uint8_t, 2> Payload{0x11, 0x22};
	std::array<std::uint8_t, ActorMessageHeaderBytes + 2> Encoded{};
	FillWithSentinel(Encoded, 0x5A);
	std::size_t WrittenBytes = 0xDEAD;

	const EMessageResult Result = EncodeActorMessage(
		InvalidHeader, TSpan<const std::uint8_t>(Payload.data(), Payload.size()), TSpan<std::uint8_t>(Encoded.data(), Encoded.size()), WrittenBytes);

	MW_EXPECT_EQ(Test, EMessageResult::InvalidType, Result, "A zero MessageTypeId must be rejected as InvalidType");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0xDEAD), WrittenBytes, "InvalidType must leave OutWrittenBytes unchanged");
	for (std::size_t Index = 0; Index < Encoded.size(); ++Index)
	{
		MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x5A), Encoded[Index], "InvalidType must leave every destination byte unchanged");
	}
}

/** Proves encode rejects a destination too small for the header plus payload as PayloadTooLarge, transactionally. */
MW_TEST_CASE(EngineMessageCodec_EncodeRejectsTooSmallDestinationTransactionally)
{
	const FActorMessageHeader Header{0x0001, 0x0002, 0x0003};
	const std::array<std::uint8_t, 3> Payload{0x11, 0x22, 0x33};
	std::array<std::uint8_t, ActorMessageHeaderBytes + 2> TooSmall{};
	FillWithSentinel(TooSmall, 0x5A);
	std::size_t WrittenBytes = 0xDEAD;

	const EMessageResult Result = EncodeActorMessage(
		Header, TSpan<const std::uint8_t>(Payload.data(), Payload.size()), TSpan<std::uint8_t>(TooSmall.data(), TooSmall.size()), WrittenBytes);

	MW_EXPECT_EQ(Test, EMessageResult::PayloadTooLarge, Result, "A destination too small for header plus payload must return PayloadTooLarge");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0xDEAD), WrittenBytes, "PayloadTooLarge must leave OutWrittenBytes unchanged");
	for (std::size_t Index = 0; Index < TooSmall.size(); ++Index)
	{
		MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x5A), TooSmall[Index], "PayloadTooLarge must leave every destination byte unchanged");
	}
}

/** Proves decode rejects an Encoded span shorter than the header as PayloadTooLarge, leaving both outputs untouched. */
MW_TEST_CASE(EngineMessageCodec_DecodeRejectsShortInputTransactionally)
{
	const std::array<std::uint8_t, ActorMessageHeaderBytes - 1> ShortInput{0x01, 0x02, 0x03, 0x04, 0x05};
	const FActorMessageHeader SentinelHeader{0x5A5A, 0x5A5A, 0x5A5A};
	FActorMessageHeader OutHeader = SentinelHeader;
	const std::array<std::uint8_t, 1> SentinelPayloadStorage{0x00};
	TSpan<const std::uint8_t> OutPayload(SentinelPayloadStorage.data(), SentinelPayloadStorage.size());

	const EMessageResult Result = DecodeActorMessage(TSpan<const std::uint8_t>(ShortInput.data(), ShortInput.size()), OutHeader, OutPayload);

	MW_EXPECT_EQ(Test, EMessageResult::PayloadTooLarge, Result, "An Encoded span shorter than the header must return PayloadTooLarge");
	MW_EXPECT_EQ(Test, SentinelHeader.MessageTypeId, OutHeader.MessageTypeId, "PayloadTooLarge must leave OutHeader.MessageTypeId unchanged");
	MW_EXPECT_EQ(Test, SentinelHeader.TargetActorId, OutHeader.TargetActorId, "PayloadTooLarge must leave OutHeader.TargetActorId unchanged");
	MW_EXPECT_EQ(Test, SentinelHeader.SenderActorId, OutHeader.SenderActorId, "PayloadTooLarge must leave OutHeader.SenderActorId unchanged");
	MW_EXPECT_EQ(Test, SentinelPayloadStorage.data(), OutPayload.Data(), "PayloadTooLarge must leave OutPayload's data pointer unchanged");
	MW_EXPECT_EQ(Test, SentinelPayloadStorage.size(), OutPayload.Size(), "PayloadTooLarge must leave OutPayload's size unchanged");
}

/** Proves decode rejects an encoded message whose header type id is zero as InvalidType, leaving both outputs untouched. */
MW_TEST_CASE(EngineMessageCodec_DecodeRejectsZeroMessageTypeIdTransactionally)
{
	// [u16 MessageTypeId=0x0000][u16 TargetActorId=0x0102][u16 SenderActorId=0x0304], little-endian, no payload.
	const std::array<std::uint8_t, ActorMessageHeaderBytes> ZeroTypeEncoded{0x00, 0x00, 0x02, 0x01, 0x04, 0x03};
	const FActorMessageHeader SentinelHeader{0x5A5A, 0x5A5A, 0x5A5A};
	FActorMessageHeader OutHeader = SentinelHeader;
	const std::array<std::uint8_t, 1> SentinelPayloadStorage{0x00};
	TSpan<const std::uint8_t> OutPayload(SentinelPayloadStorage.data(), SentinelPayloadStorage.size());

	const EMessageResult Result =
		DecodeActorMessage(TSpan<const std::uint8_t>(ZeroTypeEncoded.data(), ZeroTypeEncoded.size()), OutHeader, OutPayload);

	MW_EXPECT_EQ(Test, EMessageResult::InvalidType, Result, "A decoded MessageTypeId of zero must return InvalidType");
	MW_EXPECT_EQ(Test, SentinelHeader.MessageTypeId, OutHeader.MessageTypeId, "InvalidType must leave OutHeader.MessageTypeId unchanged");
	MW_EXPECT_EQ(Test, SentinelHeader.TargetActorId, OutHeader.TargetActorId, "InvalidType must leave OutHeader.TargetActorId unchanged");
	MW_EXPECT_EQ(Test, SentinelHeader.SenderActorId, OutHeader.SenderActorId, "InvalidType must leave OutHeader.SenderActorId unchanged");
	MW_EXPECT_EQ(Test, SentinelPayloadStorage.data(), OutPayload.Data(), "InvalidType must leave OutPayload's data pointer unchanged");
	MW_EXPECT_EQ(Test, SentinelPayloadStorage.size(), OutPayload.Size(), "InvalidType must leave OutPayload's size unchanged");
}

/** Proves a steady-state encode plus decode round trip performs no heap allocation. */
MW_TEST_CASE(EngineMessageCodec_RoundTripDoesNotAllocate)
{
	const FActorMessageHeader Header{0x0010, 0x0020, 0x0030};
	const std::array<std::uint8_t, 4> Payload{0x01, 0x02, 0x03, 0x04};
	std::array<std::uint8_t, ActorMessageHeaderBytes + 4> Encoded{};
	std::size_t WrittenBytes = 0;
	FActorMessageHeader DecodedHeader{};
	TSpan<const std::uint8_t> DecodedPayload;

	// Warm up once so any one-time lazy allocation is excluded from the steady-state measurement.
	EncodeActorMessage(
		Header, TSpan<const std::uint8_t>(Payload.data(), Payload.size()), TSpan<std::uint8_t>(Encoded.data(), Encoded.size()), WrittenBytes);
	DecodeActorMessage(TSpan<const std::uint8_t>(Encoded.data(), WrittenBytes), DecodedHeader, DecodedPayload);

	const std::uint32_t AllocationsBefore = GlobalAllocationCount;

	EncodeActorMessage(
		Header, TSpan<const std::uint8_t>(Payload.data(), Payload.size()), TSpan<std::uint8_t>(Encoded.data(), Encoded.size()), WrittenBytes);
	DecodeActorMessage(TSpan<const std::uint8_t>(Encoded.data(), WrittenBytes), DecodedHeader, DecodedPayload);

	const std::uint32_t AllocationsAfter = GlobalAllocationCount;
	MW_EXPECT_EQ(Test, AllocationsBefore, AllocationsAfter, "A steady-state encode plus decode round trip must not allocate");
}

} // namespace
