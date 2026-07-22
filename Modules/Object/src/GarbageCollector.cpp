#include <MicroWorld/Object/GarbageCollector.h>

#include <MicroWorld/Object/ObjectStore.h>

#include <limits>

namespace MicroWorld
{

namespace
{

	/** Resets one reentry flag on every explicit or early return from a bounded call. */
	class FScopedFlagReset final
	{
	public:
		/** Marks the protected operation active for this lexical scope. */
		explicit FScopedFlagReset(bool& InFlag) noexcept : Flag(InFlag) { Flag = true; }

		/** Makes the protected operation callable again after every return path. */
		~FScopedFlagReset() noexcept { Flag = false; }

		/** Preserves unique responsibility for resetting one referenced flag. */
		FScopedFlagReset(const FScopedFlagReset&) = delete;

		/** Prevents changing the referenced flag behind this guard. */
		FScopedFlagReset& operator=(const FScopedFlagReset&) = delete;

	private:
		/** Identifies the caller-owned flag that must be reset at scope exit. */
		bool& Flag;
	};

	/** Increments a diagnostic counter without allowing long-running wraparound. */
	void IncrementSaturated(std::uint32_t& Counter) noexcept
	{
		if (Counter < std::numeric_limits<std::uint32_t>::max())
		{
			++Counter;
		}
	}

	/** Adds bounded work to one aggregate without allowing diagnostic wraparound. */
	void AddSaturated(std::uint32_t& Counter, const std::uint32_t Amount) noexcept
	{
		const std::uint32_t Remaining = std::numeric_limits<std::uint32_t>::max() - Counter;
		Counter += Amount < Remaining ? Amount : Remaining;
	}

} // namespace

void FReferenceCollector::AddReferencedHandle(const FObjectHandle Handle) noexcept
{
	if (Collector != nullptr)
	{
		Collector->DiscoverReference(Handle);
	}
}

FGarbageCollector::FGarbageCollector(FObjectStore& Store, const FGarbageCollectorStorage Storage) noexcept
	: ObjectStore(&Store), CollectorStorage(Storage)
{
}

FGarbageCollector::~FGarbageCollector() noexcept
{
	if (CurrentPhase != EGarbageCollectionPhase::Idle)
	{
		ResetCycle();
	}
}

ERuntimeResult FGarbageCollector::RequestCollection() noexcept
{
	if (CurrentPhase != EGarbageCollectionPhase::Idle)
	{
		IncrementSaturated(CollectionStats.RejectedRequests);
		return ERuntimeResult::LifecycleLocked;
	}

	if (ObjectStore != nullptr && ObjectStore->CollectorIsMutationLocked())
	{
		IncrementSaturated(CollectionStats.RejectedRequests);
		return ERuntimeResult::LifecycleLocked;
	}

	const bool bStoreReady = ObjectStore != nullptr && ObjectStore->ConfigurationResult() == EObjectResult::Success;
	const bool bWorklistReady = ObjectStore != nullptr && CollectorStorage.WorklistCapacity >= ObjectStore->CollectorSlotCapacity()
		&& (CollectorStorage.WorklistCapacity == 0 || CollectorStorage.Worklist != nullptr);
	if (!bStoreReady || !bWorklistReady)
	{
		IncrementSaturated(CollectionStats.RejectedRequests);
		return ERuntimeResult::CapacityExceeded;
	}
	if (!ObjectStore->CollectorTryBegin(*this))
	{
		IncrementSaturated(CollectionStats.RejectedRequests);
		return ERuntimeResult::LifecycleLocked;
	}

	RootCursor = 0;
	WorklistCount = 0;
	SweepCursor = 0;
	CurrentPhase = EGarbageCollectionPhase::SeedRoots;
	return ERuntimeResult::Success;
}

FGarbageCollectionResult FGarbageCollector::Advance(const FGarbageCollectionBudget Budget) noexcept
{
	FGarbageCollectionResult CollectionResult{};
	CollectionResult.Phase = CurrentPhase;
	if (bAdvanceActive)
	{
		CollectionResult.Result = ERuntimeResult::LifecycleLocked;
		return CollectionResult;
	}
	if (ObjectStore != nullptr && ObjectStore->CollectorIsDispatchLocked())
	{
		CollectionResult.Result = ERuntimeResult::LifecycleLocked;
		return CollectionResult;
	}
	if (CurrentPhase == EGarbageCollectionPhase::Idle || ObjectStore == nullptr)
	{
		CollectionResult.Result = ERuntimeResult::InvalidLifecycle;
		return CollectionResult;
	}
	if (ObjectStore != nullptr && ObjectStore->CollectorIsMutationLocked())
	{
		CollectionResult.Result = ERuntimeResult::LifecycleLocked;
		return CollectionResult;
	}
	if (ObjectStore != nullptr && !ObjectStore->CollectorIsOwnedBy(*this))
	{
		CollectionResult.Result = ERuntimeResult::LifecycleLocked;
		return CollectionResult;
	}

	FScopedFlagReset AdvanceGuard(bAdvanceActive);
	while (CurrentPhase != EGarbageCollectionPhase::Idle)
	{
		if (CurrentPhase == EGarbageCollectionPhase::SeedRoots)
		{
			const std::uint32_t RootCapacity = ObjectStore->CollectorRootCapacity();
			while (RootCursor < RootCapacity && CollectionResult.RootOperations < Budget.MaxRootOperations)
			{
				const FObjectHandle RootHandle = ObjectStore->CollectorRootAt(RootCursor);
				++RootCursor;
				++CollectionResult.RootOperations;
				DiscoverReference(RootHandle);

				if (CurrentPhase == EGarbageCollectionPhase::Idle)
				{
					CollectionResult.Result = ERuntimeResult::CapacityExceeded;
					CollectionResult.Phase = CurrentPhase;
					AddSaturated(CollectionResult.OperationsPerformed, CollectionResult.RootOperations);
					return CollectionResult;
				}
			}

			if (RootCursor < RootCapacity)
			{
				break;
			}
			CurrentPhase = EGarbageCollectionPhase::Mark;
			continue;
		}

		if (CurrentPhase == EGarbageCollectionPhase::Mark)
		{
			while (WorklistCount > 0 && CollectionResult.MarkOperations < Budget.MaxMarkOperations)
			{
				--WorklistCount;
				const FObjectHandle ObjectHandle = CollectorStorage.Worklist[WorklistCount];
				UObject* const Object = ObjectStore->Resolve(ObjectHandle);

				if (Object != nullptr && !Object->IsPendingDestroy())
				{
					const FClassDescriptor& Descriptor = Object->GetClassDescriptor();
					if (Descriptor.TraceReferences != nullptr)
					{
						FReferenceCollector ReferenceCollector(*this, *ObjectStore);
						Descriptor.TraceReferences(*Object, ReferenceCollector);
					}
				}
				++CollectionResult.MarkOperations;

				if (CurrentPhase == EGarbageCollectionPhase::Idle)
				{
					CollectionResult.Result = ERuntimeResult::CapacityExceeded;
					CollectionResult.Phase = CurrentPhase;
					AddSaturated(CollectionResult.OperationsPerformed, CollectionResult.RootOperations);
					AddSaturated(CollectionResult.OperationsPerformed, CollectionResult.MarkOperations);
					return CollectionResult;
				}
			}

			if (WorklistCount > 0)
			{
				break;
			}
			CurrentPhase = EGarbageCollectionPhase::Sweep;
			continue;
		}

		const std::uint32_t SlotCapacity = ObjectStore->CollectorSlotCapacity();
		while (SweepCursor < SlotCapacity && CollectionResult.SweepOperations < Budget.MaxSweepOperations)
		{
			const ObjectIndex SlotIndex = SweepCursor;
			++SweepCursor;
			++CollectionResult.SweepOperations;

			if (!ObjectStore->CollectorIsOccupied(SlotIndex) || ObjectStore->CollectorIsPendingDestroy(SlotIndex))
			{
				continue;
			}

			if (ObjectStore->CollectorIsMarked(SlotIndex))
			{
				ObjectStore->CollectorSetMarked(SlotIndex, false);
				continue;
			}

			const FObjectHandle UnreachableHandle = ObjectStore->CollectorHandleAt(SlotIndex);
			if (UnreachableHandle.IsValid() && ObjectStore->CollectorReclaim(UnreachableHandle) == EObjectResult::Success)
			{
				++CollectionResult.ObjectsReclaimed;
				IncrementSaturated(CollectionStats.ReclaimedObjects);
			}
		}

		if (SweepCursor < SlotCapacity)
		{
			break;
		}

		CurrentPhase = EGarbageCollectionPhase::Idle;
		IncrementSaturated(CollectionStats.CompletedCycles);
		CollectionResult.bCycleComplete = true;
		CompleteCycle();
	}

	AddSaturated(CollectionResult.OperationsPerformed, CollectionResult.RootOperations);
	AddSaturated(CollectionResult.OperationsPerformed, CollectionResult.MarkOperations);
	AddSaturated(CollectionResult.OperationsPerformed, CollectionResult.SweepOperations);
	CollectionResult.Phase = CurrentPhase;
	return CollectionResult;
}

FGarbageCollectionResult FGarbageCollector::CollectFull() noexcept
{
	if (CurrentPhase == EGarbageCollectionPhase::Idle)
	{
		const ERuntimeResult RequestResult = RequestCollection();
		if (RequestResult != ERuntimeResult::Success)
		{
			FGarbageCollectionResult RejectedCollection{};
			RejectedCollection.Result = RequestResult;
			RejectedCollection.Phase = CurrentPhase;
			return RejectedCollection;
		}
	}

	constexpr std::uint32_t UnlimitedOperations = std::numeric_limits<std::uint32_t>::max();
	return Advance(FGarbageCollectionBudget{
		UnlimitedOperations,
		UnlimitedOperations,
		UnlimitedOperations,
	});
}

ERuntimeResult FGarbageCollector::CancelCollection() noexcept
{
	if (CurrentPhase == EGarbageCollectionPhase::Idle)
	{
		return ERuntimeResult::InvalidLifecycle;
	}
	ResetCycle();
	return ERuntimeResult::Success;
}

void FGarbageCollector::DiscoverReference(const FObjectHandle Handle) noexcept
{
	const bool bDiscoveryPhase = CurrentPhase == EGarbageCollectionPhase::SeedRoots || CurrentPhase == EGarbageCollectionPhase::Mark;
	if (!bDiscoveryPhase || ObjectStore == nullptr || !ObjectStore->CollectorIsOwnedBy(*this) || !Handle.IsValid()
		|| Handle.Index >= ObjectStore->CollectorSlotCapacity())
	{
		return;
	}
	if (ObjectStore->CollectorIsPendingDestroy(Handle.Index) || ObjectStore->CollectorHandleAt(Handle.Index) != Handle
		|| ObjectStore->CollectorIsMarked(Handle.Index))
	{
		return;
	}

	if (WorklistCount >= CollectorStorage.WorklistCapacity || CollectorStorage.Worklist == nullptr)
	{
		IncrementSaturated(CollectionStats.WorklistOverflows);
		IncrementSaturated(CollectionStats.RejectedRequests);
		ResetCycle();
		return;
	}

	ObjectStore->CollectorSetMarked(Handle.Index, true);
	CollectorStorage.Worklist[WorklistCount] = Handle;
	++WorklistCount;
}

void FGarbageCollector::ResetCycle() noexcept
{
	if (ObjectStore != nullptr)
	{
		for (ObjectIndex SlotIndex = 0; SlotIndex < ObjectStore->CollectorSlotCapacity(); ++SlotIndex)
		{
			ObjectStore->CollectorSetMarked(SlotIndex, false);
		}
		ObjectStore->CollectorEnd(*this);
	}
	RootCursor = 0;
	WorklistCount = 0;
	SweepCursor = 0;
	CurrentPhase = EGarbageCollectionPhase::Idle;
}

void FGarbageCollector::CompleteCycle() noexcept
{
	if (ObjectStore != nullptr)
	{
		ObjectStore->CollectorEnd(*this);
	}
	RootCursor = 0;
	WorklistCount = 0;
	SweepCursor = 0;
	CurrentPhase = EGarbageCollectionPhase::Idle;
}

} // namespace MicroWorld
