#include "TestSupport.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Transport/ByteWriter.h>
#include <MicroWorld/Transport/TransportProtocol.h>
#include <MicroWorld/Transport/TransportResult.h>

#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::Core::TSpan;
using MicroWorld::Transport::ControlChannel;
using MicroWorld::Transport::EControlMessageType;
using MicroWorld::Transport::ETransportResult;
using MicroWorld::Transport::FByteWriter;
using MicroWorld::Transport::FControlMessage;
using MicroWorld::Transport::FMessageHeader;
using MicroWorld::Transport::MaxControlPayloadBytes;
using MicroWorld::Transport::MessageHeaderBytes;
using MicroWorld::Transport::ReadControlMessage;
using MicroWorld::Transport::ReadMessage;
using MicroWorld::Transport::WriteControlMessage;
using MicroWorld::Transport::WriteMessage;

/** Channel byte the round-trip and control cases encode as the message channel. */
constexpr std::uint8_t ApplicationChannel = 7;
/** Channel byte the multi-byte round-trip case encodes, distinct from the standard channel. */
constexpr std::uint8_t DistinctChannel = 42;

/** Capacity for a buffer holding a header plus one payload byte. */
constexpr std::size_t HeaderPlusOneByteCapacity = MessageHeaderBytes + 1;
/** Capacity for a buffer holding a header plus two payload bytes. */
constexpr std::size_t HeaderPlusTwoByteCapacity = MessageHeaderBytes + 2;
/** Capacity for a buffer holding a header plus four payload bytes. */
constexpr std::size_t HeaderPlusFourByteCapacity = MessageHeaderBytes + 4;
/** Capacity for a header-only buffer used by the empty-payload round-trip case. */
constexpr std::size_t HeaderOnlyBufferCapacity = MessageHeaderBytes;
/** Capacity for a four-byte buffer too small for a header plus a two-byte payload. */
constexpr std::size_t UndersizedBufferCapacity = 4;
/** Capacity for the single-byte header byte plus no payload used by the truncated-header case. */
constexpr std::size_t TruncatedHeaderBufferCapacity = 3;
/** Capacity for a one-byte bogus payload storage the oversize case pairs with a huge declared length. */
constexpr std::size_t SingleByteStorageCapacity = 1;
/** Capacity for a one-byte payload the single-byte round-trip case delivers. */
constexpr std::size_t SingleBytePayloadCapacity = 1;
/** Capacity for a two-byte payload the Full-buffer case pairs with an undersized buffer. */
constexpr std::size_t TwoBytePayloadCapacity = 2;
/** Capacity for a four-byte payload the multi-byte round-trip case delivers. */
constexpr std::size_t FourBytePayloadCapacity = 4;
/** Capacity for a three-byte bogus payload the overlong-Hello case appends to the type byte. */
constexpr std::size_t OverlongHelloPayloadCapacity = 3;
/** Capacity for an eight-byte buffer the oversize-payload case rejects before any write. */
constexpr std::size_t OversizeRejectBufferCapacity = 8;
/** Capacity for the maximum-size control message buffer the unknown-type case writes into. */
constexpr std::size_t ControlMessageBufferCapacity = MessageHeaderBytes + MaxControlPayloadBytes;
/** Declared length that exceeds the u16 length field so the oversize case rejects it before any read. */
constexpr std::size_t OversizeDeclaredLength = 0x10000;

/** Sentinel channel byte pre-loaded into the header so an unchanged failed read is observable. */
constexpr std::uint8_t UntouchedChannelByte = 0xEE;
/** Sentinel payload bytes value pre-loaded into the header so an unchanged failed read is observable. */
constexpr std::uint16_t UntouchedPayloadBytes = 0xEEEE;
/** Payload byte value the single-byte round-trip case delivers. */
constexpr std::uint8_t SingleBytePayloadValue = 0xAB;
/** Trailing version byte the overlong-Hello case appends to force a rejection. */
constexpr std::uint8_t OverlongHelloTrailingByteA = 0x01;
/** Trailing second byte the overlong-Hello case appends to force a rejection. */
constexpr std::uint8_t OverlongHelloTrailingByteB = 0x02;
/** Two payload bytes the truncated-header case hands to ReadMessage, shorter than a header. */
constexpr std::uint8_t TruncatedHeaderMessage[TruncatedHeaderBufferCapacity] = {0x07, 0x00, 0x01};
/** Four payload bytes the nonzero-flags case hands to ReadMessage, with the flags byte at offset one. */
constexpr std::uint8_t NonzeroFlagsMessage[HeaderOnlyBufferCapacity] = {0x07, 0x01, 0x00, 0x00};
/** Six bytes declaring PayloadBytes=5 but supplying only two payload bytes (size mismatch). */
constexpr std::uint8_t SizeMismatchMessage[HeaderPlusTwoByteCapacity] = {0x07, 0x00, 0x05, 0x00, 0xAA, 0xBB};
/** Single-byte payload the unknown-control-type case hands to ReadControlMessage. */
constexpr std::uint8_t UnknownControlTypePayload[SingleBytePayloadCapacity] = {0x07};
/** Three bytes: Hello type byte, version, and an unexpected trailing byte (overlong). */
constexpr std::uint8_t OverlongHelloPayload[OverlongHelloPayloadCapacity] = {
	static_cast<std::uint8_t>(EControlMessageType::Hello), OverlongHelloTrailingByteA, OverlongHelloTrailingByteB};
/** Three bytes: Welcome type byte plus two of the three fields (truncated Welcome). */
constexpr std::uint8_t TruncatedWelcomePayload[3] = {static_cast<std::uint8_t>(EControlMessageType::Welcome), 0x01, 0x02};
/** Single-byte payload: Hello type byte with no version byte (truncated Hello). */
constexpr std::uint8_t TruncatedHelloPayload[SingleBytePayloadCapacity] = {static_cast<std::uint8_t>(EControlMessageType::Hello)};
/** Single-byte payload value the single-byte round-trip case writes. */
constexpr std::uint8_t SingleBytePayloadBytes[SingleBytePayloadCapacity] = {SingleBytePayloadValue};
/** Two payload bytes the Full-buffer case pairs with an undersized destination buffer. */
constexpr std::uint8_t FullBufferPayloadBytes[TwoBytePayloadCapacity] = {0xAA, 0xBB};
/** Four payload bytes the multi-byte round-trip case delivers in order. */
constexpr std::uint8_t MultiBytePayloadBytes[FourBytePayloadCapacity] = {0x01, 0x02, 0x03, 0x04};
/** Single-byte bogus payload storage the oversize case pairs with a huge declared length. */
constexpr std::uint8_t SmallPayloadStorage[SingleByteStorageCapacity] = {0x00};
/** Unknown control type byte (no defined message) the unknown-type case writes. */
constexpr std::uint8_t UnknownControlTypeByte = 0x09;
/** Hello protocol version the Hello round-trip case encodes and decodes. */
constexpr std::uint8_t HelloProtocolVersion = 5;
/** Welcome protocol version the Welcome round-trip case encodes and decodes. */
constexpr std::uint8_t WelcomeProtocolVersion = 9;
/** Welcome peer index the Welcome round-trip case encodes and decodes. */
constexpr std::uint8_t WelcomePeerIndex = 3;
/** Welcome peer generation the Welcome round-trip case encodes and decodes. */
constexpr std::uint8_t WelcomePeerGeneration = 7;

/**
 * Scenario: Write an application message with an empty payload, then read it back.
 * Expected: The round trip preserves the channel byte, reports zero flags, zero payload bytes, and an empty payload view.
 */
MW_TEST_CASE(TransportProtocolRoundTripsApplicationMessageWithEmptyPayload)
{
	// Arrange
	std::uint8_t Buffer[HeaderOnlyBufferCapacity] = {};
	FByteWriter Writer(TSpan<std::uint8_t>(Buffer, sizeof(Buffer)));

	// Act - write an empty-payload message
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		WriteMessage(Writer, ApplicationChannel, TSpan<const std::uint8_t>(nullptr, 0)),
		"Empty-payload write must succeed");
	MW_EXPECT_EQ(Test, MessageHeaderBytes, Writer.Position(), "Empty payload must write only the four-byte header");

	FMessageHeader Header{};
	TSpan<const std::uint8_t> Payload{};
	// Act / Assert - read it back
	MW_EXPECT_EQ(Test, ETransportResult::Success, ReadMessage(Writer.WrittenBytes(), Header, Payload), "Empty-payload read must succeed");
	MW_EXPECT_EQ(Test, ApplicationChannel, Header.Channel, "Round trip must preserve the channel byte");
	MW_EXPECT_EQ(Test, std::uint8_t{0}, Header.Flags, "Round trip must report zero flags");
	MW_EXPECT_EQ(Test, std::uint16_t{0}, Header.PayloadBytes, "Empty-payload round trip must report zero payload bytes");
	MW_EXPECT_EQ(Test, std::size_t{0}, Payload.Size(), "Empty-payload round trip must expose an empty payload view");
}

/**
 * Scenario: Write an application message with a single-byte payload, then read it back.
 * Expected: The round trip preserves the channel byte, reports one payload byte, and preserves the payload byte value.
 */
MW_TEST_CASE(TransportProtocolRoundTripsApplicationMessageWithOneBytePayload)
{
	// Arrange
	std::uint8_t Buffer[HeaderPlusOneByteCapacity] = {};
	FByteWriter Writer(TSpan<std::uint8_t>(Buffer, sizeof(Buffer)));

	// Act
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		WriteMessage(Writer, ApplicationChannel, TSpan<const std::uint8_t>(SingleBytePayloadBytes, sizeof(SingleBytePayloadBytes))),
		"One-byte payload write must succeed");

	FMessageHeader Header{};
	TSpan<const std::uint8_t> Payload{};
	// Act / Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, ReadMessage(Writer.WrittenBytes(), Header, Payload), "One-byte payload read must succeed");
	MW_EXPECT_EQ(Test, ApplicationChannel, Header.Channel, "Round trip must preserve the channel byte");
	MW_EXPECT_EQ(Test, std::uint16_t{1}, Header.PayloadBytes, "Round trip must report one payload byte");
	MW_EXPECT_EQ(Test, SingleBytePayloadCapacity, Payload.Size(), "Round trip must expose one payload byte");
	MW_EXPECT_EQ(Test, SingleBytePayloadValue, Payload[0], "Round trip must preserve the payload byte");
}

/**
 * Scenario: Write an application message with a four-byte payload, then read it back.
 * Expected: The round trip preserves the channel byte, reports four payload bytes, and preserves every payload byte in order.
 */
MW_TEST_CASE(TransportProtocolRoundTripsApplicationMessageWithMultiBytePayload)
{
	// Arrange
	std::uint8_t Buffer[HeaderPlusFourByteCapacity] = {};
	FByteWriter Writer(TSpan<std::uint8_t>(Buffer, sizeof(Buffer)));

	// Act
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		WriteMessage(Writer, DistinctChannel, TSpan<const std::uint8_t>(MultiBytePayloadBytes, sizeof(MultiBytePayloadBytes))),
		"Multi-byte payload write must succeed");

	FMessageHeader Header{};
	TSpan<const std::uint8_t> Payload{};
	// Act / Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, ReadMessage(Writer.WrittenBytes(), Header, Payload), "Multi-byte payload read must succeed");
	MW_EXPECT_EQ(Test, DistinctChannel, Header.Channel, "Round trip must preserve the channel byte");
	MW_EXPECT_EQ(Test, std::uint16_t{4}, Header.PayloadBytes, "Round trip must report four payload bytes");
	MW_EXPECT_EQ(Test, sizeof(MultiBytePayloadBytes), Payload.Size(), "Round trip must expose the full payload size");
	for (std::size_t Index = 0; Index < sizeof(MultiBytePayloadBytes); ++Index)
	{
		MW_EXPECT_EQ(Test, MultiBytePayloadBytes[Index], Payload[Index], "Round trip must preserve every payload byte in order");
	}
}

/**
 * Scenario: Attempt to write a header plus a two-byte payload into a four-byte buffer.
 * Expected: The write returns Full and does not advance the cursor.
 */
MW_TEST_CASE(TransportProtocolWriteMessageFullLeavesWriterUntouched)
{
	// Arrange - a 4-byte buffer cannot hold a 4-byte header plus a 2-byte payload.
	std::uint8_t Buffer[UndersizedBufferCapacity] = {};
	FByteWriter Writer(TSpan<std::uint8_t>(Buffer, sizeof(Buffer)));

	// Act / Assert
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Full,
		WriteMessage(Writer, ApplicationChannel, TSpan<const std::uint8_t>(FullBufferPayloadBytes, sizeof(FullBufferPayloadBytes))),
		"Write into an undersized buffer must return Full");
	MW_EXPECT_EQ(Test, std::size_t{0}, Writer.Position(), "Full write must not advance the cursor");
}

/**
 * Scenario: Attempt to write a payload whose declared length exceeds the u16 length field.
 * Expected: The write returns Invalid before any read and does not advance the cursor.
 */
MW_TEST_CASE(TransportProtocolWriteMessageRejectsOversizedPayload)
{
	// Arrange
	std::uint8_t Buffer[OversizeRejectBufferCapacity] = {};
	FByteWriter Writer(TSpan<std::uint8_t>(Buffer, sizeof(Buffer)));

	// Act - a bogus 0x10000-byte payload exceeds the u16 length field; it must be rejected before any read.
	const ETransportResult Result = WriteMessage(Writer, ApplicationChannel, TSpan<const std::uint8_t>(SmallPayloadStorage, OversizeDeclaredLength));
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, Result, "A payload larger than 0xFFFF must return Invalid");
	MW_EXPECT_EQ(Test, std::size_t{0}, Writer.Position(), "Oversized write must not advance the cursor");
}

/**
 * Scenario: Attempt to read a message shorter than a header with sentinel-loaded outputs.
 * Expected: The read returns Invalid and leaves OutHeader and OutPayload untouched.
 */
MW_TEST_CASE(TransportProtocolReadMessageRejectsTruncatedHeader)
{
	// Arrange
	FMessageHeader Header{UntouchedChannelByte, UntouchedChannelByte, UntouchedPayloadBytes};
	TSpan<const std::uint8_t> Payload{};
	const TSpan<const std::uint8_t> Message(TruncatedHeaderMessage, sizeof(TruncatedHeaderMessage));
	// Act
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, ReadMessage(Message, Header, Payload), "A sub-header-length message must return Invalid");
	// Assert
	MW_EXPECT_EQ(Test, UntouchedChannelByte, Header.Channel, "Invalid read must leave OutHeader.Channel unchanged");
	MW_EXPECT_EQ(Test, UntouchedPayloadBytes, Header.PayloadBytes, "Invalid read must leave OutHeader.PayloadBytes unchanged");
	MW_EXPECT_EQ(Test, std::size_t{0}, Payload.Size(), "Invalid read must leave OutPayload unchanged");
}

/**
 * Scenario: Attempt to read a message whose declared length disagrees with the actual payload bytes that follow.
 * Expected: The read returns Invalid.
 */
MW_TEST_CASE(TransportProtocolReadMessageRejectsPayloadSizeMismatch)
{
	// Arrange - header declares PayloadBytes=5 but only 2 payload bytes follow.
	FMessageHeader Header{};
	TSpan<const std::uint8_t> Payload{};
	// Act / Assert
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Invalid,
		ReadMessage(TSpan<const std::uint8_t>(SizeMismatchMessage, sizeof(SizeMismatchMessage)), Header, Payload),
		"A payload size mismatch must return Invalid");
}

/**
 * Scenario: Attempt to read an otherwise valid empty-payload message whose Flags byte is nonzero.
 * Expected: The read returns Invalid.
 */
MW_TEST_CASE(TransportProtocolReadMessageRejectsNonzeroFlags)
{
	// Arrange - flags byte (offset 1) is 0x01; the rest is a valid empty-payload header.
	FMessageHeader Header{};
	TSpan<const std::uint8_t> Payload{};
	// Act / Assert
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Invalid,
		ReadMessage(TSpan<const std::uint8_t>(NonzeroFlagsMessage, sizeof(NonzeroFlagsMessage)), Header, Payload),
		"A nonzero flags byte must return Invalid");
}

/**
 * Scenario: Write a Hello control message, read it back as a message, then decode its control payload.
 * Expected: The frame rides on the control channel and decodes back to the Hello type with the matching protocol version.
 */
MW_TEST_CASE(TransportProtocolRoundTripsHelloControlMessage)
{
	// Arrange
	std::uint8_t Buffer[HeaderPlusTwoByteCapacity] = {};
	FByteWriter Writer(TSpan<std::uint8_t>(Buffer, sizeof(Buffer)));

	FControlMessage Outgoing{};
	Outgoing.Type = EControlMessageType::Hello;
	Outgoing.ProtocolVersion = HelloProtocolVersion;
	// Act
	MW_EXPECT_EQ(Test, ETransportResult::Success, WriteControlMessage(Writer, Outgoing), "Hello write must succeed");

	FMessageHeader Header{};
	TSpan<const std::uint8_t> Payload{};
	// Act / Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, ReadMessage(Writer.WrittenBytes(), Header, Payload), "Hello frame read must succeed");
	MW_EXPECT_EQ(Test, ControlChannel, Header.Channel, "Control messages must ride on the control channel");

	FControlMessage Decoded{};
	// Act / Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, ReadControlMessage(Payload, Decoded), "Hello payload decode must succeed");
	MW_EXPECT_EQ(Test, EControlMessageType::Hello, Decoded.Type, "Decoded type must be Hello");
	MW_EXPECT_EQ(Test, HelloProtocolVersion, Decoded.ProtocolVersion, "Decoded protocol version must match");
}

/**
 * Scenario: Write a Welcome control message, read it back as a message, then decode its control payload.
 * Expected: The frame rides on the control channel and decodes back to the Welcome type with the matching protocol version, peer index, and peer
 * generation.
 */
MW_TEST_CASE(TransportProtocolRoundTripsWelcomeControlMessage)
{
	// Arrange
	std::uint8_t Buffer[HeaderPlusFourByteCapacity] = {};
	FByteWriter Writer(TSpan<std::uint8_t>(Buffer, sizeof(Buffer)));

	FControlMessage Outgoing{};
	Outgoing.Type = EControlMessageType::Welcome;
	Outgoing.ProtocolVersion = WelcomeProtocolVersion;
	Outgoing.PeerIndex = WelcomePeerIndex;
	Outgoing.PeerGeneration = WelcomePeerGeneration;
	// Act
	MW_EXPECT_EQ(Test, ETransportResult::Success, WriteControlMessage(Writer, Outgoing), "Welcome write must succeed");

	FMessageHeader Header{};
	TSpan<const std::uint8_t> Payload{};
	// Act / Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, ReadMessage(Writer.WrittenBytes(), Header, Payload), "Welcome frame read must succeed");
	MW_EXPECT_EQ(Test, ControlChannel, Header.Channel, "Control messages must ride on the control channel");

	FControlMessage Decoded{};
	// Act / Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, ReadControlMessage(Payload, Decoded), "Welcome payload decode must succeed");
	MW_EXPECT_EQ(Test, EControlMessageType::Welcome, Decoded.Type, "Decoded type must be Welcome");
	MW_EXPECT_EQ(Test, WelcomeProtocolVersion, Decoded.ProtocolVersion, "Decoded protocol version must match");
	MW_EXPECT_EQ(Test, WelcomePeerIndex, Decoded.PeerIndex, "Decoded peer index must match");
	MW_EXPECT_EQ(Test, WelcomePeerGeneration, Decoded.PeerGeneration, "Decoded peer generation must match");
}

/**
 * Scenario: Write a Heartbeat control message, read it back as a message, then decode its control payload.
 * Expected: The frame rides on the control channel and decodes back to the Heartbeat type.
 */
MW_TEST_CASE(TransportProtocolRoundTripsHeartbeatControlMessage)
{
	// Arrange
	std::uint8_t Buffer[HeaderPlusOneByteCapacity] = {};
	FByteWriter Writer(TSpan<std::uint8_t>(Buffer, sizeof(Buffer)));

	FControlMessage Outgoing{};
	Outgoing.Type = EControlMessageType::Heartbeat;
	// Act
	MW_EXPECT_EQ(Test, ETransportResult::Success, WriteControlMessage(Writer, Outgoing), "Heartbeat write must succeed");

	FMessageHeader Header{};
	TSpan<const std::uint8_t> Payload{};
	// Act / Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, ReadMessage(Writer.WrittenBytes(), Header, Payload), "Heartbeat frame read must succeed");
	MW_EXPECT_EQ(Test, ControlChannel, Header.Channel, "Control messages must ride on the control channel");

	FControlMessage Decoded{};
	// Act / Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, ReadControlMessage(Payload, Decoded), "Heartbeat payload decode must succeed");
	MW_EXPECT_EQ(Test, EControlMessageType::Heartbeat, Decoded.Type, "Decoded type must be Heartbeat");
}

/**
 * Scenario: Write a Bye control message, read it back as a message, then decode its control payload.
 * Expected: The frame rides on the control channel and decodes back to the Bye type.
 */
MW_TEST_CASE(TransportProtocolRoundTripsByeControlMessage)
{
	// Arrange
	std::uint8_t Buffer[HeaderPlusOneByteCapacity] = {};
	FByteWriter Writer(TSpan<std::uint8_t>(Buffer, sizeof(Buffer)));

	FControlMessage Outgoing{};
	Outgoing.Type = EControlMessageType::Bye;
	// Act
	MW_EXPECT_EQ(Test, ETransportResult::Success, WriteControlMessage(Writer, Outgoing), "Bye write must succeed");

	FMessageHeader Header{};
	TSpan<const std::uint8_t> Payload{};
	// Act / Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, ReadMessage(Writer.WrittenBytes(), Header, Payload), "Bye frame read must succeed");
	MW_EXPECT_EQ(Test, ControlChannel, Header.Channel, "Control messages must ride on the control channel");

	FControlMessage Decoded{};
	// Act / Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, ReadControlMessage(Payload, Decoded), "Bye payload decode must succeed");
	MW_EXPECT_EQ(Test, EControlMessageType::Bye, Decoded.Type, "Decoded type must be Bye");
}

/**
 * Scenario: Attempt to decode a payload whose type byte names no defined control message.
 * Expected: The decode returns Invalid and leaves OutMessage unchanged.
 */
MW_TEST_CASE(TransportProtocolReadControlMessageRejectsUnknownType)
{
	// Arrange - type byte 0x07 names no defined control message; the single-byte payload is otherwise well-formed.
	FControlMessage Decoded{};
	Decoded.Type = EControlMessageType::Bye;
	// Act / Assert
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Invalid,
		ReadControlMessage(TSpan<const std::uint8_t>(UnknownControlTypePayload, sizeof(UnknownControlTypePayload)), Decoded),
		"An unknown control type byte must return Invalid");
	MW_EXPECT_EQ(Test, EControlMessageType::Bye, Decoded.Type, "Invalid decode must leave OutMessage unchanged");
}

/**
 * Scenario: Attempt to decode a Hello payload whose version byte is missing.
 * Expected: The decode returns Invalid.
 */
MW_TEST_CASE(TransportProtocolReadControlMessageRejectsTruncatedHello)
{
	// Arrange - Hello declared by the type byte but the version byte is missing.
	FControlMessage Decoded{};
	// Act / Assert
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Invalid,
		ReadControlMessage(TSpan<const std::uint8_t>(TruncatedHelloPayload, sizeof(TruncatedHelloPayload)), Decoded),
		"A Hello payload missing its version byte must return Invalid");
}

/**
 * Scenario: Attempt to decode a Hello payload carrying an unexpected trailing byte after the version.
 * Expected: The decode returns Invalid.
 */
MW_TEST_CASE(TransportProtocolReadControlMessageRejectsOverlongHello)
{
	// Arrange - Hello type byte plus version plus an unexpected third byte.
	FControlMessage Decoded{};
	// Act / Assert
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Invalid,
		ReadControlMessage(TSpan<const std::uint8_t>(OverlongHelloPayload, sizeof(OverlongHelloPayload)), Decoded),
		"An overlong Hello payload must return Invalid");
}

/**
 * Scenario: Attempt to decode a Welcome payload missing one of its three fields bytes.
 * Expected: The decode returns Invalid.
 */
MW_TEST_CASE(TransportProtocolReadControlMessageRejectsTruncatedWelcome)
{
	// Arrange - Welcome declared by the type byte but only two of the three fields follow.
	FControlMessage Decoded{};
	// Act / Assert
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Invalid,
		ReadControlMessage(TSpan<const std::uint8_t>(TruncatedWelcomePayload, sizeof(TruncatedWelcomePayload)), Decoded),
		"A truncated Welcome payload must return Invalid");
}

/**
 * Scenario: Attempt to decode an empty control payload.
 * Expected: The decode returns Invalid and leaves OutMessage unchanged.
 */
MW_TEST_CASE(TransportProtocolReadControlMessageRejectsEmptyPayload)
{
	// Arrange
	FControlMessage Decoded{};
	Decoded.Type = EControlMessageType::Heartbeat;
	// Act / Assert
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Invalid,
		ReadControlMessage(TSpan<const std::uint8_t>(nullptr, 0), Decoded),
		"An empty control payload must return Invalid");
	MW_EXPECT_EQ(Test, EControlMessageType::Heartbeat, Decoded.Type, "Invalid decode of an empty payload must leave OutMessage unchanged");
}

/**
 * Scenario: Attempt to write a control message with an unknown type byte.
 * Expected: The write returns Invalid and does not advance the cursor.
 */
MW_TEST_CASE(TransportProtocolWriteControlMessageRejectsUnknownType)
{
	// Arrange
	std::uint8_t Buffer[ControlMessageBufferCapacity] = {};
	FByteWriter Writer(TSpan<std::uint8_t>(Buffer, sizeof(Buffer)));

	FControlMessage Outgoing{};
	Outgoing.Type = static_cast<EControlMessageType>(UnknownControlTypeByte);
	// Act / Assert
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, WriteControlMessage(Writer, Outgoing), "An unknown control type must return Invalid");
	MW_EXPECT_EQ(Test, std::size_t{0}, Writer.Position(), "An unknown-type write must not advance the cursor");
}

} // namespace
