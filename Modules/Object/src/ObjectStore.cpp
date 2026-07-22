#include <MicroWorld/Object/ObjectStore.h>

#include <limits>

namespace MicroWorld
{

namespace
{

	/** Confirms that a fixed alignment is non-zero and power-of-two. */
	bool IsValidAlignment(const std::size_t AlignmentBytes) noexcept
	{
		return AlignmentBytes > 0 && (AlignmentBytes & (AlignmentBytes - 1U)) == 0;
	}

	/** Confirms multiplication cannot wrap the storage extent calculation. */
	bool MultiplicationFitsSizeType(const std::size_t Left, const std::size_t Right) noexcept
	{
		return Right == 0 || Left <= std::numeric_limits<std::size_t>::max() / Right;
	}

} // namespace

FObjectStoreDispatchGuard::FObjectStoreDispatchGuard(FObjectStore& Store) noexcept
{
	if (Store.TryBeginDispatch())
	{
		ObjectStore = &Store;
	}
}

FObjectStoreDispatchGuard::~FObjectStoreDispatchGuard() noexcept
{
	if (ObjectStore != nullptr)
	{
		ObjectStore->EndDispatch();
	}
}

FObjectStore::FObjectStore(const FObjectStoreStorage InStorage, const FClassRegistryView InClasses) noexcept
	: Storage(InStorage), ClassRegistryLookup(InClasses)
{
	if (!IsStorageDescriptorValid(Storage))
	{
		return;
	}

	for (std::uint32_t SlotIndex = 0; SlotIndex < Storage.SlotCount; ++SlotIndex)
	{
		Storage.SlotMetadata[SlotIndex] = {};
	}
	for (std::uint32_t RootIndex = 0; RootIndex < Storage.RootCapacity; ++RootIndex)
	{
		Storage.Roots[RootIndex] = {};
	}

	StoreConfigurationResult = EObjectResult::Success;
}

bool FObjectStore::IsStorageDescriptorValid(const FObjectStoreStorage& Storage) noexcept
{
	const bool bHasSlotPointers = Storage.SlotCount > 0 && Storage.SlotPayloadBytes != nullptr && Storage.SlotMetadata != nullptr;
	const bool bHasSlotLayout =
		Storage.SlotSizeBytes > 0 && IsValidAlignment(Storage.SlotAlignmentBytes) && (Storage.SlotSizeBytes % Storage.SlotAlignmentBytes) == 0;
	const bool bStorageExtentFits = MultiplicationFitsSizeType(Storage.SlotSizeBytes, Storage.SlotCount)
		&& Storage.TotalSlotStorageBytes >= Storage.SlotSizeBytes * Storage.SlotCount;
	const bool bStorageAddressAligned =
		Storage.SlotPayloadBytes != nullptr && (reinterpret_cast<std::uintptr_t>(Storage.SlotPayloadBytes) & (Storage.SlotAlignmentBytes - 1U)) == 0;
	const bool bHasRootStorage = Storage.RootCapacity == 0 || Storage.Roots != nullptr;
	return bHasSlotPointers && bHasSlotLayout && bStorageExtentFits && bStorageAddressAligned && bHasRootStorage;
}

FObjectStore::~FObjectStore() noexcept
{
	if (StoreConfigurationResult != EObjectResult::Success)
	{
		return;
	}

	bMutationLocked = true;
	for (ObjectIndex SlotIndex = 0; SlotIndex < Storage.SlotCount; ++SlotIndex)
	{
		if (CollectorIsOccupied(SlotIndex))
		{
			(void)DestroySlot(SlotIndex);
		}
	}
}

UObject* FObjectStore::Resolve(const FObjectHandle Handle) const noexcept
{
	const FObjectSlotMetadata* const Slot = FindMatchingSlot(Handle, false);
	return Slot != nullptr ? Slot->Object : nullptr;
}

EObjectResult FObjectStore::MarkPendingDestroy(const FObjectHandle Handle) noexcept
{
	if (IsPublicMutationLocked())
	{
		return EObjectResult::LifecycleLocked;
	}

	FObjectSlotMetadata* const AnyMatchingSlot = FindMatchingSlot(Handle, true);
	if (AnyMatchingSlot == nullptr)
	{
		return EObjectResult::StaleHandle;
	}
	if (AnyMatchingSlot->State == EObjectSlotState::PendingDestroy)
	{
		return EObjectResult::AlreadyPendingDestroy;
	}
	if (AnyMatchingSlot->State != EObjectSlotState::Live || AnyMatchingSlot->Object == nullptr)
	{
		return EObjectResult::StaleHandle;
	}

	AnyMatchingSlot->State = EObjectSlotState::PendingDestroy;
	AnyMatchingSlot->Object->bPendingDestroy = true;
	AnyMatchingSlot->bMarked = false;
	++PendingDestroyCount;
	return EObjectResult::Success;
}

FObjectMutationResult FObjectStore::ApplyPendingDestroy(const std::uint32_t MaxSlotsToInspect) noexcept
{
	FObjectMutationResult Mutation{};
	Mutation.Result = StoreConfigurationResult;
	Mutation.PendingObjectsRemaining = PendingDestroyCount;
	if (IsPublicMutationLocked())
	{
		Mutation.Result = EObjectResult::LifecycleLocked;
		return Mutation;
	}
	if (StoreConfigurationResult != EObjectResult::Success || MaxSlotsToInspect == 0 || PendingDestroyCount == 0)
	{
		return Mutation;
	}

	const std::uint32_t VisitLimit = MaxSlotsToInspect < Storage.SlotCount ? MaxSlotsToInspect : Storage.SlotCount;
	while (Mutation.SlotsVisited < VisitLimit && PendingDestroyCount > 0)
	{
		const ObjectIndex SlotIndex = AdvancePendingScanCursor();
		++Mutation.SlotsVisited;

		if (Storage.SlotMetadata[SlotIndex].State == EObjectSlotState::PendingDestroy)
		{
			(void)DestroySlot(SlotIndex);
			++Mutation.ObjectsDestroyed;
		}
	}

	Mutation.PendingObjectsRemaining = PendingDestroyCount;
	return Mutation;
}

ObjectIndex FObjectStore::AdvancePendingScanCursor() noexcept
{
	const ObjectIndex SlotIndex = PendingDestroyScanCursor;
	PendingDestroyScanCursor = PendingDestroyScanCursor + 1U == Storage.SlotCount ? 0 : PendingDestroyScanCursor + 1U;
	return SlotIndex;
}

EObjectResult FObjectStore::AddRoot(const FObjectHandle Handle) noexcept
{
	if (IsPublicMutationLocked())
	{
		return EObjectResult::LifecycleLocked;
	}

	FObjectSlotMetadata* const MatchingSlot = FindMatchingSlot(Handle, true);
	if (MatchingSlot == nullptr)
	{
		return EObjectResult::StaleHandle;
	}
	if (MatchingSlot->State == EObjectSlotState::PendingDestroy)
	{
		return EObjectResult::AlreadyPendingDestroy;
	}
	if (MatchingSlot->State != EObjectSlotState::Live)
	{
		return EObjectResult::StaleHandle;
	}

	for (std::uint32_t RootIndex = 0; RootIndex < Storage.RootCapacity; ++RootIndex)
	{
		if (!Storage.Roots[RootIndex].Handle.IsValid())
		{
			Storage.Roots[RootIndex].Handle = Handle;
			++ActiveRootCount;
			return EObjectResult::Success;
		}
	}
	return EObjectResult::RootCapacityExceeded;
}

EObjectResult FObjectStore::RemoveRoot(const FObjectHandle Handle) noexcept
{
	if (!Handle.IsValid() || StoreConfigurationResult != EObjectResult::Success)
	{
		return EObjectResult::StaleHandle;
	}

	// Root release stays available to noexcept RAII cleanup. It can only affect a
	// later collection; guarded destruction and slot reuse remain impossible.
	for (std::uint32_t RootIndex = 0; RootIndex < Storage.RootCapacity; ++RootIndex)
	{
		if (Storage.Roots[RootIndex].Handle == Handle)
		{
			Storage.Roots[RootIndex].Handle = {};
			if (ActiveRootCount > 0)
			{
				--ActiveRootCount;
			}
			return EObjectResult::Success;
		}
	}
	return EObjectResult::StaleHandle;
}

FObjectStoreStats FObjectStore::Stats() const noexcept
{
	FObjectStoreStats StoreStats{};
	StoreStats.SlotCapacity = Storage.SlotCount;
	StoreStats.OccupiedSlots = OccupiedSlotCount;
	StoreStats.PendingDestroySlots = PendingDestroyCount;
	StoreStats.RetiredSlots = RetiredSlotCount;
	StoreStats.RootCapacity = Storage.RootCapacity;
	StoreStats.ActiveRoots = ActiveRootCount;
	StoreStats.SlotSizeBytes = Storage.SlotSizeBytes;
	StoreStats.ObjectPayloadBytes = ObjectPayloadByteCount;
	StoreStats.InternalFragmentationBytes = OccupiedSlotCount * Storage.SlotSizeBytes - ObjectPayloadByteCount;
	return StoreStats;
}

ObjectIndex FObjectStore::FindVacantSlot() const noexcept
{
	for (ObjectIndex SlotIndex = 0; SlotIndex < Storage.SlotCount; ++SlotIndex)
	{
		if (Storage.SlotMetadata[SlotIndex].State == EObjectSlotState::Vacant
			&& CanAdvanceObjectGeneration(Storage.SlotMetadata[SlotIndex].Generation))
		{
			return SlotIndex;
		}
	}
	return FObjectHandle::InvalidIndex;
}

void* FObjectStore::SlotAddress(const ObjectIndex SlotIndex) const noexcept
{
	return static_cast<void*>(Storage.SlotPayloadBytes + static_cast<std::size_t>(SlotIndex) * Storage.SlotSizeBytes);
}

FObjectSlotMetadata* FObjectStore::FindMatchingSlot(const FObjectHandle Handle, const bool bAllowPending) noexcept
{
	return const_cast<FObjectSlotMetadata*>(static_cast<const FObjectStore&>(*this).FindMatchingSlot(Handle, bAllowPending));
}

const FObjectSlotMetadata* FObjectStore::FindMatchingSlot(const FObjectHandle Handle, const bool bAllowPending) const noexcept
{
	if (StoreConfigurationResult != EObjectResult::Success || !Handle.IsValid() || Handle.Index >= Storage.SlotCount)
	{
		return nullptr;
	}

	const FObjectSlotMetadata& Slot = Storage.SlotMetadata[Handle.Index];
	const bool bStateAccepted = Slot.State == EObjectSlotState::Live || (bAllowPending && Slot.State == EObjectSlotState::PendingDestroy);
	if (!bStateAccepted || Slot.Generation != Handle.Generation || Slot.Object == nullptr)
	{
		return nullptr;
	}
	return &Slot;
}

EObjectResult FObjectStore::DestroySlot(const ObjectIndex SlotIndex) noexcept
{
	const EObjectResult ValidationResult = ValidateDestroyableSlot(SlotIndex);
	if (ValidationResult != EObjectResult::Success)
	{
		return ValidationResult;
	}

	FObjectSlotMetadata& Slot = Storage.SlotMetadata[SlotIndex];
	const FObjectHandle Handle{SlotIndex, Slot.Generation};
	const FClassDescriptor* const Descriptor = Slot.Descriptor;
	const bool bWasPending = Slot.State == EObjectSlotState::PendingDestroy;
	// Save and restore (not clear): this destroy can nest inside another store
	// mutation -- a destructor triggering a destroy, or destruction during
	// construction -- so clearing the lock at the end would release the outer
	// scope early.
	const bool bWasMutationLocked = bMutationLocked;

	bMutationLocked = true;
	RunDestructionCallbacks(Slot, Handle);
	RecycleOrRetireSlot(Slot);
	UpdateOccupancyCounters(bWasPending, Descriptor->SizeBytes);
	bMutationLocked = bWasMutationLocked;
	return EObjectResult::Success;
}

EObjectResult FObjectStore::ValidateDestroyableSlot(const ObjectIndex SlotIndex) const noexcept
{
	if (SlotIndex >= Storage.SlotCount)
	{
		return EObjectResult::StaleHandle;
	}
	const FObjectSlotMetadata& Slot = Storage.SlotMetadata[SlotIndex];
	if ((Slot.State != EObjectSlotState::Live && Slot.State != EObjectSlotState::PendingDestroy) || Slot.Object == nullptr
		|| Slot.Descriptor == nullptr || Slot.Descriptor->Destroy == nullptr)
	{
		return EObjectResult::StaleHandle;
	}
	return EObjectResult::Success;
}

void FObjectStore::RunDestructionCallbacks(FObjectSlotMetadata& Slot, const FObjectHandle Handle) noexcept
{
	UObject* const Object = Slot.Object;
	const FClassDescriptor* const Descriptor = Slot.Descriptor;
	Slot.State = EObjectSlotState::Destroying;
	Object->bPendingDestroy = true;
	Object->BeginDestroy();
	Descriptor->Destroy(*Object);
	RemoveAllRoots(Handle);
}

void FObjectStore::RecycleOrRetireSlot(FObjectSlotMetadata& Slot) noexcept
{
	Slot.Descriptor = nullptr;
	Slot.Object = nullptr;
	Slot.bMarked = false;
	if (CanAdvanceObjectGeneration(Slot.Generation))
	{
		Slot.State = EObjectSlotState::Vacant;
	}
	else
	{
		Slot.State = EObjectSlotState::Retired;
		++RetiredSlotCount;
	}
}

void FObjectStore::UpdateOccupancyCounters(const bool bWasPending, const std::size_t PayloadBytes) noexcept
{
	if (OccupiedSlotCount > 0)
	{
		--OccupiedSlotCount;
	}
	if (bWasPending && PendingDestroyCount > 0)
	{
		--PendingDestroyCount;
	}
	if (ObjectPayloadByteCount >= PayloadBytes)
	{
		ObjectPayloadByteCount -= PayloadBytes;
	}
}

ObjectGeneration FObjectStore::NextPublishGeneration(const ObjectGeneration CurrentGeneration) noexcept
{
	// Generation 0 means "never published" (ObjectHandle.h), so a slot's first
	// publish jumps to 1 and later reuse increments -- keeping every live handle's
	// generation nonzero. FindVacantSlot never returns a slot that cannot advance.
	return CurrentGeneration == 0 ? 1 : CurrentGeneration + 1;
}

FObjectHandle FObjectStore::PublishObjectIntoSlot(const ObjectIndex SlotIndex, const FClassDescriptor& Descriptor, UObject& ManagedObject) noexcept
{
	FObjectSlotMetadata& Slot = Storage.SlotMetadata[SlotIndex];
	const ObjectGeneration NextGeneration = NextPublishGeneration(Slot.Generation);
	const FObjectHandle Handle{SlotIndex, NextGeneration};

	ManagedObject.Store = this;
	ManagedObject.Handle = Handle;
	ManagedObject.Descriptor = &Descriptor;
	ManagedObject.bPendingDestroy = false;

	Slot.Generation = NextGeneration;
	Slot.Descriptor = &Descriptor;
	Slot.Object = &ManagedObject;
	Slot.bMarked = false;
	Slot.State = EObjectSlotState::Live;

	++OccupiedSlotCount;
	ObjectPayloadByteCount += Descriptor.SizeBytes;
	return Handle;
}

void FObjectStore::RemoveAllRoots(const FObjectHandle Handle) noexcept
{
	for (std::uint32_t RootIndex = 0; RootIndex < Storage.RootCapacity; ++RootIndex)
	{
		if (Storage.Roots[RootIndex].Handle == Handle)
		{
			Storage.Roots[RootIndex].Handle = {};
			if (ActiveRootCount > 0)
			{
				--ActiveRootCount;
			}
		}
	}
}

FObjectHandle FObjectStore::CollectorRootAt(const std::uint32_t RootIndex) const noexcept
{
	return RootIndex < Storage.RootCapacity ? Storage.Roots[RootIndex].Handle : FObjectHandle{};
}

FObjectHandle FObjectStore::CollectorHandleAt(const ObjectIndex SlotIndex) const noexcept
{
	if (SlotIndex >= Storage.SlotCount || Storage.SlotMetadata[SlotIndex].State != EObjectSlotState::Live)
	{
		return {};
	}
	return FObjectHandle{SlotIndex, Storage.SlotMetadata[SlotIndex].Generation};
}

UObject* FObjectStore::CollectorObjectAt(const ObjectIndex SlotIndex) const noexcept
{
	return SlotIndex < Storage.SlotCount && Storage.SlotMetadata[SlotIndex].State == EObjectSlotState::Live ? Storage.SlotMetadata[SlotIndex].Object
																											: nullptr;
}

bool FObjectStore::CollectorIsOccupied(const ObjectIndex SlotIndex) const noexcept
{
	if (SlotIndex >= Storage.SlotCount)
	{
		return false;
	}
	const EObjectSlotState State = Storage.SlotMetadata[SlotIndex].State;
	return State == EObjectSlotState::Live || State == EObjectSlotState::PendingDestroy || State == EObjectSlotState::Destroying;
}

bool FObjectStore::CollectorIsPendingDestroy(const ObjectIndex SlotIndex) const noexcept
{
	if (SlotIndex >= Storage.SlotCount)
	{
		return false;
	}
	const EObjectSlotState State = Storage.SlotMetadata[SlotIndex].State;
	return State == EObjectSlotState::PendingDestroy || State == EObjectSlotState::Destroying;
}

bool FObjectStore::CollectorIsMarked(const ObjectIndex SlotIndex) const noexcept
{
	return SlotIndex < Storage.SlotCount && CollectorIsOccupied(SlotIndex) && Storage.SlotMetadata[SlotIndex].bMarked;
}

void FObjectStore::CollectorSetMarked(const ObjectIndex SlotIndex, const bool bMarked) noexcept
{
	if (SlotIndex < Storage.SlotCount && CollectorIsOccupied(SlotIndex))
	{
		Storage.SlotMetadata[SlotIndex].bMarked = bMarked;
	}
}

bool FObjectStore::CollectorTryBegin(const FGarbageCollector& Collector) noexcept
{
	if (bMutationLocked || ActiveCollector != nullptr)
	{
		return false;
	}
	ActiveCollector = &Collector;
	return true;
}

void FObjectStore::CollectorEnd(const FGarbageCollector& Collector) noexcept
{
	if (ActiveCollector == &Collector)
	{
		ActiveCollector = nullptr;
	}
}

EObjectResult FObjectStore::CollectorReclaim(const FObjectHandle Handle) noexcept
{
	FObjectSlotMetadata* const Slot = FindMatchingSlot(Handle, true);
	return Slot != nullptr ? DestroySlot(Handle.Index) : EObjectResult::StaleHandle;
}

bool FObjectStore::TryBeginDispatch() noexcept
{
	if (IsMutationLocked())
	{
		return false;
	}
	bDispatchLocked = true;
	return true;
}

void FObjectStore::EndDispatch() noexcept
{
	bDispatchLocked = false;
}

UObject* ResolveObjectHandle(const FObjectStore& Store, const FObjectHandle Handle) noexcept
{
	return Store.Resolve(Handle);
}

EObjectResult AddObjectRoot(FObjectStore& Store, const FObjectHandle Handle) noexcept
{
	return Store.AddRoot(Handle);
}

void ReleaseObjectRoot(FObjectStore& Store, const FObjectHandle Handle) noexcept
{
	(void)Store.RemoveRoot(Handle);
}

void TraceManagedObjectReferences(UObject& Object, FReferenceCollector& Collector) noexcept
{
	Object.VisitReferences(Collector);
}

} // namespace MicroWorld
