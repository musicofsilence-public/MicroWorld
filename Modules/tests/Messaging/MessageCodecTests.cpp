#include "EngineAllocationCounters.h"
#include "TestSupport.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Messaging/Message.h>

#include <cstddef>
#include <cstdint>

namespace
{
using MicroWorld::TSpan;
using MicroWorld::Messaging::ActorMessageHeaderBytes;
using MicroWorld::Messaging::DecodeActorMessage;
using MicroWorld::Messaging::EMessageResult;
using MicroWorld::Messaging::EncodeActorMessage;
using MicroWorld::Messaging::FActorMessageHeader;
using MicroWorld::Messaging::FMessageActorId;
using MicroWorld::Messaging::FMessageTypeId;
using MicroWorld::Tests::GlobalAllocationCount;

/** Sentinel value pre-loaded into OutWrittenBytes so a failed encode leaving it untouched is observable. */
constexpr std::size_t UntouchedWrittenByteCount = 0xDEAD;

/** Sentinel byte pre-filled into every destination byte so a failed encode leaving it untouched is observable. */
constexpr std::uint8_t UntouchedDestinationByte = 0x5A;

/** Sentinel field value loaded into the untouched OutHeader so a failed decode leaving it untouched is observable. */
constexpr FMessageActorId UntouchedHeaderField = 0x5A5A;

/** Distinct payload bytes the round-trip tests thread through the codec. */
constexpr std::uint8_t PayloadByte01 = 0x01;
constexpr std::uint8_t PayloadByte02 = 0x02;
constexpr std::uint8_t PayloadByte03 = 0x03;
constexpr std::uint8_t PayloadByte04 = 0x04;
constexpr std::uint8_t PayloadByte11 = 0x11;
constexpr std::uint8_t PayloadByte22 = 0x22;
constexpr std::uint8_t PayloadByte33 = 0x33;
constexpr std::uint8_t PayloadByteAA = 0xAA;
constexpr std::uint8_t PayloadByteBB = 0xBB;
constexpr std::uint8_t PayloadByteCC = 0xCC;
constexpr std::uint8_t PayloadByteAD = 0xAD;
constexpr std::uint8_t PayloadByteDE = 0xDE;

/** Distinct little-endian header field bytes the byte-layout test expects. */
constexpr std::uint8_t HeaderLowByte0102 = 0x02;
constexpr std::uint8_t HeaderHighByte0102 = 0x01;
constexpr std::uint8_t HeaderLowByte0304 = 0x04;
constexpr std::uint8_t HeaderHighByte0304 = 0x03;
constexpr std::uint8_t HeaderLowByte0506 = 0x06;
constexpr std::uint8_t HeaderHighByte0506 = 0x05;

/** Header field ids the round-trip and rejection tests assign. */
constexpr FMessageTypeId MessageTypeId0102 = 0x0102;
constexpr FMessageTypeId MessageTypeId0001 = 0x0001;
constexpr FMessageTypeId MessageTypeId0010 = 0x0010;
constexpr FMessageTypeId MessageTypeId1234 = 0x1234;
constexpr FMessageActorId ActorId0042 = 0x0042;
constexpr FMessageActorId ActorId0002 = 0x0002;
constexpr FMessageActorId ActorId0003 = 0x0003;
constexpr FMessageActorId ActorId0304 = 0x0304;
constexpr FMessageActorId ActorId0506 = 0x0506;
constexpr FMessageActorId ActorId0020 = 0x0020;
constexpr FMessageActorId ActorId0030 = 0x0030;
constexpr FMessageActorId ActorId0007 = 0x0007;

/** A one-byte payload count used by both the 1-byte payload and the 1-byte sentinel storage. */
constexpr std::size_t OneBytePayloadCount = 1;

/** A two-byte payload count used by the 2-byte payloads and their matching destinations. */
constexpr std::size_t TwoBytePayloadCount = 2;

/** A three-byte payload count used by the 3-byte payloads and their matching destinations. */
constexpr std::size_t ThreeBytePayloadCount = 3;

/** A four-byte payload count used by the 4-byte payload and its matching destination. */
constexpr std::size_t FourBytePayloadCount = 4;

/** Header byte count minus one, the short-input length the decode rejection test feeds in. */
constexpr std::size_t ShortInputByteCount = ActorMessageHeaderBytes - 1;

/** Total byte count of a header plus a two-byte payload, the smallest destination several tests use. */
constexpr std::size_t HeaderPlusTwoBytes = ActorMessageHeaderBytes + 2;

/** Total byte count of a header plus a three-byte payload. */
constexpr std::size_t HeaderPlusThreeBytes = ActorMessageHeaderBytes + 3;

/** Total byte count of a header plus a four-byte payload. */
constexpr std::size_t HeaderPlusFourBytes = ActorMessageHeaderBytes + 4;

/** Fills a buffer with a recognizable sentinel so a transactional failure leaving it untouched is observable. */
void FillWithSentinel(std::uint8_t* const InBytes, const std::size_t InCount, const std::uint8_t InSentinelByte) noexcept
{
	for (std::size_t Index = 0; Index < InCount; ++Index)
	{
		InBytes[Index] = InSentinelByte;
	}
}

/**
 * Scenario: Encode a header and three-byte payload, then decode the result back through the codec.
 * Expected: Both calls succeed; the decoded header fields and payload bytes match the inputs; the written byte count equals header plus payload size.
 */
MW_TEST_CASE(EngineMessageCodec_RoundTripsHeaderAndPayload)
{
	// Arrange
	const FActorMessageHeader Header{MessageTypeId1234, ActorId0042, ActorId0007};
	const std::uint8_t Payload[ThreeBytePayloadCount] = {PayloadByteAA, PayloadByteBB, PayloadByteCC};
	std::uint8_t Encoded[HeaderPlusThreeBytes] = {};
	std::size_t WrittenBytes = 0;

	// Act
	const EMessageResult EncodeResult = EncodeActorMessage(
		Header, TSpan<const std::uint8_t>(Payload, ThreeBytePayloadCount), TSpan<std::uint8_t>(Encoded, HeaderPlusThreeBytes), WrittenBytes);

	// Assert
	MW_EXPECT_EQ(Test, EMessageResult::Success, EncodeResult, "Encoding a valid header and payload must succeed");
	MW_EXPECT_EQ(Test, HeaderPlusThreeBytes, WrittenBytes, "Written bytes must equal header plus payload size");

	// Act
	FActorMessageHeader DecodedHeader{};
	TSpan<const std::uint8_t> DecodedPayload;
	const EMessageResult DecodeResult = DecodeActorMessage(TSpan<const std::uint8_t>(Encoded, WrittenBytes), DecodedHeader, DecodedPayload);

	// Assert
	MW_EXPECT_EQ(Test, EMessageResult::Success, DecodeResult, "Decoding a freshly encoded message must succeed");
	MW_EXPECT_EQ(Test, Header.MessageTypeId, DecodedHeader.MessageTypeId, "Decoded MessageTypeId must match the encoded value");
	MW_EXPECT_EQ(Test, Header.TargetActorId, DecodedHeader.TargetActorId, "Decoded TargetActorId must match the encoded value");
	MW_EXPECT_EQ(Test, Header.SenderActorId, DecodedHeader.SenderActorId, "Decoded SenderActorId must match the encoded value");
	MW_EXPECT_EQ(Test, ThreeBytePayloadCount, DecodedPayload.Size(), "Decoded payload size must match the encoded payload size");
	for (std::size_t Index = 0; Index < ThreeBytePayloadCount; ++Index)
	{
		MW_EXPECT_EQ(Test, Payload[Index], DecodedPayload.Data()[Index], "Decoded payload bytes must match the encoded bytes byte-for-byte");
	}
}

/**
 * Scenario: Encode a known header and two-byte payload into a destination buffer.
 * Expected: Encoding succeeds; each written byte matches the expected little-endian header-then-payload layout.
 */
MW_TEST_CASE(EngineMessageCodec_EncodeProducesExactLittleEndianByteLayout)
{
	// Arrange
	const FActorMessageHeader Header{MessageTypeId0102, ActorId0304, ActorId0506};
	const std::uint8_t Payload[TwoBytePayloadCount] = {PayloadByteDE, PayloadByteAD};
	std::uint8_t Encoded[HeaderPlusTwoBytes] = {};
	std::size_t WrittenBytes = 0;

	// [u16 MessageTypeId=0x0102][u16 TargetActorId=0x0304][u16 SenderActorId=0x0506][Payload], all little-endian.
	const std::uint8_t Expected[HeaderPlusTwoBytes] = {
		HeaderLowByte0102,
		HeaderHighByte0102,
		HeaderLowByte0304,
		HeaderHighByte0304,
		HeaderLowByte0506,
		HeaderHighByte0506,
		PayloadByteDE,
		PayloadByteAD};

	// Act
	const EMessageResult EncodeResult = EncodeActorMessage(
		Header, TSpan<const std::uint8_t>(Payload, TwoBytePayloadCount), TSpan<std::uint8_t>(Encoded, HeaderPlusTwoBytes), WrittenBytes);

	// Assert
	MW_EXPECT_EQ(Test, EMessageResult::Success, EncodeResult, "Encoding a valid header and payload must succeed");
	MW_EXPECT_EQ(Test, HeaderPlusTwoBytes, WrittenBytes, "Written bytes must equal the expected encoded length");
	for (std::size_t Index = 0; Index < HeaderPlusTwoBytes; ++Index)
	{
		MW_EXPECT_EQ(Test, Expected[Index], Encoded[Index], "Each encoded byte must match the expected little-endian layout");
	}
}

/**
 * Scenario: Encode a header with a zero-length payload, then decode the result back through the codec.
 * Expected: Both calls succeed; the decoded header fields match the inputs and the decoded payload is empty; the written byte count equals exactly
 * the header size.
 */
MW_TEST_CASE(EngineMessageCodec_ZeroLengthPayloadRoundTrips)
{
	// Arrange
	const FActorMessageHeader Header{MessageTypeId0001, MicroWorld::Messaging::BroadcastActorId, MicroWorld::Messaging::BroadcastActorId};
	std::uint8_t Encoded[ActorMessageHeaderBytes] = {};
	std::size_t WrittenBytes = 0;

	// Act
	const EMessageResult EncodeResult =
		EncodeActorMessage(Header, TSpan<const std::uint8_t>(nullptr, 0), TSpan<std::uint8_t>(Encoded, ActorMessageHeaderBytes), WrittenBytes);

	// Assert
	MW_EXPECT_EQ(Test, EMessageResult::Success, EncodeResult, "Encoding a zero-length payload must succeed");
	MW_EXPECT_EQ(Test, ActorMessageHeaderBytes, WrittenBytes, "A zero-length payload must write exactly the header size");

	// Act
	FActorMessageHeader DecodedHeader{};
	TSpan<const std::uint8_t> DecodedPayload;
	const EMessageResult DecodeResult = DecodeActorMessage(TSpan<const std::uint8_t>(Encoded, WrittenBytes), DecodedHeader, DecodedPayload);

	// Assert
	MW_EXPECT_EQ(Test, EMessageResult::Success, DecodeResult, "Decoding a zero-length-payload message must succeed");
	MW_EXPECT_EQ(Test, Header.MessageTypeId, DecodedHeader.MessageTypeId, "Decoded MessageTypeId must match the encoded value");
	MW_EXPECT_EQ(Test, std::size_t{0}, DecodedPayload.Size(), "Decoded payload must be empty");
}

/**
 * Scenario: Encode a header whose MessageTypeId is zero into a sentinel-filled destination.
 * Expected: The call returns InvalidType; OutWrittenBytes and every destination byte remain untouched.
 */
MW_TEST_CASE(EngineMessageCodec_EncodeRejectsZeroMessageTypeIdTransactionally)
{
	// Arrange
	const FActorMessageHeader InvalidHeader{0, MessageTypeId0001, ActorId0002};
	const std::uint8_t Payload[TwoBytePayloadCount] = {PayloadByte11, PayloadByte22};
	std::uint8_t Encoded[HeaderPlusTwoBytes] = {};
	FillWithSentinel(Encoded, HeaderPlusTwoBytes, UntouchedDestinationByte);
	std::size_t WrittenBytes = UntouchedWrittenByteCount;

	// Act
	const EMessageResult Result = EncodeActorMessage(
		InvalidHeader, TSpan<const std::uint8_t>(Payload, TwoBytePayloadCount), TSpan<std::uint8_t>(Encoded, HeaderPlusTwoBytes), WrittenBytes);

	// Assert
	MW_EXPECT_EQ(Test, EMessageResult::InvalidType, Result, "A zero MessageTypeId must be rejected as InvalidType");
	MW_EXPECT_EQ(Test, UntouchedWrittenByteCount, WrittenBytes, "InvalidType must leave OutWrittenBytes unchanged");
	for (std::size_t Index = 0; Index < HeaderPlusTwoBytes; ++Index)
	{
		MW_EXPECT_EQ(Test, UntouchedDestinationByte, Encoded[Index], "InvalidType must leave every destination byte unchanged");
	}
}

/**
 * Scenario: Encode a header and three-byte payload into a destination too small to hold them both.
 * Expected: The call returns PayloadTooLarge; OutWrittenBytes and every destination byte remain untouched.
 */
MW_TEST_CASE(EngineMessageCodec_EncodeRejectsTooSmallDestinationTransactionally)
{
	// Arrange
	const FActorMessageHeader Header{MessageTypeId0001, ActorId0002, ActorId0003};
	const std::uint8_t Payload[ThreeBytePayloadCount] = {PayloadByte11, PayloadByte22, PayloadByte33};
	std::uint8_t TooSmall[HeaderPlusTwoBytes] = {};
	FillWithSentinel(TooSmall, HeaderPlusTwoBytes, UntouchedDestinationByte);
	std::size_t WrittenBytes = UntouchedWrittenByteCount;

	// Act
	const EMessageResult Result = EncodeActorMessage(
		Header, TSpan<const std::uint8_t>(Payload, ThreeBytePayloadCount), TSpan<std::uint8_t>(TooSmall, HeaderPlusTwoBytes), WrittenBytes);

	// Assert
	MW_EXPECT_EQ(Test, EMessageResult::PayloadTooLarge, Result, "A destination too small for header plus payload must return PayloadTooLarge");
	MW_EXPECT_EQ(Test, UntouchedWrittenByteCount, WrittenBytes, "PayloadTooLarge must leave OutWrittenBytes unchanged");
	for (std::size_t Index = 0; Index < HeaderPlusTwoBytes; ++Index)
	{
		MW_EXPECT_EQ(Test, UntouchedDestinationByte, TooSmall[Index], "PayloadTooLarge must leave every destination byte unchanged");
	}
}

/**
 * Scenario: Decode a sentinel-populated header and payload from an Encoded span shorter than the header.
 * Expected: The call returns PayloadTooLarge; OutHeader and OutPayload remain untouched.
 */
MW_TEST_CASE(EngineMessageCodec_DecodeRejectsShortInputTransactionally)
{
	// Arrange
	const std::uint8_t ShortInput[ShortInputByteCount] = {PayloadByte01, PayloadByte02, PayloadByte03, PayloadByte04, HeaderLowByte0506};
	const FActorMessageHeader SentinelHeader{UntouchedHeaderField, UntouchedHeaderField, UntouchedHeaderField};
	FActorMessageHeader OutHeader = SentinelHeader;
	const std::uint8_t SentinelPayloadStorage[OneBytePayloadCount] = {0};
	TSpan<const std::uint8_t> OutPayload(SentinelPayloadStorage, OneBytePayloadCount);

	// Act
	const EMessageResult Result = DecodeActorMessage(TSpan<const std::uint8_t>(ShortInput, ShortInputByteCount), OutHeader, OutPayload);

	// Assert
	MW_EXPECT_EQ(Test, EMessageResult::PayloadTooLarge, Result, "An Encoded span shorter than the header must return PayloadTooLarge");
	MW_EXPECT_EQ(Test, SentinelHeader.MessageTypeId, OutHeader.MessageTypeId, "PayloadTooLarge must leave OutHeader.MessageTypeId unchanged");
	MW_EXPECT_EQ(Test, SentinelHeader.TargetActorId, OutHeader.TargetActorId, "PayloadTooLarge must leave OutHeader.TargetActorId unchanged");
	MW_EXPECT_EQ(Test, SentinelHeader.SenderActorId, OutHeader.SenderActorId, "PayloadTooLarge must leave OutHeader.SenderActorId unchanged");
	MW_EXPECT_EQ(Test, SentinelPayloadStorage, OutPayload.Data(), "PayloadTooLarge must leave OutPayload's data pointer unchanged");
	MW_EXPECT_EQ(Test, OneBytePayloadCount, OutPayload.Size(), "PayloadTooLarge must leave OutPayload's size unchanged");
}

/**
 * Scenario: Decode a sentinel-populated header and payload from a header-sized message whose type id is zero.
 * Expected: The call returns InvalidType; OutHeader and OutPayload remain untouched.
 */
MW_TEST_CASE(EngineMessageCodec_DecodeRejectsZeroMessageTypeIdTransactionally)
{
	// Arrange
	// [u16 MessageTypeId=0x0000][u16 TargetActorId=0x0102][u16 SenderActorId=0x0304], little-endian, no payload.
	const std::uint8_t ZeroTypeEncoded[ActorMessageHeaderBytes] = {
		0, 0, HeaderLowByte0102, HeaderHighByte0102, HeaderLowByte0304, HeaderHighByte0304};
	const FActorMessageHeader SentinelHeader{UntouchedHeaderField, UntouchedHeaderField, UntouchedHeaderField};
	FActorMessageHeader OutHeader = SentinelHeader;
	const std::uint8_t SentinelPayloadStorage[OneBytePayloadCount] = {0};
	TSpan<const std::uint8_t> OutPayload(SentinelPayloadStorage, OneBytePayloadCount);

	// Act
	const EMessageResult Result = DecodeActorMessage(TSpan<const std::uint8_t>(ZeroTypeEncoded, ActorMessageHeaderBytes), OutHeader, OutPayload);

	// Assert
	MW_EXPECT_EQ(Test, EMessageResult::InvalidType, Result, "A decoded MessageTypeId of zero must return InvalidType");
	MW_EXPECT_EQ(Test, SentinelHeader.MessageTypeId, OutHeader.MessageTypeId, "InvalidType must leave OutHeader.MessageTypeId unchanged");
	MW_EXPECT_EQ(Test, SentinelHeader.TargetActorId, OutHeader.TargetActorId, "InvalidType must leave OutHeader.TargetActorId unchanged");
	MW_EXPECT_EQ(Test, SentinelHeader.SenderActorId, OutHeader.SenderActorId, "InvalidType must leave OutHeader.SenderActorId unchanged");
	MW_EXPECT_EQ(Test, SentinelPayloadStorage, OutPayload.Data(), "InvalidType must leave OutPayload's data pointer unchanged");
	MW_EXPECT_EQ(Test, OneBytePayloadCount, OutPayload.Size(), "InvalidType must leave OutPayload's size unchanged");
}

/**
 * Scenario: Warm up the codec once, then perform a steady-state encode plus decode round trip.
 * Expected: The steady-state round trip performs no heap allocation.
 */
MW_TEST_CASE(EngineMessageCodec_RoundTripDoesNotAllocate)
{
	// Arrange
	const FActorMessageHeader Header{MessageTypeId0010, ActorId0020, ActorId0030};
	const std::uint8_t Payload[FourBytePayloadCount] = {PayloadByte01, PayloadByte02, PayloadByte03, PayloadByte04};
	std::uint8_t Encoded[HeaderPlusFourBytes] = {};
	std::size_t WrittenBytes = 0;
	FActorMessageHeader DecodedHeader{};
	TSpan<const std::uint8_t> DecodedPayload;

	// Arrange: warm up once so any one-time lazy allocation is excluded from the steady-state measurement.
	EncodeActorMessage(
		Header, TSpan<const std::uint8_t>(Payload, FourBytePayloadCount), TSpan<std::uint8_t>(Encoded, HeaderPlusFourBytes), WrittenBytes);
	DecodeActorMessage(TSpan<const std::uint8_t>(Encoded, WrittenBytes), DecodedHeader, DecodedPayload);

	const std::uint32_t AllocationsBefore = GlobalAllocationCount;

	// Act
	EncodeActorMessage(
		Header, TSpan<const std::uint8_t>(Payload, FourBytePayloadCount), TSpan<std::uint8_t>(Encoded, HeaderPlusFourBytes), WrittenBytes);
	DecodeActorMessage(TSpan<const std::uint8_t>(Encoded, WrittenBytes), DecodedHeader, DecodedPayload);

	const std::uint32_t AllocationsAfter = GlobalAllocationCount;
	// Assert
	MW_EXPECT_EQ(Test, AllocationsBefore, AllocationsAfter, "A steady-state encode plus decode round trip must not allocate");
}

} // namespace
