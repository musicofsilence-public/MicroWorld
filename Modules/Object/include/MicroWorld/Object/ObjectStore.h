#pragma once

#include <MicroWorld/Object/ObjectPtr.h>

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace MicroWorld
{

class FGarbageCollector;
class FObjectStore;

/**
 * Prevents object publication, destruction, root acquisition, and collection
 * while one bounded callback cascade runs.
 *
 * Acquisition fails without blocking when construction, destruction, collection,
 * or another dispatch already owns the mutation boundary. Releasing an existing
 * root token remains allowed so noexcept RAII cleanup cannot leak reachability;
 * release alone cannot reclaim or reuse an object.
 */
class FObjectStoreDispatchGuard final
{
public:
	/** Tries to exclude lifetime-changing work from one callback cascade. */
	explicit FObjectStoreDispatchGuard(FObjectStore& Store) noexcept;

	/** Releases callback exclusion only when this instance acquired it. */
	~FObjectStoreDispatchGuard() noexcept;

	/** Prevents two guards from releasing one dispatch reservation. */
	FObjectStoreDispatchGuard(const FObjectStoreDispatchGuard&) = delete;

	/** Prevents replacing this guard's unique dispatch reservation. */
	FObjectStoreDispatchGuard& operator=(const FObjectStoreDispatchGuard&) = delete;

	/** Reports whether callback dispatch may proceed under this guard. */
	bool IsAcquired() const noexcept { return ObjectStore != nullptr; }

private:
	/** Identifies the store reservation released at scope exit, or null after rejection. */
	FObjectStore* ObjectStore{nullptr};
};

/** Identifies the store-owned lifecycle phase of one caller-supplied slot. */
enum class EObjectSlotState : std::uint8_t
{
	/** Allows the slot to publish another generation when one remains. */
	Vacant,

	/** Hides storage while a nothrow placement constructor is running. */
	Constructing,

	/** Makes the constructed object resolvable and eligible for tracing. */
	Live,

	/** Hides the object immediately until the destruction barrier reclaims it. */
	PendingDestroy,

	/** Prevents lifecycle callbacks from recursively destroying the same slot. */
	Destroying,

	/** Permanently prevents reuse after the generation space is exhausted. */
	Retired,
};

/** Holds lifecycle metadata in storage supplied and owned by the application. */
struct FObjectSlotMetadata
{
	/** Retains the most recently published generation without ever wrapping it. */
	ObjectGeneration Generation{0};

	/** Selects tracing, ancestry, layout, and exact destruction for the active object. */
	const FClassDescriptor* Descriptor{nullptr};

	/** Points at the active UObject base within the non-moving slot. */
	UObject* Object{nullptr};

	/** Prevents unpublished, pending, vacant, and retired storage from resolving. */
	EObjectSlotState State{EObjectSlotState::Vacant};

	/** Stores one collector mark without allocating a side table. */
	bool bMarked{false};
};

/** Holds one independently owned explicit-root token in caller-supplied storage. */
struct FObjectRootEntry
{
	/** Identifies the rooted lifetime or remains invalid while this entry is free. */
	FObjectHandle Handle{};
};

/** Describes all non-owning fixed storage required by one object store. */
struct FObjectStoreStorage
{
	/** Provides the first byte of equal-size, non-moving object slots. */
	std::byte* SlotBytes{nullptr};

	/** Provides enough bytes for SlotCount consecutive SlotSizeBytes ranges. */
	std::size_t SlotStorageSizeBytes{0};

	/** Provides one lifecycle record for every equal-size object slot. */
	FObjectSlotMetadata* Slots{nullptr};

	/** Fixes the number of object lifetimes that may be active concurrently. */
	std::uint32_t SlotCount{0};

	/** Fixes the maximum object extent and internal-fragmentation unit. */
	std::size_t SlotSizeBytes{0};

	/** Fixes the maximum supported object alignment for every slot. */
	std::size_t SlotAlignmentBytes{0};

	/** Provides independently reusable entries for explicit strong-root tokens. */
	FObjectRootEntry* Roots{nullptr};

	/** Fixes the number of simultaneous independently owned root tokens. */
	std::uint32_t RootCapacity{0};
};

/**
 * Provides a non-owning lookup over an explicitly registered class set.
 *
 * The type-erased view keeps FObjectStore independent of registry capacity
 * while retaining descriptor identity validation without RTTI.
 */
class FClassRegistryView final
{
public:
	/** Defines the only operation needed from an application-owned registry. */
	using FFindClass = const FClassDescriptor* (*)(const void*, FTypeId) noexcept;

	/** Creates an empty view that rejects every class as unknown. */
	FClassRegistryView() noexcept = default;

	/** Binds a stable registry context and its allocation-free lookup operation. */
	FClassRegistryView(const void* InContext, FFindClass InFindClass) noexcept : Context(InContext), FindClass(InFindClass) {}

	/** Finds one descriptor by local type identifier without changing registry state. */
	const FClassDescriptor* Find(const FTypeId TypeId) const noexcept
	{
		return Context != nullptr && FindClass != nullptr ? FindClass(Context, TypeId) : nullptr;
	}

private:
	/** Identifies the application-owned registry retained for the store lifetime. */
	const void* Context{nullptr};

	/** Performs bounded registry lookup without virtual allocation or RTTI. */
	FFindClass FindClass{nullptr};
};

/** Creates a type-erased non-owning view over one fixed-capacity class registry. */
template<std::size_t MaxClasses>
FClassRegistryView MakeClassRegistryView(const TClassRegistry<MaxClasses>& Registry) noexcept
{
	return FClassRegistryView(
		&Registry,
		[](const void* Context, const FTypeId TypeId) noexcept -> const FClassDescriptor*
		{ return static_cast<const TClassRegistry<MaxClasses>*>(Context)->Find(TypeId); });
}

/** Invokes the exact public nothrow destructor bound to one managed C++ type. */
template<typename T>
void DestroyManagedObject(UObject& Object) noexcept
{
	static_assert(std::is_base_of<UObject, T>::value, "Managed destruction requires a UObject-derived type.");
	static_assert(std::is_nothrow_destructible<T>::value, "Managed destruction requires an accessible noexcept destructor.");
	static_cast<T&>(Object).~T();
}

/** Creates a descriptor whose layout and exact destructor are bound to T without RTTI. */
template<typename T>
FClassDescriptor MakeClassDescriptor(
	const FTypeId TypeId,
	const char* const DiagnosticName,
	const FClassDescriptor* const Parent = nullptr,
	const FTraceObjectReferences TraceReferences = nullptr) noexcept
{
	static_assert(std::is_base_of<UObject, T>::value, "Managed descriptors require a UObject-derived type.");
	static_assert(std::is_nothrow_destructible<T>::value, "Managed descriptors require an accessible noexcept destructor.");

	return FClassDescriptor{
		TypeId,
		DiagnosticName,
		Parent,
		sizeof(T),
		alignof(T),
		TraceReferences,
		&DestroyManagedObject<T>,
		ManagedObjectTypeToken<T>(),
	};
}

/** Reports placement construction success or one explicit bounded-store failure. */
template<typename T>
struct TObjectCreationResult
{
	/** Distinguishes capacity, layout, class, generation, and successful outcomes. */
	EObjectResult Result{EObjectResult::CapacityExceeded};

	/** Resolves the newly published object only when Result is Success. */
	TObjectPtr<T> Object{};
};

/** Reports bounded pending-destruction work performed at one mutation barrier. */
struct FObjectMutationResult
{
	/** Reports invalid store configuration or successful bounded traversal. */
	EObjectResult Result{EObjectResult::Success};

	/** Counts slots inspected so every call's work remains observable and bounded. */
	std::uint32_t SlotsVisited{0};

	/** Counts objects whose BeginDestroy and exact destructor ran in this call. */
	std::uint32_t ObjectsDestroyed{0};

	/** Reports pending objects left for a later explicit mutation barrier. */
	std::uint32_t PendingObjectsRemaining{0};
};

/** Exposes fixed capacity, occupancy, roots, retirement, and fragmentation. */
struct FObjectStoreStats
{
	/** Reports the caller-selected number of equal-size object slots. */
	std::uint32_t SlotCapacity{0};

	/** Counts constructed live and pending objects that still occupy slots. */
	std::uint32_t OccupiedSlots{0};

	/** Counts occupied objects hidden until the destruction barrier reclaims them. */
	std::uint32_t PendingDestroySlots{0};

	/** Counts slots permanently unavailable because their generation was exhausted. */
	std::uint32_t RetiredSlots{0};

	/** Reports the caller-selected independent explicit-root capacity. */
	std::uint32_t RootCapacity{0};

	/** Counts independently owned root tokens, including duplicate handles. */
	std::uint32_t ActiveRoots{0};

	/** Reports the fixed byte extent reserved for every object slot. */
	std::size_t SlotSizeBytes{0};

	/** Sums exact descriptor extents for all occupied objects. */
	std::size_t ObjectPayloadBytes{0};

	/** Reports equal-slot bytes unavailable to object payloads while occupied. */
	std::size_t InternalFragmentationBytes{0};
};

/**
 * Owns managed-object lifetimes over fixed, non-moving, caller-owned storage.
 *
 * FObjectStore never allocates bookkeeping, invokes collection after failure,
 * reads a clock, or moves a published object.
 */
class FObjectStore final
{
public:
	/** Validates and initializes caller-owned slot metadata and root entries. */
	FObjectStore(FObjectStoreStorage InStorage, FClassRegistryView InClasses) noexcept;

	/** Destroys every remaining object before caller-owned storage may disappear. */
	~FObjectStore() noexcept;

	/** Preserves the unique identity of caller-owned storage and all published handles. */
	FObjectStore(const FObjectStore&) = delete;

	/** Prevents two stores from mutating the same metadata and object lifetimes. */
	FObjectStore& operator=(const FObjectStore&) = delete;

	/** Preserves the address used by every managed object and pointer bridge. */
	FObjectStore(FObjectStore&&) = delete;

	/** Prevents moving store identity behind already published TObjectPtr values. */
	FObjectStore& operator=(FObjectStore&&) = delete;

	/**
	 * Constructs one UObject-derived value using an explicit registered descriptor.
	 *
	 * T must be nothrow constructible because portable MicroWorld builds disable
	 * exception unwinding. The descriptor and slot layout are validated before
	 * a generation changes or placement construction begins.
	 */
	template<typename T, typename... TArguments>
	TObjectCreationResult<T> NewObject(const FClassDescriptor& Descriptor, TArguments&&... Arguments) noexcept
	{
		static_assert(std::is_base_of<UObject, T>::value, "FObjectStore can construct only UObject-derived values.");
		static_assert(std::is_nothrow_constructible<T, TArguments...>::value, "Managed-object construction must be noexcept.");

		TObjectCreationResult<T> Creation{};
		if (IsPublicMutationLocked())
		{
			Creation.Result = EObjectResult::LifecycleLocked;
			return Creation;
		}

		Creation.Result = ValidateConstruction<T>(Descriptor);
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

		FObjectSlotMetadata& Slot = Storage.Slots[SlotIndex];
		const ObjectGeneration NextGeneration = Slot.Generation == 0 ? 1 : Slot.Generation + 1;
		Slot.State = EObjectSlotState::Constructing;
		bMutationLocked = true;

		void* const PlacementAddress = SlotAddress(SlotIndex);
		T* const ConstructedObject = ::new (PlacementAddress) T(std::forward<TArguments>(Arguments)...);
		UObject* const ManagedObject = static_cast<UObject*>(ConstructedObject);
		const FObjectHandle Handle{SlotIndex, NextGeneration};

		ManagedObject->Store = this;
		ManagedObject->Handle = Handle;
		ManagedObject->Descriptor = &Descriptor;
		ManagedObject->bPendingDestroy = false;

		Slot.Generation = NextGeneration;
		Slot.Descriptor = &Descriptor;
		Slot.Object = ManagedObject;
		Slot.bMarked = false;
		Slot.State = EObjectSlotState::Live;

		++OccupiedSlotCount;
		ObjectPayloadByteCount += Descriptor.SizeBytes;
		bMutationLocked = false;
		Creation.Result = EObjectResult::Success;
		Creation.Object = TObjectPtr<T>(*this, Handle);
		return Creation;
	}

	/**
	 * Constructs through a type's optional static descriptor convention.
	 *
	 * Explicit descriptor composition remains the primitive API; this overload
	 * participates only when T exposes StaticClassDescriptor().
	 */
	template<typename T, typename... TArguments>
	auto NewObject(TArguments&&... Arguments) noexcept -> decltype(T::StaticClassDescriptor(), TObjectCreationResult<T>{})
	{
		return NewObject<T>(T::StaticClassDescriptor(), std::forward<TArguments>(Arguments)...);
	}

	/** Resolves only a live matching generation and never changes store state. */
	UObject* Resolve(FObjectHandle Handle) const noexcept;

	/** Hides one live object immediately and queues it for the explicit barrier. */
	EObjectResult MarkPendingDestroy(FObjectHandle Handle) noexcept;

	/** Inspects at most MaxObjects slots and destroys pending objects encountered. */
	FObjectMutationResult ApplyPendingDestroy(std::uint32_t MaxObjects) noexcept;

	/** Registers one independent root token after live-handle and capacity validation. */
	EObjectResult AddRoot(FObjectHandle Handle) noexcept;

	/**
	 * Releases one matching root token even while guarded work is active.
	 *
	 * Immediate release keeps noexcept strong-pointer cleanup leak-free. It changes
	 * only future reachability; destruction, slot reuse, and collection remain
	 * blocked until the active callback or mutation boundary ends.
	 */
	EObjectResult RemoveRoot(FObjectHandle Handle) noexcept;

	/** Registers one independent root token and transfers it into an RAII owner. */
	template<typename T>
	TStrongObjectPointerResult<T> MakeStrongObjectPtr(const TObjectPtr<T> Object) noexcept
	{
		TStrongObjectPointerResult<T> StrongResult{};
		if (Object.Store != this)
		{
			StrongResult.Result = EObjectResult::StaleHandle;
			return StrongResult;
		}

		StrongResult.Result = AddRoot(Object.Object);
		if (StrongResult.Result == EObjectResult::Success)
		{
			StrongResult.Pointer = TStrongObjectPtr<T>(*this, Object.Object);
		}
		return StrongResult;
	}

	/** Reports constructor validation so malformed storage never fails implicitly. */
	EObjectResult ConfigurationResult() const noexcept { return StoreConfigurationResult; }

	/** Reports fixed capacity, current occupancy, roots, and slot fragmentation. */
	FObjectStoreStats Stats() const noexcept;

	/** Reports whether publication, destruction, root acquisition, or collection is currently blocked. */
	bool IsMutationLocked() const noexcept { return bMutationLocked || bDispatchLocked || ActiveCollector != nullptr; }

private:
	friend class FGarbageCollector;
	friend class FObjectStoreDispatchGuard;

	/** Validates descriptor identity and exact T layout before any slot mutation. */
	template<typename T>
	EObjectResult ValidateConstruction(const FClassDescriptor& Descriptor) const noexcept
	{
		if (StoreConfigurationResult != EObjectResult::Success)
		{
			return StoreConfigurationResult;
		}
		if (Classes.Find(Descriptor.TypeId) != &Descriptor)
		{
			return EObjectResult::UnknownClass;
		}
		if (Descriptor.SizeBytes != sizeof(T) || Descriptor.AlignmentBytes != alignof(T) || Descriptor.TypeToken != ManagedObjectTypeToken<T>()
			|| Descriptor.Destroy != &DestroyManagedObject<T>)
		{
			return EObjectResult::UnsupportedObjectLayout;
		}
		if (sizeof(T) > Storage.SlotSizeBytes || alignof(T) > Storage.SlotAlignmentBytes)
		{
			return EObjectResult::UnsupportedObjectLayout;
		}
		return EObjectResult::Success;
	}

	/** Returns the first reusable slot without changing its generation or state. */
	ObjectIndex FindVacantSlot() const noexcept;

	/** Returns the stable first byte of one validated equal-size slot. */
	void* SlotAddress(ObjectIndex SlotIndex) const noexcept;

	/** Validates index, generation, and occupancy while optionally accepting pending state. */
	FObjectSlotMetadata* FindMatchingSlot(FObjectHandle Handle, bool bAllowPending) noexcept;

	/** Provides const matching-slot validation to query-only operations. */
	const FObjectSlotMetadata* FindMatchingSlot(FObjectHandle Handle, bool bAllowPending) const noexcept;

	/** Runs BeginDestroy and exact destruction once, then vacates or retires the slot. */
	EObjectResult DestroySlot(ObjectIndex SlotIndex) noexcept;

	/** Removes every independent root token for a lifetime that can no longer resolve. */
	void RemoveAllRoots(FObjectHandle Handle) noexcept;

	/** Reports collector iteration capacity without exposing mutable metadata publicly. */
	std::uint32_t CollectorSlotCapacity() const noexcept { return Storage.SlotCount; }

	/** Reports collector root iteration capacity including currently empty entries. */
	std::uint32_t CollectorRootCapacity() const noexcept { return Storage.RootCapacity; }

	/** Returns one root token or an invalid handle for a free/out-of-range entry. */
	FObjectHandle CollectorRootAt(std::uint32_t RootIndex) const noexcept;

	/** Returns the current live handle for one slot or an invalid handle otherwise. */
	FObjectHandle CollectorHandleAt(ObjectIndex SlotIndex) const noexcept;

	/** Resolves one live slot by index for descriptor-driven tracing. */
	UObject* CollectorObjectAt(ObjectIndex SlotIndex) const noexcept;

	/** Reports whether a slot contains either a live or pending object. */
	bool CollectorIsOccupied(ObjectIndex SlotIndex) const noexcept;

	/** Reports whether a slot is irreversibly pending destruction. */
	bool CollectorIsPendingDestroy(ObjectIndex SlotIndex) const noexcept;

	/** Prevents a collector cycle from starting inside construction or destruction callbacks. */
	bool CollectorIsMutationLocked() const noexcept { return bMutationLocked || bDispatchLocked; }

	/** Gives callback dispatch precedence over collector phase validation. */
	bool CollectorIsDispatchLocked() const noexcept { return bDispatchLocked; }

	/** Atomically reserves public store mutation for one collector cycle. */
	bool CollectorTryBegin(const FGarbageCollector& Collector) noexcept;

	/** Releases store ownership only for the collector that acquired it. */
	void CollectorEnd(const FGarbageCollector& Collector) noexcept;

	/** Confirms that this collector owns the active incremental cycle. */
	bool CollectorIsOwnedBy(const FGarbageCollector& Collector) const noexcept { return ActiveCollector == &Collector; }

	/** Reports the collector mark associated with one occupied slot. */
	bool CollectorIsMarked(ObjectIndex SlotIndex) const noexcept;

	/** Changes one occupied slot's collector mark without changing lifecycle state. */
	void CollectorSetMarked(ObjectIndex SlotIndex, bool bMarked) noexcept;

	/** Reclaims one generation-checked unmarked lifetime during bounded sweep. */
	EObjectResult CollectorReclaim(FObjectHandle Handle) noexcept;

	/** Excludes lifetime-changing work from one non-nested callback cascade. */
	bool TryBeginDispatch() noexcept;

	/** Releases the callback reservation acquired by one dispatch guard. */
	void EndDispatch() noexcept;

	/** Rejects guarded publication, destruction, root acquisition, and collection work. */
	bool IsPublicMutationLocked() const noexcept { return IsMutationLocked(); }

	/** Retains non-owning fixed storage whose lifetime encloses this store. */
	FObjectStoreStorage Storage{};

	/** Retains non-owning class lookup whose registry lifetime encloses this store. */
	FClassRegistryView Classes{};

	/** Prevents operations when caller-owned storage violates fixed-slot invariants. */
	EObjectResult StoreConfigurationResult{EObjectResult::UnsupportedObjectLayout};

	/** Counts active object lifetimes without rescanning all slot metadata. */
	std::uint32_t OccupiedSlotCount{0};

	/** Counts pending objects left for later bounded barrier work. */
	std::uint32_t PendingDestroyCount{0};

	/** Counts slots permanently removed before generation wrap. */
	std::uint32_t RetiredSlotCount{0};

	/** Counts independently owned root tokens without rescanning the root table. */
	std::uint32_t ActiveRootCount{0};

	/** Sums descriptor payload extents to expose equal-slot fragmentation. */
	std::size_t ObjectPayloadByteCount{0};

	/** Preserves bounded round-robin progress across partial destruction barriers. */
	ObjectIndex PendingDestroyScanCursor{0};

	/** Rejects callback reentry while construction or exact destruction is in progress. */
	bool bMutationLocked{false};

	/** Rejects structural store mutation throughout one Engine callback cascade. */
	bool bDispatchLocked{false};

	/** Identifies the collector that exclusively owns incremental store traversal. */
	const FGarbageCollector* ActiveCollector{nullptr};
};

} // namespace MicroWorld
