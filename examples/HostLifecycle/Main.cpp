#include <MicroWorld/Core/TickContext.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/DefaultEngineTraits.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Engine/WorldSubsystem.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace
{

/** Motivation: The sensor samples every 100 ms; the demo's tick times straddle this cadence. */
constexpr MicroWorld::Core::DurationMilliseconds SensorCadenceMilliseconds = 100;

/** Motivation: Distinguishes class-registration failure from later composition failures. */
constexpr int ClassRegistrationFailureExitCode = 1;

/** Motivation: Distinguishes managed-object construction failure from later composition failures. */
constexpr int ObjectCreationFailureExitCode = 2;

/** Motivation: Distinguishes subsystem registration failure from actor-graph registration failures. */
constexpr int SubsystemRegistrationFailureExitCode = 3;

/** Motivation: Distinguishes actor-graph registration or startup failure from lookup failures. */
constexpr int LifecycleStartupFailureExitCode = 4;

/** Motivation: Distinguishes actor-facing subsystem lookup failure from shutdown failures. */
constexpr int SubsystemLookupFailureExitCode = 5;

/** Motivation: Distinguishes shutdown ordering failure from earlier composition failures. */
constexpr int LifecycleShutdownFailureExitCode = 6;

/**
 * Motivation: Exposes one world-scoped application service whose state proves subsystem lifecycle ordering.
 * Responsibilities: Become ready during initialization, remain ready through actor shutdown, and record deinitialization.
 * Example:
 *   UHostLifecycleSubsystem Service;
 *   const bool bReady = Service.IsReady();
 */
class UHostLifecycleSubsystem final : public MicroWorld::Engine::UWorldSubsystem
{
public:
	/**
	 * Motivation: The host verifies that actors observe initialized service state.
	 * Responsibilities: Return whether the subsystem is between Initialize and Deinitialize.
	 */
	bool IsReady() const noexcept { return bIsReady; }

	/**
	 * Motivation: The host verifies exactly one subsystem startup callback.
	 * Responsibilities: Return the number of Initialize callbacks observed.
	 */
	std::uint32_t GetInitializeCount() const noexcept { return InitializeCount; }

	/**
	 * Motivation: The host verifies exactly one subsystem shutdown callback.
	 * Responsibilities: Return the number of Deinitialize callbacks observed.
	 */
	std::uint32_t GetDeinitializeCount() const noexcept { return DeinitializeCount; }

protected:
	/**
	 * Motivation: Makes the service available before actor startup.
	 * Responsibilities: Mark the subsystem ready and record this callback.
	 */
	void Initialize() noexcept override
	{
		bIsReady = true;
		++InitializeCount;
	}

	/**
	 * Motivation: Makes shutdown observable after actor EndPlay completes.
	 * Responsibilities: Mark the subsystem not ready and record this callback.
	 */
	void Deinitialize() noexcept override
	{
		bIsReady = false;
		++DeinitializeCount;
	}

private:
	/** Motivation: Records whether actor-facing service work is currently valid. */
	bool bIsReady{false};

	/** Motivation: Counts startup callbacks for the deterministic lifecycle assertion. */
	std::uint32_t InitializeCount{0};

	/** Motivation: Counts shutdown callbacks for the deterministic lifecycle assertion. */
	std::uint32_t DeinitializeCount{0};
};

/**
 * Motivation: Samples a host value at its own 100 ms cadence in the managed lifecycle example, so the
 *   trace shows a component ticking on its own schedule independent of its actor.
 * Responsibilities: Print begin/end markers and a canonical per-tick time and delta line.
 * Example:
 *   FSensorComponent Sensor;
 *   Sensor.TickComponent(Context);
 */
class FSensorComponent final : public MicroWorld::Engine::UActorComponent
{
public:
	/**
	 * Motivation: Selects a 100 ms schedule so the trace includes both due and not-due updates.
	 * Responsibilities: Construct the component on the shared 100 ms cadence.
	 */
	FSensorComponent() noexcept : UActorComponent(MicroWorld::Core::FTickConfiguration::EnabledEvery(SensorCadenceMilliseconds)) {}

protected:
	/**
	 * Motivation: Marks Component startup so its order relative to the Actor is visible in the trace.
	 * Responsibilities: Print the sensor begin marker and do nothing else.
	 */
	void BeginPlay() noexcept override { std::printf("sensor begin\n"); }

	/**
	 * Motivation: Prints canonical time and per-Component delta to demonstrate schedule ownership.
	 * Responsibilities: Print the now time and delta on each due tick.
	 */
	void TickComponent(const MicroWorld::Core::FTickContext& InContext) noexcept override
	{
		std::printf(
			"sensor tick now=%llu delta=%u\n",
			static_cast<unsigned long long>(InContext.NowMilliseconds),
			static_cast<unsigned>(InContext.DeltaMilliseconds));
	}

	/**
	 * Motivation: Marks Component shutdown so reverse lifecycle order is visible in the trace.
	 * Responsibilities: Print the sensor end marker and do nothing else.
	 */
	void EndPlay() noexcept override { std::printf("sensor end\n"); }
};

/**
 * Motivation: Aggregates component state through the actor's fixed component slots, with its own
 *   primary schedule disabled so component independence is observable.
 * Responsibilities: Print begin/tick/end markers that bracket the component's lifecycle.
 * Example:
 *   FDeviceActor Device;
 *   Device.BeginPlay();
 */
class FDeviceActor final : public MicroWorld::Engine::AActor
{
public:
	/**
	 * Motivation: Disables only the Actor schedule so Component independence is observable.
	 * Responsibilities: Construct the actor tickable but with tick start disabled.
	 */
	FDeviceActor() noexcept : AActor({/*bCanEverTick*/ true, /*bStartWithTickEnabled*/ false, /*TickIntervalMilliseconds*/ 0}) {}

	/**
	 * Motivation: The host verifies that subsystem initialization precedes actor startup.
	 * Responsibilities: Return whether BeginPlay resolved a ready world subsystem.
	 */
	bool ObservedReadySubsystemDuringBegin() const noexcept { return bObservedReadySubsystemDuringBegin; }

	/**
	 * Motivation: The host verifies that subsystem deinitialization follows actor shutdown.
	 * Responsibilities: Return whether EndPlay still resolved a ready world subsystem.
	 */
	bool ObservedReadySubsystemDuringEnd() const noexcept { return bObservedReadySubsystemDuringEnd; }

protected:
	/**
	 * Motivation: Marks Actor startup after the Component begin hook.
	 * Responsibilities: Print the actor begin marker and do nothing else.
	 */
	void BeginPlay() noexcept override
	{
		MicroWorld::Engine::UWorld* World = GetOwnerWorld();
		UHostLifecycleSubsystem* Subsystem = World == nullptr ? nullptr : World->GetSubsystem<UHostLifecycleSubsystem>();
		bObservedReadySubsystemDuringBegin = Subsystem != nullptr && Subsystem->IsReady();
		std::printf("actor begin (primary tick disabled)\n");
	}

	/**
	 * Motivation: Would expose an incorrect Actor execution if disabled scheduling regressed.
	 * Responsibilities: Print the actor tick marker only if its schedule ever runs.
	 */
	void Tick(const MicroWorld::Core::FTickContext&) noexcept override { std::printf("actor tick\n"); }

	/**
	 * Motivation: Marks Actor shutdown before the Component end hook.
	 * Responsibilities: Print the actor end marker and do nothing else.
	 */
	void EndPlay() noexcept override
	{
		MicroWorld::Engine::UWorld* World = GetOwnerWorld();
		UHostLifecycleSubsystem* Subsystem = World == nullptr ? nullptr : World->GetSubsystem<UHostLifecycleSubsystem>();
		bObservedReadySubsystemDuringEnd = Subsystem != nullptr && Subsystem->IsReady();
		std::printf("actor end\n");
	}

private:
	/** Motivation: Records the actor-visible subsystem state during startup. */
	bool bObservedReadySubsystemDuringBegin{false};

	/** Motivation: Records the actor-visible subsystem state during shutdown. */
	bool bObservedReadySubsystemDuringEnd{false};
};

/** Motivation: Stable type id for the example's user-derived managed actor descriptor. */
constexpr MicroWorld::Engine::FTypeId DeviceActorTypeId{0x00010001u};

/** Motivation: Stable type id for the example's user-derived managed component descriptor. */
constexpr MicroWorld::Engine::FTypeId SensorComponentTypeId{0x00010002u};

/** Motivation: Stable type id for the example's user-derived world subsystem descriptor. */
constexpr MicroWorld::Engine::FTypeId HostLifecycleSubsystemTypeId{0x00010003u};

/**
 * Motivation: Carries the measured capacities needed by the actor, component, and one world subsystem.
 * Responsibilities: Name the class, object, root, actor, subsystem, timer, and inline-callback capacities the demo uses.
 * Example:
 *   using FDeviceHost = TEngine<FDeviceHostTraits>;
 */
struct FDeviceHostTraits : MicroWorld::Engine::FDefaultEngineTraits
{
	static constexpr std::size_t MaxClasses = 7;
	static constexpr std::size_t MaxObjects = 4; // One additional inherited 512-byte slot for the subsystem.
	static constexpr std::size_t MaxRoots = 1;
	static constexpr std::size_t MaxActors = 1;
	static constexpr std::size_t MaxSubsystems = 1;
	static constexpr std::size_t MaxTimers = 1;
	static constexpr std::size_t InlineTimerCallbackBytes = 32;
};

} // namespace

/**
 * Motivation: Application entry point for the HostLifecycle demo, so the single entry point owns the one
 *   place that builds a managed TEngine composition and prints deterministic lifecycle evidence.
 * Responsibilities: Register the subsystem, actor, and component; begin play; tick a straddling schedule;
 *   then verify actor-before-subsystem shutdown.
 */
int main()
{
	using namespace MicroWorld::Core;
	using namespace MicroWorld::Engine;

	// TEngine owns every engine service — class registry, object store, garbage collector, world
	// registries, and timer manager — and registers the four engine base descriptors itself.
	// The 512-byte object slots cover the World, actor, component, and subsystem graph.
	using FDeviceHost = TEngine<FDeviceHostTraits>;
	// Per-tick garbage-collection slice for this demo's tiny managed graph: one
	// root (the world), a few mark steps for world -> actor -> component, and
	// enough sweep steps to scan every object slot, so a cycle finishes quickly.
	constexpr std::uint32_t GcRootOperationsPerTick = 1;
	constexpr std::uint32_t GcMarkOperationsPerTick = 4;
	constexpr std::uint32_t GcSweepOperationsPerTick = 8;
	FDeviceHost Host{FGarbageCollectionBudget{GcRootOperationsPerTick, GcMarkOperationsPerTick, GcSweepOperationsPerTick}};

	// RegisterClass<T> derives each parent from the engine base and registers the descriptor; the
	// host still owns the canonical copy, so CreateObject<T> looks it up by id before constructing.
	if (Host.RegisterClass<FDeviceActor>(DeviceActorTypeId, "DeviceActor") != EObjectResult::Success
		|| Host.RegisterClass<FSensorComponent>(SensorComponentTypeId, "SensorComponent") != EObjectResult::Success
		|| Host.RegisterClass<UHostLifecycleSubsystem>(HostLifecycleSubsystemTypeId, "HostLifecycleSubsystem") != EObjectResult::Success)
	{
		return ClassRegistrationFailureExitCode;
	}

	const TObjectPtr<UWorld> World = Host.CreateWorld();
	const TObjectPtr<FDeviceActor> Device = Host.CreateObject<FDeviceActor>(DeviceActorTypeId).Object;
	const TObjectPtr<FSensorComponent> Sensor = Host.CreateObject<FSensorComponent>(SensorComponentTypeId).Object;
	const TObjectPtr<UHostLifecycleSubsystem> Subsystem = Host.CreateObject<UHostLifecycleSubsystem>(HostLifecycleSubsystemTypeId).Object;
	if (World.Get() == nullptr || Device.Get() == nullptr || Sensor.Get() == nullptr || Subsystem.Get() == nullptr)
	{
		return ObjectCreationFailureExitCode;
	}

	if (Host.GetWorld().RegisterSubsystem(TObjectPtr<UWorldSubsystem>{Subsystem}) != EEngineResult::Success)
	{
		return SubsystemRegistrationFailureExitCode;
	}

	// BeginPlay begins components before their owning actor, so "sensor begin" prints first.
	if (Device.Get()->RegisterComponent(Sensor) != EEngineResult::Success
		|| Host.GetWorld().RegisterActor(TObjectPtr<AActor>{Device}) != EEngineResult::Success || Host.BeginPlay(0) != ERuntimeResult::Success)
	{
		return LifecycleStartupFailureExitCode;
	}
	if (Host.GetWorld().GetSubsystem<UHostLifecycleSubsystem>() != Subsystem.Get() || !Subsystem.Get()->IsReady()
		|| Subsystem.Get()->GetInitializeCount() != 1 || !Device.Get()->ObservedReadySubsystemDuringBegin())
	{
		return SubsystemLookupFailureExitCode;
	}

	// Early, exact-deadline, and late tick times for the 100 ms sensor schedule.
	constexpr TimePointMilliseconds TickTimesMilliseconds[] = {0, 50, 100, 175, 200};
	for (const TimePointMilliseconds Now : TickTimesMilliseconds)
	{
		// The sensor ticks only on its own 100 ms cadence; the actor's schedule is disabled and
		// never prints a tick, so this loop demonstrates component and actor schedules run independently.
		if (Host.Tick(Now) != ERuntimeResult::Success)
		{
			return LifecycleStartupFailureExitCode;
		}
	}
	// EndPlay ends the actor before its components, the reverse of BeginPlay's order.
	const ERuntimeResult EndResult = Host.EndPlay();
	if (EndResult != ERuntimeResult::Success || !Device.Get()->ObservedReadySubsystemDuringEnd() || Subsystem.Get()->IsReady()
		|| Subsystem.Get()->GetDeinitializeCount() != 1)
	{
		return LifecycleShutdownFailureExitCode;
	}
	return 0;
}
