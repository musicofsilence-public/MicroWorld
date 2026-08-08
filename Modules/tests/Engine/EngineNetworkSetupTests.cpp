#include "TestSupport.h"
#include "EngineMessagingTestHelpers.h"

#include <MicroWorld/Engine/EngineNetworkSetup.h>
#include <MicroWorld/Engine/ObjectInitializer.h>

namespace
{

using namespace ::MicroWorld::Tests;

using MicroWorld::Core::ERuntimeResult;
using MicroWorld::Core::MakeLoopbackAddress;
using MicroWorld::Engine::EEngineNetworkSetupResult;
using MicroWorld::Engine::FEngineNetworkSetup;
using MicroWorld::Networking::ENetworkRole;

/**
 * Motivation: Forces a pre-play default-subobject capacity failure after Engine-owned networking has begun.
 * Responsibilities: Request one component beyond the actor's bounded construction capacity without touching the configured device.
 * Example: FNetworkSetupBeginFailureActor Actor{Initializer};
 */
class FNetworkSetupBeginFailureActor final : public MicroWorld::Engine::AActor
{
public:
	/**
	 * Motivation: Exercises failure cleanup after systems begin.
	 * Responsibilities: Request five default components against the fixed four-component limit.
	 */
	explicit FNetworkSetupBeginFailureActor(MicroWorld::Engine::FObjectInitializer& Initializer) noexcept : AActor(Initializer)
	{
		(void)Initializer.CreateDefaultSubobject<MicroWorld::Engine::UActorComponent>();
		(void)Initializer.CreateDefaultSubobject<MicroWorld::Engine::UActorComponent>();
		(void)Initializer.CreateDefaultSubobject<MicroWorld::Engine::UActorComponent>();
		(void)Initializer.CreateDefaultSubobject<MicroWorld::Engine::UActorComponent>();
		(void)Initializer.CreateDefaultSubobject<MicroWorld::Engine::UActorComponent>();
	}
};

/**
 * Motivation: Verifies Engine owns the first client connect attempt without exposing its Messaging or Network internals.
 * Responsibilities: Confirm setup and BeginPlay do not send, while the first Engine frame starts the configured initial route.
 */
MW_TEST_CASE(EngineNetworkSetupDefersInitialClientRequestUntilFirstFrame)
{
	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	FTestTransportDevice Device;
	FEngineNetworkSetup Setup{};
	Setup.Role = ENetworkRole::Client;
	Setup.InitialServerAddress = MakeLoopbackAddress(1);

	// Act
	const EEngineNetworkSetupResult SetupResult = Engine.ConfigureNetworking(Device, Setup);
	const auto World = Engine.CreateWorld();
	const ERuntimeResult BeginResult = Engine.BeginPlay(0);
	const std::size_t SendsBeforeFrame = Device.TrySendCallCount;
	const ERuntimeResult TickResult = Engine.Tick(1);
	const ERuntimeResult EndResult = Engine.EndPlay();

	// Assert
	MW_EXPECT_EQ(Test, EEngineNetworkSetupResult::Success, SetupResult, "Valid client setup should compose Engine-owned networking");
	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "A configured engine should still create its World");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "Network setup should not prevent World startup");
	MW_EXPECT_EQ(Test, std::size_t{0}, SendsBeforeFrame, "Setup and BeginPlay must not emit the first connect request");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, TickResult, "The first Engine frame should complete normally");
	MW_EXPECT_EQ(Test, std::size_t{1}, Device.TrySendCallCount, "The first Network pre-advance should emit one connect request");
	MW_EXPECT_TRUE(Test, !World.Get()->HasActiveNetworkPeer(), "A configured but unanswered route is not an active peer");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, EndResult, "The configured Engine should end normally");
	MW_EXPECT_EQ(Test, std::size_t{1}, Device.BeginPlayCallCount, "A configuration-only device should begin once through Engine");
	MW_EXPECT_EQ(Test, std::size_t{1}, Device.PreAdvanceCallCount, "A configuration-only device should pre-advance once through Engine");
	MW_EXPECT_EQ(Test, std::size_t{1}, Device.PostAdvanceCallCount, "A configuration-only device should post-advance once through Engine");
	MW_EXPECT_EQ(Test, std::size_t{1}, Device.EndPlayCallCount, "A configuration-only device should end once through Engine");
}

/**
 * Motivation: Keeps invalid setup rejection transactional so an application can correct its value contract and retry.
 * Responsibilities: Reject an addressless client without publishing any partial chain, then accept the corrected setup once.
 */
MW_TEST_CASE(EngineNetworkSetupRejectsInvalidClientAddressAndAllowsCleanRetry)
{
	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	FTestTransportDevice Device;
	FEngineNetworkSetup InvalidSetup{};
	InvalidSetup.Role = ENetworkRole::Client;
	FEngineNetworkSetup ValidSetup = InvalidSetup;
	ValidSetup.InitialServerAddress = MakeLoopbackAddress(1);

	// Act
	const EEngineNetworkSetupResult InvalidResult = Engine.ConfigureNetworking(Device, InvalidSetup);
	const EEngineNetworkSetupResult RetryResult = Engine.ConfigureNetworking(Device, ValidSetup);
	const EEngineNetworkSetupResult DuplicateResult = Engine.ConfigureNetworking(Device, ValidSetup);

	// Assert
	MW_EXPECT_EQ(Test, EEngineNetworkSetupResult::InvalidConfiguration, InvalidResult, "A client needs one initial server address");
	MW_EXPECT_EQ(Test, std::size_t{0}, Device.BeginPlayCallCount, "Rejected configuration must not publish a lifecycle-owned device chain");
	MW_EXPECT_EQ(Test, std::size_t{0}, Device.TrySendCallCount, "Rejected configuration must not send through the candidate device");
	MW_EXPECT_EQ(Test, EEngineNetworkSetupResult::Success, RetryResult, "A rejected value contract must leave setup retryable");
	MW_EXPECT_EQ(Test, EEngineNetworkSetupResult::AlreadyConfigured, DuplicateResult, "A configured Engine must retain its one device binding");
}

/**
 * Motivation: Prevents a failed pre-play actor graph from leaving configured networking live or able to send.
 * Responsibilities: Verify Engine ends each started setup system after InitializationFailed and emits no protocol traffic.
 */
MW_TEST_CASE(EngineNetworkSetupEndsStartedSystemsWhenPrePlayConstructionFails)
{
	// Arrange
	FEngine Engine{EngineMessagingCollectionBudget};
	FTestTransportDevice Device;
	FEngineNetworkSetup Setup{};
	Setup.Role = ENetworkRole::Client;
	Setup.InitialServerAddress = MakeLoopbackAddress(1);
	const EEngineNetworkSetupResult SetupResult = Engine.ConfigureNetworking(Device, Setup);
	const auto World = Engine.CreateWorld();
	const auto Request = World.Get()->SpawnActor<FNetworkSetupBeginFailureActor>();

	// Act
	const ERuntimeResult BeginResult = Engine.BeginPlay(0);

	// Assert
	MW_EXPECT_EQ(Test, EEngineNetworkSetupResult::Success, SetupResult, "The valid network setup must compose before the unrelated actor fails");
	MW_EXPECT_EQ(
		Test,
		MicroWorld::Engine::EActorSpawnRequestResult::Queued,
		Request.Result,
		"The failing actor should queue before its constructor-time capacity is known");
	MW_EXPECT_EQ(Test, ERuntimeResult::InitializationFailed, BeginResult, "Pre-play construction failure must abort composed Engine startup");
	MW_EXPECT_EQ(Test, std::size_t{1}, Device.BeginPlayCallCount, "The configured device must start before the World reports its pre-play failure");
	MW_EXPECT_EQ(Test, std::size_t{1}, Device.EndPlayCallCount, "Engine must end the configured device during startup rollback");
	MW_EXPECT_EQ(Test, std::size_t{0}, Device.TrySendCallCount, "Failed startup must not advance networking far enough to emit a connect request");
}

/**
 * Motivation: Preserves the legacy caller-bound system constructor without allowing it to advance a configured device twice.
 * Responsibilities: Bind one transport through both compatible entry points and observe exactly one lifecycle and frame turn per Engine call.
 */
MW_TEST_CASE(EngineNetworkSetupDoesNotDoublePumpDeviceBoundAsLegacySystem)
{
	// Arrange
	FTestTransportDevice Device;
	FEngine Engine{EngineMessagingCollectionBudget, Device};
	FEngineNetworkSetup Setup{};
	Setup.Role = ENetworkRole::Client;
	Setup.InitialServerAddress = MakeLoopbackAddress(1);

	// Act
	const EEngineNetworkSetupResult SetupResult = Engine.ConfigureNetworking(Device, Setup);
	const auto World = Engine.CreateWorld();
	const ERuntimeResult BeginResult = Engine.BeginPlay(0);
	const ERuntimeResult TickResult = Engine.Tick(1);
	const ERuntimeResult EndResult = Engine.EndPlay();

	// Assert
	MW_EXPECT_EQ(
		Test, EEngineNetworkSetupResult::Success, SetupResult, "The same transport may satisfy the legacy system constructor and network setup");
	MW_EXPECT_TRUE(Test, World.Get() != nullptr, "The configured Engine should create its World");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "The composed Engine should begin successfully");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, TickResult, "The composed Engine should tick successfully");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, EndResult, "The composed Engine should end successfully");
	MW_EXPECT_EQ(Test, std::size_t{1}, Device.BeginPlayCallCount, "The configured device should begin exactly once");
	MW_EXPECT_EQ(Test, std::size_t{1}, Device.PreAdvanceCallCount, "The configured device should pre-advance exactly once per frame");
	MW_EXPECT_EQ(Test, std::size_t{1}, Device.PostAdvanceCallCount, "The configured device should post-advance exactly once per frame");
	MW_EXPECT_EQ(Test, std::size_t{1}, Device.EndPlayCallCount, "The configured device should end exactly once");
}

} // namespace
