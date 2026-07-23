#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/InlineTypes.h>
#include <MicroWorld/Object/ClassDescriptor.h>
#include <MicroWorld/Object/ObjectPtr.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace
{

/** The sensor samples every 100 ms; the demo's tick times straddle this cadence. */
constexpr MicroWorld::DurationMilliseconds SensorCadenceMilliseconds = 100;

/** Samples a host value at its own 100 ms cadence in the managed lifecycle example. */
class FSensorComponent final : public MicroWorld::UActorComponent
{
public:
	/** Selects a 100 ms schedule so the trace includes due and not-due updates. */
	FSensorComponent() noexcept : UActorComponent(MicroWorld::FTickConfiguration::EnabledEvery(SensorCadenceMilliseconds)) {}

protected:
	/** Marks Component startup so its order relative to the Actor is visible. */
	void BeginPlay() noexcept override { std::printf("sensor begin\n"); }

	/** Prints canonical time and per-Component delta to demonstrate schedule ownership. */
	void TickComponent(const MicroWorld::FTickContext& Context) noexcept override
	{
		std::printf(
			"sensor tick now=%llu delta=%u\n",
			static_cast<unsigned long long>(Context.NowMilliseconds),
			static_cast<unsigned>(Context.DeltaMilliseconds));
	}

	/** Marks Component shutdown so reverse lifecycle order is visible. */
	void EndPlay() noexcept override { std::printf("sensor end\n"); }
};

/** Aggregates Component state while owning its component registry inline. */
class FDeviceActor final : public MicroWorld::TInlineActor<1>
{
public:
	/** Disables only the Actor schedule so Component independence is observable. */
	FDeviceActor() noexcept : TInlineActor<1>({/*bCanEverTick*/ true, /*bStartWithTickEnabled*/ false, /*TickIntervalMilliseconds*/ 0}) {}

protected:
	/** Marks Actor startup after the Component begin hook. */
	void BeginPlay() noexcept override { std::printf("actor begin (primary tick disabled)\n"); }

	/** Would expose an incorrect Actor execution if disabled scheduling regressed. */
	void Tick(const MicroWorld::FTickContext&) noexcept override { std::printf("actor tick\n"); }

	/** Marks Actor shutdown before the Component end hook. */
	void EndPlay() noexcept override { std::printf("actor end\n"); }
};

/** Stable type id for the example's user-derived managed actor descriptor. */
constexpr MicroWorld::FTypeId DeviceActorTypeId{0x00010001u};

/** Stable type id for the example's user-derived managed component descriptor. */
constexpr MicroWorld::FTypeId SensorComponentTypeId{0x00010002u};

} // namespace

/** Builds a managed composition through TEngineHost and prints deterministic lifecycle evidence. */
int main()
{
	using namespace MicroWorld;

	// TEngineHost owns every subsystem — class registry, object store, garbage collector, world
	// actor registry, and timer manager — and registers the three engine base descriptors itself.
	// The inline actor embeds its component registry, so slots stay as wide as before (512 bytes).
	using FDeviceHost = TEngineHost<
		5 /*MaxClasses*/,
		3 /*MaxObjects*/,
		512 /*SlotSizeBytes*/,
		16 /*SlotAlign*/,
		1 /*MaxRoots*/,
		1 /*MaxActors*/,
		1 /*MaxTimers*/,
		32 /*InlineTimerCallbackBytes*/>;
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
		|| Host.RegisterClass<FSensorComponent>(SensorComponentTypeId, "SensorComponent") != EObjectResult::Success)
	{
		return 1;
	}

	const TObjectPtr<UWorld> World = Host.CreateWorld();
	const TObjectPtr<FDeviceActor> Device = Host.CreateObject<FDeviceActor>(DeviceActorTypeId).Object;
	const TObjectPtr<FSensorComponent> Sensor = Host.CreateObject<FSensorComponent>(SensorComponentTypeId).Object;
	if (World.Get() == nullptr || Device.Get() == nullptr || Sensor.Get() == nullptr)
	{
		return 1;
	}

	// BeginPlay begins components before their owning actor, so "sensor begin" prints first.
	if (Device.Get()->RegisterComponent(Sensor) != EEngineResult::Success
		|| Host.GetWorld().RegisterActor(TObjectPtr<AActor>{Device}) != EEngineResult::Success || Host.BeginPlay(0) != ERuntimeResult::Success)
	{
		return 1;
	}

	// Early, exact-deadline, and late tick times for the 100 ms sensor schedule.
	constexpr TimePointMilliseconds TickTimesMilliseconds[] = {0, 50, 100, 175, 200};
	for (const TimePointMilliseconds Now : TickTimesMilliseconds)
	{
		// The sensor ticks only on its own 100 ms cadence; the actor's schedule is disabled and
		// never prints a tick, so this loop demonstrates component and actor schedules run independently.
		if (Host.Tick(Now) != ERuntimeResult::Success)
		{
			return 1;
		}
	}
	// EndPlay ends the actor before its components, the reverse of BeginPlay's order.
	return Host.EndPlay() == ERuntimeResult::Success ? 0 : 1;
}
