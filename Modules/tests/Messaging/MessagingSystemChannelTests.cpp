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
using MicroWorld::Messaging::FChannelTraits;
using MicroWorld::Messaging::FMessagingLinkId;
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
 * Motivation: Confirms a device-backed channel retains the device and address and routes outbound messages to them.
 * Responsibilities: Verify a non-null transport device and non-default address create a channel, and that a subsequent
 *   send on that channel drives exactly one device send, proving the route was retained rather than ignored.
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
	FMessage Message;
	Message.SetMessageNameId("Telemetry");

	// Act
	const EMessagingResult CreateResult = System.CreateChannel(Information);
	const EMessagingResult SendResult = System.SendMessageToChannel(Message, "Telemetry");

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "A device-backed channel should be accepted");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "A send on a device-backed channel should succeed");
	MW_EXPECT_EQ(Test, std::size_t{1}, Device.GetTrySendCallCount(), "The retained device should receive exactly one outbound send");
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

/**
 * Motivation: Makes caller-owned device registration stable when several channels and explicit routes share one device.
 * Responsibilities: Verify duplicate registration returns the original opaque id and all channel creation reuses that one registration.
 */
MW_TEST_CASE(MessagingSystem_RegisterLinkIsIdempotentAcrossChannels)
{
	// Arrange
	FMessagingSystem System;
	FTestTransportDevice Device;
	FMessagingLinkId FirstLinkId;
	FMessagingLinkId SecondLinkId;
	const FChannelInformation FirstChannel{"Telemetry", false, &Device, {}};
	const FChannelInformation SecondChannel{"Commands", false, &Device, {}};

	// Act
	const EMessagingResult FirstRegisterResult = System.RegisterLink(Device, FirstLinkId);
	const EMessagingResult SecondRegisterResult = System.RegisterLink(Device, SecondLinkId);
	const EMessagingResult FirstCreateResult = System.CreateChannel(FirstChannel);
	const EMessagingResult SecondCreateResult = System.CreateChannel(SecondChannel);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstRegisterResult, "The first device registration should succeed");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondRegisterResult, "The same device should register idempotently");
	MW_EXPECT_EQ(Test, FirstLinkId, SecondLinkId, "Repeated registration should return the stable link id");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstCreateResult, "The first channel should normalize the registered device");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondCreateResult, "The second channel should reuse the registered device");
}

/**
 * Motivation: Exposes the fixed link boundary without partially registering a rejected fifth device.
 * Responsibilities: Fill all link slots, reject one more device, and keep its output id invalid.
 */
MW_TEST_CASE(MessagingSystem_RejectsLinkRegistrationPastCapacityWithoutMutation)
{
	// Arrange
	FMessagingSystem System;
	FTestTransportDevice Devices[FMessagingSystem::MaxLinks + 1];
	FMessagingLinkId LinkIds[FMessagingSystem::MaxLinks]{};
	FMessagingLinkId OverflowLinkId;

	// Act
	EMessagingResult FillResult = EMessagingResult::Success;
	for (std::size_t LinkIndex = 0; LinkIndex < FMessagingSystem::MaxLinks; ++LinkIndex)
	{
		FillResult = System.RegisterLink(Devices[LinkIndex], LinkIds[LinkIndex]);
		if (FillResult != EMessagingResult::Success)
		{
			break;
		}
	}
	const EMessagingResult OverflowResult = System.RegisterLink(Devices[FMessagingSystem::MaxLinks], OverflowLinkId);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FillResult, "Every fixed link slot should accept one distinct device");
	MW_EXPECT_EQ(Test, EMessagingResult::Full, OverflowResult, "A fifth device should not be registered");
	MW_EXPECT_TRUE(Test, !OverflowLinkId.IsValid(), "A rejected registration should leave its output id invalid");
}

/**
 * Motivation: Lets Network safely unwind private channels without exposing channel internals.
 * Responsibilities: Verify traits report normalized routing and destruction rejects subscribed channels but removes an idle channel.
 */
MW_TEST_CASE(MessagingSystem_ReportsTraitsAndDestroysOnlyIdleChannels)
{
	// Arrange
	FMessagingSystem System;
	FTestTransportDevice Device;
	const FChannelInformation Channel{"Telemetry", true, &Device, {}};
	FChannelTraits Traits{};
	FSubscriberDelegate Subscriber;
	const EDelegateResult BindingResult = Subscriber.Bind([](const FMessage&) noexcept {});
	FMessagingSystem::FSubscriptionHandle SubscriptionHandle{};

	// Act
	const EMessagingResult CreateResult = System.CreateChannel(Channel);
	const EMessagingResult TraitsResult = System.GetChannelTraits("Telemetry", Traits);
	const EMessagingResult SubscribeResult = System.SubscribeToChannel("Telemetry", std::move(Subscriber), {}, &SubscriptionHandle);
	const EMessagingResult BusyDestroyResult = System.DestroyChannel("Telemetry");
	const EMessagingResult UnsubscribeResult = System.Unsubscribe(SubscriptionHandle);
	const EMessagingResult DestroyResult = System.DestroyChannel("Telemetry");
	const EMessagingResult MissingTraitsResult = System.GetChannelTraits("Telemetry", Traits);

	// Assert
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateResult, "The routed reliable channel should be created");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, TraitsResult, "A created channel should expose narrow traits");
	MW_EXPECT_TRUE(Test, Traits.bIsReliable, "The traits should preserve reliable policy");
	MW_EXPECT_TRUE(Test, Traits.bHasDefaultRoute, "The traits should report a normalized default route");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindingResult, "The destruction guard subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The destruction guard subscriber should register");
	MW_EXPECT_EQ(Test, EMessagingResult::Busy, BusyDestroyResult, "A subscribed channel must not be destroyed");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, UnsubscribeResult, "The live subscription should be removable");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, DestroyResult, "An idle channel should be removable");
	MW_EXPECT_EQ(Test, EMessagingResult::NotFound, MissingTraitsResult, "Destroyed channels should expose no traits");
}

} // namespace
