#pragma once

#include "NetworkingConsumerProbe.h"

#include <MicroWorld/Application/Application.h>
#include <MicroWorld/Core/RuntimeResult.h>
#include <MicroWorld/Engine/EngineRuntime.h>
#include <MicroWorld/Engine/World.h>

#include <cstdint>

static_assert(__cplusplus >= 201703L);

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#error "The MicroWorld Application consumer must compile with exceptions disabled."
#endif

#if defined(__GXX_RTTI) || defined(_CPPRTTI)
#error "The MicroWorld Application consumer must compile with RTTI disabled."
#endif

namespace MicroWorldConsumer
{

/**
 * Motivation: Supplies a public engine-runtime contract implementation for an Application consumer probe.
 * Responsibilities: Count the lifecycle calls Application forwards and report success for each bounded turn.
 * Example: FApplicationConsumerRuntime Runtime; FApplicationConsumer Application(Runtime);
 */
class FApplicationConsumerRuntime final : public MicroWorld::Engine::IEngineRuntime
{
public:
	/** Motivation: Counts Application's successful start forwarding. */
	std::uint8_t BeginCount{0};
	/** Motivation: Counts Application's successful frame forwarding. */
	std::uint8_t TickCount{0};
	/** Motivation: Counts Application's successful shutdown forwarding. */
	std::uint8_t EndCount{0};

	/**
	 * Motivation: Lets the Application probe begin an observable engine lifecycle.
	 * Responsibilities: Count one begin call and report success.
	 */
	MicroWorld::Core::ERuntimeResult BeginPlay(MicroWorld::Core::TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		(void)InNowMilliseconds;
		++BeginCount;
		return MicroWorld::Core::ERuntimeResult::Success;
	}

	/**
	 * Motivation: Lets the Application probe advance an observable engine lifecycle.
	 * Responsibilities: Count one tick call and report success.
	 */
	MicroWorld::Core::ERuntimeResult Tick(MicroWorld::Core::TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		(void)InNowMilliseconds;
		++TickCount;
		return MicroWorld::Core::ERuntimeResult::Success;
	}

	/**
	 * Motivation: Lets the Application probe finish an observable engine lifecycle.
	 * Responsibilities: Count one end call and report success.
	 */
	MicroWorld::Core::ERuntimeResult EndPlay() noexcept override
	{
		++EndCount;
		return MicroWorld::Core::ERuntimeResult::Success;
	}
};

/**
 * Motivation: Makes FApplication's protected construction available to the public consumer probe.
 * Responsibilities: Bind one externally owned runtime and record no additional application state.
 * Example: FApplicationConsumer Application(Runtime);
 */
class FApplicationConsumer final : public MicroWorld::Application::FApplication
{
public:
	/**
	 * Motivation: Binds the consumer application to its probe runtime.
	 * Responsibilities: Forward the runtime reference to FApplication unchanged.
	 */
	explicit FApplicationConsumer(MicroWorld::Engine::IEngineRuntime& InRuntime) noexcept : FApplication(InRuntime) {}

private:
	/**
	 * Motivation: Satisfies FApplication's rollback hook for an all-success probe runtime.
	 * Responsibilities: Perform no work because the probe creates no external resources before begin.
	 */
	void OnBeginPlayFailed() noexcept override {}
};

} // namespace MicroWorldConsumer

/**
 * Motivation: Exercises the public Application lifecycle contract above the Engine runtime interface.
 * Responsibilities: Prove Application forwards begin, one frame, and end once in lifecycle order.
 */
inline int RunApplicationConsumerProbe() noexcept
{
	if (RunNetworkingConsumerProbe() != 0)
	{
		return 1;
	}
	const MicroWorld::Engine::FClassDescriptor* const WorldDescriptor = &MicroWorld::Engine::UWorld::StaticClassDescriptor();
	if (WorldDescriptor == nullptr)
	{
		return 2;
	}

	MicroWorldConsumer::FApplicationConsumerRuntime Runtime;
	MicroWorldConsumer::FApplicationConsumer Application(Runtime);
	const bool bLifecycleSucceeded = Application.BeginPlay(0) == MicroWorld::Core::ERuntimeResult::Success
		&& Application.Advance(1) == MicroWorld::Core::ERuntimeResult::Success && Application.EndPlay() == MicroWorld::Core::ERuntimeResult::Success;
	const bool bForwardedExactlyOnce = Runtime.BeginCount == 1 && Runtime.TickCount == 1 && Runtime.EndCount == 1;
	return bLifecycleSucceeded && bForwardedExactlyOnce ? 0 : 3;
}
