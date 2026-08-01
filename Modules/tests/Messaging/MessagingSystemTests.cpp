#include "TestSupport.h"

#include <MicroWorld/Messaging/MessagingSystem.h>

#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::Core::ETransportResult;
using MicroWorld::Core::FDeviceAddress;
using MicroWorld::Core::FReceiveResult;
using MicroWorld::Core::ITransportDevice;
using MicroWorld::Core::TimePointMilliseconds;
using MicroWorld::Core::TSpan;
using MicroWorld::Messaging::EMessagingResult;
using MicroWorld::Messaging::FChannelInformation;
using MicroWorld::Messaging::FMessagingSystemInformation;
using MicroWorld::Messaging::FNameId;
using MicroWorld::Messaging::InvalidNameId;
using MicroWorld::Messaging::TMessagingSystem;

/**
 * Motivation: Provides a non-owning device pointer for channel creation tests.
 * Responsibilities: Satisfy the transport device contract without retaining packets or performing I/O.
 * Example:
 *   FTestTransportDevice Device;
 */
class FTestTransportDevice final : public ITransportDevice
{
public:
	/**
	 * Motivation: Completes the transport contract for tests that only create a channel.
	 * Responsibilities: Report success without retaining the supplied packet or destination.
	 */
	ETransportResult TrySend(const FDeviceAddress&, const TSpan<const std::uint8_t>) noexcept override { return ETransportResult::Success; }

	/**
	 * Motivation: Completes the transport contract without modelling inbound packets for channel creation tests.
	 * Responsibilities: Report no packet available without changing output values.
	 */
	ETransportResult TryReceive(FDeviceAddress&, TSpan<std::uint8_t>, FReceiveResult&) noexcept override { return ETransportResult::Unavailable; }

	/**
	 * Motivation: Supplies a bounded device packet size without requiring hardware state.
	 * Responsibilities: Return the fixed test packet byte limit.
	 */
	std::size_t MaxPacketBytes() const noexcept override { return 64; }

	/**
	 * Motivation: Completes the caller-driven device lifecycle for a no-op test double.
	 * Responsibilities: Perform no pre-advance work.
	 */
	void PreAdvance(TimePointMilliseconds) noexcept override {}

	/**
	 * Motivation: Completes the caller-driven device lifecycle for a no-op test double.
	 * Responsibilities: Perform no post-advance work.
	 */
	void PostAdvance(TimePointMilliseconds) noexcept override {}
};

/**
 * Motivation: Makes capacity behavior testable without depending on the production channel limit.
 * Responsibilities: Bound this test system to two channel creation slots.
 * Example:
 *   TMessagingSystem<FSmallMessagingTraits> System;
 */
struct FSmallMessagingTraits
{
	/** Motivation: Bounds this test system to two channels. */
	static constexpr std::size_t MaxChannels = 2;
};

/**
 * Motivation: Confirms a valid named channel starts an empty system successfully.
 * Responsibilities: Verify valid local-only channel creation reports success.
 */
MW_TEST_CASE(MessagingSystem_CreatesAValidChannel)
{
	// Arrange
	TMessagingSystem<> System;
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
	TMessagingSystem<> System;
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
	TMessagingSystem<> System;
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
	TMessagingSystem<> System;
	const FChannelInformation Information{"Telemetry", false, nullptr, {}};
	System.CreateChannel(Information);

	// Act
	const EMessagingResult Result = System.CreateChannel(Information);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Duplicate, Result, "An existing channel name should be rejected as duplicate");
}

/**
 * Motivation: Confirms a rejected duplicate never consumes bounded channel capacity.
 * Responsibilities: Verify a full system still reports Duplicate for an existing name and Full for a new name.
 */
MW_TEST_CASE(MessagingSystem_DuplicateDoesNotConsumeChannelCapacity)
{
	// Arrange
	TMessagingSystem<FSmallMessagingTraits> System;
	const FChannelInformation FirstInformation{"Telemetry", false, nullptr, {}};
	const FChannelInformation SecondInformation{"Commands", false, nullptr, {}};
	const FChannelInformation ThirdInformation{"Status", false, nullptr, {}};
	System.CreateChannel(FirstInformation);
	System.CreateChannel(SecondInformation);

	// Act
	const EMessagingResult DuplicateResult = System.CreateChannel(FirstInformation);
	const EMessagingResult FullResult = System.CreateChannel(ThirdInformation);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Duplicate, DuplicateResult, "A duplicate should remain distinguishable after capacity is full");
	MW_EXPECT_EQ(Test, EMessagingResult::Full, FullResult, "A new channel should still observe the original full capacity");
}

/**
 * Motivation: Makes fixed channel storage exhaustion visible to callers.
 * Responsibilities: Verify one valid channel beyond the configured capacity reports Full.
 */
MW_TEST_CASE(MessagingSystem_RejectsAChannelPastCapacity)
{
	// Arrange
	TMessagingSystem<FSmallMessagingTraits> System;
	const FChannelInformation FirstInformation{"Telemetry", false, nullptr, {}};
	const FChannelInformation SecondInformation{"Commands", false, nullptr, {}};
	const FChannelInformation ThirdInformation{"Status", false, nullptr, {}};
	System.CreateChannel(FirstInformation);
	System.CreateChannel(SecondInformation);

	// Act
	const EMessagingResult Result = System.CreateChannel(ThirdInformation);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Full, Result, "A channel beyond capacity should be rejected");
}

/**
 * Motivation: Preserves local-only messaging as a valid channel configuration.
 * Responsibilities: Verify a null transport device does not make a valid channel invalid.
 */
MW_TEST_CASE(MessagingSystem_AcceptsALocalOnlyChannel)
{
	// Arrange
	TMessagingSystem<> System;
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
	TMessagingSystem<> System;
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
	TMessagingSystem<> System;
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
 * Motivation: Lets composition roots provide reliability policy before channel setup.
 * Responsibilities: Verify the system exposes exactly the policy supplied at construction.
 */
MW_TEST_CASE(MessagingSystem_ReturnsSuppliedSystemInformation)
{
	// Arrange
	FMessagingSystemInformation Information{};
	Information.ReliableRetryIntervalMilliseconds = 400;
	Information.MaxReliableSendAttempts = 3;
	TMessagingSystem<> System{Information};

	// Act
	const FMessagingSystemInformation& ReturnedInformation = System.GetInformation();

	// Assert
	MW_EXPECT_EQ(
		Test,
		Information.ReliableRetryIntervalMilliseconds,
		ReturnedInformation.ReliableRetryIntervalMilliseconds,
		"The retry interval should be preserved");
	MW_EXPECT_EQ(Test, Information.MaxReliableSendAttempts, ReturnedInformation.MaxReliableSendAttempts, "The attempt budget should be preserved");
}

/**
 * Motivation: Reserves lifecycle calls for later device and queue work without changing channel identity today.
 * Responsibilities: Verify empty lifecycle turns preserve the channel set and duplicate behavior.
 */
MW_TEST_CASE(MessagingSystem_LifecycleTurnsPreserveChannels)
{
	// Arrange
	TMessagingSystem<> System;
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
