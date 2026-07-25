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

#if defined(_MSC_VER)
// C4324: padding after the alignas(SlotAlign) slot storage is intentional; the
// host trades a few bytes for the caller-specified slot alignment.
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

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
	std::size_t SlotSizeBytes,
	std::size_t SlotAlign,
	std::size_t MaxRoots,
	std::size_t MaxActors,
	std::size_t MaxTimers,
	std::size_t InlineTimerCallbackBytes>
class TEngineHost final
{
public:
	/** Alias for the timer manager this host owns, so callers name one concrete type. */
	using FTimerManager = TTimerManager<MaxTimers, InlineTimerCallbackBytes>;

	/**
	 * Builds every subsystem over this host's storage and registers the three
	 * engine base descriptors so worlds, actors, and components are constructible.
	 *
	 * CollectionBudget bounds the per-tick garbage-collection slice; ReclamationBudget
	 * bounds how many store slots the per-tick destruction barrier inspects (default:
	 * every slot, reclaiming all pending destroys each frame).
	 */
	explicit TEngineHost(
		const FGarbageCollectionBudget InCollectionBudget, const std::uint32_t InReclamationBudget = static_cast<std::uint32_t>(MaxObjects)) noexcept
		: GarbageCollectionBudget(InCollectionBudget)
		, FrameReclamationBudget(InReclamationBudget)
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
		const FGarbageCollectionBudget InCollectionBudget,
		INetworkFrame& InNetworkFrame,
		const std::uint32_t InReclamationBudget = static_cast<std::uint32_t>(MaxObjects)) noexcept
		: TEngineHost(InCollectionBudget, InReclamationBudget)
	{
		Network = &InNetworkFrame;
	}

	/** Copying or moving would duplicate this host's unique ownership of the
	 * store, garbage collector, registries, and timer manager. */
	TEngineHost(const TEngineHost&) = delete;
	TEngineHost& operator=(const TEngineHost&) = delete;
	TEngineHost(TEngineHost&&) = delete;
	TEngineHost& operator=(TEngineHost&&) = delete;

	/** Registers one user class descriptor so the store accepts its construction. */
	EObjectResult RegisterClass(const FClassDescriptor& InDescriptor) noexcept { return Registry.Register(InDescriptor); }

	/**
	 * Returns one registered descriptor by type id, or null. Callers need this handle
	 * to build child descriptors (whose parent must point at the registry's own copy)
	 * and to construct user types, because the store validates descriptor identity
	 * against the registry's copy rather than the descriptor the caller registered.
	 */
	const FClassDescriptor* FindClass(const FTypeId InTypeId) const noexcept { return Registry.Find(InTypeId); }

	/**
	 * Registers a user type by deriving its parent from TManagedType's engine base (AActor,
	 * UActorComponent, or UWorld) and building the descriptor with the shared managed
	 * tracer, so callers register in one line instead of hand-building a descriptor.
	 * Handles single-level derivation from an engine base; a deeper user hierarchy
	 * must use the descriptor overload with an explicit FindClass parent.
	 */
	template<typename TManagedType>
	EObjectResult RegisterClass(const FTypeId InTypeId, const char* const InName) noexcept
	{
		const FClassDescriptor* Parent = nullptr;
		if constexpr (std::is_base_of<AActor, TManagedType>::value)
		{
			Parent = Registry.Find(AActorClassId);
		}
		else if constexpr (std::is_base_of<UActorComponent, TManagedType>::value)
		{
			Parent = Registry.Find(UActorComponentClassId);
		}
		else if constexpr (std::is_base_of<UWorld, TManagedType>::value)
		{
			Parent = Registry.Find(UWorldClassId);
		}
		const FClassDescriptor Candidate = MakeClassDescriptor<TManagedType>(InTypeId, InName, Parent, &TraceManagedObjectReferences);
		return Registry.Register(Candidate);
	}

	/**
	 * Constructs a registered user type by folding FindClass and NewObject into one
	 * call, so callers create an instance by type id without repeating the lookup.
	 * Returns an UnknownClass result with a null object if the id was never registered.
	 */
	template<typename TManagedType, typename... TArguments>
	TObjectCreationResult<TManagedType> CreateObject(const FTypeId InTypeId, TArguments&&... Arguments) noexcept
	{
		const FClassDescriptor* const Descriptor = FindClass(InTypeId);
		if (Descriptor == nullptr)
		{
			return TObjectCreationResult<TManagedType>{EObjectResult::UnknownClass, {}};
		}
		return Store.NewObject<TManagedType>(*Descriptor, std::forward<TArguments>(Arguments)...);
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

		const TObjectCreationResult<UWorld> Creation = Store.NewObject<UWorld>(*Descriptor, ActorRegistry.MakeReference());
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
	template<typename TManagedType, typename... TArguments>
	TObjectCreationResult<TManagedType> NewObject(TArguments&&... Arguments) noexcept
	{
		return Store.NewObject<TManagedType>(std::forward<TArguments>(Arguments)...);
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
	ERuntimeResult BeginPlay(const TimePointMilliseconds InNowMilliseconds) noexcept
	{
		UWorld* const World = WorldRoot.Get();
		if (World == nullptr)
		{
			return ERuntimeResult::InvalidLifecycle;
		}

		LastTickMilliseconds = InNowMilliseconds;
		return World->BeginPlay(InNowMilliseconds);
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
	ERuntimeResult Tick(const TimePointMilliseconds InNowMilliseconds) noexcept
	{
		UWorld* const World = WorldRoot.Get();
		if (World == nullptr)
		{
			return ERuntimeResult::InvalidLifecycle;
		}
		if (InNowMilliseconds < LastTickMilliseconds)
		{
			return ERuntimeResult::NonMonotonicTime;
		}
		LastTickMilliseconds = InNowMilliseconds;

		DispatchInboundNetwork(InNowMilliseconds);
		(void)Timers.Advance(InNowMilliseconds);
		const ERuntimeResult FrameResult = AdvanceWorldAndApplyBarrier(*World, InNowMilliseconds);
		ReclaimAndCollect();
		FlushOutboundNetwork(InNowMilliseconds);

		return FrameResult;
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
	static_assert(SlotSizeBytes % SlotAlign == 0, "Slot stride must preserve slot alignment.");

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
			SlotSizeBytes,
			SlotAlign,
			RootStorage.data(),
			static_cast<std::uint32_t>(MaxRoots),
		};
	}

	/** Frame step 1: drains inbound traffic, dispatches messages, and ages peers when a network frame is bound. */
	void DispatchInboundNetwork(const TimePointMilliseconds InNowMilliseconds) noexcept
	{
		if (Network != nullptr)
		{
			Network->TickDispatch(InNowMilliseconds);
		}
	}

	/** Frame steps 3-4: advances every actor then applies the spawn/destroy barrier, returning the
	 * authoritative per-frame result (the advance error if any, otherwise the barrier result). */
	ERuntimeResult AdvanceWorldAndApplyBarrier(UWorld& InWorld, const TimePointMilliseconds InNowMilliseconds) noexcept
	{
		const ERuntimeResult AdvanceResult = InWorld.Advance(InNowMilliseconds);
		const ERuntimeResult PendingResult = InWorld.ApplyPending(InNowMilliseconds);
		return AdvanceResult != ERuntimeResult::Success ? AdvanceResult : PendingResult;
	}

	/** Frame steps 5-6: reclaims the slots the barrier marked, then starts a GC cycle when idle and advances one bounded slice. */
	void ReclaimAndCollect() noexcept
	{
		// The GC sweep skips pending-destroy slots, so this bounded slice is what frees them.
		(void)Store.ApplyPendingDestroy(FrameReclamationBudget);
		if (Collector.Phase() == EGarbageCollectionPhase::Idle)
		{
			(void)Collector.RequestCollection();
		}
		(void)Collector.Advance(GarbageCollectionBudget);
	}

	/** Frame step 7: flushes outbound traffic and heartbeats when a network frame is bound. */
	void FlushOutboundNetwork(const TimePointMilliseconds InNowMilliseconds) noexcept
	{
		if (Network != nullptr)
		{
			Network->TickFlush(InNowMilliseconds);
		}
	}

	/** Bounds the per-tick garbage-collection slice supplied at construction. */
	FGarbageCollectionBudget GarbageCollectionBudget;

	/** Bounds the per-tick store slots inspected by the destruction barrier. */
	std::uint32_t FrameReclamationBudget;

	/** Optional caller-owned network frame advanced first and last each tick; null when standalone. */
	INetworkFrame* Network{nullptr};

	/** Records the last accepted tick time so a rolled-back clock is rejected. */
	TimePointMilliseconds LastTickMilliseconds{0};

	/** Owns the class registry that validates every managed construction. */
	TClassRegistry<MaxClasses> Registry;

	/** Provides the first byte of equal-size, non-moving object slots. */
	alignas(SlotAlign) std::array<std::byte, SlotSizeBytes * MaxObjects> SlotStorage{};

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

	/** Owns the fixed actor registry referenced by the single world. */
	FWorldActorRegistry<MaxActors> ActorRegistry;

	/** Owns the bounded timer set advanced first in every frame. */
	FTimerManager Timers;

	/** Roots the single world so it survives collection for this host's lifetime. */
	TStrongObjectPtr<UWorld> WorldRoot;
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace MicroWorld
