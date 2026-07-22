#pragma once

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineClassIds.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Object/ClassDescriptor.h>
#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/Object.h>
#include <MicroWorld/Object/ObjectStore.h>
#include <MicroWorld/Object/ObjectPtr.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace MicroWorld::Tests
{

/**
 * Shares one monotonic event sequence across every observed object in a test so
 * begin/tick/end ordering is recorded without per-object clocks.
 */
class FSequenceCounter final
{
public:
	/** Returns the next sequence value so callers can record relative ordering. */
	std::uint32_t Next() noexcept { return ++Value; }

private:
	/** Tracks the highest sequence value handed out in this test. */
	std::uint32_t Value{0};
};

/** Records the begin/tick/end sequence values observed by one actor. */
struct FActorEventState final
{
	/** Sequence value of this actor's BeginPlay hook. */
	std::uint32_t BeginOrder{0};
	/** Counts BeginPlay hook invocations so repeated lifecycle calls are observable. */
	std::uint32_t BeginCount{0};
	/** Sequence value of this actor's Tick hook. */
	std::uint32_t TickOrder{0};
	/** Sequence value of this actor's EndPlay hook. */
	std::uint32_t EndOrder{0};
	/** Counts EndPlay hook invocations so repeated shutdown is observable. */
	std::uint32_t EndCount{0};
	/** Counts Tick hook invocations so interval tests can bound ticks per advance. */
	std::uint32_t TickCount{0};
};

/** Records the begin/tick/end sequence values observed by one component. */
struct FComponentEventState final
{
	/** Sequence value of this component's BeginPlay hook. */
	std::uint32_t BeginOrder{0};
	/** Counts BeginPlay hook invocations so repeated lifecycle calls are observable. */
	std::uint32_t BeginCount{0};
	/** Sequence value of this component's TickComponent hook. */
	std::uint32_t TickOrder{0};
	/** Sequence value of this component's EndPlay hook. */
	std::uint32_t EndOrder{0};
	/** Counts EndPlay hook invocations so repeated shutdown is observable. */
	std::uint32_t EndCount{0};
	/** Counts TickComponent hook invocations so interval tests can bound ticks per advance. */
	std::uint32_t TickCount{0};
};

/**
 * Owns all fixed object-store, root, worklist, and class-registry storage for
 * one isolated engine behavior test.
 *
 * The fixture registers the three engine base descriptors so the base types are
 * constructible through their StaticClassDescriptor overloads. User-derived test
 * types call RegisterDerivedClass before CreateObject so their explicit
 * descriptors participate in store validation.
 */
template<std::size_t SlotSizeBytes, std::size_t SlotAlignmentBytes, std::uint32_t SlotCount, std::uint32_t RootCapacity>
class TEngineEnvironment final
{
public:
	/** Builds the store with this environment's storage and base classes registered. */
	TEngineEnvironment() noexcept : Store(MakeStorage(), MakeClassRegistryView(Registry)) { RegisterBaseClasses(); }

	TEngineEnvironment(const TEngineEnvironment&) = delete;
	TEngineEnvironment& operator=(const TEngineEnvironment&) = delete;
	TEngineEnvironment(TEngineEnvironment&&) = delete;
	TEngineEnvironment& operator=(TEngineEnvironment&&) = delete;

	/** Returns the public store backed by this environment's caller-owned storage. */
	FObjectStore& GetStore() noexcept { return Store; }

	/** Returns the class registry so tests can register user-derived descriptors. */
	TClassRegistry<8>& GetRegistry() noexcept { return Registry; }

	/** Returns the registry-owned descriptor for one engine base type id. */
	const FClassDescriptor* FindDescriptor(const FTypeId TypeId) noexcept { return Registry.Find(TypeId); }

	/**
	 * Constructs one base engine object (UWorld, AActor, or UActorComponent) in
	 * this environment's store using its registry-owned descriptor.
	 */
	template<typename T, typename... TArguments>
	TObjectPtr<T> CreateObject(const FTypeId TypeId, TArguments&&... Arguments) noexcept
	{
		const FClassDescriptor* const Descriptor = Registry.Find(TypeId);
		const auto Result = Store.NewObject<T>(*Descriptor, std::forward<TArguments>(Arguments)...);
		return Result.Object;
	}

	/**
	 * Registers one user-derived descriptor under a stable type id and constructs
	 * an instance of the derived type through that descriptor.
	 */
	template<typename T, typename... TArguments>
	TObjectPtr<T> CreateDerivedObject(const FTypeId TypeId, const char* const Name, TArguments&&... Arguments) noexcept
	{
		RegisterDerivedClass<T>(TypeId, Name);
		const FClassDescriptor* const Descriptor = Registry.Find(TypeId);
		const auto Result = Store.NewObject<T>(*Descriptor, std::forward<TArguments>(Arguments)...);
		return Result.Object;
	}

	/** Registers one user-derived descriptor using the shared managed tracer. */
	template<typename T>
	EObjectResult RegisterDerivedClass(const FTypeId TypeId, const char* const Name) noexcept
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
		const FClassDescriptor Candidate = MakeClassDescriptor<T>(TypeId, Name, Parent, &TraceManagedObjectReferences);
		return Registry.Register(Candidate);
	}

	/** Roots one traced reference using this environment's root capacity. */
	template<typename T>
	TStrongObjectPtr<T> MakeRoot(const TObjectPtr<T> Object) noexcept
	{
		auto Result = Store.MakeStrongObjectPtr(Object);
		return std::move(Result.Pointer);
	}

private:
	static_assert(SlotCount > 0, "Engine tests require at least one object slot.");
	static_assert(SlotSizeBytes % SlotAlignmentBytes == 0, "Slot stride must preserve alignment.");

	/** Registers the three engine base descriptors so the store accepts them. */
	void RegisterBaseClasses() noexcept
	{
		(void)Registry.Register(UActorComponent::StaticClassDescriptor());
		(void)Registry.Register(AActor::StaticClassDescriptor());
		(void)Registry.Register(UWorld::StaticClassDescriptor());
	}

	/** Describes this environment's complete caller-owned store storage. */
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

	/** Keeps every equal-size slot correctly aligned for placement construction. */
	alignas(SlotAlignmentBytes) std::array<std::byte, SlotSizeBytes * SlotCount> SlotBytes{};

	/** Gives the store one lifecycle record per fixed object slot. */
	std::array<FObjectSlotMetadata, SlotCount> Slots{};

	/** Gives each successful strong pointer one independently reusable token entry. */
	std::array<FObjectRootEntry, RootCapacity> Roots{};

	/** Owns the class registry used to validate construction and tracing. */
	TClassRegistry<8> Registry;

	/** Owns all managed lifetimes while the environment remains alive. */
	FObjectStore Store;
};

} // namespace MicroWorld::Tests
