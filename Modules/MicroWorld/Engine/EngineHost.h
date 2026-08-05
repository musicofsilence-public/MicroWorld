#pragma once

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/TDeferredActorSpawnStorage.h>
#include <MicroWorld/Engine/DefaultEngineTraits.h>
#include <MicroWorld/Engine/EngineClassIds.h>
#include <MicroWorld/Engine/EngineRuntime.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Core/PlaySystem.h>
#include <MicroWorld/Messaging/MessagingSystem.h>
#include <MicroWorld/Messaging/MessagingSystemInformation.h>
#include <MicroWorld/Networking/NetworkSystem.h>
#include <MicroWorld/Networking/NetworkSystemInformation.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Engine/WorldSubsystem.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/ClassRegistry.h>
#include <MicroWorld/Engine/ClassRegistryRegistrationView.h>
#include <MicroWorld/Engine/ClassRegistryView.h>
#include <MicroWorld/Engine/GarbageCollectionBudget.h>
#include <MicroWorld/Engine/GarbageCollectionPhase.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/GarbageCollectorStorage.h>
#include <MicroWorld/Engine/Object.h>
#include <MicroWorld/Engine/ObjectCreationResult.h>
#include <MicroWorld/Engine/ObjectHandle.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Engine/ObjectResult.h>
#include <MicroWorld/Engine/ObjectRootEntry.h>
#include <MicroWorld/Engine/ObjectSlotMetadata.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/ObjectStoreStorage.h>
#include <MicroWorld/Engine/StrongObjectPtr.h>
#include <MicroWorld/Engine/StrongObjectPointerResult.h>
#include <MicroWorld/Core/Containers/StaticVector.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Core/TimerManager.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace MicroWorld::Engine
{

#if defined(_MSC_VER)
// C4324: padding after the alignas(SlotAlign) slot storage is intentional; the
// host trades a few bytes for the caller-specified slot alignment.
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

/**
 * Motivation: Owns and wires every fixed-capacity runtime subsystem behind one canonical per-frame order, sizing all
 *   storage at compile time and never allocating.
 * Responsibilities: Own the class registry, object store, garbage collector, World actor/subsystem registries, timer
 *   manager, one Messaging system, and one optional Network system; run the documented fixed frame order; and drive
 *   an optional caller-owned system's lifecycle and per-frame turns.
 * Example:
 *   TEngine<> Engine(Budget);
 *   (void)Engine.CreateWorld();
 *   (void)Engine.BeginPlay(Now); Engine.Tick(Now + 16);
 */
template<typename TTraits = FDefaultEngineTraits>
class TEngine final : public IEngineRuntime
{
public:
	/** Motivation: Pulls the capacities out of the traits type so the body reads as compile-time constants. */
	static constexpr std::size_t MaxClasses = TTraits::MaxClasses;
	static constexpr std::size_t MaxObjects = TTraits::MaxObjects;
	static constexpr std::size_t SlotSizeBytes = TTraits::SlotSizeBytes;
	static constexpr std::size_t SlotAlign = TTraits::SlotAlign;
	static constexpr std::size_t MaxRoots = TTraits::MaxRoots;
	static constexpr std::size_t MaxActors = TTraits::MaxActors;
	/** Motivation: Bounds caller-owned subsystem reference storage without charging every object slot. */
	static constexpr std::size_t MaxSubsystems = TTraits::MaxSubsystems;
	static constexpr std::size_t MaxTimers = TTraits::MaxTimers;
	static constexpr std::size_t InlineTimerCallbackBytes = TTraits::InlineTimerCallbackBytes;
	static constexpr std::size_t InlineDeferredSpawnFactoryBytes = TTraits::InlineDeferredSpawnFactoryBytes;

	/** Motivation: Aliases the timer manager this engine owns, so callers name one concrete type. */
	using FTimerManager = Core::TTimerManager<MaxTimers, InlineTimerCallbackBytes>;

	/**
	 * Motivation: Builds every subsystem over this host's storage and registers the four Engine base descriptors so
	 *   Worlds, actors, components, and World subsystems are constructible.
	 * Responsibilities: Size collection and reclamation budgets (the latter defaulting to every slot so all pending
	 *   destroys are reclaimed each frame) and register the engine base classes.
	 */
	explicit TEngine(
		const FGarbageCollectionBudget InCollectionBudget, const std::uint32_t InReclamationBudget = static_cast<std::uint32_t>(MaxObjects)) noexcept
		: GarbageCollectionBudget(InCollectionBudget)
		, FrameReclamationBudget(InReclamationBudget)
		, Store(MakeStoreStorage(), MakeClassRegistryView(Registry))
		, Collector(Store, FGarbageCollectorStorage{Worklist.data(), static_cast<std::uint32_t>(MaxObjects)})
		, Timers(Core::TimePointMilliseconds{0})
	{
		RegisterBaseClasses();
	}

	/**
	 * Motivation: Builds the engine as the budget-only constructor does, then binds a caller-owned engine system so
	 *   BeginPlay and EndPlay drive its lifecycle turns while Tick drives its pre-advance turn first and post-advance
	 *   turn last.
	 * Responsibilities: Construct the engine and record the system pointer; the system and what stands behind it must
	 *   outlive this engine.
	 */
	explicit TEngine(
		const FGarbageCollectionBudget InCollectionBudget,
		Core::IPlaySystem& InSystem,
		const std::uint32_t InReclamationBudget = static_cast<std::uint32_t>(MaxObjects)) noexcept
		: TEngine(InCollectionBudget, InReclamationBudget)
	{
		System = &InSystem;
	}

	/**
	 * Motivation: Prevents copying or moving from duplicating this engine's unique ownership of the store, garbage
	 *   collector, registries, and timer manager.
	 * Responsibilities: Reject copy and move construction and assignment.
	 */
	TEngine(const TEngine&) = delete;

	/**
	 * Motivation: Prevents copy assignment from duplicating this engine's unique ownership of the store, garbage
	 *   collector, registries, and timer manager.
	 * Responsibilities: Reject copy assignment so the engine stays the single owner.
	 */
	TEngine& operator=(const TEngine&) = delete;

	/**
	 * Motivation: Prevents move construction from relocating this engine's uniquely owned subsystems and escaped references.
	 * Responsibilities: Reject move construction so engine identity stays fixed.
	 */
	TEngine(TEngine&&) = delete;

	/**
	 * Motivation: Prevents move assignment from relocating this engine's uniquely owned subsystems and escaped references.
	 * Responsibilities: Reject move assignment so engine identity stays fixed.
	 */
	TEngine& operator=(TEngine&&) = delete;

	/**
	 * Motivation: Lets a caller register one user class descriptor so the store accepts its construction.
	 * Responsibilities: Forward the descriptor to the registry and return its outcome.
	 */
	EObjectResult RegisterClass(const FClassDescriptor& InDescriptor) noexcept { return Registry.Register(InDescriptor); }

	/**
	 * Motivation: Lets a caller read one registered descriptor by type id, needed to build child descriptors (whose parent
	 *   must point at the registry's own copy) and to construct user types.
	 * Responsibilities: Return the registry's stored descriptor copy for the id, or null if it was never registered.
	 */
	const FClassDescriptor* FindClass(const FTypeId InTypeId) const noexcept { return Registry.Find(InTypeId); }

	/**
	 * Motivation: Lets a caller register a user type in one line by deriving its parent from TManagedType's engine base and
	 *   building the descriptor with the shared managed tracer.
	 * Responsibilities: Resolve the single-level parent from the engine base and register the descriptor; deeper user
	 *   hierarchies must use the descriptor overload with an explicit FindClass parent.
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
		else if constexpr (std::is_base_of<UWorldSubsystem, TManagedType>::value)
		{
			Parent = Registry.Find(UWorldSubsystemClassId);
		}
		const FClassDescriptor Candidate = MakeClassDescriptor<TManagedType>(InTypeId, InName, Parent, &TraceManagedObjectReferences);
		return Registry.Register(Candidate);
	}

	/**
	 * Motivation: Lets a caller construct a registered user type by type id without repeating the FindClass lookup.
	 * Responsibilities: Fold FindClass and NewObject into one call and return UnknownClass with a null object when the id
	 *   was never registered.
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
	 * Motivation: Lets a caller construct and root the single UWorld in the store in one call before BeginPlay.
	 * Responsibilities: Construct and root the world exactly once; return an empty reference if a world already exists or
	 *   creation fails.
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

		const TObjectCreationResult<UWorld> Creation = Store.NewObject<UWorld>(
			*Descriptor,
			ActorRegistry.MakeReference(),
			DeferredSpawns.MakeReference(),
			MakeClassRegistryRegistrationView(Registry),
			SubsystemRegistry.MakeReference(),
			GetNetworkSystem());
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

	/**
	 * Motivation: Lets callers construct managed objects in this store by forwarding to FObjectStore::NewObject.
	 * Responsibilities: Forward the arguments and return the store's creation result.
	 */
	template<typename TManagedType, typename... TArguments>
	TObjectCreationResult<TManagedType> NewObject(TArguments&&... Arguments) noexcept
	{
		return Store.NewObject<TManagedType>(std::forward<TArguments>(Arguments)...);
	}

	/**
	 * Motivation: Lets a caller reach the rooted world after CreateWorld has succeeded.
	 * Responsibilities: Return a reference to the single world.
	 */
	UWorld& GetWorld() noexcept { return *WorldRoot.Get(); }

	/**
	 * Motivation: Lets callers query stats or manage roots directly against the backing store.
	 * Responsibilities: Return the object store.
	 */
	FObjectStore& GetObjectStore() noexcept { return Store; }

	/**
	 * Motivation: Lets callers schedule and cancel bounded timers against the engine's timer manager.
	 * Responsibilities: Return the timer manager.
	 */
	FTimerManager& GetTimerManager() noexcept { return Timers; }

	/**
	 * Motivation: Gives the application entry point one bounded messaging system the engine drives, without needing a world first.
	 * Responsibilities: Construct the system in the reserved slot, and report Duplicate leaving the existing system and
	 *   its channels and subscriptions untouched when the slot is already filled.
	 */
	Core::ERuntimeResult CreateMessagingSystem(const Messaging::FMessagingSystemInformation& InInformation) noexcept
	{
		if (!MessagingSystems.IsEmpty())
		{
			return Core::ERuntimeResult::Duplicate;
		}

		return MessagingSystems.Emplace(InInformation);
	}

	/**
	 * Motivation: Lets a caller open channels and subscribe on the system the engine created and drives.
	 * Responsibilities: Return the one live messaging system, or null until a creation has succeeded.
	 */
	Messaging::FMessagingSystem* GetMessagingSystem() noexcept { return MessagingSystems.Data(); }

	/**
	 * Motivation: Lets the composition root create the optional Network layer only while Messaging is available and no
	 *   world has captured the layer pointer.
	 * Responsibilities: Construct and initialize one Network system transactionally; reject requests without Messaging,
	 *   after World creation, or after a prior Network creation without changing the existing engine state.
	 */
	Networking::ENetworkResult CreateNetworkSystem(const Networking::FNetworkSystemInformation& InInformation = {}) noexcept
	{
		Messaging::FMessagingSystem* const MessagingSystem = GetMessagingSystem();
		if (MessagingSystem == nullptr || WorldRoot.Get() != nullptr || !NetworkSystems.IsEmpty())
		{
			return Networking::ENetworkResult::Invalid;
		}

		const Core::ERuntimeResult StorageResult = NetworkSystems.Emplace(*MessagingSystem, InInformation);
		if (StorageResult != Core::ERuntimeResult::Success)
		{
			return Networking::ENetworkResult::Full;
		}

		Networking::FNetworkSystem* const NetworkSystem = GetNetworkSystem();
		const Networking::ENetworkResult InitializeResult = NetworkSystem->Initialize();
		if (InitializeResult == Networking::ENetworkResult::Success)
		{
			return Networking::ENetworkResult::Success;
		}

		NetworkSystems.Clear();
		return InitializeResult;
	}

	/**
	 * Motivation: Lets World construction and application code reach the optional Engine-owned Network layer.
	 * Responsibilities: Return the one initialized Network system, or null until creation succeeds.
	 */
	Networking::FNetworkSystem* GetNetworkSystem() noexcept { return NetworkSystems.Data(); }

	/**
	 * Motivation: Starts the bound system then the world at one canonical time and records it as the tick baseline.
	 * Responsibilities: Reject the start when no world has been created, then record the baseline and begin the system
	 *   before the world.
	 */
	Core::ERuntimeResult BeginPlay(const Core::TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		UWorld* const World = WorldRoot.Get();
		if (World == nullptr)
		{
			return Core::ERuntimeResult::InvalidLifecycle;
		}

		LastTickMilliseconds = InNowMilliseconds;
		BeginPlaySystems(InNowMilliseconds);
		return World->BeginPlay(InNowMilliseconds);
	}

	/**
	 * Motivation: Runs one canonical frame in the documented fixed order and returns the authoritative per-frame outcome.
	 * Responsibilities: Reject a rolled-back clock transactionally before any step runs; drive system pre-advance, timers,
	 *   world advance and apply, bounded reclamation, a GC slice, and system post-advance; the world advance/apply result
	 *   is authoritative since every other step is bounded best-effort.
	 */
	Core::ERuntimeResult Tick(const Core::TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		UWorld* const World = WorldRoot.Get();
		if (World == nullptr)
		{
			return Core::ERuntimeResult::InvalidLifecycle;
		}
		if (InNowMilliseconds < LastTickMilliseconds)
		{
			return Core::ERuntimeResult::NonMonotonicTime;
		}
		LastTickMilliseconds = InNowMilliseconds;

		PreAdvanceSystems(InNowMilliseconds);
		(void)Timers.Advance(InNowMilliseconds);
		const Core::ERuntimeResult FrameResult = AdvanceWorldAndApplyBarrier(*World, InNowMilliseconds);
		ReclaimAndCollect();
		PostAdvanceSystems(InNowMilliseconds);

		return FrameResult;
	}

	/**
	 * Motivation: Ends the world then the bound system so shutdown mirrors startup order.
	 * Responsibilities: End in reverse registration order and stay idempotent after a successful first call.
	 */
	Core::ERuntimeResult EndPlay() noexcept override
	{
		UWorld* const World = WorldRoot.Get();
		if (World == nullptr)
		{
			return Core::ERuntimeResult::InvalidLifecycle;
		}

		const Core::ERuntimeResult EndResult = World->EndPlay();
		EndPlaySystems();
		return EndResult;
	}

private:
	static_assert(MaxClasses >= 4, "TEngine requires class capacity for four Engine base descriptors.");
	static_assert(MaxObjects > 0, "TEngine needs at least one object slot for the world.");
	static_assert(MaxRoots > 0, "TEngine roots its world, so it needs at least one root entry.");
	static_assert(SlotSizeBytes % SlotAlign == 0, "Slot stride must preserve slot alignment.");

	/**
	 * Motivation: Registers the four engine base descriptors so base types are constructible.
	 * Responsibilities: Register UActorComponent, AActor, UWorld, and UWorldSubsystem descriptors exactly once.
	 */
	void RegisterBaseClasses() noexcept
	{
		(void)Registry.Register(UActorComponent::StaticClassDescriptor());
		(void)Registry.Register(AActor::StaticClassDescriptor());
		(void)Registry.Register(UWorld::StaticClassDescriptor());
		(void)Registry.Register(UWorldSubsystem::StaticClassDescriptor());
	}

	/**
	 * Motivation: Describes this host's complete caller-owned store storage for the store constructor.
	 * Responsibilities: Hand the store every caller-owned array, capacity, slot size, and alignment.
	 */
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

	/**
	 * Motivation: Starts the engine's systems before the world's actors receive their BeginPlay turn.
	 * Responsibilities: Begin the bound system, Messaging, then Network, skipping any absent optional system.
	 */
	void BeginPlaySystems(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
	{
		if (System != nullptr)
		{
			System->BeginPlay(InNowMilliseconds);
		}

		if (Messaging::FMessagingSystem* const MessagingSystem = GetMessagingSystem(); MessagingSystem != nullptr)
		{
			MessagingSystem->BeginPlay(InNowMilliseconds);
		}

		if (Networking::FNetworkSystem* const NetworkSystem = GetNetworkSystem(); NetworkSystem != nullptr)
		{
			NetworkSystem->BeginPlay(InNowMilliseconds);
		}
	}

	/**
	 * Motivation: Frame step 1 — device work and inbound messages land before the world advances, so a frame's arriving
	 *   messages are already delivered when actors run.
	 * Responsibilities: Pre-advance the bound system, Messaging, then Network, skipping any absent optional system.
	 */
	void PreAdvanceSystems(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
	{
		if (System != nullptr)
		{
			System->PreAdvance(InNowMilliseconds);
		}

		if (Messaging::FMessagingSystem* const MessagingSystem = GetMessagingSystem(); MessagingSystem != nullptr)
		{
			MessagingSystem->PreAdvance(InNowMilliseconds);
		}

		if (Networking::FNetworkSystem* const NetworkSystem = GetNetworkSystem(); NetworkSystem != nullptr)
		{
			NetworkSystem->PreAdvance(InNowMilliseconds);
		}
	}

	/**
	 * Motivation: Frame steps 3-4 — advances every actor then applies the spawn/destroy barrier.
	 * Responsibilities: Return the authoritative per-frame result (the advance error if any, otherwise the barrier result).
	 */
	Core::ERuntimeResult AdvanceWorldAndApplyBarrier(UWorld& InWorld, const Core::TimePointMilliseconds InNowMilliseconds) noexcept
	{
		const Core::ERuntimeResult AdvanceResult = InWorld.Advance(InNowMilliseconds);
		if (AdvanceResult != Core::ERuntimeResult::Success)
		{
			return AdvanceResult;
		}
		const Core::ERuntimeResult PendingResult = InWorld.ApplyPending(InNowMilliseconds);
		return PendingResult;
	}

	/**
	 * Motivation: Frame steps 5-6 — reclaims the slots the barrier marked, then starts a GC cycle when idle and advances
	 *   one bounded slice.
	 * Responsibilities: Reclaim pending-destroy slots (which the GC sweep skips) and advance the collector by its budget.
	 */
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

	/**
	 * Motivation: Frame step 7 — outbound work leaves after the world advanced, so messages an actor sent this frame go
	 *   out in the same frame; the reverse order mirrors startup and shutdown.
	 * Responsibilities: Post-advance Network, Messaging, then the bound system, skipping any absent optional system.
	 */
	void PostAdvanceSystems(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
	{
		if (Networking::FNetworkSystem* const NetworkSystem = GetNetworkSystem(); NetworkSystem != nullptr)
		{
			NetworkSystem->PostAdvance(InNowMilliseconds);
		}

		if (Messaging::FMessagingSystem* const MessagingSystem = GetMessagingSystem(); MessagingSystem != nullptr)
		{
			MessagingSystem->PostAdvance(InNowMilliseconds);
		}

		if (System != nullptr)
		{
			System->PostAdvance(InNowMilliseconds);
		}
	}

	/**
	 * Motivation: Ends the engine's systems after the world has delivered all actor EndPlay turns.
	 * Responsibilities: End Network, Messaging, then the bound system, skipping any absent optional system.
	 */
	void EndPlaySystems() noexcept
	{
		if (Networking::FNetworkSystem* const NetworkSystem = GetNetworkSystem(); NetworkSystem != nullptr)
		{
			NetworkSystem->EndPlay();
		}

		if (Messaging::FMessagingSystem* const MessagingSystem = GetMessagingSystem(); MessagingSystem != nullptr)
		{
			MessagingSystem->EndPlay();
		}

		if (System != nullptr)
		{
			System->EndPlay();
		}
	}

	/** Motivation: Bounds the per-tick garbage-collection slice supplied at construction. */
	FGarbageCollectionBudget GarbageCollectionBudget;

	/** Motivation: Bounds the per-tick store slots inspected by the destruction barrier. */
	std::uint32_t FrameReclamationBudget;

	/** Motivation: Owns composition-root device turns that start and pre-advance before Engine Messaging. */
	Core::IPlaySystem* System{nullptr};

	/** Motivation: Holds the one optional messaging system; its storage is reserved whether or not a system is created. */
	Core::TStaticVector<Messaging::FMessagingSystem, 1> MessagingSystems;

	/** Motivation: Holds the optional Network system after its Messaging-backed initialization succeeds. */
	Core::TStaticVector<Networking::FNetworkSystem, 1> NetworkSystems;

	/** Motivation: Records the last accepted tick time so a rolled-back clock is rejected. */
	Core::TimePointMilliseconds LastTickMilliseconds{0};

	/** Motivation: Owns the class registry that validates every managed construction. */
	TClassRegistry<MaxClasses> Registry;

	/** Motivation: Owns typed factory captures and completion slots that must outlive Store and World destruction. */
	TDeferredActorSpawnStorage<MaxActors, InlineDeferredSpawnFactoryBytes> DeferredSpawns;

	/** Motivation: Provides the first byte of equal-size, non-moving object slots. */
	alignas(SlotAlign) std::array<std::byte, SlotSizeBytes * MaxObjects> SlotStorage{};

	/** Motivation: Provides one lifecycle record per object slot. */
	std::array<FObjectSlotMetadata, MaxObjects> SlotMetadata{};

	/** Motivation: Provides independently reusable entries for explicit strong-root tokens. */
	std::array<FObjectRootEntry, MaxRoots> RootStorage{};

	/** Motivation: Owns every managed lifetime over this host's caller-owned storage. */
	FObjectStore Store;

	/** Motivation: Holds generation-checked reachable identities for one collector run. */
	std::array<FObjectHandle, MaxObjects> Worklist{};

	/** Motivation: Performs bounded incremental mark/sweep over the store. */
	FGarbageCollector Collector;

	/** Motivation: Owns the fixed actor registry referenced by the single world. */
	FWorldActorRegistry<MaxActors> ActorRegistry;

	/** Motivation: Owns the fixed subsystem registry referenced by the single world. */
	FWorldSubsystemRegistry<MaxSubsystems> SubsystemRegistry;

	/** Motivation: Owns the bounded timer set advanced first in every frame. */
	FTimerManager Timers;

	/** Motivation: Roots the single world so it survives collection for this host's lifetime. */
	TStrongObjectPtr<UWorld> WorldRoot;
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace MicroWorld::Engine
