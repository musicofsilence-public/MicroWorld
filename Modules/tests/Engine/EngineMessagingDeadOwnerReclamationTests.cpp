#include "TestSupport.h"
#include "EngineMessagingTestHelpers.h"

#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Core/WeakOwner.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Messaging/ChannelInformation.h>
#include <MicroWorld/Messaging/DefaultMessagingTraits.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessagingResult.h>
#include <MicroWorld/Messaging/MessagingSystemInformation.h>

#include <cstddef>
#include <cstdint>

namespace
{

using namespace ::MicroWorld::Tests;

using MicroWorld::Core::ERuntimeResult;
using MicroWorld::Core::FWeakOwner;
using MicroWorld::Core::TimePointMilliseconds;
using MicroWorld::Engine::EEngineResult;
using MicroWorld::Messaging::EMessagingResult;
using MicroWorld::Messaging::FChannelInformation;
using MicroWorld::Messaging::FDefaultMessagingTraits;
using MicroWorld::Messaging::FMessage;
using MicroWorld::Messaging::FMessagingSystem;
using MicroWorld::Messaging::FMessagingSystemInformation;

/**
 * Motivation: Spawn an actor whose BeginPlay owns a Messaging subscription, then destroy and reclaim that actor.
 * Responsibilities: The live actor receives one message; after its destruction, the same channel skips the callback
 *   and reclaims its actor-owned subscription.
 */
MW_TEST_CASE(EngineMessagingDestroyedActorOwnedSubscriptionIsSkippedAndReclaimed)
{
	/** Motivation: Names the local channel used to route the post-destruction message. */
	constexpr FNameId DestroyedOwnerChannelNameId = MakeNameId("DestroyedOwnerChannel");

	/** Motivation: Names the valid message that exercises local channel delivery. */
	constexpr FNameId DestroyedOwnerMessageNameId = MakeNameId("DestroyedOwnerMessage");

	/** Motivation: Keeps this local delivery test independent from device reliability behavior. */
	constexpr bool bDestroyedOwnerChannelIsReliable = false;

	/** Motivation: Starts the engine world before the subscriber actor is queued to spawn. */
	constexpr TimePointMilliseconds DestroyedOwnerBeginPlayMilliseconds{1000};

	/** Motivation: Applies the queued spawn so the actor subscribes from BeginPlay. */
	constexpr TimePointMilliseconds DestroyedOwnerSpawnTickMilliseconds{1010};

	/** Motivation: Applies actor destruction and the engine's reclamation slice. */
	constexpr TimePointMilliseconds DestroyedOwnerDestructionTickMilliseconds{1020};

	/** Motivation: States that the live spawned actor must receive the first routed message. */
	constexpr std::size_t ExpectedInitialDeliveryCount = 1;

	/** Motivation: States that actor destruction must prevent any later callback delivery. */
	constexpr std::size_t ExpectedDeliveryCountAfterDestruction = ExpectedInitialDeliveryCount;

	/** Motivation: States the one dead-owner subscription delivery must reclaim. */
	constexpr std::uint32_t ExpectedReclaimedSubscriptionCount = 1;

	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	FBeginPlayMessagingSubscriptionContext SubscriptionContext{};
	const FMessagingSystemInformation Information{};
	const ERuntimeResult CreateMessagingResult = Engine.CreateMessagingSystem(Information);
	FMessagingSystem* const MessagingSystem = Engine.GetMessagingSystem();
	const bool bMessagingSystemCreated = MessagingSystem != nullptr;
	const FChannelInformation ChannelInformation{DestroyedOwnerChannelNameId, bDestroyedOwnerChannelIsReliable, nullptr, {}};
	const EMessagingResult CreateChannelResult =
		MessagingSystem != nullptr ? MessagingSystem->CreateChannel(ChannelInformation) : EMessagingResult::Invalid;
	const EObjectResult RegisterClassResult =
		Engine.RegisterClass<FBeginPlayMessagingSubscriberActor>(BeginPlayMessagingSubscriberActorTypeId, BeginPlayMessagingSubscriberActorClassName);
	const auto ActorCreation = Engine.CreateObject<FBeginPlayMessagingSubscriberActor>(
		BeginPlayMessagingSubscriberActorTypeId, MessagingSystem, DestroyedOwnerChannelNameId, SubscriptionContext);
	const bool bActorCreated = ActorCreation.Object.Get() != nullptr;
	const auto World = Engine.CreateWorld();
	const bool bWorldCreated = World.Get() != nullptr;
	const ERuntimeResult BeginPlayResult = Engine.BeginPlay(DestroyedOwnerBeginPlayMilliseconds);
	FMessage Message;
	Message.SetMessageNameId(DestroyedOwnerMessageNameId);

	// Act
	const EEngineResult SpawnResult = World.Get()->SpawnActor(MicroWorld::Engine::TObjectPtr<MicroWorld::Engine::AActor>{ActorCreation.Object});
	const ERuntimeResult SpawnTickResult = Engine.Tick(DestroyedOwnerSpawnTickMilliseconds);
	const EDelegateResult BindResult = SubscriptionContext.BindResult;
	const EMessagingResult SubscribeResult = SubscriptionContext.SubscribeResult;
	const EMessagingResult InitialSendResult =
		MessagingSystem != nullptr ? MessagingSystem->SendMessageToChannel(Message, DestroyedOwnerChannelNameId) : EMessagingResult::Invalid;
	const std::size_t InitialDeliveryCount = SubscriptionContext.DeliveryCount;
	const EEngineResult DestroyResult = World.Get()->DestroyActor(MicroWorld::Engine::TObjectPtr<MicroWorld::Engine::AActor>{ActorCreation.Object});
	const ERuntimeResult DestructionTickResult = Engine.Tick(DestroyedOwnerDestructionTickMilliseconds);
	const bool bActorWasReclaimed = ActorCreation.Object.Get() == nullptr;
	const EMessagingResult PostDestructionSendResult =
		MessagingSystem != nullptr ? MessagingSystem->SendMessageToChannel(Message, DestroyedOwnerChannelNameId) : EMessagingResult::Invalid;
	const std::size_t DeliveryCountAfterDestruction = SubscriptionContext.DeliveryCount;
	const std::uint32_t ReclaimedSubscriptionCount = MessagingSystem != nullptr ? MessagingSystem->GetReclaimedDeadOwnerSubscriptionCount() : 0;

	// Assert
	MW_EXPECT_EQ(Test, EObjectResult::Success, RegisterClassResult, "The BeginPlay subscriber actor class should register");
	MW_EXPECT_TRUE(Test, bActorCreated, "The Messaging owner actor should be created");
	MW_EXPECT_TRUE(Test, bWorldCreated, "The engine should create a world for actor ownership");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, CreateMessagingResult, "The engine should create Messaging for the owner subscription");
	MW_EXPECT_TRUE(Test, bMessagingSystemCreated, "The engine should expose Messaging for the owner subscription");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateChannelResult, "The owner subscription channel should be created");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginPlayResult, "The owner world should begin play");
	MW_EXPECT_EQ(Test, EEngineResult::Success, SpawnResult, "The owner actor should queue for spawn");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, SpawnTickResult, "The spawn tick should begin the owner actor");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindResult, "The BeginPlay actor subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SubscribeResult, "The BeginPlay actor should subscribe successfully");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, InitialSendResult, "The live actor message should send successfully");
	MW_EXPECT_EQ(Test, ExpectedInitialDeliveryCount, InitialDeliveryCount, "The live actor subscription should receive one message");
	MW_EXPECT_EQ(Test, EEngineResult::Success, DestroyResult, "The owner actor should queue for destruction");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, DestructionTickResult, "The destruction tick should complete successfully");
	MW_EXPECT_TRUE(Test, bActorWasReclaimed, "The destruction tick should reclaim the owner actor");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, PostDestructionSendResult, "A message after owner reclamation should send safely");
	MW_EXPECT_EQ(
		Test, ExpectedDeliveryCountAfterDestruction, DeliveryCountAfterDestruction, "A reclaimed actor owner should receive no further callback");
	MW_EXPECT_EQ(
		Test, ExpectedReclaimedSubscriptionCount, ReclaimedSubscriptionCount, "The dead actor subscription should be reclaimed during delivery");
}

/**
 * Motivation: Fill the engine-owned Messaging system with one actor owner's default-capacity subscriptions, then destroy that owner.
 * Responsibilities: One subsequent delivery pass reclaims every dead-owner subscription and reports the cumulative
 *   reclamation count.
 */
MW_TEST_CASE(EngineMessagingReclaimsDestroyedOwnerSubscriptionsAtDefaultCapacity)
{
	/** Motivation: Names the channel that holds the default-capacity subscription set. */
	constexpr FNameId CapacityReuseChannelNameId = MakeNameId("CapacityReuseChannel");

	/** Motivation: Names the message that triggers dead-owner reclamation during local routing. */
	constexpr FNameId CapacityReuseMessageNameId = MakeNameId("CapacityReuseMessage");

	/** Motivation: Keeps capacity reclamation local and free of transport reliability behavior. */
	constexpr bool bCapacityReuseChannelIsReliable = false;

	/** Motivation: States the fixed subscription capacity of the engine-owned default Messaging alias. */
	constexpr std::size_t DefaultMessagingSubscriptionCapacity = FDefaultMessagingTraits::MaxSubscriptions;

	/** Motivation: Starts the world before the common subscription owner is destroyed. */
	constexpr TimePointMilliseconds CapacityReuseBeginPlayMilliseconds{2000};

	/** Motivation: Applies destruction and reclaims the common actor owner. */
	constexpr TimePointMilliseconds CapacityReuseDestructionTickMilliseconds{2010};

	/** Motivation: States that every default-capacity owner subscription must be reclaimed. */
	constexpr std::uint32_t ExpectedReclaimedSubscriptionCount = static_cast<std::uint32_t>(DefaultMessagingSubscriptionCapacity);

	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	FBeginPlayMessagingSubscriptionContext SubscriptionContext{};
	const FMessagingSystemInformation Information{};
	const ERuntimeResult CreateMessagingResult = Engine.CreateMessagingSystem(Information);
	FMessagingSystem* const MessagingSystem = Engine.GetMessagingSystem();
	const bool bMessagingSystemCreated = MessagingSystem != nullptr;
	const EObjectResult RegisterClassResult =
		Engine.RegisterClass<FBeginPlayMessagingSubscriberActor>(BeginPlayMessagingSubscriberActorTypeId, BeginPlayMessagingSubscriberActorClassName);
	const auto ActorCreation = Engine.CreateObject<FBeginPlayMessagingSubscriberActor>(
		BeginPlayMessagingSubscriberActorTypeId, nullptr, CapacityReuseChannelNameId, SubscriptionContext);
	const bool bActorCreated = ActorCreation.Object.Get() != nullptr;
	const auto World = Engine.CreateWorld();
	const bool bWorldCreated = World.Get() != nullptr;
	const FChannelInformation ChannelInformation{CapacityReuseChannelNameId, bCapacityReuseChannelIsReliable, nullptr, {}};
	const EMessagingResult CreateChannelResult =
		MessagingSystem != nullptr ? MessagingSystem->CreateChannel(ChannelInformation) : EMessagingResult::Invalid;
	const FWeakOwner Owner = MicroWorld::Engine::MakeWeakOwner(Engine.GetObjectStore(), ActorCreation.Object.Handle());
	bool bAllSubscribersBound = true;
	bool bAllSubscriptionsRegistered = true;
	for (std::size_t SubscriberIndex = 0; SubscriberIndex < DefaultMessagingSubscriptionCapacity; ++SubscriberIndex)
	{
		FSubscriberDelegate Subscriber;
		const EDelegateResult BindResult = Subscriber.Bind([](const FMessage&) noexcept {});
		const EMessagingResult SubscribeResult = MessagingSystem != nullptr
			? MessagingSystem->SubscribeToChannel(CapacityReuseChannelNameId, std::move(Subscriber), Owner)
			: EMessagingResult::Invalid;
		const bool bSubscriberBound = BindResult == EDelegateResult::Success;
		const bool bSubscriptionRegistered = SubscribeResult == EMessagingResult::Success;
		bAllSubscribersBound = bAllSubscribersBound && bSubscriberBound;
		bAllSubscriptionsRegistered = bAllSubscriptionsRegistered && bSubscriptionRegistered;
	}
	const EEngineResult RegisterActorResult =
		World.Get()->RegisterActor(MicroWorld::Engine::TObjectPtr<MicroWorld::Engine::AActor>{ActorCreation.Object});
	const ERuntimeResult BeginPlayResult = Engine.BeginPlay(CapacityReuseBeginPlayMilliseconds);
	FMessage Message;
	Message.SetMessageNameId(CapacityReuseMessageNameId);

	// Act
	const EEngineResult DestroyResult = World.Get()->DestroyActor(MicroWorld::Engine::TObjectPtr<MicroWorld::Engine::AActor>{ActorCreation.Object});
	const ERuntimeResult DestructionTickResult = Engine.Tick(CapacityReuseDestructionTickMilliseconds);
	const EMessagingResult SendResult =
		MessagingSystem != nullptr ? MessagingSystem->SendMessageToChannel(Message, CapacityReuseChannelNameId) : EMessagingResult::Invalid;
	const std::uint32_t ReclaimedSubscriptionCount = MessagingSystem != nullptr ? MessagingSystem->GetReclaimedDeadOwnerSubscriptionCount() : 0;

	// Assert
	MW_EXPECT_EQ(Test, EObjectResult::Success, RegisterClassResult, "The capacity owner actor class should register");
	MW_EXPECT_TRUE(Test, bActorCreated, "The capacity owner actor should be created");
	MW_EXPECT_TRUE(Test, bWorldCreated, "The engine should create a world for capacity reclamation");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, CreateMessagingResult, "The engine should create Messaging for capacity reclamation");
	MW_EXPECT_TRUE(Test, bMessagingSystemCreated, "The engine should expose Messaging for capacity reclamation");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateChannelResult, "The capacity reclamation channel should be created");
	MW_EXPECT_TRUE(Test, bAllSubscribersBound, "Every default-capacity dead-owner subscriber should bind");
	MW_EXPECT_TRUE(Test, bAllSubscriptionsRegistered, "Every default-capacity dead-owner subscription should register");
	MW_EXPECT_EQ(Test, EEngineResult::Success, RegisterActorResult, "The capacity owner actor should register with the world");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginPlayResult, "The capacity owner world should begin play");
	MW_EXPECT_EQ(Test, EEngineResult::Success, DestroyResult, "The capacity owner actor should queue for destruction");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, DestructionTickResult, "The capacity owner destruction tick should complete");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "The reclamation trigger message should send successfully");
	MW_EXPECT_EQ(
		Test,
		ExpectedReclaimedSubscriptionCount,
		ReclaimedSubscriptionCount,
		"The delivery pass should reclaim every default-capacity dead-owner subscription");
}

/**
 * Motivation: Capture a weak owner from an actor handle only after the actor has been reclaimed.
 * Responsibilities: A stale handle produces a dead owner token, so subscription fails without storing a callback that
 *   could later receive a message.
 */
MW_TEST_CASE(EngineMessagingStaleActorHandleOwnerSubscriptionIsInvalidAndNeverDelivers)
{
	/** Motivation: Names the channel used to exercise stale-owner subscription validation. */
	constexpr FNameId StaleOwnerChannelNameId = MakeNameId("StaleOwnerChannel");

	/** Motivation: Names the message sent after stale-owner subscription rejection. */
	constexpr FNameId StaleOwnerMessageNameId = MakeNameId("StaleOwnerMessage");

	/** Motivation: Keeps stale-owner validation independent from transport reliability behavior. */
	constexpr bool bStaleOwnerChannelIsReliable = false;

	/** Motivation: Starts the world before the actor used to mint the stale handle is destroyed. */
	constexpr TimePointMilliseconds StaleOwnerBeginPlayMilliseconds{3000};

	/** Motivation: Applies destruction and makes the captured actor handle stale. */
	constexpr TimePointMilliseconds StaleOwnerDestructionTickMilliseconds{3010};

	/** Motivation: States that a rejected stale-owner subscription must never receive delivery. */
	constexpr std::size_t ExpectedStaleOwnerDeliveryCount = 0;

	/** Motivation: States that rejected subscriptions need no dead-owner cleanup. */
	constexpr std::uint32_t ExpectedReclaimedSubscriptionCount = 0;

	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	std::size_t DeliveryCount{};
	FBeginPlayMessagingSubscriptionContext SubscriptionContext{};
	const FMessagingSystemInformation Information{};
	const ERuntimeResult CreateMessagingResult = Engine.CreateMessagingSystem(Information);
	FMessagingSystem* const MessagingSystem = Engine.GetMessagingSystem();
	const bool bMessagingSystemCreated = MessagingSystem != nullptr;
	const EObjectResult RegisterClassResult =
		Engine.RegisterClass<FBeginPlayMessagingSubscriberActor>(BeginPlayMessagingSubscriberActorTypeId, BeginPlayMessagingSubscriberActorClassName);
	const auto ActorCreation = Engine.CreateObject<FBeginPlayMessagingSubscriberActor>(
		BeginPlayMessagingSubscriberActorTypeId, nullptr, StaleOwnerChannelNameId, SubscriptionContext);
	const auto World = Engine.CreateWorld();
	const bool bWorldCreated = World.Get() != nullptr;
	const FChannelInformation ChannelInformation{StaleOwnerChannelNameId, bStaleOwnerChannelIsReliable, nullptr, {}};
	const EMessagingResult CreateChannelResult =
		MessagingSystem != nullptr ? MessagingSystem->CreateChannel(ChannelInformation) : EMessagingResult::Invalid;
	const MicroWorld::Engine::FObjectHandle ActorHandle = ActorCreation.Object.Handle();
	const EEngineResult RegisterActorResult =
		World.Get()->RegisterActor(MicroWorld::Engine::TObjectPtr<MicroWorld::Engine::AActor>{ActorCreation.Object});
	const ERuntimeResult BeginPlayResult = Engine.BeginPlay(StaleOwnerBeginPlayMilliseconds);
	FMessage Message;
	Message.SetMessageNameId(StaleOwnerMessageNameId);

	// Act
	const EEngineResult DestroyResult = World.Get()->DestroyActor(MicroWorld::Engine::TObjectPtr<MicroWorld::Engine::AActor>{ActorCreation.Object});
	const ERuntimeResult DestructionTickResult = Engine.Tick(StaleOwnerDestructionTickMilliseconds);
	const bool bActorWasReclaimed = ActorCreation.Object.Get() == nullptr;
	const FWeakOwner StaleOwner = MicroWorld::Engine::MakeWeakOwner(Engine.GetObjectStore(), ActorHandle);
	FSubscriberDelegate Subscriber;
	const EDelegateResult BindResult = Subscriber.Bind([&DeliveryCount](const FMessage&) noexcept { ++DeliveryCount; });
	const EMessagingResult SubscribeResult = MessagingSystem != nullptr
		? MessagingSystem->SubscribeToChannel(StaleOwnerChannelNameId, std::move(Subscriber), StaleOwner)
		: EMessagingResult::Invalid;
	const EMessagingResult SendResult =
		MessagingSystem != nullptr ? MessagingSystem->SendMessageToChannel(Message, StaleOwnerChannelNameId) : EMessagingResult::Invalid;
	const std::uint32_t ReclaimedSubscriptionCount = MessagingSystem != nullptr ? MessagingSystem->GetReclaimedDeadOwnerSubscriptionCount() : 0;

	// Assert
	MW_EXPECT_EQ(Test, EObjectResult::Success, RegisterClassResult, "The stale owner actor class should register");
	MW_EXPECT_TRUE(Test, bActorWasReclaimed, "The actor handle should be stale after its destruction tick");
	MW_EXPECT_TRUE(Test, bWorldCreated, "The engine should create a world for stale-owner validation");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, CreateMessagingResult, "The engine should create Messaging for stale-owner validation");
	MW_EXPECT_TRUE(Test, bMessagingSystemCreated, "The engine should expose Messaging for stale-owner validation");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, CreateChannelResult, "The stale-owner channel should be created");
	MW_EXPECT_EQ(Test, EEngineResult::Success, RegisterActorResult, "The stale owner actor should register with the world");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginPlayResult, "The stale owner world should begin play");
	MW_EXPECT_EQ(Test, EEngineResult::Success, DestroyResult, "The stale owner actor should queue for destruction");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, DestructionTickResult, "The stale owner destruction tick should complete");
	MW_EXPECT_EQ(Test, EDelegateResult::Success, BindResult, "The stale-owner subscriber should bind");
	MW_EXPECT_EQ(Test, EMessagingResult::Invalid, SubscribeResult, "A stale actor owner should reject subscription");
	MW_EXPECT_EQ(Test, EMessagingResult::Success, SendResult, "A message after stale-owner rejection should send successfully");
	MW_EXPECT_EQ(Test, ExpectedStaleOwnerDeliveryCount, DeliveryCount, "A rejected stale-owner subscription should never receive delivery");
	MW_EXPECT_EQ(
		Test, ExpectedReclaimedSubscriptionCount, ReclaimedSubscriptionCount, "A rejected stale owner should require no subscription reclamation");
}

} // namespace
