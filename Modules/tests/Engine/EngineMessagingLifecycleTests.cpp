#include "TestSupport.h"
#include "EngineMessagingTestHelpers.h"

#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Messaging/ChannelInformation.h>
#include <MicroWorld/Messaging/MessagingResult.h>
#include <MicroWorld/Messaging/MessagingSystemInformation.h>

namespace
{

using namespace ::MicroWorld::Tests;

using MicroWorld::Core::ERuntimeResult;
using MicroWorld::Messaging::EMessagingResult;
using MicroWorld::Messaging::FChannelInformation;
using MicroWorld::Messaging::FMessage;
using MicroWorld::Messaging::FMessagingSystem;
using MicroWorld::Messaging::FMessagingSystemInformation;

/**
 * Motivation: Read the optional Messaging system from a newly constructed engine.
 * Responsibilities: Verify an engine exposes no Messaging pointer before creation succeeds.
 */
MW_TEST_CASE(EngineMessagingSystemIsNullBeforeCreation)
{
	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};

	// Act
	FMessagingSystem* const MessagingSystem = Engine.GetMessagingSystem();

	// Assert
	MW_EXPECT_TRUE(Test, MessagingSystem == nullptr, "A fresh engine should expose no Messaging system");
}

/**
 * Motivation: Create Messaging through the engine and read the owned system back.
 * Responsibilities: Verify successful creation publishes one non-null Messaging system pointer.
 */
MW_TEST_CASE(EngineCreatesAndExposesMessagingSystem)
{
	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	const FMessagingSystemInformation Information{};

	// Act
	const ERuntimeResult CreateResult = Engine.CreateMessagingSystem(Information);
	FMessagingSystem* const MessagingSystem = Engine.GetMessagingSystem();

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, CreateResult, "The engine should create its Messaging system");
	MW_EXPECT_TRUE(Test, MessagingSystem != nullptr, "A successful create should publish the owned Messaging system");
}

/**
 * Motivation: Attempt a second Messaging creation after adding a channel to the original system.
 * Responsibilities: Verify duplicate creation preserves the original system pointer and channel state.
 */
MW_TEST_CASE(EngineRejectsDuplicateMessagingCreationWithoutReplacingState)
{
	/** Motivation: Identifies the channel that must survive a duplicate Messaging-system creation request. */
	constexpr FNameId PreservedChannelNameId = MakeNameId("PreservedChannel");

	/** Motivation: Keeps the preserved channel best-effort because this case only proves channel identity survives. */
	constexpr bool bPreservedChannelIsReliable = false;

	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	const FMessagingSystemInformation Information{};
	const ERuntimeResult FirstCreateResult = Engine.CreateMessagingSystem(Information);
	FMessagingSystem* const FirstMessagingSystem = Engine.GetMessagingSystem();
	const FChannelInformation PreservedChannelInformation{PreservedChannelNameId, bPreservedChannelIsReliable, nullptr, {}};
	const EMessagingResult InitialChannelCreateResult =
		FirstMessagingSystem != nullptr ? FirstMessagingSystem->CreateChannel(PreservedChannelInformation) : EMessagingResult::Invalid;

	// Act
	const ERuntimeResult DuplicateCreateResult = Engine.CreateMessagingSystem(Information);
	FMessagingSystem* const MessagingSystemAfterDuplicate = Engine.GetMessagingSystem();
	const EMessagingResult PreservedChannelCreateResult = MessagingSystemAfterDuplicate != nullptr
		? MessagingSystemAfterDuplicate->CreateChannel(PreservedChannelInformation)
		: EMessagingResult::Invalid;

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, FirstCreateResult, "The first Messaging creation should succeed");
	MW_EXPECT_TRUE(Test, FirstMessagingSystem != nullptr, "The first Messaging creation should publish a system");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, InitialChannelCreateResult, "The original system should accept its first channel");
	MW_EXPECT_EQ(Test, ERuntimeResult::Duplicate, DuplicateCreateResult, "A second Messaging creation should be rejected as duplicate");
	MW_EXPECT_TRUE(
		Test, FirstMessagingSystem == MessagingSystemAfterDuplicate, "A duplicate request should retain the original Messaging-system pointer");
	MW_EXPECT_EQ(Test, EMessagingResult::Duplicate, PreservedChannelCreateResult, "A duplicate request should retain original channel state");
}

/**
 * Motivation: Create Messaging before any engine world exists.
 * Responsibilities: Verify Messaging ownership is independent from world construction.
 */
MW_TEST_CASE(EngineCreatesMessagingSystemBeforeWorldCreation)
{
	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	const FMessagingSystemInformation Information{};

	// Act
	const ERuntimeResult CreateResult = Engine.CreateMessagingSystem(Information);
	FMessagingSystem* const MessagingSystem = Engine.GetMessagingSystem();

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, CreateResult, "Messaging creation should not require a world");
	MW_EXPECT_TRUE(Test, MessagingSystem != nullptr, "Messaging created before a world should remain available");
}

/**
 * Motivation: Run a complete engine lifecycle without creating its optional Messaging system.
 * Responsibilities: Verify absent Messaging wiring leaves ordinary world lifecycle behavior unchanged.
 */
MW_TEST_CASE(EngineLifecycleRemainsOptionalWithoutMessagingSystem)
{
	/** Motivation: Opens the engine world without a Messaging system attached. */
	constexpr MicroWorld::Core::TimePointMilliseconds NoMessagingBeginPlayMilliseconds{3000};

	/** Motivation: Advances the no-Messaging engine world after its play-start baseline. */
	constexpr MicroWorld::Core::TimePointMilliseconds NoMessagingTickMilliseconds{3010};

	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	const auto World = Engine.CreateWorld();
	const bool bWorldCreated = World.Get() != nullptr;

	// Act
	const ERuntimeResult BeginPlayResult = Engine.BeginPlay(NoMessagingBeginPlayMilliseconds);
	const ERuntimeResult TickResult = Engine.Tick(NoMessagingTickMilliseconds);
	const ERuntimeResult EndPlayResult = Engine.EndPlay();
	FMessagingSystem* const MessagingSystem = Engine.GetMessagingSystem();

	// Assert
	MW_EXPECT_TRUE(Test, bWorldCreated, "The no-Messaging engine should still create a world");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginPlayResult, "The no-Messaging engine should begin play normally");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, TickResult, "The no-Messaging engine should tick normally");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, EndPlayResult, "The no-Messaging engine should end play normally");
	MW_EXPECT_TRUE(Test, MessagingSystem == nullptr, "The no-Messaging lifecycle should leave the optional system absent");
}

} // namespace
