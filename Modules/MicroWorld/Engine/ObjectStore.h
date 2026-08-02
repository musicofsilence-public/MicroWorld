#pragma once

#include <MicroWorld/Core/WeakOwner.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/ClassRegistry.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Engine/StrongObjectPtr.h>
#include <MicroWorld/Engine/StrongObjectPointerResult.h>

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace MicroWorld::Engine
{

class FGarbageCollector;
class FObjectStore;

/**
 * Motivation: Prevents object publication, destruction, root acquisition, and collection while one bounded callback
 *   cascade runs, so a BeginPlay or EndPlay dispatch cannot be reentered.
 * Responsibilities: Try to reserve the mutation boundary once and release it at scope exit; tolerate acquisition failure
 *   as a non-success rather than blocking, and still allow an existing root token to be released so noexcept RAII cleanup
 *   cannot leak reachability.
 * Example:
 *   FObjectStoreDispatchGuard Guard(Store);
 *   if (Guard.IsAcquired()) { Dispatch(); }
 */
class FObjectStoreDispatchGuard final
{
public:
	/**
	 * Motivation: Tries to exclude lifetime-changing work from one callback cascade.
	 * Responsibilities: Reserve the dispatch boundary or leave the guard not acquired.
	 */
	explicit FObjectStoreDispatchGuard(FObjectStore& InStore) noexcept;

	/**
	 * Motivation: Ensures the callback exclusion is released when the guard leaves scope.
	 * Responsibilities: Release the reservation only when this instance acquired it.
	 */
	~FObjectStoreDispatchGuard() noexcept;

	/**
	 * Motivation: Prevents two guards from releasing one dispatch reservation.
	 * Responsibilities: Reject copy construction so each reservation has one owner.
	 */
	FObjectStoreDispatchGuard(const FObjectStoreDispatchGuard&) = delete;

	/**
	 * Motivation: Prevents replacing this guard's unique dispatch reservation via assignment.
	 * Responsibilities: Reject copy assignment so each reservation has one owner.
	 */
	FObjectStoreDispatchGuard& operator=(const FObjectStoreDispatchGuard&) = delete;

	/**
	 * Motivation: Lets a caller branch on whether callback dispatch may proceed under this guard.
	 * Responsibilities: Report true only when this instance holds the reservation.
	 */
	bool IsAcquired() const noexcept { return ObjectStore != nullptr; }

private:
	/** Motivation: Identifies the store reservation released at scope exit, or null after rejection. */
	FObjectStore* ObjectStore{nullptr};
};

/**
 * Motivation: Names the store-owned lifecycle phase of one caller-supplied slot so lifecycle, destruction, retirement,
 *   and reuse decisions read as one value instead of several flags.
 * Responsibilities: Distinguish vacant, constructing, live, pending-destroy, destroying, and retired phases.
 * Example:
 *   if (Slot.State == EObjectSlotState::Live) { Resolve(); }
 */
enum class EObjectSlotState : std::uint8_t
{
	/** Motivation: Allows the slot to publish another generation when one remains. */
	Vacant,

	/** Motivation: Hides storage while a nothrow placement constructor is running. */
	Constructing,

	/** Motivation: Makes the constructed object resolvable and eligible for tracing. */
	Live,

	/** Motivation: Hides the object immediately until the destruction barrier reclaims it. */
	PendingDestroy,

	/** Motivation: Prevents lifecycle callbacks from recursively destroying the same slot. */
	Destroying,

	/** Motivation: Permanently prevents reuse after the generation space is exhausted. */
	Retired,
};

/**
 * Motivation: Holds lifecycle metadata in storage supplied and owned by the application so the store keeps no
 *   bookkeeping of its own.
 * Responsibilities: Carry the slot's generation, descriptor, object pointer, state, and collector mark for one slot.
 * Example:
 *   FObjectSlotMetadata Slot;
 *   Slot.State = EObjectSlotState::Live;
 */
struct FObjectSlotMetadata
{
	/** Motivation: Names this slot's current identity without ever wrapping it; it advances the moment an object dies, so a stale handle and a weak
	 * reference both stop matching immediately rather than waiting for the slot to be reused. */
	ObjectGeneration Generation{0};

	/** Motivation: Selects tracing, ancestry, layout, and exact destruction for the active object. */
	const FClassDescriptor* Descriptor{nullptr};

	/** Motivation: Points at the active UObject base within the non-moving slot. */
	UObject* Object{nullptr};

	/** Motivation: Prevents unpublished, pending, vacant, and retired storage from resolving. */
	EObjectSlotState State{EObjectSlotState::Vacant};

	/** Motivation: Stores one collector mark without allocating a side table. */
	bool bMarked{false};
};

/**
 * Motivation: Holds one independently owned explicit-root token in caller-supplied storage.
 * Responsibilities: Carry the rooted lifetime's handle or remain invalid while free.
 * Example:
 *   FObjectRootEntry Root;
 *   Root.Handle = Store.AddRoot(Handle);
 */
struct FObjectRootEntry
{
	/** Motivation: Identifies the rooted lifetime or remains invalid while this entry is free. */
	FObjectHandle Handle{};
};

/**
 * Motivation: Describes all non-owning fixed storage required by one object store so the store itself stays allocation-free.
 * Responsibilities: Bundle slot payload bytes, slot metadata, slot sizing, root entries, and root capacity into one value
 *   the application owns for the store's lifetime.
 * Example:
 *   FObjectStoreStorage Storage{Bytes, N, Meta, N, Size, Align, Roots, R};
 */
struct FObjectStoreStorage
{
	/** Motivation: Provides the first byte of equal-size, non-moving object slots. */
	std::byte* SlotPayloadBytes{nullptr};

	/** Motivation: Provides enough bytes for SlotCount consecutive SlotSizeBytes ranges. */
	std::size_t TotalSlotStorageBytes{0};

	/** Motivation: Provides one lifecycle record for every equal-size object slot. */
	FObjectSlotMetadata* SlotMetadata{nullptr};

	/** Motivation: Fixes the number of object lifetimes that may be active concurrently. */
	std::uint32_t SlotCount{0};

	/** Motivation: Fixes the maximum object extent and internal-fragmentation unit. */
	std::size_t SlotSizeBytes{0};

	/** Motivation: Fixes the maximum supported object alignment for every slot. */
	std::size_t SlotAlignmentBytes{0};

	/** Motivation: Provides independently reusable entries for explicit strong-root tokens. */
	FObjectRootEntry* Roots{nullptr};

	/** Motivation: Fixes the number of simultaneous independently owned root tokens. */
	std::uint32_t RootCapacity{0};
};

/**
 * Motivation: Provides a non-owning lookup over an explicitly registered class set so FObjectStore stays independent of
 *   registry capacity while still validating descriptor identity without RTTI.
 * Responsibilities: Bind one application-owned registry context with an allocation-free find operation and answer class
 *   lookups or null when unbound.
 * Example:
 *   FClassRegistryView View = MakeClassRegistryView(Registry);
 *   const FClassDescriptor* D = View.Find(Id);
 */
class FClassRegistryView final
{
public:
	/** Motivation: Defines the only operation needed from an application-owned registry. */
	using FFindClass = const FClassDescriptor* (*)(const void*, FTypeId) noexcept;

	/**
	 * Motivation: Creates an empty view that rejects every class as unknown.
	 * Responsibilities: Produce an unbound view whose Find always returns null.
	 */
	FClassRegistryView() noexcept = default;

	/**
	 * Motivation: Binds a stable registry context and its allocation-free lookup operation.
	 * Responsibilities: Store the context and find callable for the store's lifetime.
	 */
	FClassRegistryView(const void* InContext, FFindClass InFindClass) noexcept : Context(InContext), FindClass(InFindClass) {}

	/**
	 * Motivation: Finds one descriptor by local type identifier without changing registry state.
	 * Responsibilities: Return the descriptor for a bound view or null when unbound or unknown.
	 */
	const FClassDescriptor* Find(const FTypeId InTypeId) const noexcept
	{
		return Context != nullptr && FindClass != nullptr ? FindClass(Context, InTypeId) : nullptr;
	}

private:
	/** Motivation: Identifies the application-owned registry retained for the store lifetime. */
	const void* Context{nullptr};

	/** Motivation: Performs bounded registry lookup without virtual allocation or RTTI. */
	FFindClass FindClass{nullptr};
};

/**
 * Motivation: Gives World the only two mutable-registry operations it needs for deferred actor construction without
 *   exposing registry capacity or storage.
 * Responsibilities: Bind one application-owned registry for find, type-token lookup, and automatic registration, and
 *   forward each operation or a safe rejection when unbound.
 * Example:
 *   FClassRegistryRegistrationView View = MakeClassRegistryRegistrationView(Registry);
 *   View.RegisterAutomatic(Candidate, OutDescriptor);
 */
class FClassRegistryRegistrationView final
{
public:
	/** Motivation: Defines bounded find, type-token lookup, and automatic registration operations. */
	using FFindClass = const FClassDescriptor* (*)(const void*, FTypeId) noexcept;
	using FFindByTypeToken = const FClassDescriptor* (*)(const void*, const void*) noexcept;
	using FRegisterAutomatic = EObjectResult (*)(void*, FClassDescriptor, const FClassDescriptor*&) noexcept;

	/**
	 * Motivation: Creates an empty view that rejects registration without mutation.
	 * Responsibilities: Produce an unbound view whose operations reject without touching any registry.
	 */
	FClassRegistryRegistrationView() noexcept = default;

	/**
	 * Motivation: Binds one application-owned registry for the world lifetime.
	 * Responsibilities: Store the context and all three callables for the world's lifetime.
	 */
	FClassRegistryRegistrationView(
		void* const InContext,
		const FFindClass InFindClass,
		const FFindByTypeToken InFindByTypeToken,
		const FRegisterAutomatic InRegisterAutomatic) noexcept
		: Context(InContext), FindClass(InFindClass), FindTypeToken(InFindByTypeToken), RegisterAutomaticFunction(InRegisterAutomatic)
	{
	}

	/**
	 * Motivation: Finds a canonical descriptor by local class ID without mutation.
	 * Responsibilities: Return the descriptor for a bound view or null when unbound or unknown.
	 */
	const FClassDescriptor* Find(const FTypeId InTypeId) const noexcept
	{
		return Context != nullptr && FindClass != nullptr ? FindClass(Context, InTypeId) : nullptr;
	}

	/**
	 * Motivation: Finds a canonical descriptor by exact no-RTTI type token without mutation.
	 * Responsibilities: Return the descriptor for a bound view or null when unbound or unknown.
	 */
	const FClassDescriptor* FindByTypeToken(const void* const InTypeToken) const noexcept
	{
		return Context != nullptr && FindTypeToken != nullptr ? FindTypeToken(Context, InTypeToken) : nullptr;
	}

	/**
	 * Motivation: Registers a candidate or returns its existing canonical descriptor.
	 * Responsibilities: Forward to automatic registration for a bound view or return UnknownClass when unbound.
	 */
	EObjectResult RegisterAutomatic(const FClassDescriptor InCandidate, const FClassDescriptor*& OutDescriptor) const noexcept
	{
		OutDescriptor = nullptr;
		return Context != nullptr && RegisterAutomaticFunction != nullptr ? RegisterAutomaticFunction(Context, InCandidate, OutDescriptor)
																		  : EObjectResult::UnknownClass;
	}

	/**
	 * Motivation: Lets a caller confirm all required registry operations are available before use.
	 * Responsibilities: Report true only when context and all three callables are set.
	 */
	bool IsValid() const noexcept
	{
		return Context != nullptr && FindClass != nullptr && FindTypeToken != nullptr && RegisterAutomaticFunction != nullptr;
	}

private:
	/** Motivation: Identifies the registry whose lifetime encloses this view. */
	void* Context{nullptr};

	/** Motivation: Performs canonical descriptor lookup by local ID. */
	FFindClass FindClass{nullptr};

	/** Motivation: Reuses an explicitly registered descriptor by exact C++ type token. */
	FFindByTypeToken FindTypeToken{nullptr};

	/** Motivation: Adds only a validated descriptor to caller-owned fixed registry storage. */
	FRegisterAutomatic RegisterAutomaticFunction{nullptr};
};

/**
 * Motivation: Creates a type-erased non-owning view over one fixed-capacity class registry.
 * Responsibilities: Bind the registry and a lambda that forwards Find by id.
 */
template<std::size_t MaxClasses>
FClassRegistryView MakeClassRegistryView(const TClassRegistry<MaxClasses>& Registry) noexcept
{
	return FClassRegistryView(
		&Registry,
		[](const void* InContext, const FTypeId InTypeId) noexcept -> const FClassDescriptor*
		{ return static_cast<const TClassRegistry<MaxClasses>*>(InContext)->Find(InTypeId); });
}

/**
 * Motivation: Creates World's narrow mutable capability over an application-owned class registry.
 * Responsibilities: Bind the registry and lambdas that forward Find, FindByTypeToken, and RegisterAutomatic.
 */
template<std::size_t MaxClasses>
FClassRegistryRegistrationView MakeClassRegistryRegistrationView(TClassRegistry<MaxClasses>& Registry) noexcept
{
	return FClassRegistryRegistrationView(
		&Registry,
		[](const void* const InContext, const FTypeId InTypeId) noexcept -> const FClassDescriptor*
		{ return static_cast<const TClassRegistry<MaxClasses>*>(InContext)->Find(InTypeId); },
		[](const void* const InContext, const void* const InTypeToken) noexcept -> const FClassDescriptor*
		{ return static_cast<const TClassRegistry<MaxClasses>*>(InContext)->FindByTypeToken(InTypeToken); },
		[](void* const InContext, const FClassDescriptor InCandidate, const FClassDescriptor*& OutDescriptor) noexcept -> EObjectResult
		{ return static_cast<TClassRegistry<MaxClasses>*>(InContext)->RegisterAutomatic(InCandidate, OutDescriptor); });
}

/**
 * Motivation: Invokes the exact public nothrow destructor bound to one managed C++ type so the store can destroy an
 *   object through a type-erased pointer without exposing delete.
 * Responsibilities: Call the exact T destructor on the object after asserting T is UObject-derived and nothrow
 *   destructible.
 */
template<typename T>
void DestroyManagedObject(UObject& InObject) noexcept
{
	static_assert(std::is_base_of<UObject, T>::value, "Managed destruction requires a UObject-derived type.");
	static_assert(std::is_nothrow_destructible<T>::value, "Managed destruction requires an accessible noexcept destructor.");
	static_cast<T&>(InObject).~T();
}

/**
 * Motivation: Creates a descriptor whose layout and exact destructor are bound to T without RTTI.
 * Responsibilities: Build a descriptor with T's size, alignment, type token, and exact destructor after asserting T is
 *   UObject-derived and nothrow destructible.
 */
template<typename T>
FClassDescriptor MakeClassDescriptor(
	const FTypeId InTypeId,
	const char* const InDiagnosticName,
	const FClassDescriptor* const InParent = nullptr,
	const FTraceObjectReferences InTraceReferences = nullptr) noexcept
{
	static_assert(std::is_base_of<UObject, T>::value, "Managed descriptors require a UObject-derived type.");
	static_assert(std::is_nothrow_destructible<T>::value, "Managed descriptors require an accessible noexcept destructor.");

	return FClassDescriptor{
		InTypeId,
		InDiagnosticName,
		InParent,
		sizeof(T),
		alignof(T),
		InTraceReferences,
		&DestroyManagedObject<T>,
		ManagedObjectTypeToken<T>(),
	};
}

/**
 * Motivation: Reports placement construction success or one explicit bounded-store failure alongside the published
 *   object.
 * Responsibilities: Distinguish the outcome and resolve the object only when Result is Success.
 * Example:
 *   TObjectCreationResult<AActor> R = Store.NewObject<AActor>(Args);
 *   if (R.Result == EObjectResult::Success) { R.Object.Get()->Tick(); }
 */
template<typename T>
struct TObjectCreationResult
{
	/** Motivation: Distinguishes capacity, layout, class, generation, and successful outcomes. */
	EObjectResult Result{EObjectResult::CapacityExceeded};

	/** Motivation: Resolves the newly published object only when Result is Success. */
	TObjectPtr<T> Object{};
};

/**
 * Motivation: Reports bounded pending-destruction work performed at one mutation barrier so a caller can drive
 *   reclamation across multiple frames.
 * Responsibilities: Summarize the result, slots visited, objects destroyed, and pending objects remaining.
 * Example:
 *   FObjectMutationResult R = Store.ApplyPendingDestroy(Budget);
 *   if (R.PendingObjectsRemaining == 0) { Done(); }
 */
struct FObjectMutationResult
{
	/** Motivation: Reports invalid store configuration or successful bounded traversal. */
	EObjectResult Result{EObjectResult::Success};

	/** Motivation: Counts slots inspected so every call's work remains observable and bounded. */
	std::uint32_t SlotsVisited{0};

	/** Motivation: Counts objects whose BeginDestroy and exact destructor ran in this call. */
	std::uint32_t ObjectsDestroyed{0};

	/** Motivation: Reports pending objects left for a later explicit mutation barrier. */
	std::uint32_t PendingObjectsRemaining{0};
};

/**
 * Motivation: Exposes fixed capacity, occupancy, roots, retirement, and fragmentation as one snapshot so a caller can
 *   observe store health without inspecting private state.
 * Responsibilities: Count slot capacity, occupancy, pending-destroy and retired slots, root capacity and active roots,
 *   and payload and fragmentation bytes.
 * Example:
 *   FObjectStoreStats S = Store.Stats();
 *   if (S.OccupiedSlots == S.SlotCapacity) { Full(); }
 */
struct FObjectStoreStats
{
	/** Motivation: Reports the caller-selected number of equal-size object slots. */
	std::uint32_t SlotCapacity{0};

	/** Motivation: Counts constructed live and pending objects that still occupy slots. */
	std::uint32_t OccupiedSlots{0};

	/** Motivation: Counts occupied objects hidden until the destruction barrier reclaims them. */
	std::uint32_t PendingDestroySlots{0};

	/** Motivation: Counts slots permanently unavailable because their generation was exhausted. */
	std::uint32_t RetiredSlots{0};

	/** Motivation: Reports the caller-selected independent explicit-root capacity. */
	std::uint32_t RootCapacity{0};

	/** Motivation: Counts independently owned root tokens, including duplicate handles. */
	std::uint32_t ActiveRoots{0};

	/** Motivation: Reports the fixed byte extent reserved for every object slot. */
	std::size_t SlotSizeBytes{0};

	/** Motivation: Sums exact descriptor extents for all occupied objects. */
	std::size_t ObjectPayloadBytes{0};

	/** Motivation: Reports equal-slot bytes unavailable to object payloads while occupied. */
	std::size_t InternalFragmentationBytes{0};
};

/**
 * Motivation: Owns managed-object lifetimes over fixed, non-moving, caller-owned storage so the engine keeps a stable
 *   object identity without allocating bookkeeping or moving published objects.
 * Responsibilities: Validate and initialize caller-owned storage, construct and publish UObject-derived values through
 *   registered descriptors, resolve generation-checked handles, mark and reclaim pending destroys at an explicit
 *   barrier, and manage independent root tokens; never invoke collection after failure, read a clock, or move a
 *   published object.
 * Example:
 *   FObjectStore Store(Storage, MakeClassRegistryView(Registry));
 *   auto R = Store.NewObject<AActor>(Descriptor);
 *   if (R.Result == EObjectResult::Success) { R.Object.Get()->Tick(); }
 */
class FObjectStore final
{
public:
	/**
	 * Motivation: Validates and initializes caller-owned slot metadata and root entries.
	 * Responsibilities: Record the storage and class view and store the configuration result for later queries.
	 */
	FObjectStore(FObjectStoreStorage InStorage, FClassRegistryView InClasses) noexcept;

	/**
	 * Motivation: Ensures every remaining object is destroyed before caller-owned storage may disappear.
	 * Responsibilities: Destroy every live and pending object through its exact destructor.
	 */
	~FObjectStore() noexcept;

	/**
	 * Motivation: Preserves the unique identity of caller-owned storage and all published handles.
	 * Responsibilities: Reject copy construction so storage and handle identity stay single-owner.
	 */
	FObjectStore(const FObjectStore&) = delete;

	/**
	 * Motivation: Prevents two stores from mutating the same metadata and object lifetimes.
	 * Responsibilities: Reject copy assignment so storage and handle identity stay single-owner.
	 */
	FObjectStore& operator=(const FObjectStore&) = delete;

	/**
	 * Motivation: Preserves the address used by every managed object and pointer bridge.
	 * Responsibilities: Reject move construction so published TObjectPtr values never dangle.
	 */
	FObjectStore(FObjectStore&&) = delete;

	/**
	 * Motivation: Prevents moving store identity behind already published TObjectPtr values.
	 * Responsibilities: Reject move assignment so published TObjectPtr values never dangle.
	 */
	FObjectStore& operator=(FObjectStore&&) = delete;

	/**
	 * Motivation: Constructs one UObject-derived value using an explicit registered descriptor.
	 * Responsibilities: Reject mutation during a locked boundary and a bad descriptor or layout before any generation
	 *   changes or placement construction begins, then publish a generation-checked handle; T must be nothrow
	 *   constructible because portable builds disable exception unwinding.
	 */
	template<typename T, typename... TArguments>
	TObjectCreationResult<T> NewObject(const FClassDescriptor& InDescriptor, TArguments&&... Arguments) noexcept
	{
		static_assert(std::is_base_of<UObject, T>::value, "FObjectStore can construct only UObject-derived values.");
		static_assert(std::is_nothrow_constructible<T, TArguments...>::value, "Managed-object construction must be noexcept.");

		TObjectCreationResult<T> Creation{};
		if (IsPublicMutationLocked())
		{
			Creation.Result = EObjectResult::LifecycleLocked;
			return Creation;
		}

		Creation.Result = ValidateConstruction<T>(InDescriptor);
		if (Creation.Result != EObjectResult::Success)
		{
			return Creation;
		}

		const ObjectIndex SlotIndex = FindVacantSlot();
		if (SlotIndex == FObjectHandle::InvalidIndex)
		{
			Creation.Result = RetiredSlotCount == Storage.SlotCount ? EObjectResult::GenerationExhausted : EObjectResult::CapacityExceeded;
			return Creation;
		}

		Storage.SlotMetadata[SlotIndex].State = EObjectSlotState::Constructing;
		bMutationLocked = true;

		void* const PlacementAddress = SlotAddress(SlotIndex);
		T* const ConstructedObject = ::new (PlacementAddress) T(std::forward<TArguments>(Arguments)...);
		UObject* const ManagedObject = static_cast<UObject*>(ConstructedObject);

		const FObjectHandle Handle = PublishObjectIntoSlot(SlotIndex, InDescriptor, *ManagedObject);
		bMutationLocked = false;
		Creation.Result = EObjectResult::Success;
		Creation.Object = TObjectPtr<T>(*this, Handle);
		return Creation;
	}

	/**
	 * Motivation: Constructs through a type's optional static descriptor convention so callers spawn without naming the
	 *   descriptor explicitly.
	 * Responsibilities: Forward to the explicit-descriptor overload when T exposes StaticClassDescriptor().
	 */
	template<typename T, typename... TArguments>
	auto NewObject(TArguments&&... Arguments) noexcept -> decltype(T::StaticClassDescriptor(), TObjectCreationResult<T>{})
	{
		return NewObject<T>(T::StaticClassDescriptor(), std::forward<TArguments>(Arguments)...);
	}

	/**
	 * Motivation: Lets traced and weak references resolve a handle without changing store state.
	 * Responsibilities: Return the live UObject for a matching generation or null.
	 */
	UObject* Resolve(FObjectHandle InHandle) const noexcept;

	/**
	 * Motivation: Lets a weak reference outside Engine watch one object's lifetime through a plain counter, without naming any Engine type.
	 * Responsibilities: Return the stable address of the live slot's generation, or null when InHandle no longer names a live object. The address
	 *   stays valid for the store's lifetime because slot metadata is caller-owned and never moves; the value behind it changes the moment the object
	 *   dies, which is how a holder learns the object is gone.
	 */
	const ObjectGeneration* GetSlotGenerationAddress(FObjectHandle InHandle) const noexcept;

	/**
	 * Motivation: Hides one live object immediately and queues it for the explicit destruction barrier.
	 * Responsibilities: Move a live matching-generation slot to PendingDestroy or report why it cannot.
	 */
	EObjectResult MarkPendingDestroy(FObjectHandle InHandle) noexcept;

	/**
	 * Motivation: Bounds pending-destruction work so reclamation can run across multiple frames.
	 * Responsibilities: Inspect at most InMaxSlotsToInspect slots and destroy the pending objects encountered.
	 */
	FObjectMutationResult ApplyPendingDestroy(std::uint32_t InMaxSlotsToInspect) noexcept;

	/**
	 * Motivation: Registers one independent root token after live-handle and capacity validation.
	 * Responsibilities: Reject a stale or foreign handle, a duplicate, and a full root table, leaving state unchanged.
	 */
	EObjectResult AddRoot(FObjectHandle InHandle) noexcept;

	/**
	 * Motivation: Releases one matching root token even while guarded work is active, so noexcept strong-pointer cleanup
	 *   stays leak-free.
	 * Responsibilities: Remove the matching token immediately; this changes only future reachability, not destruction,
	 *   slot reuse, or collection, which stay blocked until the active boundary ends.
	 */
	EObjectResult RemoveRoot(FObjectHandle InHandle) noexcept;

	/**
	 * Motivation: Registers one independent root token and transfers it into an RAII owner in one call.
	 * Responsibilities: Validate the reference belongs to this store, then AddRoot and return a strong pointer on success.
	 */
	template<typename T>
	TStrongObjectPointerResult<T> MakeStrongObjectPtr(const TObjectPtr<T> InObject) noexcept
	{
		TStrongObjectPointerResult<T> StrongResult{};
		if (InObject.Store != this)
		{
			StrongResult.Result = EObjectResult::StaleHandle;
			return StrongResult;
		}

		StrongResult.Result = AddRoot(InObject.TargetHandle);
		if (StrongResult.Result == EObjectResult::Success)
		{
			StrongResult.Pointer = TStrongObjectPtr<T>(*this, InObject.TargetHandle);
		}
		return StrongResult;
	}

	/**
	 * Motivation: Lets a caller confirm constructor validation so malformed storage never fails implicitly.
	 * Responsibilities: Return the configuration result recorded at construction.
	 */
	EObjectResult ConfigurationResult() const noexcept { return StoreConfigurationResult; }

	/**
	 * Motivation: Lets a caller observe fixed capacity, current occupancy, roots, and slot fragmentation.
	 * Responsibilities: Return one FObjectStoreStats snapshot of the current store state.
	 */
	FObjectStoreStats Stats() const noexcept;

	/**
	 * Motivation: Lets a caller branch on whether publication, destruction, root acquisition, or collection is blocked.
	 * Responsibilities: Report true when mutation, dispatch, or collection is active.
	 */
	bool IsMutationLocked() const noexcept { return bMutationLocked || bDispatchLocked || ActiveCollector != nullptr; }

	/**
	 * Motivation: Lets callbacks report only active incremental collection so deferred construction may still queue safely.
	 * Responsibilities: Report true only while a collector owns store traversal.
	 */
	bool IsCollectionActive() const noexcept { return ActiveCollector != nullptr; }

private:
	friend class FGarbageCollector;
	friend class FObjectStoreDispatchGuard;

	/**
	 * Motivation: Validates descriptor identity and exact T layout before any slot mutation.
	 * Responsibilities: Reject a bad configuration, a descriptor the registry does not own, and a size, alignment, type
	 *   token, or destructor mismatch with T.
	 */
	template<typename T>
	EObjectResult ValidateConstruction(const FClassDescriptor& InDescriptor) const noexcept
	{
		if (StoreConfigurationResult != EObjectResult::Success)
		{
			return StoreConfigurationResult;
		}
		if (ClassRegistryLookup.Find(InDescriptor.TypeId) != &InDescriptor)
		{
			return EObjectResult::UnknownClass;
		}
		if (InDescriptor.SizeBytes != sizeof(T) || InDescriptor.AlignmentBytes != alignof(T) || InDescriptor.TypeToken != ManagedObjectTypeToken<T>()
			|| InDescriptor.Destroy != &DestroyManagedObject<T>)
		{
			return EObjectResult::UnsupportedObjectLayout;
		}
		if (sizeof(T) > Storage.SlotSizeBytes || alignof(T) > Storage.SlotAlignmentBytes)
		{
			return EObjectResult::UnsupportedObjectLayout;
		}
		return EObjectResult::Success;
	}

	/**
	 * Motivation: Reports whether the caller-supplied storage descriptor has valid slot and root layout before use.
	 * Responsibilities: Return true only when slot and root arrays, counts, and alignment invariants hold.
	 */
	static bool IsStorageDescriptorValid(const FObjectStoreStorage& InStorage) noexcept;

	/**
	 * Motivation: Lets NewObject locate the next slot without changing its generation or state.
	 * Responsibilities: Return the first reusable slot index or the invalid sentinel when none remains.
	 */
	ObjectIndex FindVacantSlot() const noexcept;

	/**
	 * Motivation: Lets placement construction reach one validated equal-size slot.
	 * Responsibilities: Return the stable first byte of the slot at the index.
	 */
	void* SlotAddress(ObjectIndex InSlotIndex) const noexcept;

	/**
	 * Motivation: Lets resolve and mutation paths share one generation-checked slot lookup.
	 * Responsibilities: Return the matching slot or null for an index, generation, or occupancy mismatch.
	 */
	FObjectSlotMetadata* FindMatchingSlot(FObjectHandle InHandle, bool bInAllowPending) noexcept;

	/**
	 * Motivation: Provides const matching-slot validation to query-only operations.
	 * Responsibilities: Return the matching slot or null for an index, generation, or occupancy mismatch.
	 */
	const FObjectSlotMetadata* FindMatchingSlot(FObjectHandle InHandle, bool bInAllowPending) const noexcept;

	/**
	 * Motivation: Reclaims one slot by running BeginDestroy and exact destruction once, then vacating or retiring it.
	 * Responsibilities: Reject a non-destroyable slot, run the destruction callbacks, and recycle or retire the slot.
	 */
	EObjectResult DestroySlot(ObjectIndex InSlotIndex) noexcept;

	/**
	 * Motivation: Removes every independent root token for a lifetime that can no longer resolve.
	 * Responsibilities: Drop all root entries whose handle matches without failing when none match.
	 */
	void RemoveAllRoots(FObjectHandle InHandle) noexcept;

	/**
	 * Motivation: Rejects an index that is out of range or not a destroyable live/pending slot.
	 * Responsibilities: Return a result naming why a slot cannot be destroyed or Success.
	 */
	EObjectResult ValidateDestroyableSlot(ObjectIndex InSlotIndex) const noexcept;

	/**
	 * Motivation: Reports whether a slot holds a destroyable object with a complete destructor chain.
	 * Responsibilities: Return true only for a live or pending-destroy slot with a descriptor and destructor.
	 */
	static bool IsSlotDestroyable(const FObjectSlotMetadata& InSlot) noexcept;

	/**
	 * Motivation: Runs BeginDestroy and exact destruction, then drops the lifetime's root tokens.
	 * Responsibilities: Invoke BeginDestroy and the exact destructor once and remove the lifetime's roots.
	 */
	void RunDestructionCallbacks(FObjectSlotMetadata& InSlot, FObjectHandle InHandle) noexcept;

	/**
	 * Motivation: Clears the slot's live pointers and either vacates it or retires it before wrap.
	 * Responsibilities: Reset the slot's object and descriptor and retire it when its generation cannot advance.
	 */
	void RecycleOrRetireSlot(FObjectSlotMetadata& InSlot) noexcept;

	/**
	 * Motivation: Decrements occupancy, pending, and payload totals for one destroyed object.
	 * Responsibilities: Adjust the occupied, pending, and payload counters consistently with the destroyed slot.
	 */
	void UpdateOccupancyCounters(bool bInWasPending, std::size_t InPayloadBytes) noexcept;

	/**
	 * Motivation: Computes the generation a reused slot publishes next.
	 * Responsibilities: Return the next generation for a current value, with zero meaning never published.
	 */
	static ObjectGeneration NextPublishGeneration(ObjectGeneration InCurrentGeneration) noexcept;

	/**
	 * Motivation: Wires object identity and slot metadata for a freshly constructed object and returns its handle.
	 * Responsibilities: Stamp the object's store, handle, and descriptor and advance the slot to Live.
	 */
	FObjectHandle PublishObjectIntoSlot(ObjectIndex InSlotIndex, const FClassDescriptor& InDescriptor, UObject& InManagedObject) noexcept;

	/**
	 * Motivation: Returns the next pending-destroy slot to inspect and advances the wrapping scan cursor.
	 * Responsibilities: Advance the round-robin cursor and return the next pending slot index or the sentinel.
	 */
	ObjectIndex AdvancePendingScanCursor() noexcept;

	// --- Collector-only interface (FGarbageCollector friend) ---
	// FGarbageCollector reaches slot marks, occupancy, and cycle ownership through
	// these private methods rather than public mutators, so no ordinary caller can
	// start, observe, or end a collection out of phase. Keeping the back-channel
	// private means the store's public surface can never corrupt an in-progress cycle.
	/**
	 * Motivation: Reports collector iteration capacity without exposing mutable metadata publicly.
	 * Responsibilities: Return the caller-selected slot count.
	 */
	std::uint32_t CollectorSlotCapacity() const noexcept { return Storage.SlotCount; }

	/**
	 * Motivation: Reports collector root iteration capacity including currently empty entries.
	 * Responsibilities: Return the caller-selected root capacity.
	 */
	std::uint32_t CollectorRootCapacity() const noexcept { return Storage.RootCapacity; }

	/**
	 * Motivation: Lets the collector read one root token by index.
	 * Responsibilities: Return the root handle at the index or an invalid handle for a free or out-of-range entry.
	 */
	FObjectHandle CollectorRootAt(std::uint32_t InRootIndex) const noexcept;

	/**
	 * Motivation: Lets the collector read the current live handle for one slot.
	 * Responsibilities: Return the live handle at the index or an invalid handle otherwise.
	 */
	FObjectHandle CollectorHandleAt(ObjectIndex InSlotIndex) const noexcept;

	/**
	 * Motivation: Lets the collector resolve one live slot by index for descriptor-driven tracing.
	 * Responsibilities: Return the live UObject at the index or null.
	 */
	UObject* CollectorObjectAt(ObjectIndex InSlotIndex) const noexcept;

	/**
	 * Motivation: Lets the collector test whether a slot contains a live or pending object.
	 * Responsibilities: Report true for any occupied slot state.
	 */
	bool CollectorIsOccupied(ObjectIndex InSlotIndex) const noexcept;

	/**
	 * Motivation: Lets the collector test whether a slot is irreversibly pending destruction.
	 * Responsibilities: Report true only for a pending-destroy slot state.
	 */
	bool CollectorIsPendingDestroy(ObjectIndex InSlotIndex) const noexcept;

	/**
	 * Motivation: Reports whether a slot state counts as occupied (live, pending destroy, or destroying).
	 * Responsibilities: Return true for any state the collector treats as reachable storage.
	 */
	static bool IsStateOccupied(EObjectSlotState InState) noexcept;

	/**
	 * Motivation: Reports whether a slot state is irreversibly pending destruction.
	 * Responsibilities: Return true only for the pending-destroy state.
	 */
	static bool IsStatePendingDestroy(EObjectSlotState InState) noexcept;

	/**
	 * Motivation: Prevents a collector cycle from starting inside construction or destruction callbacks.
	 * Responsibilities: Report true when mutation or dispatch is active.
	 */
	bool CollectorIsMutationLocked() const noexcept { return bMutationLocked || bDispatchLocked; }

	/**
	 * Motivation: Gives callback dispatch precedence over collector phase validation.
	 * Responsibilities: Report true throughout one Engine callback cascade.
	 */
	bool CollectorIsDispatchLocked() const noexcept { return bDispatchLocked; }

	/**
	 * Motivation: Atomically reserves public store mutation for one collector cycle.
	 * Responsibilities: Set the active collector when no other boundary is held and report success.
	 */
	bool CollectorTryBegin(const FGarbageCollector& InCollector) noexcept;

	/**
	 * Motivation: Releases store ownership only for the collector that acquired it.
	 * Responsibilities: Clear the active collector when it matches the caller.
	 */
	void CollectorEnd(const FGarbageCollector& InCollector) noexcept;

	/**
	 * Motivation: Confirms that this collector owns the active incremental cycle.
	 * Responsibilities: Return true only when the caller is the active collector.
	 */
	bool CollectorIsOwnedBy(const FGarbageCollector& InCollector) const noexcept { return ActiveCollector == &InCollector; }

	/**
	 * Motivation: Lets the collector read the mark associated with one occupied slot.
	 * Responsibilities: Return the slot's collector mark.
	 */
	bool CollectorIsMarked(ObjectIndex InSlotIndex) const noexcept;

	/**
	 * Motivation: Lets the collector change one occupied slot's mark without changing lifecycle state.
	 * Responsibilities: Set the slot's mark for the given index.
	 */
	void CollectorSetMarked(ObjectIndex InSlotIndex, bool bInMarked) noexcept;

	/**
	 * Motivation: Reclaims one generation-checked unmarked lifetime during bounded sweep.
	 * Responsibilities: Destroy the matching unmarked live slot or report why it cannot.
	 */
	EObjectResult CollectorReclaim(FObjectHandle InHandle) noexcept;

	/**
	 * Motivation: Excludes lifetime-changing work from one non-nested callback cascade.
	 * Responsibilities: Reserve the dispatch boundary when no other is held and report success.
	 */
	bool TryBeginDispatch() noexcept;

	/**
	 * Motivation: Releases the callback reservation acquired by one dispatch guard.
	 * Responsibilities: Clear the dispatch lock exactly once.
	 */
	void EndDispatch() noexcept;

	/**
	 * Motivation: Rejects guarded publication, destruction, root acquisition, and collection work.
	 * Responsibilities: Report the union of mutation, dispatch, and collection locks.
	 */
	bool IsPublicMutationLocked() const noexcept { return IsMutationLocked(); }

	/** Motivation: Retains non-owning fixed storage whose lifetime encloses this store. */
	FObjectStoreStorage Storage{};

	/** Motivation: Retains non-owning class lookup whose registry lifetime encloses this store. */
	FClassRegistryView ClassRegistryLookup{};

	/** Motivation: Prevents operations when caller-owned storage violates fixed-slot invariants. */
	EObjectResult StoreConfigurationResult{EObjectResult::UnsupportedObjectLayout};

	/** Motivation: Counts active object lifetimes without rescanning all slot metadata. */
	std::uint32_t OccupiedSlotCount{0};

	/** Motivation: Counts pending objects left for later bounded barrier work. */
	std::uint32_t PendingDestroyCount{0};

	/** Motivation: Counts slots permanently removed before generation wrap. */
	std::uint32_t RetiredSlotCount{0};

	/** Motivation: Counts independently owned root tokens without rescanning the root table. */
	std::uint32_t ActiveRootCount{0};

	/** Motivation: Sums descriptor payload extents to expose equal-slot fragmentation. */
	std::size_t ObjectPayloadByteCount{0};

	/** Motivation: Preserves bounded round-robin progress across partial destruction barriers. */
	ObjectIndex PendingDestroyScanCursor{0};

	// Locking trio, each guarding a distinct phase (see IsMutationLocked):
	//   bMutationLocked -- set while a construct / exact-destruction callback runs.
	//   bDispatchLocked -- set throughout one Engine callback cascade.
	//   ActiveCollector -- non-null while one collection cycle owns store traversal.
	/** Motivation: Rejects callback reentry while construction or exact destruction is in progress. */
	bool bMutationLocked{false};

	/** Motivation: Rejects structural store mutation throughout one Engine callback cascade. */
	bool bDispatchLocked{false};

	/** Motivation: Identifies the collector that exclusively owns incremental store traversal. */
	const FGarbageCollector* ActiveCollector{nullptr};
};

/**
 * Motivation: Lets a system that must not name Engine — Messaging above all — hold a reference that stops working when
 *   an object dies, in the same way UE5 lets a delegate hold a weak object reference.
 * Responsibilities: Build a token watching InHandle's slot generation, or a token that is already dead when InHandle
 *   names no live object, so a subscription captured too late can never fire.
 * Example:
 *   MessagingSystem->SubscribeToChannel("Telemetry", std::move(Subscriber), MakeWeakOwner(Store, GetObjectHandle()));
 */
inline Core::FWeakOwner MakeWeakOwner(const FObjectStore& InStore, const FObjectHandle InHandle) noexcept
{
	return Core::FWeakOwner{InStore.GetSlotGenerationAddress(InHandle), InHandle.Generation, true};
}

} // namespace MicroWorld::Engine
