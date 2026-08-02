#pragma once

#include "EngineTestSupport.h"
#include "TestSupport.h"

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorSpawnRequestResult.h>
#include <MicroWorld/Engine/DefaultEngineTraits.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/GarbageCollectionBudget.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/GarbageCollectorStorage.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/StrongObjectPtr.h>
#include <MicroWorld/Engine/TDeferredActorSpawnStorage.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace MicroWorld::Tests
{

using MicroWorld::Core::ERuntimeResult;
using MicroWorld::Engine::AActor;
using MicroWorld::Engine::FDefaultEngineTraits;
using MicroWorld::Engine::FGarbageCollectionBudget;
using MicroWorld::Engine::FGarbageCollector;
using MicroWorld::Engine::FGarbageCollectorStorage;
using MicroWorld::Engine::FObjectHandle;
using MicroWorld::Engine::FObjectStore;
using MicroWorld::Engine::TEngine;
using MicroWorld::Engine::TObjectPtr;
using MicroWorld::Engine::TStrongObjectPtr;
using MicroWorld::Engine::UObject;
using MicroWorld::Engine::UWorld;

/**
 * Motivation: Records public BeginPlay observation for one deferred actor instance.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FDeferredSpawnState final
{
	/** Motivation: Counts barrier-time BeginPlay calls without observing queue internals. */
	std::uint32_t BeginCount{0};
};

/**
 * Motivation: Records BeginPlay order so composition-time and registered actor order stays externally observable.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FDeferredSpawnOrderState final
{
	/** Motivation: Holds the actor labels in the order the World began them. */
	std::array<std::uint32_t, 2> BeginOrder{};

	/** Motivation: Identifies the next observation slot in the fixed test sequence. */
	std::size_t BeginCount{0};
};

/**
 * Motivation: Deliberately exceeds the configured inline factory budget without side effects.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FDeferredLargeCapture final
{
	/** Motivation: Holds enough bytes to force public factory-layout rejection before argument movement. */
	std::array<std::byte, 96> Bytes{};
};

/**
 * Motivation: Gives tests enough class and object capacity for a World plus bounded deferred actors.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FDeferredSpawnTraits : FDefaultEngineTraits
{
	static constexpr std::size_t MaxClasses = 6;
	static constexpr std::size_t MaxObjects = 6;
	static constexpr std::size_t MaxActors = 2;
	static constexpr std::size_t MaxRoots = 1;
	static constexpr std::size_t InlineDeferredSpawnFactoryBytes = 96;
};

/** Motivation: Names the deferred-spawn engine host used by the deferred-spawn suite. */
using FDeferredSpawnHost = TEngine<FDeferredSpawnTraits>;

/**
 * Motivation: Reduces live actor capacity to one so a queued typed request exhausts it before publication.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FCombinedSpawnCapacityTraits : FDeferredSpawnTraits
{
	static constexpr std::size_t MaxActors = 1;
};

/** Motivation: Names the engine host that restricts live actor capacity to one for capacity regression. */
using FCombinedSpawnCapacityHost = TEngine<FCombinedSpawnCapacityTraits>;

/** Motivation: Keeps the manually constructed actor descriptor stable for the mixed-spawn capacity regression. */
constexpr MicroWorld::Engine::FTypeId DeferredActorTypeId{0x00070001u};

/** Motivation: Provides a public fixed-store fixture with enough room for collection and deferred construction tests. */
using FDeferredSpawnEnvironment = TEngineEnvironment<256, 16, 8, 1>;

/** Motivation: Restricts the store to a world and one blocking object for construction-failure coverage. */
using FDeferredSpawnCapacityEnvironment = TEngineEnvironment<256, 16, 2, 0>;

/**
 * Motivation: Owns the caller-provided collection worklist used by the direct deferred-spawn fixtures.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FDeferredSpawnCollectorFixture final
{
public:
	/**
	 * Motivation: Binds collection to the fixture's store without borrowing test-local temporary storage.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	explicit FDeferredSpawnCollectorFixture(FObjectStore& InStore) noexcept
		: Collector(InStore, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(Worklist.size())})
	{
	}

	/**
	 * Motivation: Each test can drive its public collection lifecycle.
	 * Responsibilities: Exposes the collector.
	 */
	FGarbageCollector& GetCollector() noexcept { return Collector; }

private:
	/** Motivation: Backs bounded mark traversal for the entire fixture lifetime. */
	std::array<FObjectHandle, 8> Worklist{};

	/** Motivation: Owns the active-cycle capability for the fixture's object store. */
	FGarbageCollector Collector;
};

/**
 * Motivation: Detects any move performed while deferred request preflight is expected to reject.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FDeferredMoveProbe final
{
	/** Motivation: Keeps the caller-visible move state unchanged until factory capture actually begins. */
	bool bWasMovedFrom{false};

	FDeferredMoveProbe() noexcept = default;
	FDeferredMoveProbe(const FDeferredMoveProbe&) = delete;
	FDeferredMoveProbe& operator=(const FDeferredMoveProbe&) = delete;
	FDeferredMoveProbe(FDeferredMoveProbe&& InOther) noexcept { InOther.bWasMovedFrom = true; }
	FDeferredMoveProbe& operator=(FDeferredMoveProbe&&) = delete;
};

/**
 * Motivation: Begins only when World publishes it from the deferred barrier.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FDeferredActor final : public AActor
{
public:
	/**
	 * Motivation: Binds the per-test event sink through the deferred factory.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FDeferredActor(FDeferredSpawnState* const InState) noexcept : AActor(), State(InState) {}

protected:
	/**
	 * Motivation: Makes barrier publication directly observable through the actor lifecycle contract.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void BeginPlay() noexcept override { ++State->BeginCount; }

private:
	/** Motivation: Belongs to the test and proves the world did not call BeginPlay at queue time. */
	FDeferredSpawnState* State{nullptr};
};

/**
 * Motivation: Records a caller-selected label when World dispatches this actor's BeginPlay.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FOrderedDeferredActor final : public AActor
{
public:
	/**
	 * Motivation: Retains isolated component storage, the shared order sink, and this actor's expected label.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FOrderedDeferredActor(FDeferredSpawnOrderState* const InState, const std::uint32_t InLabel) noexcept : AActor(), State(InState), Label(InLabel) {}

protected:
	/**
	 * Motivation: Makes the public actor lifecycle order directly observable without reading World internals.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void BeginPlay() noexcept override
	{
		if (State->BeginCount < State->BeginOrder.size())
		{
			State->BeginOrder[State->BeginCount] = Label;
		}
		++State->BeginCount;
	}

private:
	/** Motivation: Belongs to the test and records dispatch order across registered and queued actors. */
	FDeferredSpawnOrderState* State{nullptr};

	/** Motivation: Distinguishes the actor in the compact fixed-size observation sequence. */
	std::uint32_t Label{0};
};

/**
 * Motivation: Accepts the large capture only so the request tests factory layout rather than constructor validity.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FLargeDeferredActor final : public AActor
{
public:
	/**
	 * Motivation: Retains the same observable state dependency while intentionally ignoring the oversized value.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FLargeDeferredActor(FDeferredSpawnState* const InState, FDeferredLargeCapture) noexcept : AActor(), State(InState) {}

protected:
	/**
	 * Motivation: Makes any accidental admission observable through the normal actor lifecycle.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void BeginPlay() noexcept override { ++State->BeginCount; }

private:
	/** Motivation: Belongs to the test and remains unchanged when layout preflight rejects. */
	FDeferredSpawnState* State{nullptr};
};

/**
 * Motivation: Forces an unsupported factory alignment while leaving actor construction otherwise ordinary.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct alignas(32) FOveralignedCapture final
{
	/** Motivation: Makes the capture itself require stricter alignment than caller-provided inline storage guarantees. */
	std::array<std::byte, 32> Bytes{};
};

/**
 * Motivation: Accepts the over-aligned capture only to exercise public factory-layout preflight.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FOveralignedDeferredActor final : public AActor
{
public:
	/**
	 * Motivation: Keeps the test actor constructible if a future storage policy supports this alignment.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FOveralignedDeferredActor(FDeferredSpawnState* const InState, FOveralignedCapture) noexcept : AActor(), State(InState) {}

protected:
	/**
	 * Motivation: Makes unexpected admission observable.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void BeginPlay() noexcept override { ++State->BeginCount; }

private:
	/** Motivation: Belongs to this isolated test. */
	FDeferredSpawnState* State{nullptr};
};

/**
 * Motivation: Retains a caller-provided world pointer so typed factory value capture is observable after
 *   publication.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FLvaluePointerDeferredActor final : public AActor
{
public:
	/**
	 * Motivation: Captures an lvalue managed pointer by value for later barrier-time construction.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FLvaluePointerDeferredActor(const TObjectPtr<UWorld> InCapturedWorld) noexcept : AActor(), CapturedWorld(InCapturedWorld) {}

	/**
	 * Motivation: Returns the managed pointer value received by the spawned actor constructor.
	 * Responsibilities: Return the stored value and touch nothing else.
	 */
	TObjectPtr<UWorld> GetCapturedWorld() const noexcept { return CapturedWorld; }

private:
	/** Motivation: Preserves the constructor input so the lvalue factory-capture contract remains observable. */
	TObjectPtr<UWorld> CapturedWorld{};
};

/**
 * Motivation: Accepts a move probe so active collection can prove queue preflight happens before argument capture.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FMoveProbeDeferredActor final : public AActor
{
public:
	/**
	 * Motivation: Takes ownership only if deferred request admission reaches factory construction.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FMoveProbeDeferredActor(FDeferredMoveProbe) noexcept : AActor() {}
};

/**
 * Motivation: Retains a captured managed object so queued-factory tracing is directly observable through
 *   collection.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FObjectCaptureDeferredActor final : public AActor
{
public:
	/**
	 * Motivation: Stores the direct managed capture until the World publishes this actor.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FObjectCaptureDeferredActor(const TObjectPtr<UObject> InCapturedObject) noexcept : AActor(), CapturedObject(InCapturedObject) {}

private:
	/** Motivation: Keeps the managed capture meaningful after barrier-time construction. */
	TObjectPtr<UObject> CapturedObject{};
};

} // namespace MicroWorld::Tests
