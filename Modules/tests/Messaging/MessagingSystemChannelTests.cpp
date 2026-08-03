#include "TestSupport.h"
#include "MessagingSystemTestHelpers.h"

#include <MicroWorld/Messaging/ChannelInformation.h>
#include <MicroWorld/Messaging/MessagingResult.h>
#include <MicroWorld/Messaging/MessagingSystemInformation.h>

#include <utility>

namespace
{

using namespace ::MicroWorld::Tests;

using MicroWorld::Messaging::FChannelInformation;
using MicroWorld::Messaging::FMessagingSystemInformation;
using MicroWorld::Messaging::InvalidNameId;

/** Motivation: Names the first deterministic channel created by FillChannelSlots. */
constexpr std::uint32_t FirstCapacityChannelValue = FirstGeneratedChannelValue;

/** Motivation: Names the last channel identity within the concrete fixed channel capacity. */
constexpr std::uint32_t FinalCapacityChannelValue = static_cast<std::uint32_t>(FirstCapacityChannelValue + FMessagingSystem::MaxChannels - 1);

/** Motivation: Names the first channel identity beyond the concrete fixed channel capacity. */
constexpr std::uint32_t OverflowCapacityChannelValue = static_cast<std::uint32_t>(FMessagingSystem::MaxChannels + 1);

/**
 * Motivation: Confirms a valid named channel starts an empty system successfully.
 * Responsibilities: Verify valid local-only channel creation reports success.
 */
MW_TEST_CASE(MessagingSystem_CreatesAValidChannel)
{
	// Arrange
	FMessagingSystem System;
	const FChannelInformation Information{"Telemetry", false, nullptr, {}};

	// Act
	const EMessagingResult Result = System.CreateChannel(Information);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, Result, "A valid channel should be created");
}

/**
 * Motivation: Confirms distinct channel identities occupy independent slots.
 * Responsibilities: Verify two valid names can be created in the same system.
 */
MW_TEST_CASE(MessagingSystem_CreatesTwoChannelsWithDifferentNames)
{
	// Arrange
	FMessagingSystem System;
	const FChannelInformation FirstInformation{"Telemetry", false, nullptr, {}};
	const FChannelInformation SecondInformation{"Commands", false, nullptr, {}};

	// Act
	const EMessagingResult FirstResult = System.CreateChannel(FirstInformation);
	const EMessagingResult SecondResult = System.CreateChannel(SecondInformation);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstResult, "The first valid channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondResult, "A different valid channel should be created");
}

/**
 * Motivation: Prevents the unset name sentinel from becoming an addressable live channel.
 * Responsibilities: Verify invalid channel creation reports Invalid before consuming capacity.
 */
MW_TEST_CASE(MessagingSystem_RejectsAnUnsetChannelName)
{
	// Arrange
	FMessagingSystem System;
	FChannelInformation Information{};
	Information.ChannelNameId = InvalidNameId;

	// Act
	const EMessagingResult Result = System.CreateChannel(Information);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Invalid, Result, "An unset channel name should be invalid");
}

/**
 * Motivation: Prevents two channel configurations from sharing one routing identity.
 * Responsibilities: Verify a repeated valid name reports Duplicate.
 */
MW_TEST_CASE(MessagingSystem_RejectsDuplicateChannelNames)
{
	// Arrange
	FMessagingSystem System;
	const FChannelInformation Information{"Telemetry", false, nullptr, {}};
	System.CreateChannel(Information);

	// Act
	const EMessagingResult Result = System.CreateChannel(Information);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Duplicate, Result, "An existing channel name should be rejected as duplicate");
}

/**
 * Motivation: Confirms a rejected duplicate never consumes bounded channel capacity.
 * Responsibilities: Fill all but one slot, reject a duplicate, then verify the final distinct channel still succeeds.
 */
MW_TEST_CASE(MessagingSystem_DuplicateDoesNotConsumeChannelCapacity)
{
	// Arrange
	FMessagingSystem System;
	const EMessagingResult FillResult = FillChannelSlots(System, FMessagingSystem::MaxChannels - 1);
	const FChannelInformation ExistingInformation{FNameId{FirstCapacityChannelValue}, false, nullptr, {}};
	const FChannelInformation FinalInformation{FNameId{FinalCapacityChannelValue}, false, nullptr, {}};

	// Act
	const EMessagingResult DuplicateResult = System.CreateChannel(ExistingInformation);
	const EMessagingResult FinalCreateResult = System.CreateChannel(FinalInformation);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FillResult, "All but one concrete channel slot should be filled");
	MW_EXPECT_EQ(Test, EMessagingResult::Duplicate, DuplicateResult, "A duplicate should be rejected before the final slot is used");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FinalCreateResult, "The duplicate should leave the final channel slot available");
}

/**
 * Motivation: Makes fixed channel storage exhaustion visible to callers.
 * Responsibilities: Verify one valid channel beyond the concrete capacity reports Full while a previously created channel still sends.
 */
MW_TEST_CASE(MessagingSystem_RejectsAChannelPastCapacityAndPreservesExistingChannels)
{
	// Arrange
	FMessagingSystem System;
	const EMessagingResult FillResult = FillChannelSlots(System, FMessagingSystem::MaxChannels);
	const FNameId ExistingChannelNameId{FirstCapacityChannelValue};
	const FChannelInformation OverflowInformation{FNameId{OverflowCapacityChannelValue}, false, nullptr, {}};
	FMessage Message;
	Message.SetMessageNameId("CapacityProbe");

	// Act
	const EMessagingResult OverflowResult = System.CreateChannel(OverflowInformation);
	const EMessagingResult ExistingSendResult = System.SendMessageToChannel(Message, ExistingChannelNameId);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FillResult, "Every concrete channel slot should be filled before overflow");
	MW_EXPECT_EQ(Test, EMessagingResult::Full, OverflowResult, "A channel beyond concrete capacity should be rejected");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ExistingSendResult, "An existing channel should remain usable after overflow rejection");
}

/**
 * Motivation: Preserves local-only messaging as a valid channel configuration.
 * Responsibilities: Verify a null transport device does not make a valid channel invalid.
 */
MW_TEST_CASE(MessagingSystem_AcceptsALocalOnlyChannel)
{
	// Arrange
	FMessagingSystem System;
	const FChannelInformation Information{"Telemetry", false, nullptr, {}};

	// Act
	const EMessagingResult Result = System.CreateChannel(Information);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, Result, "A local-only channel should be accepted");
}

/**
 * Motivation: Confirms remote channel configuration accepts an explicit device and route.
 * Responsibilities: Verify a non-null transport device and non-default address create a channel.
 */
MW_TEST_CASE(MessagingSystem_AcceptsAChannelWithDeviceAndAddress)
{
	// Arrange
	FMessagingSystem System;
	FTestTransportDevice Device;
	FDeviceAddress Address{};
	Address.Bytes[0] = 7;
	Address.Size = 1;
	const FChannelInformation Information{"Telemetry", false, &Device, Address};

	// Act
	const EMessagingResult Result = System.CreateChannel(Information);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, Result, "A device-backed channel should be accepted");
}

/**
 * Motivation: Allows separate named routes to share one physical transport device.
 * Responsibilities: Verify distinct addresses on one device create two independent channels.
 */
MW_TEST_CASE(MessagingSystem_AcceptsTwoAddressesOnOneDevice)
{
	// Arrange
	FMessagingSystem System;
	FTestTransportDevice Device;
	FDeviceAddress FirstAddress{};
	FirstAddress.Bytes[0] = 3;
	FirstAddress.Size = 1;
	FDeviceAddress SecondAddress{};
	SecondAddress.Bytes[0] = 4;
	SecondAddress.Size = 1;
	const FChannelInformation FirstInformation{"Telemetry", false, &Device, FirstAddress};
	const FChannelInformation SecondInformation{"Commands", false, &Device, SecondAddress};

	// Act
	const EMessagingResult FirstResult = System.CreateChannel(FirstInformation);
	const EMessagingResult SecondResult = System.CreateChannel(SecondInformation);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstResult, "The first device route should be accepted");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondResult, "The second device route should be accepted");
}

/**
 * Motivation: Lets application entry points provide reliability policy before channel setup.
 * Responsibilities: Verify the system exposes exactly the policy supplied at construction.
 */
MW_TEST_CASE(MessagingSystem_ReturnsSuppliedSystemInformation)
{
	// Arrange
	FMessagingSystemInformation Information{};
	Information.ReliableRetryIntervalMilliseconds = 400;
	Information.MaxReliableSendAttempts = 3;
	Information.MaxReceiveFramesPerDevicePerAdvance = 2;
	FMessagingSystem System{Information};

	// Act
	const FMessagingSystemInformation& ReturnedInformation = System.GetInformation();

	// Assert
	MW_EXPECT_EQ(
		Test,
		Information.ReliableRetryIntervalMilliseconds,
		ReturnedInformation.ReliableRetryIntervalMilliseconds,
		"The retry interval should be preserved");
	MW_EXPECT_EQ(Test, Information.MaxReliableSendAttempts, ReturnedInformation.MaxReliableSendAttempts, "The attempt budget should be preserved");
	MW_EXPECT_EQ(
		Test,
		Information.MaxReceiveFramesPerDevicePerAdvance,
		ReturnedInformation.MaxReceiveFramesPerDevicePerAdvance,
		"The receive budget should be preserved");
}

/**
 * Motivation: Reserves lifecycle calls for later device and queue work without changing channel identity today.
 * Responsibilities: Verify empty lifecycle turns preserve the channel set and duplicate behavior.
 */
MW_TEST_CASE(MessagingSystem_LifecycleTurnsPreserveChannels)
{
	// Arrange
	FMessagingSystem System;
	const FChannelInformation Information{"Telemetry", false, nullptr, {}};
	System.CreateChannel(Information);

	// Act
	System.PreAdvance(TimePointMilliseconds{125});
	System.PostAdvance(TimePointMilliseconds{250});
	const EMessagingResult Result = System.CreateChannel(Information);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Duplicate, Result, "Lifecycle turns should preserve existing channels");
}

} // namespace
