#include "TestSupport.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Net/ByteWriter.h>
#include <MicroWorld/Net/NetProtocol.h>
#include <MicroWorld/Net/NetResult.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::ControlChannel;
using MicroWorld::EControlMessageType;
using MicroWorld::ENetResult;
using MicroWorld::FByteWriter;
using MicroWorld::FControlMessage;
using MicroWorld::FMessageHeader;
using MicroWorld::MaxControlPayloadBytes;
using MicroWorld::MessageHeaderBytes;
using MicroWorld::ReadControlMessage;
using MicroWorld::ReadMessage;
using MicroWorld::TSpan;
using MicroWorld::WriteControlMessage;
using MicroWorld::WriteMessage;

/** Proves WriteMessage and ReadMessage round-trip an application message with an empty payload. */
MW_TEST_CASE(NetProtocolRoundTripsApplicationMessageWithEmptyPayload)
{
	std::array<std::uint8_t, MessageHeaderBytes> Buffer{};
	FByteWriter Writer(TSpan<std::uint8_t>(Buffer.data(), Buffer.size()));

	MW_EXPECT_EQ(
		Test, ENetResult::Success, WriteMessage(Writer, std::uint8_t{7}, TSpan<const std::uint8_t>(nullptr, 0)), "Empty-payload write must succeed");
	MW_EXPECT_EQ(Test, MessageHeaderBytes, Writer.Position(), "Empty payload must write only the four-byte header");

	FMessageHeader Header{};
	TSpan<const std::uint8_t> Payload{};
	MW_EXPECT_EQ(Test, ENetResult::Success, ReadMessage(Writer.WrittenBytes(), Header, Payload), "Empty-payload read must succeed");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(7), Header.Channel, "Round trip must preserve the channel byte");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0), Header.Flags, "Round trip must report zero flags");
	MW_EXPECT_EQ(Test, static_cast<std::uint16_t>(0), Header.PayloadBytes, "Empty-payload round trip must report zero payload bytes");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Payload.Size(), "Empty-payload round trip must expose an empty payload view");
}

/** Proves WriteMessage and ReadMessage round-trip a single-byte application payload. */
MW_TEST_CASE(NetProtocolRoundTripsApplicationMessageWithOneBytePayload)
{
	std::array<std::uint8_t, MessageHeaderBytes + 1> Buffer{};
	FByteWriter Writer(TSpan<std::uint8_t>(Buffer.data(), Buffer.size()));

	const std::array<std::uint8_t, 1> PayloadBytes{0xAB};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		WriteMessage(Writer, std::uint8_t{7}, TSpan<const std::uint8_t>(PayloadBytes.data(), PayloadBytes.size())),
		"One-byte payload write must succeed");

	FMessageHeader Header{};
	TSpan<const std::uint8_t> Payload{};
	MW_EXPECT_EQ(Test, ENetResult::Success, ReadMessage(Writer.WrittenBytes(), Header, Payload), "One-byte payload read must succeed");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(7), Header.Channel, "Round trip must preserve the channel byte");
	MW_EXPECT_EQ(Test, static_cast<std::uint16_t>(1), Header.PayloadBytes, "Round trip must report one payload byte");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Payload.Size(), "Round trip must expose one payload byte");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xAB), Payload[0], "Round trip must preserve the payload byte");
}

/** Proves WriteMessage and ReadMessage round-trip a multi-byte application payload. */
MW_TEST_CASE(NetProtocolRoundTripsApplicationMessageWithMultiBytePayload)
{
	std::array<std::uint8_t, MessageHeaderBytes + 4> Buffer{};
	FByteWriter Writer(TSpan<std::uint8_t>(Buffer.data(), Buffer.size()));

	const std::array<std::uint8_t, 4> PayloadBytes{0x01, 0x02, 0x03, 0x04};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		WriteMessage(Writer, std::uint8_t{42}, TSpan<const std::uint8_t>(PayloadBytes.data(), PayloadBytes.size())),
		"Multi-byte payload write must succeed");

	FMessageHeader Header{};
	TSpan<const std::uint8_t> Payload{};
	MW_EXPECT_EQ(Test, ENetResult::Success, ReadMessage(Writer.WrittenBytes(), Header, Payload), "Multi-byte payload read must succeed");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(42), Header.Channel, "Round trip must preserve the channel byte");
	MW_EXPECT_EQ(Test, static_cast<std::uint16_t>(4), Header.PayloadBytes, "Round trip must report four payload bytes");
	MW_EXPECT_EQ(Test, PayloadBytes.size(), Payload.Size(), "Round trip must expose the full payload size");
	for (std::size_t Index = 0; Index < PayloadBytes.size(); ++Index)
	{
		MW_EXPECT_EQ(Test, PayloadBytes[Index], Payload[Index], "Round trip must preserve every payload byte in order");
	}
}

/** Proves WriteMessage returns Full and writes nothing when the buffer cannot hold the header plus payload. */
MW_TEST_CASE(NetProtocolWriteMessageFullLeavesWriterUntouched)
{
	std::array<std::uint8_t, 4> Buffer{};
	FByteWriter Writer(TSpan<std::uint8_t>(Buffer.data(), Buffer.size()));

	const std::array<std::uint8_t, 2> PayloadBytes{0xAA, 0xBB};
	// A 4-byte buffer cannot hold a 4-byte header plus a 2-byte payload.
	MW_EXPECT_EQ(
		Test,
		ENetResult::Full,
		WriteMessage(Writer, std::uint8_t{7}, TSpan<const std::uint8_t>(PayloadBytes.data(), PayloadBytes.size())),
		"Write into an undersized buffer must return Full");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Writer.Position(), "Full write must not advance the cursor");
}

/** Proves WriteMessage rejects an oversized payload before any write and leaves the writer untouched. */
MW_TEST_CASE(NetProtocolWriteMessageRejectsOversizedPayload)
{
	std::array<std::uint8_t, 8> Buffer{};
	FByteWriter Writer(TSpan<std::uint8_t>(Buffer.data(), Buffer.size()));

	// A bogus 0x10000-byte payload exceeds the u16 length field; it must be rejected before any read.
	std::array<std::uint8_t, 1> SmallStorage{0x00};
	const ENetResult Result = WriteMessage(Writer, std::uint8_t{7}, TSpan<const std::uint8_t>(SmallStorage.data(), 0x10000));
	MW_EXPECT_EQ(Test, ENetResult::Invalid, Result, "A payload larger than 0xFFFF must return Invalid");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Writer.Position(), "Oversized write must not advance the cursor");
}

/** Proves ReadMessage rejects a too-short header without touching its outputs. */
MW_TEST_CASE(NetProtocolReadMessageRejectsTruncatedHeader)
{
	const std::array<std::uint8_t, 3> TooShort{0x07, 0x00, 0x01};
	FMessageHeader Header{std::uint8_t{0xEE}, std::uint8_t{0xEE}, std::uint16_t{0xEEEE}};
	TSpan<const std::uint8_t> Payload{};
	const TSpan<const std::uint8_t> Message(TooShort.data(), TooShort.size());
	MW_EXPECT_EQ(Test, ENetResult::Invalid, ReadMessage(Message, Header, Payload), "A sub-header-length message must return Invalid");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xEE), Header.Channel, "Invalid read must leave OutHeader.Channel unchanged");
	MW_EXPECT_EQ(Test, static_cast<std::uint16_t>(0xEEEE), Header.PayloadBytes, "Invalid read must leave OutHeader.PayloadBytes unchanged");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Payload.Size(), "Invalid read must leave OutPayload unchanged");
}

/** Proves ReadMessage rejects a message whose declared length disagrees with the actual payload. */
MW_TEST_CASE(NetProtocolReadMessageRejectsPayloadSizeMismatch)
{
	// Header declares PayloadBytes=5 but only 2 payload bytes follow.
	const std::array<std::uint8_t, MessageHeaderBytes + 2> Message{0x07, 0x00, 0x05, 0x00, 0xAA, 0xBB};
	FMessageHeader Header{};
	TSpan<const std::uint8_t> Payload{};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Invalid,
		ReadMessage(TSpan<const std::uint8_t>(Message.data(), Message.size()), Header, Payload),
		"A payload size mismatch must return Invalid");
}

/** Proves ReadMessage rejects a message whose Flags byte is nonzero. */
MW_TEST_CASE(NetProtocolReadMessageRejectsNonzeroFlags)
{
	// Flags byte (offset 1) is 0x01; the rest is a valid empty-payload header.
	const std::array<std::uint8_t, MessageHeaderBytes> Message{0x07, 0x01, 0x00, 0x00};
	FMessageHeader Header{};
	TSpan<const std::uint8_t> Payload{};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Invalid,
		ReadMessage(TSpan<const std::uint8_t>(Message.data(), Message.size()), Header, Payload),
		"A nonzero flags byte must return Invalid");
}

/** Proves a Hello control message round-trips through WriteControlMessage, ReadMessage, and ReadControlMessage. */
MW_TEST_CASE(NetProtocolRoundTripsHelloControlMessage)
{
	std::array<std::uint8_t, MessageHeaderBytes + 2> Buffer{};
	FByteWriter Writer(TSpan<std::uint8_t>(Buffer.data(), Buffer.size()));

	FControlMessage Outgoing{};
	Outgoing.Type = EControlMessageType::Hello;
	Outgoing.ProtocolVersion = 5;
	MW_EXPECT_EQ(Test, ENetResult::Success, WriteControlMessage(Writer, Outgoing), "Hello write must succeed");

	FMessageHeader Header{};
	TSpan<const std::uint8_t> Payload{};
	MW_EXPECT_EQ(Test, ENetResult::Success, ReadMessage(Writer.WrittenBytes(), Header, Payload), "Hello frame read must succeed");
	MW_EXPECT_EQ(Test, ControlChannel, Header.Channel, "Control messages must ride on the control channel");

	FControlMessage Decoded{};
	MW_EXPECT_EQ(Test, ENetResult::Success, ReadControlMessage(Payload, Decoded), "Hello payload decode must succeed");
	MW_EXPECT_EQ(Test, EControlMessageType::Hello, Decoded.Type, "Decoded type must be Hello");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(5), Decoded.ProtocolVersion, "Decoded protocol version must match");
}

/** Proves a Welcome control message round-trips with all three fields preserved. */
MW_TEST_CASE(NetProtocolRoundTripsWelcomeControlMessage)
{
	std::array<std::uint8_t, MessageHeaderBytes + 4> Buffer{};
	FByteWriter Writer(TSpan<std::uint8_t>(Buffer.data(), Buffer.size()));

	FControlMessage Outgoing{};
	Outgoing.Type = EControlMessageType::Welcome;
	Outgoing.ProtocolVersion = 9;
	Outgoing.PeerIndex = 3;
	Outgoing.PeerGeneration = 7;
	MW_EXPECT_EQ(Test, ENetResult::Success, WriteControlMessage(Writer, Outgoing), "Welcome write must succeed");

	FMessageHeader Header{};
	TSpan<const std::uint8_t> Payload{};
	MW_EXPECT_EQ(Test, ENetResult::Success, ReadMessage(Writer.WrittenBytes(), Header, Payload), "Welcome frame read must succeed");
	MW_EXPECT_EQ(Test, ControlChannel, Header.Channel, "Control messages must ride on the control channel");

	FControlMessage Decoded{};
	MW_EXPECT_EQ(Test, ENetResult::Success, ReadControlMessage(Payload, Decoded), "Welcome payload decode must succeed");
	MW_EXPECT_EQ(Test, EControlMessageType::Welcome, Decoded.Type, "Decoded type must be Welcome");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(9), Decoded.ProtocolVersion, "Decoded protocol version must match");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(3), Decoded.PeerIndex, "Decoded peer index must match");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(7), Decoded.PeerGeneration, "Decoded peer generation must match");
}

/** Proves a Heartbeat control message round-trips with no payload fields. */
MW_TEST_CASE(NetProtocolRoundTripsHeartbeatControlMessage)
{
	std::array<std::uint8_t, MessageHeaderBytes + 1> Buffer{};
	FByteWriter Writer(TSpan<std::uint8_t>(Buffer.data(), Buffer.size()));

	FControlMessage Outgoing{};
	Outgoing.Type = EControlMessageType::Heartbeat;
	MW_EXPECT_EQ(Test, ENetResult::Success, WriteControlMessage(Writer, Outgoing), "Heartbeat write must succeed");

	FMessageHeader Header{};
	TSpan<const std::uint8_t> Payload{};
	MW_EXPECT_EQ(Test, ENetResult::Success, ReadMessage(Writer.WrittenBytes(), Header, Payload), "Heartbeat frame read must succeed");
	MW_EXPECT_EQ(Test, ControlChannel, Header.Channel, "Control messages must ride on the control channel");

	FControlMessage Decoded{};
	MW_EXPECT_EQ(Test, ENetResult::Success, ReadControlMessage(Payload, Decoded), "Heartbeat payload decode must succeed");
	MW_EXPECT_EQ(Test, EControlMessageType::Heartbeat, Decoded.Type, "Decoded type must be Heartbeat");
}

/** Proves a Bye control message round-trips with no payload fields. */
MW_TEST_CASE(NetProtocolRoundTripsByeControlMessage)
{
	std::array<std::uint8_t, MessageHeaderBytes + 1> Buffer{};
	FByteWriter Writer(TSpan<std::uint8_t>(Buffer.data(), Buffer.size()));

	FControlMessage Outgoing{};
	Outgoing.Type = EControlMessageType::Bye;
	MW_EXPECT_EQ(Test, ENetResult::Success, WriteControlMessage(Writer, Outgoing), "Bye write must succeed");

	FMessageHeader Header{};
	TSpan<const std::uint8_t> Payload{};
	MW_EXPECT_EQ(Test, ENetResult::Success, ReadMessage(Writer.WrittenBytes(), Header, Payload), "Bye frame read must succeed");
	MW_EXPECT_EQ(Test, ControlChannel, Header.Channel, "Control messages must ride on the control channel");

	FControlMessage Decoded{};
	MW_EXPECT_EQ(Test, ENetResult::Success, ReadControlMessage(Payload, Decoded), "Bye payload decode must succeed");
	MW_EXPECT_EQ(Test, EControlMessageType::Bye, Decoded.Type, "Decoded type must be Bye");
}

/** Proves ReadControlMessage rejects an unknown control type byte. */
MW_TEST_CASE(NetProtocolReadControlMessageRejectsUnknownType)
{
	// Type byte 0x07 names no defined control message; the single-byte payload is otherwise well-formed.
	const std::array<std::uint8_t, 1> PayloadBytes{0x07};
	FControlMessage Decoded{};
	Decoded.Type = EControlMessageType::Bye;
	MW_EXPECT_EQ(
		Test,
		ENetResult::Invalid,
		ReadControlMessage(TSpan<const std::uint8_t>(PayloadBytes.data(), PayloadBytes.size()), Decoded),
		"An unknown control type byte must return Invalid");
	MW_EXPECT_EQ(Test, EControlMessageType::Bye, Decoded.Type, "Invalid decode must leave OutMessage unchanged");
}

/** Proves ReadControlMessage rejects a malformed Hello payload: too short (missing version). */
MW_TEST_CASE(NetProtocolReadControlMessageRejectsTruncatedHello)
{
	// Hello declared by the type byte but the version byte is missing.
	const std::array<std::uint8_t, 1> PayloadBytes{static_cast<std::uint8_t>(EControlMessageType::Hello)};
	FControlMessage Decoded{};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Invalid,
		ReadControlMessage(TSpan<const std::uint8_t>(PayloadBytes.data(), PayloadBytes.size()), Decoded),
		"A Hello payload missing its version byte must return Invalid");
}

/** Proves ReadControlMessage rejects a malformed Hello payload: too long (trailing byte). */
MW_TEST_CASE(NetProtocolReadControlMessageRejectsOverlongHello)
{
	// Hello type byte plus version plus an unexpected third byte.
	const std::array<std::uint8_t, 3> PayloadBytes{static_cast<std::uint8_t>(EControlMessageType::Hello), 0x01, 0x02};
	FControlMessage Decoded{};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Invalid,
		ReadControlMessage(TSpan<const std::uint8_t>(PayloadBytes.data(), PayloadBytes.size()), Decoded),
		"An overlong Hello payload must return Invalid");
}

/** Proves ReadControlMessage rejects a truncated Welcome payload. */
MW_TEST_CASE(NetProtocolReadControlMessageRejectsTruncatedWelcome)
{
	// Welcome declared by the type byte but only two of the three fields follow.
	const std::array<std::uint8_t, 3> PayloadBytes{static_cast<std::uint8_t>(EControlMessageType::Welcome), 0x01, 0x02};
	FControlMessage Decoded{};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Invalid,
		ReadControlMessage(TSpan<const std::uint8_t>(PayloadBytes.data(), PayloadBytes.size()), Decoded),
		"A truncated Welcome payload must return Invalid");
}

/** Proves ReadControlMessage rejects an empty control payload. */
MW_TEST_CASE(NetProtocolReadControlMessageRejectsEmptyPayload)
{
	FControlMessage Decoded{};
	Decoded.Type = EControlMessageType::Heartbeat;
	MW_EXPECT_EQ(
		Test,
		ENetResult::Invalid,
		ReadControlMessage(TSpan<const std::uint8_t>(nullptr, 0), Decoded),
		"An empty control payload must return Invalid");
	MW_EXPECT_EQ(Test, EControlMessageType::Heartbeat, Decoded.Type, "Invalid decode of an empty payload must leave OutMessage unchanged");
}

/** Proves WriteControlMessage rejects an unknown type before touching the writer. */
MW_TEST_CASE(NetProtocolWriteControlMessageRejectsUnknownType)
{
	std::array<std::uint8_t, MessageHeaderBytes + MaxControlPayloadBytes> Buffer{};
	FByteWriter Writer(TSpan<std::uint8_t>(Buffer.data(), Buffer.size()));

	FControlMessage Outgoing{};
	Outgoing.Type = static_cast<EControlMessageType>(0x09);
	MW_EXPECT_EQ(Test, ENetResult::Invalid, WriteControlMessage(Writer, Outgoing), "An unknown control type must return Invalid");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Writer.Position(), "An unknown-type write must not advance the cursor");
}

} // namespace
