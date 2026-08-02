#pragma once

#include "TestSupport.h"

#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/ClassRegistry.h>
#include <MicroWorld/Engine/ClassRegistryView.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/GarbageCollectionBudget.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/GarbageCollectorStorage.h>
#include <MicroWorld/Engine/Object.h>
#include <MicroWorld/Engine/ObjectCreationResult.h>
#include <MicroWorld/Engine/ObjectMutationResult.h>
#include <MicroWorld/Engine/ObjectRootEntry.h>
#include <MicroWorld/Engine/ObjectSlotMetadata.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/ObjectStoreStats.h>
#include <MicroWorld/Engine/ObjectStoreStorage.h>
#include <MicroWorld/Engine/StrongObjectPtr.h>
#include <MicroWorld/Engine/WeakObjectPtr.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace MicroWorld::Tests
{

using MicroWorld::Core::ERuntimeResult;
using MicroWorld::Engine::EObjectResult;
using MicroWorld::Engine::FClassDescriptor;
using MicroWorld::Engine::FClassRegistryView;
using MicroWorld::Engine::FGarbageCollector;
using MicroWorld::Engine::FGarbageCollectorStorage;
using MicroWorld::Engine::FObjectHandle;
using MicroWorld::Engine::FObjectRootEntry;
using MicroWorld::Engine::FObjectSlotMetadata;
using MicroWorld::Engine::FObjectStore;
using MicroWorld::Engine::FObjectStoreStats;
using MicroWorld::Engine::FObjectStoreStorage;
using MicroWorld::Engine::FTypeId;
using MicroWorld::Engine::MakeClassDescriptor;
using MicroWorld::Engine::MakeClassRegistryView;
using MicroWorld::Engine::TClassRegistry;
using MicroWorld::Engine::TObjectCreationResult;
using MicroWorld::Engine::TObjectPtr;
using MicroWorld::Engine::TStrongObjectPointerResult;
using MicroWorld::Engine::TStrongObjectPtr;
using MicroWorld::Engine::TWeakObjectPtr;
using MicroWorld::Engine::UObject;

/**
 * Motivation: Records managed lifetime hooks in fresh state owned by one test.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FObjectLifetimeState final
{
	/** Motivation: Proves rejected construction attempts do not start an object lifetime. */
	std::uint32_t ConstructionCount{0};

	/** Motivation: Proves the deferred destruction hook runs exactly once per object. */
	std::uint32_t BeginDestroyCount{0};

	/** Motivation: Proves exact descriptor destruction runs exactly once per object. */
	std::uint32_t DestructionCount{0};
};

/**
 * Motivation: Exposes construction and destruction through caller-owned counters.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FTrackedObject : public UObject
{
public:
	/**
	 * Motivation: Begins one observable lifetime after every store validation succeeds.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	explicit FTrackedObject(FObjectLifetimeState& InState) noexcept : State(InState) { ++State.ConstructionCount; }

	/**
	 * Motivation: Records exact derived destruction selected by the registered descriptor.
	 * Responsibilities: Release the documented observation exactly once and leave no leak behind.
	 */
	~FTrackedObject() noexcept override { ++State.DestructionCount; }

protected:
	/**
	 * Motivation: Records the one deferred lifecycle hook before exact destruction.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void BeginDestroy() noexcept override { ++State.BeginDestroyCount; }

private:
	/** Motivation: Reports lifecycle events without global mutable test state. */
	FObjectLifetimeState& State;
};

/**
 * Motivation: Supplies an equally laid-out wrong type whose destructor may be linker-folded.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FWrongDestructorObject : public UObject
{
public:
	/**
	 * Motivation: Keeps this type layout-equivalent to FTrackedObject for descriptor validation.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	explicit FWrongDestructorObject(FObjectLifetimeState& InState) noexcept : State(InState) {}

	/**
	 * Motivation: Type tokens carry identity.
	 * Responsibilities: Deliberately matches tracked destructor work.
	 */
	~FWrongDestructorObject() noexcept override { ++State.DestructionCount; }

private:
	/** Motivation: Preserves the same instance layout as the intended managed type. */
	FObjectLifetimeState& State;
};

/**
 * Motivation: Captures every store/collector operation attempted from one lifecycle callback.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FReentryState final
{
	/** Motivation: Records a nested construction attempt while the store is callback-locked. */
	EObjectResult ConstructionResult{EObjectResult::Success};

	/** Motivation: Records a nested destruction barrier while the store is callback-locked. */
	EObjectResult BarrierResult{EObjectResult::Success};

	/** Motivation: Records a nested root registration while the store is callback-locked. */
	EObjectResult AddRootResult{EObjectResult::Success};

	/** Motivation: Records a nested destruction request while the store is callback-locked. */
	EObjectResult MarkPendingResult{EObjectResult::Success};

	/** Motivation: Records the one root release that remains safe during exact destruction. */
	EObjectResult RemoveRootResult{EObjectResult::StaleHandle};

	/** Motivation: Records a collection request attempted from construction or destruction. */
	ERuntimeResult CollectionRequestResult{ERuntimeResult::Success};

	/** Motivation: Records collection advancement attempted from a destruction callback. */
	ERuntimeResult CollectionAdvanceResult{ERuntimeResult::Success};

	/** Motivation: Counts exact destruction of the outer object. */
	std::uint32_t DestructionCount{0};

	/** Motivation: Counts destruction hooks for the outer object. */
	std::uint32_t BeginDestroyCount{0};
};

/**
 * Motivation: Attempts forbidden store and collector operations from a placement constructor.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FConstructorReentryObject final : public UObject
{
public:
	/**
	 * Motivation: Proves the slot remains unpublished and locked until construction completes.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FConstructorReentryObject(
		FObjectStore& InStore,
		const FClassDescriptor& InNestedDescriptor,
		FObjectLifetimeState& InNestedLifetime,
		FGarbageCollector& InCollector,
		FReentryState& InState) noexcept
		: State(InState)
	{
		State.ConstructionResult = InStore.NewObject<FTrackedObject>(InNestedDescriptor, InNestedLifetime).Result;
		State.BarrierResult = InStore.ApplyPendingDestroy(1).Result;
		State.CollectionRequestResult = InCollector.RequestCollection();
	}

	/**
	 * Motivation: Records exact outer destruction after successful publication.
	 * Responsibilities: Release the documented observation exactly once and leave no leak behind.
	 */
	~FConstructorReentryObject() noexcept override { ++State.DestructionCount; }

protected:
	/**
	 * Motivation: Records the normal later destruction barrier independently from construction.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void BeginDestroy() noexcept override { ++State.BeginDestroyCount; }

private:
	/** Motivation: Shares callback observations only with the owning test. */
	FReentryState& State;
};

/**
 * Motivation: Attempts recursive mutation while BeginDestroy owns the explicit barrier.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FDestroyReentryObject final : public UObject
{
public:
	/**
	 * Motivation: Retains injected public boundaries used by the adversarial destruction hook.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FDestroyReentryObject(
		FObjectStore& InStore,
		const FClassDescriptor& InNestedDescriptor,
		FObjectLifetimeState& InNestedLifetime,
		FGarbageCollector& InCollector,
		FReentryState& InState) noexcept
		: Store(InStore), NestedDescriptor(InNestedDescriptor), NestedLifetime(InNestedLifetime), Collector(InCollector), State(InState)
	{
	}

	/**
	 * Motivation: Records the one exact destructor after all recursive attempts are rejected.
	 * Responsibilities: Release the documented observation exactly once and leave no leak behind.
	 */
	~FDestroyReentryObject() noexcept override { ++State.DestructionCount; }

protected:
	/**
	 * Motivation: Exercises every mutation path that must not reenter destruction callbacks.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void BeginDestroy() noexcept override
	{
		++State.BeginDestroyCount;
		State.BarrierResult = Store.ApplyPendingDestroy(1).Result;
		State.ConstructionResult = Store.NewObject<FTrackedObject>(NestedDescriptor, NestedLifetime).Result;
		State.AddRootResult = Store.AddRoot(GetObjectHandle());
		State.MarkPendingResult = Store.MarkPendingDestroy(GetObjectHandle());
		State.CollectionRequestResult = Collector.RequestCollection();
		State.CollectionAdvanceResult = Collector.Advance(MicroWorld::Engine::FGarbageCollectionBudget{1, 1, 1}).Result;
		State.RemoveRootResult = Store.RemoveRoot(GetObjectHandle());
	}

private:
	/** Motivation: Identifies the store whose barrier owns this callback. */
	FObjectStore& Store;

	/** Motivation: Supplies a valid nested type so rejection proves locking rather than validation. */
	const FClassDescriptor& NestedDescriptor;

	/** Motivation: Detects any nested lifetime that escaped the mutation lock. */
	FObjectLifetimeState& NestedLifetime;

	/** Motivation: Exercises collection-request rejection during the mutation barrier. */
	FGarbageCollector& Collector;

	/** Motivation: Shares callback results only with the owning test. */
	FReentryState& State;
};

static_assert(sizeof(FTrackedObject) == sizeof(FWrongDestructorObject));
static_assert(alignof(FTrackedObject) == alignof(FWrongDestructorObject));
static_assert(!std::is_copy_constructible<TStrongObjectPtr<FTrackedObject>>::value);
static_assert(!std::is_copy_assignable<TStrongObjectPtr<FTrackedObject>>::value);
static_assert(std::is_move_constructible<TStrongObjectPtr<FTrackedObject>>::value);
static_assert(std::is_move_assignable<TStrongObjectPtr<FTrackedObject>>::value);

/**
 * Motivation: Owns all fixed store storage locally so every behavior test is isolated.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
template<std::size_t SlotSizeBytes, std::size_t SlotAlignmentBytes, std::uint32_t SlotCount, std::uint32_t RootCapacity>
class TObjectStoreFixture final
{
public:
	/**
	 * Motivation: Binds a store to this fixture's aligned slots, metadata, and root entries.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	explicit TObjectStoreFixture(const FClassRegistryView InClasses) noexcept : Store(MakeStorage(), InClasses) {}

	/**
	 * Motivation: Provides the public store under test without exposing fixture storage.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FObjectStore& GetStore() noexcept { return Store; }

private:
	static_assert(SlotCount > 0, "Object-store tests require at least one slot.");
	static_assert(SlotSizeBytes % SlotAlignmentBytes == 0, "Slot stride must preserve alignment.");

	/**
	 * Motivation: Describes this fixture's complete caller-owned store storage.
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

	/** Motivation: Owns all managed lifetimes while the fixture storage remains alive. */
	FObjectStore Store;
};

/**
 * Motivation: Registers one tracked-object descriptor and returns its registry-owned identity.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
inline EObjectResult RegisterTrackedDescriptor(
	TClassRegistry<2>& InRegistry, const FClassDescriptor*& OutDescriptor, const FTypeId InTypeId = 1) noexcept
{
	const FClassDescriptor Candidate = MakeClassDescriptor<FTrackedObject>(InTypeId, "Tracked");
	const EObjectResult Result = InRegistry.Register(Candidate);
	OutDescriptor = InRegistry.Find(InTypeId);
	return Result;
}

} // namespace MicroWorld::Tests
