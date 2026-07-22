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

/** Samples a host value at its own 100 ms cadence in the managed lifecycle example. */
class FSensorComponent final : public MicroWorld::UActorComponent
{
public:
	/** Selects a 100 ms schedule so the trace includes due and not-due updates. */
	FSensorComponent() noexcept : UActorComponent({true, true, 100}) {}

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
	FDeviceActor() noexcept : TInlineActor<1>({true, false, 0}) {}

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
	using FDeviceHost = TEngineHost<5, 3, 512, 16, 1, 1, 1, 32>;
	FDeviceHost Host{FGarbageCollectionBudget{1, 4, 8}};

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

	if (Device.Get()->RegisterComponent(Sensor) != EEngineResult::Success
		|| Host.GetWorld().RegisterActor(TObjectPtr<AActor>{Device}) != EEngineResult::Success || Host.BeginPlay(0) != ERuntimeResult::Success)
	{
		return 1;
	}

	// Early, exact-deadline, and late updates for the 100 ms Component schedule.
	constexpr TimePointMilliseconds Updates[] = {0, 50, 100, 175, 200};
	for (const TimePointMilliseconds Now : Updates)
	{
		if (Host.Tick(Now) != ERuntimeResult::Success)
		{
			return 1;
		}
	}
	return Host.EndPlay() == ERuntimeResult::Success ? 0 : 1;
}
