#pragma once

#include "TestSupport.h"

#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/ClassRegistry.h>
#include <MicroWorld/Engine/ClassRegistryView.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/GarbageCollectionBudget.h>
#include <MicroWorld/Engine/GarbageCollectionResult.h>
#include <MicroWorld/Engine/GarbageCollectionStats.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/GarbageCollectorStorage.h>
#include <MicroWorld/Engine/Object.h>
#include <MicroWorld/Engine/ObjectCreationResult.h>
#include <MicroWorld/Engine/ObjectRootEntry.h>
#include <MicroWorld/Engine/ObjectSlotMetadata.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/ObjectStoreStats.h>
#include <MicroWorld/Engine/ObjectStoreStorage.h>
#include <MicroWorld/Engine/ReferenceCollector.h>
#include <MicroWorld/Engine/StrongObjectPtr.h>
#include <MicroWorld/Engine/WeakObjectPtr.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace MicroWorld::Tests
{

using MicroWorld::Core::ERuntimeResult;
using MicroWorld::Engine::EGarbageCollectionPhase;
using MicroWorld::Engine::EObjectResult;
using MicroWorld::Engine::FClassDescriptor;
using MicroWorld::Engine::FClassRegistryView;
using MicroWorld::Engine::FGarbageCollectionBudget;
using MicroWorld::Engine::FGarbageCollectionResult;
using MicroWorld::Engine::FGarbageCollector;
using MicroWorld::Engine::FGarbageCollectorStorage;
using MicroWorld::Engine::FObjectHandle;
using MicroWorld::Engine::FObjectRootEntry;
using MicroWorld::Engine::FObjectSlotMetadata;
using MicroWorld::Engine::FObjectStore;
using MicroWorld::Engine::FObjectStoreStats;
using MicroWorld::Engine::FObjectStoreStorage;
using MicroWorld::Engine::FReferenceCollector;
using MicroWorld::Engine::MakeClassDescriptor;
using MicroWorld::Engine::MakeClassRegistryView;
using MicroWorld::Engine::TClassRegistry;
using MicroWorld::Engine::TObjectCreationResult;
using MicroWorld::Engine::TObjectPtr;
using MicroWorld::Engine::TraceManagedObjectReferences;
using MicroWorld::Engine::TStrongObjectPointerResult;
using MicroWorld::Engine::TWeakObjectPtr;
using MicroWorld::Engine::UObject;

/** Motivation: Per-slice operation budget used by the bounded-slice test: one root, one mark, one sweep per advance. */
constexpr FGarbageCollectionBudget UnitSliceBudget{1, 1, 1};

/** Motivation: Zero-budget advance used to prove a no-op slice performs no work. */
constexpr FGarbageCollectionBudget ZeroSliceBudget{0, 0, 0};

/** Motivation: Upper bound on bounded-slice iterations before the test declares the cycle cannot complete. */
constexpr std::uint32_t BoundedSliceIterationCap = 16;

/** Motivation: Largest total operation count one UnitSliceBudget advance may report (root + mark + sweep). */
constexpr std::uint32_t MaxOperationsPerUnitSlice = 3;

/**
 * Motivation: Records collector-visible lifetime completion in fresh per-test state.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FGraphLifetimeState final
{
	/** Motivation: Counts successfully constructed graph nodes. */
	std::uint32_t ConstructionCount{0};

	/** Motivation: Counts nodes entering managed destruction. */
	std::uint32_t BeginDestroyCount{0};

	/** Motivation: Counts exact graph-node destructor executions. */
	std::uint32_t DestructionCount{0};
};

/**
 * Motivation: Provides two explicit outgoing handles for graph and bounded-visitor tests.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FGraphObject final : public UObject
{
public:
	/**
	 * Motivation: Begins one observable graph-node lifetime.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	explicit FGraphObject(FGraphLifetimeState& InState) noexcept : State(InState) { ++State.ConstructionCount; }

	/**
	 * Motivation: Records exact derived destruction after collector reclamation.
	 * Responsibilities: Release the documented observation exactly once and leave no leak behind.
	 */
	~FGraphObject() noexcept override { ++State.DestructionCount; }

	/**
	 * Motivation: Replaces one bounded outgoing edge without changing target lifetime.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void SetReference(const std::size_t InIndex, const TObjectPtr<FGraphObject> InReference) noexcept
	{
		if (InIndex >= References.size())
		{
			return;
		}

		References[InIndex] = InReference;
		if (ReferenceCount <= InIndex)
		{
			ReferenceCount = InIndex + 1;
		}
	}

	/**
	 * Motivation: Requests one deliberately recursive advance from the next reference visit.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void SetReentrantAdvance(FGarbageCollector& InCollector, ERuntimeResult& OutResult) noexcept
	{
		ReentrantCollector = &InCollector;
		ReentrantResult = &OutResult;
	}

protected:
	/**
	 * Motivation: Presents every configured edge to the active iterative collector.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void VisitReferences(FReferenceCollector& InCollector) noexcept override
	{
		if (ReentrantCollector != nullptr && ReentrantResult != nullptr)
		{
			*ReentrantResult = ReentrantCollector->Advance(FGarbageCollectionBudget{1, 1, 1}).Result;
		}

		for (std::size_t Index = 0; Index < ReferenceCount; ++Index)
		{
			InCollector.AddReferencedObject(References[Index]);
		}
	}

	/**
	 * Motivation: Records the lifecycle barrier before exact destruction.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void BeginDestroy() noexcept override { ++State.BeginDestroyCount; }

private:
	/** Motivation: Shares observations only with the test that owns this node. */
	FGraphLifetimeState& State;

	/** Motivation: Holds a small same-store typed graph without dynamic storage. */
	std::array<TObjectPtr<FGraphObject>, 2> References{};

	/** Motivation: Bounds visitor work to edges explicitly configured by the test. */
	std::size_t ReferenceCount{0};

	/** Motivation: Selects the active collector targeted only by the recursive-advance regression. */
	FGarbageCollector* ReentrantCollector{nullptr};

	/** Motivation: Exposes the recursive call result without global test state. */
	ERuntimeResult* ReentrantResult{nullptr};
};

/**
 * Motivation: Provides a complete typed target for cross-store pointer-origin validation.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FCrossStoreLeaf final : public UObject
{
public:
	/**
	 * Motivation: Begins one observable leaf lifetime.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	explicit FCrossStoreLeaf(FGraphLifetimeState& InState) noexcept : State(InState) { ++State.ConstructionCount; }

	/**
	 * Motivation: Records exact leaf destruction independently from holder destruction.
	 * Responsibilities: Release the documented observation exactly once and leave no leak behind.
	 */
	~FCrossStoreLeaf() noexcept override { ++State.DestructionCount; }

protected:
	/**
	 * Motivation: Records managed leaf teardown before exact destruction.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void BeginDestroy() noexcept override { ++State.BeginDestroyCount; }

private:
	/** Motivation: Shares observations only with the store-specific test state. */
	FGraphLifetimeState& State;
};

/**
 * Motivation: Presents one typed reference whose originating store must be validated.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FCrossStoreReferenceHolder final : public UObject
{
public:
	/**
	 * Motivation: Begins one observable holder lifetime.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	explicit FCrossStoreReferenceHolder(FGraphLifetimeState& InState) noexcept : State(InState) { ++State.ConstructionCount; }

	/**
	 * Motivation: Records exact holder destruction.
	 * Responsibilities: Release the documented observation exactly once and leave no leak behind.
	 */
	~FCrossStoreReferenceHolder() noexcept override { ++State.DestructionCount; }

	/**
	 * Motivation: Selects the typed reference presented during the next trace.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void SetReference(const TObjectPtr<FCrossStoreLeaf> InReference) noexcept { Reference = InReference; }

protected:
	/**
	 * Motivation: Exercises TObjectPtr store-origin validation at the collector boundary.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void VisitReferences(FReferenceCollector& InCollector) noexcept override { InCollector.AddReferencedObject(Reference); }

	/**
	 * Motivation: Records managed holder teardown before exact destruction.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void BeginDestroy() noexcept override { ++State.BeginDestroyCount; }

private:
	/** Motivation: Shares observations only with the holder's store-specific test state. */
	FGraphLifetimeState& State;

	/** Motivation: Retains the foreign-or-local typed identity without caching its raw address. */
	TObjectPtr<FCrossStoreLeaf> Reference{};
};

/**
 * Motivation: Owns one isolated fixed object store for collector behavior tests.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
template<std::uint32_t SlotCount, std::uint32_t RootCapacity>
class TGraphStoreFixture final
{
public:
	/**
	 * Motivation: Binds the store to this fixture's complete aligned caller-owned storage.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	explicit TGraphStoreFixture(const FClassRegistryView InClasses) noexcept : Store(MakeStorage(), InClasses) {}

	/**
	 * Motivation: Exposes the public store under test.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FObjectStore& GetStore() noexcept { return Store; }

private:
	static constexpr std::size_t SlotSizeBytes = 128;
	static constexpr std::size_t SlotAlignmentBytes = 16;

	/**
	 * Motivation: Describes the fixed storage retained by this fixture.
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

	/** Motivation: Provides aligned non-moving placement storage for every graph node. */
	alignas(SlotAlignmentBytes) std::array<std::byte, SlotSizeBytes * SlotCount> SlotBytes{};

	/** Motivation: Provides one lifecycle record per fixed slot. */
	std::array<FObjectSlotMetadata, SlotCount> Slots{};

	/** Motivation: Provides independently counted explicit root entries. */
	std::array<FObjectRootEntry, RootCapacity> Roots{};

	/** Motivation: Owns managed graph lifetimes while all fixture storage is alive. */
	FObjectStore Store;
};

/**
 * Motivation: Registers the one graph-node class used by a fresh test.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
inline EObjectResult RegisterGraphDescriptor(TClassRegistry<2>& InRegistry, const FClassDescriptor*& OutDescriptor) noexcept
{
	const FClassDescriptor Candidate = MakeClassDescriptor<FGraphObject>(1, "GraphObject", nullptr, &TraceManagedObjectReferences);
	const EObjectResult Result = InRegistry.Register(Candidate);
	OutDescriptor = InRegistry.Find(Candidate.TypeId);
	return Result;
}

/**
 * Motivation: Captures equivalent observable outcomes from full and incremental collection.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FCollectionObservation final
{
	/** Motivation: Reports whether the tested cycle completed successfully. */
	bool bCycleComplete{false};

	/** Motivation: Counts objects reclaimed by the tested cycle. */
	std::uint32_t ReclaimedObjects{0};

	/** Motivation: Counts objects remaining immediately after the tested cycle. */
	std::uint32_t OccupiedSlots{0};

	/** Motivation: Counts exact destructors run by the tested cycle. */
	std::uint32_t DestructionCount{0};
};

/**
 * Motivation: Runs the same rooted-chain graph using either full or one-operation slices.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
inline FCollectionObservation ObserveEquivalentCollection(const bool bIncremental) noexcept
{
	FGraphLifetimeState Lifetime{};
	TClassRegistry<2> Registry;
	const FClassDescriptor* Descriptor = nullptr;
	(void)RegisterGraphDescriptor(Registry, Descriptor);
	TGraphStoreFixture<4, 1> Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	const TObjectCreationResult<FGraphObject> First = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const TObjectCreationResult<FGraphObject> Second = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const TObjectCreationResult<FGraphObject> Third = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	const TObjectCreationResult<FGraphObject> Unreachable = Store.NewObject<FGraphObject>(*Descriptor, Lifetime);
	static_cast<void>(Unreachable);
	First.Object.Get()->SetReference(0, Second.Object);
	Second.Object.Get()->SetReference(0, Third.Object);
	TStrongObjectPointerResult<FGraphObject> Root = Store.MakeStrongObjectPtr(First.Object);
	static_cast<void>(Root);

	std::array<FObjectHandle, 4> Worklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())});
	FGarbageCollectionResult FinalResult{};
	if (!bIncremental)
	{
		FinalResult = Collector.CollectFull();
	}
	else
	{
		(void)Collector.RequestCollection();
		for (std::uint32_t Slice = 0; Slice < 32 && Collector.Phase() != EGarbageCollectionPhase::Idle; ++Slice)
		{
			FinalResult = Collector.Advance(FGarbageCollectionBudget{1, 1, 1});
		}
	}

	const FObjectStoreStats StoreStats = Store.Stats();
	const MicroWorld::Engine::FGarbageCollectionStats CollectionStats = Collector.Stats();
	return FCollectionObservation{
		FinalResult.bCycleComplete,
		CollectionStats.ReclaimedObjects,
		StoreStats.OccupiedSlots,
		Lifetime.DestructionCount,
	};
}

} // namespace MicroWorld::Tests
