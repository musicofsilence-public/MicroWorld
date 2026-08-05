#pragma once

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineClassIds.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/ClassRegistry.h>
#include <MicroWorld/Engine/ClassRegistryView.h>
#include <MicroWorld/Engine/GarbageCollectionBudget.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/GarbageCollectorStorage.h>
#include <MicroWorld/Engine/Object.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Engine/ObjectRootEntry.h>
#include <MicroWorld/Engine/ObjectSlotMetadata.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/ObjectStoreStorage.h>
#include <MicroWorld/Engine/StrongObjectPtr.h>
#include <MicroWorld/Engine/WorldSubsystem.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace MicroWorld::Tests
{

using namespace ::MicroWorld::Engine;

/**
 * Motivation: Shares one monotonic event sequence across every observed object in a test so begin/tick/end
 *   ordering is recorded without per-object clocks.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FSequenceCounter final
{
public:
	/**
	 * Motivation: Callers can record relative ordering.
	 * Responsibilities: Returns the next sequence value.
	 */
	std::uint32_t Next() noexcept { return ++Value; }

private:
	/** Motivation: Tracks the highest sequence value handed out in this test. */
	std::uint32_t Value{0};
};

/**
 * Motivation: Records the begin/tick/end sequence values observed by one actor.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FActorEventState final
{
	/** Motivation: Sequence value of this actor's BeginPlay hook. */
	std::uint32_t BeginOrder{0};
	/** Motivation: Counts BeginPlay hook invocations so repeated lifecycle calls are observable. */
	std::uint32_t BeginCount{0};
	/** Motivation: Sequence value of this actor's Tick hook. */
	std::uint32_t TickOrder{0};
	/** Motivation: Sequence value of this actor's EndPlay hook. */
	std::uint32_t EndOrder{0};
	/** Motivation: Counts EndPlay hook invocations so repeated shutdown is observable. */
	std::uint32_t EndCount{0};
	/** Motivation: Counts Tick hook invocations so interval tests can bound ticks per advance. */
	std::uint32_t TickCount{0};
};

/**
 * Motivation: Records the begin/tick/end sequence values observed by one component.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FComponentEventState final
{
	/** Motivation: Sequence value of this component's BeginPlay hook. */
	std::uint32_t BeginOrder{0};
	/** Motivation: Counts BeginPlay hook invocations so repeated lifecycle calls are observable. */
	std::uint32_t BeginCount{0};
	/** Motivation: Sequence value of this component's TickComponent hook. */
	std::uint32_t TickOrder{0};
	/** Motivation: Sequence value of this component's EndPlay hook. */
	std::uint32_t EndOrder{0};
	/** Motivation: Counts EndPlay hook invocations so repeated shutdown is observable. */
	std::uint32_t EndCount{0};
	/** Motivation: Counts TickComponent hook invocations so interval tests can bound ticks per advance. */
	std::uint32_t TickCount{0};
};

/**
 * Motivation: Owns fixed storage for one isolated Engine behavior test and registers the four Engine bases.
 * Responsibilities: Expose construction,
 * root, and storage helpers.
 * Example:
 *   TEngineEnvironment<256, 8, 8, 2> Environment;
 */
template<std::size_t SlotSizeBytes, std::size_t SlotAlignmentBytes, std::uint32_t SlotCount, std::uint32_t RootCapacity>
class TEngineEnvironment final
{
public:
	/**
	 * Motivation: Builds the store with this environment's storage and base classes registered.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	TEngineEnvironment() noexcept : Store(MakeStorage(), MakeClassRegistryView(Registry)) { RegisterBaseClasses(); }

	TEngineEnvironment(const TEngineEnvironment&) = delete;
	TEngineEnvironment& operator=(const TEngineEnvironment&) = delete;
	TEngineEnvironment(TEngineEnvironment&&) = delete;
	TEngineEnvironment& operator=(TEngineEnvironment&&) = delete;

	/**
	 * Motivation: Returns the public store backed by this environment's caller-owned storage.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FObjectStore& GetStore() noexcept { return Store; }

	/**
	 * Motivation: Tests can register user-derived descriptors.
	 * Responsibilities: Returns the class registry.
	 */
	TClassRegistry<8>& GetRegistry() noexcept { return Registry; }

	/**
	 * Motivation: Returns the registry-owned descriptor for one engine base type id.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	const FClassDescriptor* FindDescriptor(const FTypeId InTypeId) noexcept { return Registry.Find(InTypeId); }

	/**
	 * Motivation: Constructs one base engine object (UWorld, AActor, or UActorComponent) in this environment's store
	 *   using its registry-owned descriptor.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	template<typename T, typename... TArguments>
	TObjectPtr<T> CreateObject(const FTypeId InTypeId, TArguments&&... Arguments) noexcept
	{
		const FClassDescriptor* const Descriptor = Registry.Find(InTypeId);
		const auto Result = Store.NewObject<T>(*Descriptor, std::forward<TArguments>(Arguments)...);
		return Result.Object;
	}

	/**
	 * Motivation: Registers one user-derived descriptor under a stable type id and constructs an instance of the
	 *   derived type through that descriptor.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	template<typename T, typename... TArguments>
	TObjectPtr<T> CreateDerivedObject(const FTypeId InTypeId, const char* const InName, TArguments&&... Arguments) noexcept
	{
		RegisterDerivedClass<T>(InTypeId, InName);
		const FClassDescriptor* const Descriptor = Registry.Find(InTypeId);
		const auto Result = Store.NewObject<T>(*Descriptor, std::forward<TArguments>(Arguments)...);
		return Result.Object;
	}

	/**
	 * Motivation: Registers one user-derived descriptor using the shared managed tracer.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	template<typename T>
	EObjectResult RegisterDerivedClass(const FTypeId InTypeId, const char* const InName) noexcept
	{
		const FClassDescriptor* Parent = nullptr;
		if constexpr (std::is_base_of<AActor, T>::value)
		{
			Parent = Registry.Find(AActorClassId);
		}
		else if constexpr (std::is_base_of<UActorComponent, T>::value)
		{
			Parent = Registry.Find(UActorComponentClassId);
		}
		else if constexpr (std::is_base_of<UWorld, T>::value)
		{
			Parent = Registry.Find(UWorldClassId);
		}
		else if constexpr (std::is_base_of<UWorldSubsystem, T>::value)
		{
			Parent = Registry.Find(UWorldSubsystemClassId);
		}
		const FClassDescriptor Candidate = MakeClassDescriptor<T>(InTypeId, InName, Parent, &TraceManagedObjectReferences);
		return Registry.Register(Candidate);
	}

	/**
	 * Motivation: Roots one traced reference using this environment's root capacity.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	template<typename T>
	TStrongObjectPtr<T> MakeRoot(const TObjectPtr<T> InObject) noexcept
	{
		auto Result = Store.MakeStrongObjectPtr(InObject);
		return std::move(Result.Pointer);
	}

private:
	static_assert(SlotCount > 0, "Engine tests require at least one object slot.");
	static_assert(SlotSizeBytes % SlotAlignmentBytes == 0, "Slot stride must preserve alignment.");

	/**
	 * Motivation: The store accepts them.
	 * Responsibilities: Registers the four engine base descriptors.
	 */
	void RegisterBaseClasses() noexcept
	{
		(void)Registry.Register(UActorComponent::StaticClassDescriptor());
		(void)Registry.Register(AActor::StaticClassDescriptor());
		(void)Registry.Register(UWorld::StaticClassDescriptor());
		(void)Registry.Register(UWorldSubsystem::StaticClassDescriptor());
	}

	/**
	 * Motivation: Describes this environment's complete caller-owned store storage.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FObjectStoreStorage MakeStorage() noexcept
	{
		return FObjectStoreStorage{
			SlotBytes.data(),
			SlotBytes.size(),
			Slots.data(),
			SlotCount,
			SlotSizeBytes,
			SlotAlignmentBytes,
			RootCapacity == 0 ? nullptr : Roots.data(),
			RootCapacity,
		};
	}

	/** Motivation: Keeps every equal-size slot correctly aligned for placement construction. */
	alignas(SlotAlignmentBytes) std::array<std::byte, SlotSizeBytes * SlotCount> SlotBytes{};

	/** Motivation: Gives the store one lifecycle record per fixed object slot. */
	std::array<FObjectSlotMetadata, SlotCount> Slots{};

	/** Motivation: Gives each successful strong pointer one independently reusable token entry. */
	std::array<FObjectRootEntry, RootCapacity> Roots{};

	/** Motivation: Owns the class registry used to validate construction and tracing. */
	TClassRegistry<8> Registry;

	/** Motivation: Owns all managed lifetimes while the environment remains alive. */
	FObjectStore Store;
};

/**
 * Motivation: Names the canonical 16-slot, 4-root engine environment so every suite that needs that shape shares one
 *   instantiation rather than redeclaring the same alias under a suite-specific name.
 * Responsibilities: Alias one TEngineEnvironment shape used by the registration and spawn/destroy families.
 */
using FEngineEnvironmentSlots16 = TEngineEnvironment<256, 16, 16, 4>;

/**
 * Motivation: Names the canonical 8-slot, 4-root engine environment so the garbage-collection and lifecycle suites
 *   share one instantiation rather than redeclaring the same alias under a suite-specific name.
 * Responsibilities: Alias one TEngineEnvironment shape used by the garbage-collection and lifecycle families.
 */
using FEngineEnvironmentSlots8 = TEngineEnvironment<256, 16, 8, 4>;

/**
 * Motivation: Provides one shared collector-over-store fixture so the garbage-collection and spawn/destroy suites do
 *   not each redeclare the same worklist-backed FGarbageCollector wrapper.
 * Responsibilities: Own a fixed-capacity worklist and bind one FGarbageCollector to a caller-supplied store.
 * Example:
 *   FCollectorFixture Fixture{Store};
 *   FGarbageCollector& Collector = Fixture.GetCollector();
 */
class FCollectorFixture final
{
public:
	/**
	 * Motivation: Binds the collector to a caller-owned store over this fixture's fixed worklist storage.
	 * Responsibilities: Construct the collector referencing the supplied store and this fixture's worklist.
	 */
	explicit FCollectorFixture(FObjectStore& InStore) noexcept : Collector(InStore, FGarbageCollectorStorage{Worklist, Capacity}) {}

	/**
	 * Motivation: Lets a test drive the bound collector without exposing the worklist storage.
	 * Responsibilities: Return a reference to the owned collector.
	 */
	FGarbageCollector& GetCollector() noexcept { return Collector; }

private:
	/** Motivation: Bounds the worklist backing the fixture's collector; sized to cover every slot in the test stores. */
	static constexpr std::uint32_t Capacity{16};

	/** Motivation: Holds caller-owned iterative traversal storage without recursion or heap fallback. */
	FObjectHandle Worklist[Capacity]{};

	/** Motivation: Performs bounded incremental mark/sweep over the bound store. */
	FGarbageCollector Collector;
};

/**
 * Motivation: Provides one shared minimal AActor stub so the registration and spawn/destroy families do not each
 *   redeclare the same bare default-constructed actor.
 * Responsibilities: Subclass AActor with no added state or behaviour.
 * Example:
 *   const auto Creation = Store.NewObject<FPlainActor>(*Descriptor);
 */
class FPlainActor final : public AActor
{
public:
	/**
	 * Motivation: Constructs a plain actor with the default AActor lifecycle configuration.
	 * Responsibilities: Forward to the AActor default constructor and add no further state.
	 */
	explicit FPlainActor() noexcept : AActor() {}
};

/**
 * Motivation: Provides one shared minimal UActorComponent stub so the registration and spawn/destroy families do not
 *   each redeclare the same bare default-constructed component.
 * Responsibilities: Subclass UActorComponent with no added state or behaviour.
 * Example:
 *   const auto Creation = Store.NewObject<FPlainComponent>(*Descriptor);
 */
class FPlainComponent final : public UActorComponent
{
public:
	/**
	 * Motivation: Constructs a plain component with the default UActorComponent lifecycle configuration.
	 * Responsibilities: Forward to the UActorComponent default constructor and add no further state.
	 */
	FPlainComponent() noexcept : UActorComponent() {}
};

} // namespace MicroWorld::Tests
