#include <MicroWorld/Engine/ObjectStore.h>

#include <limits>

namespace MicroWorld
{

namespace
{

	/** Confirms that a fixed alignment is non-zero and power-of-two. */
	bool IsValidAlignment(const std::size_t InAlignmentBytes) noexcept
	{
		return InAlignmentBytes > 0 && (InAlignmentBytes & (InAlignmentBytes - 1U)) == 0;
	}

	/** Confirms multiplication cannot wrap the storage extent calculation. */
	bool MultiplicationFitsSizeType(const std::size_t InLeft, const std::size_t InRight) noexcept
	{
		return InRight == 0 || InLeft <= std::numeric_limits<std::size_t>::max() / InRight;
	}

} // namespace

FObjectStoreDispatchGuard::FObjectStoreDispatchGuard(FObjectStore& InStore) noexcept
{
	if (InStore.TryBeginDispatch())
	{
		ObjectStore = &InStore;
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

bool FObjectStore::IsStorageDescriptorValid(const FObjectStoreStorage& InStorage) noexcept
{
	const bool bHasSlotPointers = InStorage.SlotCount > 0 && InStorage.SlotPayloadBytes != nullptr && InStorage.SlotMetadata != nullptr;
	const bool bHasSlotLayout = InStorage.SlotSizeBytes > 0 && IsValidAlignment(InStorage.SlotAlignmentBytes)
		&& (InStorage.SlotSizeBytes % InStorage.SlotAlignmentBytes) == 0;
	const bool bStorageExtentFits = MultiplicationFitsSizeType(InStorage.SlotSizeBytes, InStorage.SlotCount)
		&& InStorage.TotalSlotStorageBytes >= InStorage.SlotSizeBytes * InStorage.SlotCount;
	const bool bStorageAddressAligned = InStorage.SlotPayloadBytes != nullptr
		&& (reinterpret_cast<std::uintptr_t>(InStorage.SlotPayloadBytes) & (InStorage.SlotAlignmentBytes - 1U)) == 0;
	const bool bHasRootStorage = InStorage.RootCapacity == 0 || InStorage.Roots != nullptr;
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

UObject* FObjectStore::Resolve(const FObjectHandle InHandle) const noexcept
{
	const FObjectSlotMetadata* const Slot = FindMatchingSlot(InHandle, false);
	return Slot != nullptr ? Slot->Object : nullptr;
}

EObjectResult FObjectStore::MarkPendingDestroy(const FObjectHandle InHandle) noexcept
{
	if (IsPublicMutationLocked())
	{
		return EObjectResult::LifecycleLocked;
	}

	FObjectSlotMetadata* const AnyMatchingSlot = FindMatchingSlot(InHandle, true);
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

FObjectMutationResult FObjectStore::ApplyPendingDestroy(const std::uint32_t InMaxSlotsToInspect) noexcept
{
	FObjectMutationResult Mutation{};
	Mutation.Result = StoreConfigurationResult;
	Mutation.PendingObjectsRemaining = PendingDestroyCount;
	if (IsPublicMutationLocked())
	{
		Mutation.Result = EObjectResult::LifecycleLocked;
		return Mutation;
	}
	if (StoreConfigurationResult != EObjectResult::Success || InMaxSlotsToInspect == 0 || PendingDestroyCount == 0)
	{
		return Mutation;
	}

	const std::uint32_t VisitLimit = InMaxSlotsToInspect < Storage.SlotCount ? InMaxSlotsToInspect : Storage.SlotCount;
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

EObjectResult FObjectStore::AddRoot(const FObjectHandle InHandle) noexcept
{
	if (IsPublicMutationLocked())
	{
		return EObjectResult::LifecycleLocked;
	}

	FObjectSlotMetadata* const MatchingSlot = FindMatchingSlot(InHandle, true);
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
			Storage.Roots[RootIndex].Handle = InHandle;
			++ActiveRootCount;
			return EObjectResult::Success;
		}
	}
	return EObjectResult::RootCapacityExceeded;
}

EObjectResult FObjectStore::RemoveRoot(const FObjectHandle InHandle) noexcept
{
	if (!InHandle.IsValid() || StoreConfigurationResult != EObjectResult::Success)
	{
		return EObjectResult::StaleHandle;
	}

	// Root release stays available to noexcept RAII cleanup. It can only affect a
	// later collection; guarded destruction and slot reuse remain impossible.
	for (std::uint32_t RootIndex = 0; RootIndex < Storage.RootCapacity; ++RootIndex)
	{
		if (Storage.Roots[RootIndex].Handle == InHandle)
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

void* FObjectStore::SlotAddress(const ObjectIndex InSlotIndex) const noexcept
{
	return static_cast<void*>(Storage.SlotPayloadBytes + static_cast<std::size_t>(InSlotIndex) * Storage.SlotSizeBytes);
}

FObjectSlotMetadata* FObjectStore::FindMatchingSlot(const FObjectHandle InHandle, const bool bInAllowPending) noexcept
{
	return const_cast<FObjectSlotMetadata*>(static_cast<const FObjectStore&>(*this).FindMatchingSlot(InHandle, bInAllowPending));
}

const FObjectSlotMetadata* FObjectStore::FindMatchingSlot(const FObjectHandle InHandle, const bool bInAllowPending) const noexcept
{
	if (StoreConfigurationResult != EObjectResult::Success || !InHandle.IsValid() || InHandle.Index >= Storage.SlotCount)
	{
		return nullptr;
	}

	const FObjectSlotMetadata& Slot = Storage.SlotMetadata[InHandle.Index];
	const bool bStateAccepted = Slot.State == EObjectSlotState::Live || (bInAllowPending && Slot.State == EObjectSlotState::PendingDestroy);
	const bool bGenerationMatches = Slot.Generation == InHandle.Generation;
	const bool bHoldsLiveObject = Slot.Object != nullptr;
	if (!bStateAccepted || !bGenerationMatches || !bHoldsLiveObject)
	{
		return nullptr;
	}
	return &Slot;
}

EObjectResult FObjectStore::DestroySlot(const ObjectIndex InSlotIndex) noexcept
{
	const EObjectResult ValidationResult = ValidateDestroyableSlot(InSlotIndex);
	if (ValidationResult != EObjectResult::Success)
	{
		return ValidationResult;
	}

	FObjectSlotMetadata& Slot = Storage.SlotMetadata[InSlotIndex];
	const FObjectHandle Handle{InSlotIndex, Slot.Generation};
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

EObjectResult FObjectStore::ValidateDestroyableSlot(const ObjectIndex InSlotIndex) const noexcept
{
	if (InSlotIndex >= Storage.SlotCount)
	{
		return EObjectResult::StaleHandle;
	}
	const FObjectSlotMetadata& Slot = Storage.SlotMetadata[InSlotIndex];
	if (!IsSlotDestroyable(Slot))
	{
		return EObjectResult::StaleHandle;
	}
	return EObjectResult::Success;
}

bool FObjectStore::IsSlotDestroyable(const FObjectSlotMetadata& InSlot) noexcept
{
	const bool bStateIsDestroyable = InSlot.State == EObjectSlotState::Live || InSlot.State == EObjectSlotState::PendingDestroy;
	const bool bHasObject = InSlot.Object != nullptr;
	const bool bHasDescriptor = InSlot.Descriptor != nullptr;
	const bool bHasDestructor = InSlot.Descriptor != nullptr && InSlot.Descriptor->Destroy != nullptr;
	return bStateIsDestroyable && bHasObject && bHasDescriptor && bHasDestructor;
}

void FObjectStore::RunDestructionCallbacks(FObjectSlotMetadata& InSlot, const FObjectHandle InHandle) noexcept
{
	UObject* const Object = InSlot.Object;
	const FClassDescriptor* const Descriptor = InSlot.Descriptor;
	InSlot.State = EObjectSlotState::Destroying;
	Object->bPendingDestroy = true;
	Object->BeginDestroy();
	Descriptor->Destroy(*Object);
	RemoveAllRoots(InHandle);
}

void FObjectStore::RecycleOrRetireSlot(FObjectSlotMetadata& InSlot) noexcept
{
	InSlot.Descriptor = nullptr;
	InSlot.Object = nullptr;
	InSlot.bMarked = false;
	if (CanAdvanceObjectGeneration(InSlot.Generation))
	{
		InSlot.State = EObjectSlotState::Vacant;
	}
	else
	{
		InSlot.State = EObjectSlotState::Retired;
		++RetiredSlotCount;
	}
}

void FObjectStore::UpdateOccupancyCounters(const bool bInWasPending, const std::size_t InPayloadBytes) noexcept
{
	if (OccupiedSlotCount > 0)
	{
		--OccupiedSlotCount;
	}
	if (bInWasPending && PendingDestroyCount > 0)
	{
		--PendingDestroyCount;
	}
	if (ObjectPayloadByteCount >= InPayloadBytes)
	{
		ObjectPayloadByteCount -= InPayloadBytes;
	}
}

ObjectGeneration FObjectStore::NextPublishGeneration(const ObjectGeneration InCurrentGeneration) noexcept
{
	// Generation 0 means "never published" (ObjectHandle.h), so a slot's first
	// publish jumps to 1 and later reuse increments -- keeping every live handle's
	// generation nonzero. FindVacantSlot never returns a slot that cannot advance.
	return InCurrentGeneration == 0 ? 1 : InCurrentGeneration + 1;
}

FObjectHandle FObjectStore::PublishObjectIntoSlot(
	const ObjectIndex InSlotIndex, const FClassDescriptor& InDescriptor, UObject& InManagedObject) noexcept
{
	FObjectSlotMetadata& Slot = Storage.SlotMetadata[InSlotIndex];
	const ObjectGeneration NextGeneration = NextPublishGeneration(Slot.Generation);
	const FObjectHandle Handle{InSlotIndex, NextGeneration};

	InManagedObject.Store = this;
	InManagedObject.Handle = Handle;
	InManagedObject.Descriptor = &InDescriptor;
	InManagedObject.bPendingDestroy = false;

	Slot.Generation = NextGeneration;
	Slot.Descriptor = &InDescriptor;
	Slot.Object = &InManagedObject;
	Slot.bMarked = false;
	Slot.State = EObjectSlotState::Live;

	++OccupiedSlotCount;
	ObjectPayloadByteCount += InDescriptor.SizeBytes;
	return Handle;
}

void FObjectStore::RemoveAllRoots(const FObjectHandle InHandle) noexcept
{
	for (std::uint32_t RootIndex = 0; RootIndex < Storage.RootCapacity; ++RootIndex)
	{
		if (Storage.Roots[RootIndex].Handle == InHandle)
		{
			Storage.Roots[RootIndex].Handle = {};
			if (ActiveRootCount > 0)
			{
				--ActiveRootCount;
			}
		}
	}
}

FObjectHandle FObjectStore::CollectorRootAt(const std::uint32_t InRootIndex) const noexcept
{
	return InRootIndex < Storage.RootCapacity ? Storage.Roots[InRootIndex].Handle : FObjectHandle{};
}

FObjectHandle FObjectStore::CollectorHandleAt(const ObjectIndex InSlotIndex) const noexcept
{
	if (InSlotIndex >= Storage.SlotCount || Storage.SlotMetadata[InSlotIndex].State != EObjectSlotState::Live)
	{
		return {};
	}
	return FObjectHandle{InSlotIndex, Storage.SlotMetadata[InSlotIndex].Generation};
}

UObject* FObjectStore::CollectorObjectAt(const ObjectIndex InSlotIndex) const noexcept
{
	if (InSlotIndex >= Storage.SlotCount || Storage.SlotMetadata[InSlotIndex].State != EObjectSlotState::Live)
	{
		return nullptr;
	}
	return Storage.SlotMetadata[InSlotIndex].Object;
}

bool FObjectStore::IsStateOccupied(const EObjectSlotState InState) noexcept
{
	return InState == EObjectSlotState::Live || InState == EObjectSlotState::PendingDestroy || InState == EObjectSlotState::Destroying;
}

bool FObjectStore::IsStatePendingDestroy(const EObjectSlotState InState) noexcept
{
	return InState == EObjectSlotState::PendingDestroy || InState == EObjectSlotState::Destroying;
}

bool FObjectStore::CollectorIsOccupied(const ObjectIndex InSlotIndex) const noexcept
{
	if (InSlotIndex >= Storage.SlotCount)
	{
		return false;
	}
	const EObjectSlotState State = Storage.SlotMetadata[InSlotIndex].State;
	return IsStateOccupied(State);
}

bool FObjectStore::CollectorIsPendingDestroy(const ObjectIndex InSlotIndex) const noexcept
{
	if (InSlotIndex >= Storage.SlotCount)
	{
		return false;
	}
	const EObjectSlotState State = Storage.SlotMetadata[InSlotIndex].State;
	return IsStatePendingDestroy(State);
}

bool FObjectStore::CollectorIsMarked(const ObjectIndex InSlotIndex) const noexcept
{
	return InSlotIndex < Storage.SlotCount && CollectorIsOccupied(InSlotIndex) && Storage.SlotMetadata[InSlotIndex].bMarked;
}

void FObjectStore::CollectorSetMarked(const ObjectIndex InSlotIndex, const bool bInMarked) noexcept
{
	if (InSlotIndex < Storage.SlotCount && CollectorIsOccupied(InSlotIndex))
	{
		Storage.SlotMetadata[InSlotIndex].bMarked = bInMarked;
	}
}

bool FObjectStore::CollectorTryBegin(const FGarbageCollector& InCollector) noexcept
{
	if (bMutationLocked || ActiveCollector != nullptr)
	{
		return false;
	}
	ActiveCollector = &InCollector;
	return true;
}

void FObjectStore::CollectorEnd(const FGarbageCollector& InCollector) noexcept
{
	if (ActiveCollector == &InCollector)
	{
		ActiveCollector = nullptr;
	}
}

EObjectResult FObjectStore::CollectorReclaim(const FObjectHandle InHandle) noexcept
{
	FObjectSlotMetadata* const Slot = FindMatchingSlot(InHandle, true);
	return Slot != nullptr ? DestroySlot(InHandle.Index) : EObjectResult::StaleHandle;
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

UObject* ResolveObjectHandle(const FObjectStore& InStore, const FObjectHandle InHandle) noexcept
{
	return InStore.Resolve(InHandle);
}

EObjectResult AddObjectRoot(FObjectStore& InStore, const FObjectHandle InHandle) noexcept
{
	return InStore.AddRoot(InHandle);
}

void ReleaseObjectRoot(FObjectStore& InStore, const FObjectHandle InHandle) noexcept
{
	(void)InStore.RemoveRoot(InHandle);
}

void TraceManagedObjectReferences(UObject& InObject, FReferenceCollector& InCollector) noexcept
{
	InObject.VisitReferences(InCollector);
}

} // namespace MicroWorld
