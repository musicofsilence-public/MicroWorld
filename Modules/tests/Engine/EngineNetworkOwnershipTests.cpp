#include "TestSupport.h"
#include "EngineMessagingTestHelpers.h"

#include <MicroWorld/Messaging/ChannelInformation.h>
#include <MicroWorld/Messaging/MessagingResult.h>
#include <MicroWorld/Networking/NetworkResult.h>
#include <MicroWorld/Networking/NetworkSystem.h>
#include <MicroWorld/Networking/NetworkSystemInformation.h>

namespace
{

using namespace ::MicroWorld::Tests;

using MicroWorld::Core::ERuntimeResult;
using MicroWorld::Messaging::EMessagingResult;
using MicroWorld::Messaging::FChannelInformation;
using MicroWorld::Messaging::FMessagingSystem;
using MicroWorld::Messaging::FMessagingSystemInformation;
using MicroWorld::Networking::ENetworkResult;
using MicroWorld::Networking::FNetworkSystem;
using MicroWorld::Networking::FNetworkSystemInformation;

/**
 * Motivation: Prevents Network from existing without the Messaging owner its private channels and subscriptions need.
 * Responsibilities: Verify failed creation leaves the optional Network pointer null.
 */
MW_TEST_CASE(EngineRejectsNetworkCreationWithoutMessaging)
{
	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	const FNetworkSystemInformation Information{};

	// Act
	const ENetworkResult CreateResult = Engine.CreateNetworkSystem(Information);
	FNetworkSystem* const NetworkSystem = Engine.GetNetworkSystem();

	// Assert
	MW_EXPECT_EQ(Test, ENetworkResult::Invalid, CreateResult, "Network creation should require Engine-owned Messaging");
	MW_EXPECT_TRUE(Test, NetworkSystem == nullptr, "A rejected Network creation should not publish a Network system");
}

/**
 * Motivation: Gives one Engine World access to the Network instance it will outlive.
 * Responsibilities: Verify Network creation precedes World construction and the World exposes the identical borrowed pointer.
 */
MW_TEST_CASE(EngineCreatesNetworkBeforeWorldAndWorldExposesBorrowedPointer)
{
	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	const ERuntimeResult MessagingCreateResult = Engine.CreateMessagingSystem(FMessagingSystemInformation{});
	const FNetworkSystemInformation NetworkInformation{};

	// Act
	const ENetworkResult NetworkCreateResult = Engine.CreateNetworkSystem(NetworkInformation);
	FNetworkSystem* const NetworkSystem = Engine.GetNetworkSystem();
	const auto World = Engine.CreateWorld();
	FNetworkSystem* const WorldNetworkSystem = World.Get() != nullptr ? World.Get()->GetNetwork() : nullptr;
	const ENetworkResult DuplicateNetworkCreateResult = Engine.CreateNetworkSystem(NetworkInformation);

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, MessagingCreateResult, "Messaging should create before the optional Network system");
	MW_EXPECT_EQ(Test, ENetworkResult::Success, NetworkCreateResult, "Network should initialize before World construction");
	MW_EXPECT_TRUE(Test, NetworkSystem != nullptr, "A successful Network creation should publish the Engine-owned system");
	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "The Engine should create a World after Network initialization");
	MW_EXPECT_TRUE(Test, WorldNetworkSystem == NetworkSystem, "World should retain the Engine-owned Network pointer without owning it");
	MW_EXPECT_EQ(Test, ENetworkResult::Invalid, DuplicateNetworkCreateResult, "A duplicate Network request should preserve the original system");
}

/**
 * Motivation: Prevents failed private-channel initialization from leaving a hidden Network object or channel behind.
 * Responsibilities: Fill all but one Messaging channel slot, reject Network creation, and verify the remaining slot is reusable.
 */
MW_TEST_CASE(EngineRollsBackNetworkCreationWhenMessagingChannelCapacityIsInsufficient)
{
	/** Motivation: Uses the first application channel to leave only one slot for Network's two private channels. */
	constexpr FNameId FirstApplicationChannelNameId = MakeNameId("EngineNetworkFirstApplication");

	/** Motivation: Uses the second application channel to exhaust Network's initialization preflight capacity. */
	constexpr FNameId SecondApplicationChannelNameId = MakeNameId("EngineNetworkSecondApplication");

	/** Motivation: Uses the third application channel to exhaust Network's initialization preflight capacity. */
	constexpr FNameId ThirdApplicationChannelNameId = MakeNameId("EngineNetworkThirdApplication");

	/** Motivation: Proves failed initialization restores the final channel slot for normal Messaging use. */
	constexpr FNameId RecoveredApplicationChannelNameId = MakeNameId("EngineNetworkRecoveredApplication");

	/** Motivation: Keeps capacity setup local-only because this test observes Engine rollback, not delivery. */
	constexpr bool bApplicationChannelIsReliable = false;

	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	const ERuntimeResult MessagingCreateResult = Engine.CreateMessagingSystem(FMessagingSystemInformation{});
	FMessagingSystem* const MessagingSystem = Engine.GetMessagingSystem();
	const FChannelInformation FirstChannel{FirstApplicationChannelNameId, bApplicationChannelIsReliable, nullptr, {}};
	const FChannelInformation SecondChannel{SecondApplicationChannelNameId, bApplicationChannelIsReliable, nullptr, {}};
	const FChannelInformation ThirdChannel{ThirdApplicationChannelNameId, bApplicationChannelIsReliable, nullptr, {}};
	const EMessagingResult FirstChannelCreateResult =
		MessagingSystem != nullptr ? MessagingSystem->CreateChannel(FirstChannel) : EMessagingResult::Invalid;
	const EMessagingResult SecondChannelCreateResult =
		MessagingSystem != nullptr ? MessagingSystem->CreateChannel(SecondChannel) : EMessagingResult::Invalid;
	const EMessagingResult ThirdChannelCreateResult =
		MessagingSystem != nullptr ? MessagingSystem->CreateChannel(ThirdChannel) : EMessagingResult::Invalid;

	// Act
	const ENetworkResult NetworkCreateResult = Engine.CreateNetworkSystem(FNetworkSystemInformation{});
	const EMessagingResult RecoveredChannelCreateResult = MessagingSystem != nullptr
		? MessagingSystem->CreateChannel({RecoveredApplicationChannelNameId, bApplicationChannelIsReliable, nullptr, {}})
		: EMessagingResult::Invalid;

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, MessagingCreateResult, "The rollback case should own Messaging first");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, FirstChannelCreateResult, "The first application channel should reserve one slot");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SecondChannelCreateResult, "The second application channel should reserve one slot");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, ThirdChannelCreateResult, "The third application channel should reserve one slot");
	MW_EXPECT_EQ(Test, ENetworkResult::Full, NetworkCreateResult, "Network should reject initialization when two private channels cannot fit");
	MW_EXPECT_TRUE(Test, Engine.GetNetworkSystem() == nullptr, "A failed Network initialization should not publish a Network system");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, RecoveredChannelCreateResult, "Rollback should restore the channel slot Network briefly used");
}

/**
 * Motivation: Keeps World construction valid for Engine users that intentionally do not compose Network.
 * Responsibilities: Verify World exposes a null optional Network pointer when it was created without one.
 */
MW_TEST_CASE(EngineWorldExposesNullNetworkWhenNetworkWasNotCreated)
{
	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	const ERuntimeResult MessagingCreateResult = Engine.CreateMessagingSystem(FMessagingSystemInformation{});

	// Act
	const auto World = Engine.CreateWorld();
	FNetworkSystem* const WorldNetworkSystem = World.Get() != nullptr ? World.Get()->GetNetwork() : nullptr;
	const ENetworkResult LateNetworkCreateResult = Engine.CreateNetworkSystem(FNetworkSystemInformation{});

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, MessagingCreateResult, "The late-Network case should own Messaging first");
	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "The Engine should create a World without Network composition");
	MW_EXPECT_TRUE(Test, WorldNetworkSystem == nullptr, "A World created without Network should expose a null borrowed pointer");
	MW_EXPECT_EQ(Test, ENetworkResult::Invalid, LateNetworkCreateResult, "Network creation should be rejected after World construction");
	MW_EXPECT_TRUE(Test, Engine.GetNetworkSystem() == nullptr, "A late Network request should leave the optional system absent");
}

} // namespace
