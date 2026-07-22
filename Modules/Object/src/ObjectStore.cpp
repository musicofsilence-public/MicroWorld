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
	bool CanMultiply(const std::size_t Left, const std::size_t Right) noexcept
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

FObjectStore::FObjectStore(const FObjectStoreStorage InStorage, const FClassRegistryView InClasses) noexcept : Storage(InStorage), Classes(InClasses)
{
	const bool bHasSlotPointers = Storage.SlotCount > 0 && Storage.SlotBytes != nullptr && Storage.Slots != nullptr;
	const bool bHasSlotLayout =
		Storage.SlotSizeBytes > 0 && IsValidAlignment(Storage.SlotAlignmentBytes) && (Storage.SlotSizeBytes % Storage.SlotAlignmentBytes) == 0;
	const bool bStorageExtentFits =
		CanMultiply(Storage.SlotSizeBytes, Storage.SlotCount) && Storage.SlotStorageSizeBytes >= Storage.SlotSizeBytes * Storage.SlotCount;
	const bool bStorageAddressAligned =
		Storage.SlotBytes != nullptr && (reinterpret_cast<std::uintptr_t>(Storage.SlotBytes) & (Storage.SlotAlignmentBytes - 1U)) == 0;
	const bool bHasRootStorage = Storage.RootCapacity == 0 || Storage.Roots != nullptr;

	if (!bHasSlotPointers || !bHasSlotLayout || !bStorageExtentFits || !bStorageAddressAligned || !bHasRootStorage)
	{
		return;
	}

	for (std::uint32_t SlotIndex = 0; SlotIndex < Storage.SlotCount; ++SlotIndex)
	{
		Storage.Slots[SlotIndex] = {};
	}
	for (std::uint32_t RootIndex = 0; RootIndex < Storage.RootCapacity; ++RootIndex)
	{
		Storage.Roots[RootIndex] = {};
	}

	StoreConfigurationResult = EObjectResult::Success;
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

FObjectMutationResult FObjectStore::ApplyPendingDestroy(const std::uint32_t MaxObjects) noexcept
{
	FObjectMutationResult Mutation{};
	Mutation.Result = StoreConfigurationResult;
	Mutation.PendingObjectsRemaining = PendingDestroyCount;
	if (IsPublicMutationLocked())
	{
		Mutation.Result = EObjectResult::LifecycleLocked;
		return Mutation;
	}
	if (StoreConfigurationResult != EObjectResult::Success || MaxObjects == 0 || PendingDestroyCount == 0)
	{
		return Mutation;
	}

	const std::uint32_t VisitLimit = MaxObjects < Storage.SlotCount ? MaxObjects : Storage.SlotCount;
	while (Mutation.SlotsVisited < VisitLimit && PendingDestroyCount > 0)
	{
		const ObjectIndex SlotIndex = PendingDestroyScanCursor;
		PendingDestroyScanCursor = PendingDestroyScanCursor + 1U == Storage.SlotCount ? 0 : PendingDestroyScanCursor + 1U;
		++Mutation.SlotsVisited;

		if (Storage.Slots[SlotIndex].State == EObjectSlotState::PendingDestroy)
		{
			(void)DestroySlot(SlotIndex);
			++Mutation.ObjectsDestroyed;
		}
	}

	Mutation.PendingObjectsRemaining = PendingDestroyCount;
	return Mutation;
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
		if (Storage.Slots[SlotIndex].State == EObjectSlotState::Vacant && CanAdvanceObjectGeneration(Storage.Slots[SlotIndex].Generation))
		{
			return SlotIndex;
		}
	}
	return FObjectHandle::InvalidIndex;
}

void* FObjectStore::SlotAddress(const ObjectIndex SlotIndex) const noexcept
{
	return static_cast<void*>(Storage.SlotBytes + static_cast<std::size_t>(SlotIndex) * Storage.SlotSizeBytes);
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

	const FObjectSlotMetadata& Slot = Storage.Slots[Handle.Index];
	const bool bStateAccepted = Slot.State == EObjectSlotState::Live || (bAllowPending && Slot.State == EObjectSlotState::PendingDestroy);
	if (!bStateAccepted || Slot.Generation != Handle.Generation || Slot.Object == nullptr)
	{
		return nullptr;
	}
	return &Slot;
}

EObjectResult FObjectStore::DestroySlot(const ObjectIndex SlotIndex) noexcept
{
	if (SlotIndex >= Storage.SlotCount)
	{
		return EObjectResult::StaleHandle;
	}

	FObjectSlotMetadata& Slot = Storage.Slots[SlotIndex];
	if ((Slot.State != EObjectSlotState::Live && Slot.State != EObjectSlotState::PendingDestroy) || Slot.Object == nullptr
		|| Slot.Descriptor == nullptr || Slot.Descriptor->Destroy == nullptr)
	{
		return EObjectResult::StaleHandle;
	}

	const FObjectHandle Handle{SlotIndex, Slot.Generation};
	UObject* const Object = Slot.Object;
	const FClassDescriptor* const Descriptor = Slot.Descriptor;
	const bool bWasPending = Slot.State == EObjectSlotState::PendingDestroy;
	const bool bWasMutationLocked = bMutationLocked;

	bMutationLocked = true;
	Slot.State = EObjectSlotState::Destroying;
	Object->bPendingDestroy = true;
	Object->BeginDestroy();
	Descriptor->Destroy(*Object);
	RemoveAllRoots(Handle);

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

	if (OccupiedSlotCount > 0)
	{
		--OccupiedSlotCount;
	}
	if (bWasPending && PendingDestroyCount > 0)
	{
		--PendingDestroyCount;
	}
	if (ObjectPayloadByteCount >= Descriptor->SizeBytes)
	{
		ObjectPayloadByteCount -= Descriptor->SizeBytes;
	}
	bMutationLocked = bWasMutationLocked;
	return EObjectResult::Success;
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
	if (SlotIndex >= Storage.SlotCount || Storage.Slots[SlotIndex].State != EObjectSlotState::Live)
	{
		return {};
	}
	return FObjectHandle{SlotIndex, Storage.Slots[SlotIndex].Generation};
}

UObject* FObjectStore::CollectorObjectAt(const ObjectIndex SlotIndex) const noexcept
{
	return SlotIndex < Storage.SlotCount && Storage.Slots[SlotIndex].State == EObjectSlotState::Live ? Storage.Slots[SlotIndex].Object : nullptr;
}

bool FObjectStore::CollectorIsOccupied(const ObjectIndex SlotIndex) const noexcept
{
	if (SlotIndex >= Storage.SlotCount)
	{
		return false;
	}
	const EObjectSlotState State = Storage.Slots[SlotIndex].State;
	return State == EObjectSlotState::Live || State == EObjectSlotState::PendingDestroy || State == EObjectSlotState::Destroying;
}

bool FObjectStore::CollectorIsPendingDestroy(const ObjectIndex SlotIndex) const noexcept
{
	if (SlotIndex >= Storage.SlotCount)
	{
		return false;
	}
	const EObjectSlotState State = Storage.Slots[SlotIndex].State;
	return State == EObjectSlotState::PendingDestroy || State == EObjectSlotState::Destroying;
}

bool FObjectStore::CollectorIsMarked(const ObjectIndex SlotIndex) const noexcept
{
	return SlotIndex < Storage.SlotCount && CollectorIsOccupied(SlotIndex) && Storage.Slots[SlotIndex].bMarked;
}

void FObjectStore::CollectorSetMarked(const ObjectIndex SlotIndex, const bool bMarked) noexcept
{
	if (SlotIndex < Storage.SlotCount && CollectorIsOccupied(SlotIndex))
	{
		Storage.Slots[SlotIndex].bMarked = bMarked;
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
