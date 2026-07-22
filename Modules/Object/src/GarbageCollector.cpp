#include <MicroWorld/Object/GarbageCollector.h>

#include <MicroWorld/Object/ObjectStore.h>

#include <limits>

namespace MicroWorld
{

namespace
{

	/** Resets one reentry flag on every explicit or early return from a bounded call. */
	class FScopedReentryGuard final
	{
	public:
		/** Marks the protected operation active for this lexical scope. */
		explicit FScopedReentryGuard(bool& InFlag) noexcept : Flag(InFlag) { Flag = true; }

		/** Makes the protected operation callable again after every return path. */
		~FScopedReentryGuard() noexcept { Flag = false; }

		/** Preserves unique responsibility for resetting one referenced flag. */
		FScopedReentryGuard(const FScopedReentryGuard&) = delete;

		/** Prevents changing the referenced flag behind this guard. */
		FScopedReentryGuard& operator=(const FScopedReentryGuard&) = delete;

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
	const ERuntimeResult StartFailure = ClassifyStartFailure();
	if (StartFailure != ERuntimeResult::Success)
	{
		IncrementSaturated(CollectionStats.RejectedRequests);
		return StartFailure;
	}

	RootCursor = 0;
	WorklistCount = 0;
	SweepCursor = 0;
	CurrentPhase = EGarbageCollectionPhase::SeedRoots;
	return ERuntimeResult::Success;
}

ERuntimeResult FGarbageCollector::ClassifyStartFailure() noexcept
{
	if (CurrentPhase != EGarbageCollectionPhase::Idle)
	{
		return ERuntimeResult::LifecycleLocked;
	}
	if (ObjectStore != nullptr && ObjectStore->CollectorIsMutationLocked())
	{
		return ERuntimeResult::LifecycleLocked;
	}
	const bool bStoreReady = ObjectStore != nullptr && ObjectStore->ConfigurationResult() == EObjectResult::Success;
	const bool bWorklistReady = ObjectStore != nullptr && CollectorStorage.WorklistCapacity >= ObjectStore->CollectorSlotCapacity()
		&& (CollectorStorage.WorklistCapacity == 0 || CollectorStorage.Worklist != nullptr);
	if (!bStoreReady || !bWorklistReady)
	{
		return ERuntimeResult::CapacityExceeded;
	}
	if (!ObjectStore->CollectorTryBegin(*this))
	{
		return ERuntimeResult::LifecycleLocked;
	}
	return ERuntimeResult::Success;
}

FGarbageCollectionResult FGarbageCollector::Advance(const FGarbageCollectionBudget Budget) noexcept
{
	FGarbageCollectionResult CollectionResult{};
	CollectionResult.Phase = CurrentPhase;
	const ERuntimeResult PreconditionResult = ValidateAdvancePreconditions();
	if (PreconditionResult != ERuntimeResult::Success)
	{
		CollectionResult.Result = PreconditionResult;
		return CollectionResult;
	}

	// Clear any overflow flag left set by a prior cycle's abort so only this
	// call's discovery can trip it.
	bWorklistOverflowed = false;
	FScopedReentryGuard AdvanceGuard(bAdvanceActive);
	while (CurrentPhase != EGarbageCollectionPhase::Idle)
	{
		bool bPhaseAdvanced = false;
		if (CurrentPhase == EGarbageCollectionPhase::SeedRoots)
		{
			bPhaseAdvanced = AdvanceSeedRootsPhase(Budget, CollectionResult);
		}
		else if (CurrentPhase == EGarbageCollectionPhase::Mark)
		{
			bPhaseAdvanced = AdvanceMarkPhase(Budget, CollectionResult);
		}
		else
		{
			bPhaseAdvanced = AdvanceSweepPhase(Budget, CollectionResult);
		}

		if (bWorklistOverflowed)
		{
			// DiscoverReference aborts the cycle on worklist overflow -- detect that abort here.
			CollectionResult.Result = ERuntimeResult::CapacityExceeded;
			AccumulateOperations(CollectionResult);
			CollectionResult.Phase = CurrentPhase;
			return CollectionResult;
		}
		if (!bPhaseAdvanced)
		{
			break;
		}
	}

	AccumulateOperations(CollectionResult);
	CollectionResult.Phase = CurrentPhase;
	return CollectionResult;
}

ERuntimeResult FGarbageCollector::ValidateAdvancePreconditions() const noexcept
{
	if (bAdvanceActive)
	{
		return ERuntimeResult::LifecycleLocked;
	}
	if (ObjectStore != nullptr && ObjectStore->CollectorIsDispatchLocked())
	{
		return ERuntimeResult::LifecycleLocked;
	}
	if (CurrentPhase == EGarbageCollectionPhase::Idle || ObjectStore == nullptr)
	{
		return ERuntimeResult::InvalidLifecycle;
	}
	if (ObjectStore != nullptr && ObjectStore->CollectorIsMutationLocked())
	{
		return ERuntimeResult::LifecycleLocked;
	}
	if (ObjectStore != nullptr && !ObjectStore->CollectorIsOwnedBy(*this))
	{
		return ERuntimeResult::LifecycleLocked;
	}
	return ERuntimeResult::Success;
}

bool FGarbageCollector::AdvanceSeedRootsPhase(const FGarbageCollectionBudget& Budget, FGarbageCollectionResult& Result) noexcept
{
	const std::uint32_t RootCapacity = ObjectStore->CollectorRootCapacity();
	while (!bWorklistOverflowed && RootCursor < RootCapacity && Result.RootOperations < Budget.MaxRootOperations)
	{
		const FObjectHandle RootHandle = ObjectStore->CollectorRootAt(RootCursor);
		++RootCursor;
		++Result.RootOperations;
		DiscoverReference(RootHandle);
	}
	if (bWorklistOverflowed)
	{
		return false;
	}
	if (RootCursor < RootCapacity)
	{
		return false;
	}
	CurrentPhase = EGarbageCollectionPhase::Mark;
	return true;
}

bool FGarbageCollector::AdvanceMarkPhase(const FGarbageCollectionBudget& Budget, FGarbageCollectionResult& Result) noexcept
{
	while (!bWorklistOverflowed && WorklistCount > 0 && Result.MarkOperations < Budget.MaxMarkOperations)
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
		++Result.MarkOperations;
	}
	if (bWorklistOverflowed)
	{
		return false;
	}
	if (WorklistCount > 0)
	{
		return false;
	}
	CurrentPhase = EGarbageCollectionPhase::Sweep;
	return true;
}

bool FGarbageCollector::AdvanceSweepPhase(const FGarbageCollectionBudget& Budget, FGarbageCollectionResult& Result) noexcept
{
	const std::uint32_t SlotCapacity = ObjectStore->CollectorSlotCapacity();
	while (SweepCursor < SlotCapacity && Result.SweepOperations < Budget.MaxSweepOperations)
	{
		const ObjectIndex SlotIndex = SweepCursor;
		++SweepCursor;
		++Result.SweepOperations;
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
			++Result.ObjectsReclaimed;
			IncrementSaturated(CollectionStats.ReclaimedObjects);
		}
	}
	if (SweepCursor < SlotCapacity)
	{
		return false;
	}
	FinalizeCompletedCycle(Result);
	return true;
}

void FGarbageCollector::FinalizeCompletedCycle(FGarbageCollectionResult& Result) noexcept
{
	CurrentPhase = EGarbageCollectionPhase::Idle;
	IncrementSaturated(CollectionStats.CompletedCycles);
	Result.bCycleComplete = true;
	CompleteCycle();
}

void FGarbageCollector::AccumulateOperations(FGarbageCollectionResult& Result) noexcept
{
	AddSaturated(Result.OperationsPerformed, Result.RootOperations);
	AddSaturated(Result.OperationsPerformed, Result.MarkOperations);
	AddSaturated(Result.OperationsPerformed, Result.SweepOperations);
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
		bWorklistOverflowed = true;
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
	bWorklistOverflowed = false;
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
