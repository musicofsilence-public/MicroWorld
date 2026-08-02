#pragma once

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineClassIds.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/Object.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Engine/StrongObjectPtr.h>

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
 * Motivation: Owns all fixed object-store, root, worklist, and class-registry storage for one isolated engine
 *   behavior test. The fixture registers the three engine base descriptors so the base types are
 *   constructible through their StaticClassDescriptor overloads. User-derived test types call
 *   RegisterDerivedClass before CreateObject so their explicit descriptors participate in store
 *   validation.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
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
	 * Responsibilities: Registers the three engine base descriptors.
	 */
	void RegisterBaseClasses() noexcept
	{
		(void)Registry.Register(UActorComponent::StaticClassDescriptor());
		(void)Registry.Register(AActor::StaticClassDescriptor());
		(void)Registry.Register(UWorld::StaticClassDescriptor());
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

} // namespace MicroWorld::Tests
