#include "TestSupport.h"

#include <MicroWorld/Messaging/MessageTypes.h>

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
using MicroWorld::Messaging::FChannelInformation;
using MicroWorld::Messaging::FMessage;
using MicroWorld::Messaging::FMessagingSystemInformation;
using MicroWorld::Messaging::FNameId;

/**
 * Motivation: Supplies a non-owning device pointer for channel configuration tests.
 * Responsibilities: Return fixed non-blocking results and record no state.
 * Example:
 *   FTestTransportDevice Device;
 */
class FTestTransportDevice final : public ITransportDevice
{
public:
	/**
	 * Motivation: Confirms channel configuration accepts a concrete transport device.
	 * Responsibilities: Report acceptance without retaining packet data or destination state.
	 */
	ETransportResult TrySend(const FDeviceAddress&, const TSpan<const std::uint8_t>) noexcept override { return ETransportResult::Success; }

	/**
	 * Motivation: Lets configuration tests provide a complete device without inbound packets.
	 * Responsibilities: Report unavailable without changing any output parameter.
	 */
	ETransportResult TryReceive(FDeviceAddress&, TSpan<std::uint8_t>, FReceiveResult&) noexcept override { return ETransportResult::Unavailable; }

	/**
	 * Motivation: Gives tests a fixed packet limit without involving a hardware transport.
	 * Responsibilities: Return the fixed maximum packet size.
	 */
	std::size_t MaxPacketBytes() const noexcept override { return 64; }

	/**
	 * Motivation: Satisfies the device lifecycle contract for a no-op test double.
	 * Responsibilities: Make no progress and retain no time state.
	 */
	void PreAdvance(const TimePointMilliseconds) noexcept override {}

	/**
	 * Motivation: Satisfies the device lifecycle contract for a no-op test double.
	 * Responsibilities: Make no progress and retain no time state.
	 */
	void PostAdvance(const TimePointMilliseconds) noexcept override {}
};

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
 * Motivation: Lets received messages retain their device-provided source route for replies and inspection.
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
	Message.SetSender(Sender);
	const FDeviceAddress& ReturnedSender = Message.GetSender();

	// Assert
	MW_EXPECT_EQ(Test, &ReturnedSender, &Message.GetSender(), "The sender getter should return a stable reference");
	MW_EXPECT_EQ(Test, Sender, ReturnedSender, "The sender address should be preserved");
}

/**
 * Motivation: Keeps reliable delivery bounded with a visible retry cadence and attempt budget.
 * Responsibilities: Confirm system information uses the documented retry defaults.
 */
MW_TEST_CASE(MessageTypes_SystemInformationUsesReliableDefaults)
{
	// Arrange
	const FMessagingSystemInformation Information{};

	// Act
	const DurationMilliseconds RetryInterval = Information.ReliableRetryIntervalMilliseconds;

	// Assert
	MW_EXPECT_EQ(Test, DurationMilliseconds{200}, RetryInterval, "The retry interval should default to 200 milliseconds");
	MW_EXPECT_EQ(Test, std::uint8_t{8}, Information.MaxReliableSendAttempts, "The send attempt limit should default to eight");
}

} // namespace
