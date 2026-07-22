#pragma once

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineClassIds.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/NetworkFrame.h>
#include <MicroWorld/Engine/Timer.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Object/ClassDescriptor.h>
#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/Object.h>
#include <MicroWorld/Object/ObjectPtr.h>
#include <MicroWorld/Object/ObjectStore.h>
#include <MicroWorld/Time.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace MicroWorld
{

/**
 * Owns and wires every fixed-capacity runtime subsystem — class registry, object
 * store, garbage collector, world actor registry, and timer manager — behind one
 * canonical per-frame order. Construct it in static storage or a stack frame,
 * register user classes, call CreateWorld once, then drive BeginPlay/Tick/EndPlay.
 * Every instantiation sizes all storage at compile time and never allocates; an
 * optional caller-owned network frame bound at construction makes the per-frame
 * network slots live.
 */
template<
	std::size_t MaxClasses,
	std::size_t MaxObjects,
	std::size_t SlotBytes,
	std::size_t SlotAlign,
	std::size_t MaxRoots,
	std::size_t MaxActors,
	std::size_t MaxTimers,
	std::size_t TimerCallbackBytes>
class TEngineHost final
{
public:
	/** Alias for the timer manager this host owns, so callers name one concrete type. */
	using FTimerManager = TTimerManager<MaxTimers, TimerCallbackBytes>;

	/**
	 * Builds every subsystem over this host's storage and registers the three
	 * engine base descriptors so worlds, actors, and components are constructible.
	 *
	 * CollectionBudget bounds the per-tick garbage-collection slice; ReclamationBudget
	 * bounds how many store slots the per-tick destruction barrier inspects (default:
	 * every slot, reclaiming all pending destroys each frame).
	 */
	explicit TEngineHost(
		const FGarbageCollectionBudget CollectionBudget, const std::uint32_t ReclamationBudget = static_cast<std::uint32_t>(MaxObjects)) noexcept
		: GcBudget(CollectionBudget)
		, FrameReclamationBudget(ReclamationBudget)
		, Store(MakeStoreStorage(), MakeClassRegistryView(Registry))
		, Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(MaxObjects)})
		, Timers(TimePointMilliseconds{0})
	{
		RegisterBaseClasses();
	}

	/**
	 * Builds the host exactly as the budget-only constructor does, then binds a
	 * caller-owned network frame so Tick drives its inbound step first (step 1) and
	 * its outbound step last (step 7). The frame and the network host behind it must
	 * outlive this host.
	 */
	explicit TEngineHost(
		const FGarbageCollectionBudget CollectionBudget,
		INetworkFrame& NetworkFrame,
		const std::uint32_t ReclamationBudget = static_cast<std::uint32_t>(MaxObjects)) noexcept
		: TEngineHost(CollectionBudget, ReclamationBudget)
	{
		Network = &NetworkFrame;
	}

	TEngineHost(const TEngineHost&) = delete;
	TEngineHost& operator=(const TEngineHost&) = delete;
	TEngineHost(TEngineHost&&) = delete;
	TEngineHost& operator=(TEngineHost&&) = delete;

	/** Registers one user class descriptor so the store accepts its construction. */
	EObjectResult RegisterClass(const FClassDescriptor& Descriptor) noexcept { return Registry.Register(Descriptor); }

	/**
	 * Returns one registered descriptor by type id, or null. Callers need this handle
	 * to build child descriptors (whose parent must point at the registry's own copy)
	 * and to construct user types, because the store validates descriptor identity
	 * against the registry's copy rather than the descriptor the caller registered.
	 */
	const FClassDescriptor* FindClass(const FTypeId TypeId) const noexcept { return Registry.Find(TypeId); }

	/**
	 * Registers a user type by deriving its parent from T's engine base (AActor,
	 * UActorComponent, or UWorld) and building the descriptor with the shared managed
	 * tracer, so callers register in one line instead of hand-building a descriptor.
	 * Handles single-level derivation from an engine base; a deeper user hierarchy
	 * must use the descriptor overload with an explicit FindClass parent.
	 */
	template<typename T>
	EObjectResult RegisterClass(const FTypeId TypeId, const char* const Name) noexcept
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

	/**
	 * Constructs a registered user type by folding FindClass and NewObject into one
	 * call, so callers create an instance by type id without repeating the lookup.
	 * Returns an UnknownClass result with a null object if the id was never registered.
	 */
	template<typename T, typename... TArguments>
	TObjectCreationResult<T> CreateObject(const FTypeId TypeId, TArguments&&... Arguments) noexcept
	{
		const FClassDescriptor* const Descriptor = FindClass(TypeId);
		if (Descriptor == nullptr)
		{
			return TObjectCreationResult<T>{EObjectResult::UnknownClass, {}};
		}
		return Store.NewObject<T>(*Descriptor, std::forward<TArguments>(Arguments)...);
	}

	/**
	 * Constructs the single UWorld in the store and roots it, returning the world
	 * reference; returns an empty reference if a world already exists or creation
	 * fails. Call exactly once before BeginPlay.
	 */
	TObjectPtr<UWorld> CreateWorld() noexcept
	{
		if (WorldRoot.Get() != nullptr)
		{
			return {};
		}

		const FClassDescriptor* const Descriptor = Registry.Find(UWorldClassId);
		if (Descriptor == nullptr)
		{
			return {};
		}

		const TObjectCreationResult<UWorld> Creation = Store.NewObject<UWorld>(*Descriptor, ActorRegistry.MakeView());
		if (Creation.Result != EObjectResult::Success)
		{
			return {};
		}

		TStrongObjectPointerResult<UWorld> RootResult = Store.MakeStrongObjectPtr(Creation.Object);
		if (RootResult.Result != EObjectResult::Success)
		{
			return {};
		}

		WorldRoot = std::move(RootResult.Pointer);
		return Creation.Object;
	}

	/** Forwards to FObjectStore::NewObject so callers construct managed objects in this store. */
	template<typename T, typename... TArguments>
	TObjectCreationResult<T> NewObject(TArguments&&... Arguments) noexcept
	{
		return Store.NewObject<T>(std::forward<TArguments>(Arguments)...);
	}

	/** Returns the rooted world; only valid after CreateWorld has succeeded. */
	UWorld& GetWorld() noexcept { return *WorldRoot.Get(); }

	/** Returns the object store so callers can query stats or manage roots directly. */
	FObjectStore& GetObjectStore() noexcept { return Store; }

	/** Returns the timer manager so callers schedule and cancel bounded timers. */
	FTimerManager& GetTimerManager() noexcept { return Timers; }

	/**
	 * Starts the world at one canonical time and records it as the tick baseline.
	 * Returns InvalidLifecycle if no world has been created.
	 */
	ERuntimeResult BeginPlay(const TimePointMilliseconds NowMilliseconds) noexcept
	{
		UWorld* const World = WorldRoot.Get();
		if (World == nullptr)
		{
			return ERuntimeResult::InvalidLifecycle;
		}

		LastTickMilliseconds = NowMilliseconds;
		return World->BeginPlay(NowMilliseconds);
	}

	/**
	 * Runs one canonical frame in fixed order and returns the world's advance/apply
	 * result. The full order is:
	 *   1. NetworkFrame TickDispatch — drain inbound traffic, dispatch messages, age peers.
	 *   2. Timers.Advance — fire due timer callbacks.
	 *   3. World.Advance — tick every component, then every actor.
	 *   4. World.ApplyPending — begin pending spawns; end and unregister pending destroys.
	 *   5. Store.ApplyPendingDestroy — bounded reclamation of the slots step 4 marked.
	 *   6. GC slice — start a cycle when idle, then advance one bounded slice.
	 *   7. NetworkFrame TickFlush — flush outbound traffic and heartbeats.
	 *
	 * Rejects a rolled-back clock transactionally before any step runs. Every step
	 * but the world advance/apply is bounded best-effort, so the world result is the
	 * authoritative per-frame outcome. The two network slots run only when a frame
	 * was bound at construction; otherwise they are inert.
	 */
	ERuntimeResult Tick(const TimePointMilliseconds NowMilliseconds) noexcept
	{
		UWorld* const World = WorldRoot.Get();
		if (World == nullptr)
		{
			return ERuntimeResult::InvalidLifecycle;
		}
		if (NowMilliseconds < LastTickMilliseconds)
		{
			return ERuntimeResult::NonMonotonicTime;
		}
		LastTickMilliseconds = NowMilliseconds;

		// 1. Drain inbound network traffic and dispatch messages before the frame advances.
		if (Network != nullptr)
		{
			Network->TickDispatch(NowMilliseconds);
		}
		// 2. Fire due timer callbacks.
		(void)Timers.Advance(NowMilliseconds);
		// 3. Tick every component then every actor.
		const ERuntimeResult AdvanceResult = World->Advance(NowMilliseconds);
		// 4. Apply pending destroys, then pending spawns, at the one structural barrier.
		const ERuntimeResult PendingResult = World->ApplyPending(NowMilliseconds);
		// 5. Reclaim the actors/components the barrier marked; the GC sweep skips
		//    pending-destroy slots, so this bounded slice is what frees them.
		(void)Store.ApplyPendingDestroy(FrameReclamationBudget);
		// 6. Advance one bounded GC slice, starting a fresh cycle when idle.
		if (Collector.Phase() == EGarbageCollectionPhase::Idle)
		{
			(void)Collector.RequestCollection();
		}
		(void)Collector.Advance(GcBudget);
		// 7. Flush queued outbound network traffic and heartbeats after the frame settles.
		if (Network != nullptr)
		{
			Network->TickFlush(NowMilliseconds);
		}

		return AdvanceResult != ERuntimeResult::Success ? AdvanceResult : PendingResult;
	}

	/** Ends the world in reverse registration order; idempotent after success. */
	ERuntimeResult EndPlay() noexcept
	{
		UWorld* const World = WorldRoot.Get();
		if (World == nullptr)
		{
			return ERuntimeResult::InvalidLifecycle;
		}

		return World->EndPlay();
	}

private:
	static_assert(MaxObjects > 0, "TEngineHost needs at least one object slot for the world.");
	static_assert(MaxRoots > 0, "TEngineHost roots its world, so it needs at least one root entry.");
	static_assert(SlotBytes % SlotAlign == 0, "Slot stride must preserve slot alignment.");

	/** Registers the three engine base descriptors so base types are constructible. */
	void RegisterBaseClasses() noexcept
	{
		(void)Registry.Register(UActorComponent::StaticClassDescriptor());
		(void)Registry.Register(AActor::StaticClassDescriptor());
		(void)Registry.Register(UWorld::StaticClassDescriptor());
	}

	/** Describes this host's complete caller-owned store storage for the store constructor. */
	FObjectStoreStorage MakeStoreStorage() noexcept
	{
		return FObjectStoreStorage{
			SlotStorage.data(),
			SlotStorage.size(),
			SlotMetadata.data(),
			static_cast<std::uint32_t>(MaxObjects),
			SlotBytes,
			SlotAlign,
			RootStorage.data(),
			static_cast<std::uint32_t>(MaxRoots),
		};
	}

	/** Bounds the per-tick garbage-collection slice supplied at construction. */
	FGarbageCollectionBudget GcBudget;

	/** Bounds the per-tick store slots inspected by the destruction barrier. */
	std::uint32_t FrameReclamationBudget;

	/** Optional caller-owned network frame advanced first and last each tick; null when standalone. */
	INetworkFrame* Network{nullptr};

	/** Records the last accepted tick time so a rolled-back clock is rejected. */
	TimePointMilliseconds LastTickMilliseconds{0};

	/** Owns the class registry that validates every managed construction. */
	TClassRegistry<MaxClasses> Registry;

	/** Provides the first byte of equal-size, non-moving object slots. */
	alignas(SlotAlign) std::array<std::byte, SlotBytes * MaxObjects> SlotStorage{};

	/** Provides one lifecycle record per object slot. */
	std::array<FObjectSlotMetadata, MaxObjects> SlotMetadata{};

	/** Provides independently reusable entries for explicit strong-root tokens. */
	std::array<FObjectRootEntry, MaxRoots> RootStorage{};

	/** Owns every managed lifetime over this host's caller-owned storage. */
	FObjectStore Store;

	/** Holds generation-checked reachable identities for one collector run. */
	std::array<FObjectHandle, MaxObjects> Worklist{};

	/** Performs bounded incremental mark/sweep over the store. */
	FGarbageCollector Collector;

	/** Owns the fixed actor registry leased to the single world. */
	FWorldActorRegistry<MaxActors> ActorRegistry;

	/** Owns the bounded timer set advanced first in every frame. */
	FTimerManager Timers;

	/** Roots the single world so it survives collection for this host's lifetime. */
	TStrongObjectPtr<UWorld> WorldRoot;
};

} // namespace MicroWorld
