#include "TestSupport.h"

#include "MessagingSystemTestHelpers.h"

#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/ReceiveResult.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Messaging/ChannelInformation.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessageReader.h>
#include <MicroWorld/Messaging/MessageWriter.h>
#include <MicroWorld/Messaging/MessagingSystemInformation.h>
#include <MicroWorld/Messaging/TypedMessageCodec.h>

#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::Core::DurationMilliseconds;
using MicroWorld::Core::ETransportResult;
using MicroWorld::Core::FDeviceAddress;
using MicroWorld::Core::FReceiveResult;
using MicroWorld::Core::ITransportDevice;
using MicroWorld::Core::TimePointMilliseconds;
using MicroWorld::Core::TSpan;
using MicroWorld::Messaging::DecodeTypedMessage;
using MicroWorld::Messaging::EMessagingResult;
using MicroWorld::Messaging::FChannelInformation;
using MicroWorld::Messaging::FMessage;
using MicroWorld::Messaging::FMessageReader;
using MicroWorld::Messaging::FMessageWriter;
using MicroWorld::Messaging::FMessagingLinkId;
using MicroWorld::Messaging::FMessagingRoute;
using MicroWorld::Messaging::FMessagingSystem;
using MicroWorld::Messaging::FMessagingSystemInformation;
using MicroWorld::Messaging::FNameId;
using MicroWorld::Tests::FTestTransportDevice;

/**
 * Motivation: Supplies a small ADL codec subject whose fields verify every primitive reader and writer width used by Network protocol types.
 * Responsibilities: Hold two portable integer fields without allocation or behavior.
 * Example: FCodecProbe Message{7, 42};
 */
struct FCodecProbe final
{
	/** Motivation: Carries the two-byte test field. */
	std::uint16_t Sequence{0};

	/** Motivation: Carries the four-byte test field. */
	std::uint32_t Value{0};
};

/**
 * Motivation: Supplies the stable ADL message identity for FCodecProbe.
 * Responsibilities: Return the fixed name without mutation.
 */
constexpr FNameId GetMessageNameId(const FCodecProbe&) noexcept
{
	return "CodecProbe";
}

/**
 * Motivation: Exercises bounded typed-message writing.
 * Responsibilities: Encode both fields little-endian or return the first failure.
 */
EMessagingResult EncodeMessagePayload(const FCodecProbe& InMessage, FMessageWriter& InWriter) noexcept
{
	const EMessagingResult SequenceResult = InWriter.WriteU16(InMessage.Sequence);
	if (SequenceResult != EMessagingResult::Success)
	{
		return SequenceResult;
	}

	return InWriter.WriteU32(InMessage.Value);
}

/**
 * Motivation: Exercises bounded typed-message reading.
 * Responsibilities: Decode both fields or return the first failure.
 */
EMessagingResult DecodeMessagePayload(FMessageReader& InReader, FCodecProbe& OutMessage) noexcept
{
	const EMessagingResult SequenceResult = InReader.ReadU16(OutMessage.Sequence);
	if (SequenceResult != EMessagingResult::Success)
	{
		return SequenceResult;
	}

	return InReader.ReadU32(OutMessage.Value);
}

/**
 * Motivation: Supplies a codec that deliberately exceeds Messaging's fixed payload budget.
 * Responsibilities: Trigger the writer's Full result without retaining the temporary byte span.
 * Example: FOverflowCodecProbe Message;
 */
struct FOverflowCodecProbe final
{
};

/**
 * Motivation: Supplies the stable ADL message identity for the oversize codec probe.
 * Responsibilities: Return the fixed name without mutation.
 */
constexpr FNameId GetMessageNameId(const FOverflowCodecProbe&) noexcept
{
	return "OverflowCodecProbe";
}

/**
 * Motivation: Proves typed sends propagate codec capacity failures.
 * Responsibilities: Attempt one byte beyond the fixed writer capacity.
 */
EMessagingResult EncodeMessagePayload(const FOverflowCodecProbe&, FMessageWriter& InWriter) noexcept
{
	const std::uint8_t Bytes[FMessagingSystem::MaxMessageBytes + 1]{};
	return InWriter.WriteBytes(TSpan<const std::uint8_t>(Bytes, FMessagingSystem::MaxMessageBytes + 1));
}

/**
 * Motivation: Completes the exact typed-codec ADL contract for the oversize probe.
 * Responsibilities: Reject inbound decoding without mutation.
 */
EMessagingResult DecodeMessagePayload(FMessageReader&, FOverflowCodecProbe&) noexcept
{
	return EMessagingResult::Invalid;
}

/**
 * Motivation: Confirms channel configuration retains every supplied field for later system creation.
 * Responsibilities: Verify name, reliability, device pointer, and destination read back from the aggregate.
 */
MW_TEST_CASE(MessageTypes_ChannelInformationPreservesSuppliedFields)
{
	// Arrange
	FTestTransportDevice Device;
	FDeviceAddress Address{};
	Address.Bytes[0] = 7;
	Address.Size = 1;
	FChannelInformation Information{};

	// Act
	Information.ChannelNameId = "SensorA";
	Information.bIsReliable = true;
	Information.TransportDevice = &Device;
	Information.Address = Address;

	// Assert
	MW_EXPECT_EQ(Test, FNameId{"SensorA"}, Information.ChannelNameId, "The channel name should be preserved");
	MW_EXPECT_TRUE(Test, Information.bIsReliable, "The reliability option should be preserved");
	MW_EXPECT_EQ(Test, &Device, Information.TransportDevice, "The device pointer should be preserved");
	MW_EXPECT_EQ(Test, Address, Information.Address, "The address should be preserved");
}

/**
 * Motivation: Gives channel construction a safe local-only configuration without explicit initialization.
 * Responsibilities: Confirm every FChannelInformation default is its documented empty value.
 */
MW_TEST_CASE(MessageTypes_ChannelInformationDefaultsToLocalUnreliable)
{
	// Arrange
	const FChannelInformation Information{};

	// Act
	const bool bHasDefaultName = Information.ChannelNameId == FNameId{};

	// Assert
	MW_EXPECT_TRUE(Test, bHasDefaultName, "The default channel name should be unset");
	MW_EXPECT_TRUE(Test, !Information.bIsReliable, "The default channel should be unreliable");
	MW_EXPECT_EQ(Test, nullptr, Information.TransportDevice, "The default channel should have no device");
	MW_EXPECT_EQ(Test, std::uint8_t{0}, Information.Address.Size, "The default address should be empty");
}

/**
 * Motivation: Confirms FMessage preserves a non-owning payload view rather than allocating or copying bytes.
 * Responsibilities: Verify the message name and payload pointer and length read back unchanged.
 */
MW_TEST_CASE(MessageTypes_MessagePreservesNameAndPayloadView)
{
	// Arrange
	const std::uint8_t PayloadBytes[]{1, 2, 3};
	FMessage Message;
	Message.SetMessageNameId("SensorA_Temperature");

	// Act
	Message.SetPayload(TSpan<const std::uint8_t>{PayloadBytes});
	const TSpan<const std::uint8_t> Payload = Message.GetPayload();

	// Assert
	MW_EXPECT_EQ(Test, FNameId{"SensorA_Temperature"}, Message.GetMessageNameId(), "The message name should be preserved");
	MW_EXPECT_EQ(Test, PayloadBytes, Payload.Data(), "The payload pointer should be preserved");
	MW_EXPECT_EQ(Test, std::size_t{3}, Payload.Size(), "The payload length should be preserved");
}

/**
 * Motivation: Makes a default message safe to use before a caller provides message data.
 * Responsibilities: Confirm name, payload, and sender use their empty default values.
 */
MW_TEST_CASE(MessageTypes_MessageDefaultsToEmptyValues)
{
	// Arrange
	const FMessage Message{};

	// Act
	const TSpan<const std::uint8_t> Payload = Message.GetPayload();

	// Assert
	MW_EXPECT_EQ(Test, FNameId{}, Message.GetMessageNameId(), "The default message name should be unset");
	MW_EXPECT_TRUE(Test, Payload.IsEmpty(), "The default payload should be empty");
	MW_EXPECT_EQ(Test, std::uint8_t{0}, Message.GetSender().Size, "The default sender should be empty");
}

/**
 * Motivation: Preserves direct Messaging consumers' address-only sender context.
 * Responsibilities: Confirm a sender address is returned by reference with its value intact.
 */
MW_TEST_CASE(MessageTypes_MessageReturnsTheConfiguredSender)
{
	// Arrange
	FDeviceAddress Sender{};
	Sender.Bytes[0] = 9;
	Sender.Size = 1;
	FMessage Message;

	// Act
	Message.SetSenderContext(Sender);
	const FDeviceAddress& ReturnedSender = Message.GetSender();

	// Assert
	MW_EXPECT_EQ(Test, &ReturnedSender, &Message.GetSender(), "The sender getter should return a stable reference");
	MW_EXPECT_EQ(Test, Sender, ReturnedSender, "The sender address should be preserved");
}

/**
 * Motivation: Keeps complete inbound sender context coherent with the existing address getter and safely removable for local republishing.
 * Responsibilities: Confirm route context supplies its address and clearing it removes both public sender views.
 */
MW_TEST_CASE(MessageTypes_MessageSenderContextPreservesAndClearsInboundRoute)
{
	// Arrange
	FDeviceAddress Sender{};
	Sender.Bytes[0] = 9;
	Sender.Size = 1;
	const FMessagingRoute SenderRoute{FMessagingLinkId{0}, Sender};
	FMessage Message;

	// Act
	Message.SetSenderContext(SenderRoute);
	const FMessagingRoute RecordedRoute = Message.GetSenderRoute();
	const FDeviceAddress RecordedSender = Message.GetSender();
	Message.ClearSenderContext();

	// Assert
	MW_EXPECT_EQ(Test, SenderRoute, RecordedRoute, "Route context should preserve the inbound link and address");
	MW_EXPECT_EQ(Test, Sender, RecordedSender, "Route context should preserve the legacy sender address");
	MW_EXPECT_TRUE(Test, !Message.GetSenderRoute().IsValid(), "Clearing sender context should remove the inbound route");
	MW_EXPECT_EQ(Test, std::uint8_t{0}, Message.GetSender().Size, "Clearing sender context should clear the legacy address");
}

/**
 * Motivation: Keeps reliable delivery and inbound wire work bounded by visible default policies.
 * Responsibilities: Confirm system information uses the documented retry, attempt, and receive-budget defaults.
 */
MW_TEST_CASE(MessageTypes_SystemInformationUsesBoundedPolicyDefaults)
{
	// Arrange
	const FMessagingSystemInformation Information{};

	// Act
	const DurationMilliseconds RetryInterval = Information.ReliableRetryIntervalMilliseconds;

	// Assert
	MW_EXPECT_EQ(Test, DurationMilliseconds{200}, RetryInterval, "The retry interval should default to 200 milliseconds");
	MW_EXPECT_EQ(Test, std::uint8_t{8}, Information.MaxReliableSendAttempts, "The send attempt limit should default to eight");
	MW_EXPECT_EQ(
		Test, std::uint8_t{4}, Information.MaxReceiveFramesPerDevicePerAdvance, "The per-device receive budget should default to four frames");
}

/**
 * Motivation: Confirms Network protocol types can use the exact bounded ADL codec contract without a Messaging-owned registry.
 * Responsibilities: Decode a complete typed payload and preserve both primitive values.
 */
MW_TEST_CASE(MessageTypes_DecodeTypedMessageRoundTripsAnExactPayload)
{
	// Arrange
	const std::uint8_t Payload[]{0x34, 0x12, 0x78, 0x56, 0x34, 0x12};
	FMessage Message;
	Message.SetMessageNameId(GetMessageNameId(FCodecProbe{}));
	Message.SetPayload(TSpan<const std::uint8_t>{Payload});
	FCodecProbe Decoded{};

	// Act
	const EMessagingResult DecodeResult = DecodeTypedMessage(Message, Decoded);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, DecodeResult, "An exact typed payload should decode");
	MW_EXPECT_EQ(Test, std::uint16_t{0x1234}, Decoded.Sequence, "The typed decoder should read U16 little-endian");
	MW_EXPECT_EQ(Test, std::uint32_t{0x12345678}, Decoded.Value, "The typed decoder should read U32 little-endian");
}

/**
 * Motivation: Keeps malformed wire payloads from partially altering the higher-layer protocol object.
 * Responsibilities: Reject truncated and trailing typed payloads while preserving the caller's prior output value.
 */
MW_TEST_CASE(MessageTypes_DecodeTypedMessageRejectsMalformedOrTrailingPayloadWithoutMutation)
{
	// Arrange
	const std::uint8_t TruncatedPayload[]{0x34, 0x12, 0x78};
	const std::uint8_t TrailingPayload[]{0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0xFF};
	FMessage TruncatedMessage;
	TruncatedMessage.SetMessageNameId(GetMessageNameId(FCodecProbe{}));
	TruncatedMessage.SetPayload(TSpan<const std::uint8_t>{TruncatedPayload});
	FMessage TrailingMessage;
	TrailingMessage.SetMessageNameId(GetMessageNameId(FCodecProbe{}));
	TrailingMessage.SetPayload(TSpan<const std::uint8_t>{TrailingPayload});
	FCodecProbe Decoded{9, 99};

	// Act
	const EMessagingResult TruncatedResult = DecodeTypedMessage(TruncatedMessage, Decoded);
	const EMessagingResult TrailingResult = DecodeTypedMessage(TrailingMessage, Decoded);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Invalid, TruncatedResult, "A truncated typed payload should be rejected");
	MW_EXPECT_EQ(Test, EMessagingResult::Invalid, TrailingResult, "A typed payload with unconsumed bytes should be rejected");
	MW_EXPECT_EQ(Test, std::uint16_t{9}, Decoded.Sequence, "Rejected decoding should preserve the previous U16 value");
	MW_EXPECT_EQ(Test, std::uint32_t{99}, Decoded.Value, "Rejected decoding should preserve the previous U32 value");
}

/**
 * Motivation: Exposes a typed protocol message that exceeds the fixed Messaging payload budget before device I/O starts.
 * Responsibilities: Return Full from the codec and leave the route's device untouched.
 */
MW_TEST_CASE(MessageTypes_TypedRemoteSendPropagatesOversizeCodecFailure)
{
	// Arrange
	FMessagingSystem System;
	FTestTransportDevice Device;
	FMessagingLinkId LinkId;
	const FChannelInformation Channel{"Telemetry", false, nullptr, {}};
	FDeviceAddress Peer{};
	Peer.Bytes[0] = 1;
	Peer.Size = 1;

	// Act
	const EMessagingResult RegisterResult = System.RegisterLink(Device, LinkId);
	const EMessagingResult CreateResult = System.CreateChannel(Channel);
	const EMessagingResult SendResult = System.SendTypedMessageToRemoteChannel(FOverflowCodecProbe{}, "Telemetry", {LinkId, Peer});

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, RegisterResult, "The typed-send device should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The typed-send channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Full, SendResult, "An oversize typed payload should report fixed-capacity exhaustion");
	MW_EXPECT_EQ(Test, std::size_t{0}, Device.GetTrySendCallCount(), "A rejected typed payload should not reach the device");
}

} // namespace
