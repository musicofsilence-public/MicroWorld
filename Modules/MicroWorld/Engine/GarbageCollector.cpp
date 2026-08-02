#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/ReferenceCollector.h>

#include <MicroWorld/Engine/ObjectStore.h>

#include <limits>

namespace MicroWorld::Engine
{

namespace
{

	/**
	 * Motivation: Resets one reentry flag on every explicit or early return from a bounded call so a caller cannot
	 *   forget to clear it.
	 * Responsibilities: Set the flag on construction and clear it exactly once on destruction.
	 * Example:
	 *   FScopedReentryGuard Guard(bAdvanceActive);
	 *   RunBoundedWork();
	 */
	class FScopedReentryGuard final
	{
	public:
		/**
		 * Motivation: Marks the protected operation active for this lexical scope.
		 * Responsibilities: Set the referenced flag true and remember it for cleanup.
		 */
		explicit FScopedReentryGuard(bool& InFlag) noexcept : Flag(InFlag) { Flag = true; }

		/**
		 * Motivation: Makes the protected operation callable again after every return path.
		 * Responsibilities: Set the referenced flag false exactly once.
		 */
		~FScopedReentryGuard() noexcept { Flag = false; }

		/**
		 * Motivation: Preserves unique responsibility for resetting one referenced flag.
		 * Responsibilities: Reject copy construction so two owners never clear the same flag.
		 */
		FScopedReentryGuard(const FScopedReentryGuard&) = delete;

		/**
		 * Motivation: Prevents changing the referenced flag behind this guard via assignment.
		 * Responsibilities: Reject copy assignment so the guard keeps one owner.
		 */
		FScopedReentryGuard& operator=(const FScopedReentryGuard&) = delete;

	private:
		/** Motivation: Identifies the caller-owned flag that must be reset at scope exit. */
		bool& Flag;
	};

	/**
	 * Motivation: Increments a diagnostic counter without allowing long-running wraparound.
	 * Responsibilities: Increment only while the counter is below its maximum.
	 */
	void IncrementSaturated(std::uint32_t& InOutCounter) noexcept
	{
		if (InOutCounter < std::numeric_limits<std::uint32_t>::max())
		{
			++InOutCounter;
		}
	}

	/**
	 * Motivation: Adds bounded work to one aggregate without allowing diagnostic wraparound.
	 * Responsibilities: Add the amount or saturate at the counter maximum.
	 */
	void AddSaturated(std::uint32_t& InOutCounter, const std::uint32_t InAmount) noexcept
	{
		const std::uint32_t Remaining = std::numeric_limits<std::uint32_t>::max() - InOutCounter;
		InOutCounter += InAmount < Remaining ? InAmount : Remaining;
	}

} // namespace

void FReferenceCollector::AddReferencedHandle(const FObjectHandle InHandle) noexcept
{
	if (Collector != nullptr)
	{
		Collector->DiscoverReference(InHandle);
	}
}

FGarbageCollector::FGarbageCollector(FObjectStore& InStore, const FGarbageCollectorStorage InStorage) noexcept
	: ObjectStore(&InStore), CollectorStorage(InStorage)
{
}

FGarbageCollector::~FGarbageCollector() noexcept
{
	if (CurrentPhase != EGarbageCollectionPhase::Idle)
	{
		ResetCycle();
	}
}

Core::ERuntimeResult FGarbageCollector::RequestCollection() noexcept
{
	const Core::ERuntimeResult StartFailure = ClassifyStartFailure();
	if (StartFailure != Core::ERuntimeResult::Success)
	{
		IncrementSaturated(CollectionStats.RejectedRequests);
		return StartFailure;
	}

	RootCursor = 0;
	WorklistCount = 0;
	SweepCursor = 0;
	CurrentPhase = EGarbageCollectionPhase::SeedRoots;
	return Core::ERuntimeResult::Success;
}

Core::ERuntimeResult FGarbageCollector::ClassifyStartFailure() noexcept
{
	if (CurrentPhase != EGarbageCollectionPhase::Idle)
	{
		return Core::ERuntimeResult::LifecycleLocked;
	}
	if (ObjectStore != nullptr && ObjectStore->CollectorIsMutationLocked())
	{
		return Core::ERuntimeResult::LifecycleLocked;
	}
	const bool bStoreReady = ObjectStore != nullptr && ObjectStore->ConfigurationResult() == EObjectResult::Success;
	const bool bWorklistReady = ObjectStore != nullptr && CollectorStorage.WorklistCapacity >= ObjectStore->CollectorSlotCapacity()
		&& (CollectorStorage.WorklistCapacity == 0 || CollectorStorage.Worklist != nullptr);
	if (!bStoreReady || !bWorklistReady)
	{
		return Core::ERuntimeResult::CapacityExceeded;
	}
	if (!ObjectStore->CollectorTryBegin(*this))
	{
		return Core::ERuntimeResult::LifecycleLocked;
	}
	return Core::ERuntimeResult::Success;
}

FGarbageCollectionResult FGarbageCollector::Advance(const FGarbageCollectionBudget InBudget) noexcept
{
	FGarbageCollectionResult CollectionResult{};
	CollectionResult.Phase = CurrentPhase;
	const Core::ERuntimeResult PreconditionResult = ValidateAdvancePreconditions();
	if (PreconditionResult != Core::ERuntimeResult::Success)
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
			bPhaseAdvanced = AdvanceSeedRootsPhase(InBudget, CollectionResult);
		}
		else if (CurrentPhase == EGarbageCollectionPhase::Mark)
		{
			bPhaseAdvanced = AdvanceMarkPhase(InBudget, CollectionResult);
		}
		else
		{
			bPhaseAdvanced = AdvanceSweepPhase(InBudget, CollectionResult);
		}

		if (bWorklistOverflowed)
		{
			// DiscoverReference aborts the cycle on worklist overflow -- detect that abort here.
			CollectionResult.Result = Core::ERuntimeResult::CapacityExceeded;
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

Core::ERuntimeResult FGarbageCollector::ValidateAdvancePreconditions() const noexcept
{
	if (bAdvanceActive)
	{
		return Core::ERuntimeResult::LifecycleLocked;
	}
	if (ObjectStore != nullptr && ObjectStore->CollectorIsDispatchLocked())
	{
		return Core::ERuntimeResult::LifecycleLocked;
	}
	if (CurrentPhase == EGarbageCollectionPhase::Idle || ObjectStore == nullptr)
	{
		return Core::ERuntimeResult::InvalidLifecycle;
	}
	if (ObjectStore != nullptr && ObjectStore->CollectorIsMutationLocked())
	{
		return Core::ERuntimeResult::LifecycleLocked;
	}
	if (ObjectStore != nullptr && !ObjectStore->CollectorIsOwnedBy(*this))
	{
		return Core::ERuntimeResult::LifecycleLocked;
	}
	return Core::ERuntimeResult::Success;
}

bool FGarbageCollector::AdvanceSeedRootsPhase(const FGarbageCollectionBudget& InBudget, FGarbageCollectionResult& OutResult) noexcept
{
	const std::uint32_t RootCapacity = ObjectStore->CollectorRootCapacity();
	while (!bWorklistOverflowed && RootCursor < RootCapacity && OutResult.RootOperations < InBudget.MaxRootOperations)
	{
		const FObjectHandle RootHandle = ObjectStore->CollectorRootAt(RootCursor);
		++RootCursor;
		++OutResult.RootOperations;
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

bool FGarbageCollector::AdvanceMarkPhase(const FGarbageCollectionBudget& InBudget, FGarbageCollectionResult& OutResult) noexcept
{
	while (!bWorklistOverflowed && WorklistCount > 0 && OutResult.MarkOperations < InBudget.MaxMarkOperations)
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
		++OutResult.MarkOperations;
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

bool FGarbageCollector::AdvanceSweepPhase(const FGarbageCollectionBudget& InBudget, FGarbageCollectionResult& OutResult) noexcept
{
	const std::uint32_t SlotCapacity = ObjectStore->CollectorSlotCapacity();
	while (SweepCursor < SlotCapacity && OutResult.SweepOperations < InBudget.MaxSweepOperations)
	{
		const ObjectIndex SlotIndex = SweepCursor;
		++SweepCursor;
		++OutResult.SweepOperations;
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
			++OutResult.ObjectsReclaimed;
			IncrementSaturated(CollectionStats.ReclaimedObjects);
		}
	}
	if (SweepCursor < SlotCapacity)
	{
		return false;
	}
	FinalizeCompletedCycle(OutResult);
	return true;
}

void FGarbageCollector::FinalizeCompletedCycle(FGarbageCollectionResult& OutResult) noexcept
{
	CurrentPhase = EGarbageCollectionPhase::Idle;
	IncrementSaturated(CollectionStats.CompletedCycles);
	OutResult.bCycleComplete = true;
	CompleteCycle();
}

void FGarbageCollector::AccumulateOperations(FGarbageCollectionResult& OutResult) noexcept
{
	AddSaturated(OutResult.OperationsPerformed, OutResult.RootOperations);
	AddSaturated(OutResult.OperationsPerformed, OutResult.MarkOperations);
	AddSaturated(OutResult.OperationsPerformed, OutResult.SweepOperations);
}

FGarbageCollectionResult FGarbageCollector::CollectFull() noexcept
{
	if (CurrentPhase == EGarbageCollectionPhase::Idle)
	{
		const Core::ERuntimeResult RequestResult = RequestCollection();
		if (RequestResult != Core::ERuntimeResult::Success)
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

Core::ERuntimeResult FGarbageCollector::CancelCollection() noexcept
{
	if (CurrentPhase == EGarbageCollectionPhase::Idle)
	{
		return Core::ERuntimeResult::InvalidLifecycle;
	}
	ResetCycle();
	return Core::ERuntimeResult::Success;
}

bool FGarbageCollector::IsHandleDiscoverable(const FObjectHandle InHandle) const noexcept
{
	const bool bDiscoveryPhase = CurrentPhase == EGarbageCollectionPhase::SeedRoots || CurrentPhase == EGarbageCollectionPhase::Mark;
	if (!bDiscoveryPhase || ObjectStore == nullptr || !ObjectStore->CollectorIsOwnedBy(*this))
	{
		return false;
	}
	return InHandle.IsValid() && InHandle.Index < ObjectStore->CollectorSlotCapacity();
}

bool FGarbageCollector::IsHandleAlreadyProcessed(const FObjectHandle InHandle) const noexcept
{
	const bool bPendingDestroy = ObjectStore->CollectorIsPendingDestroy(InHandle.Index);
	const bool bGenerationMismatch = ObjectStore->CollectorHandleAt(InHandle.Index) != InHandle;
	const bool bAlreadyMarked = ObjectStore->CollectorIsMarked(InHandle.Index);
	return bPendingDestroy || bGenerationMismatch || bAlreadyMarked;
}

void FGarbageCollector::DiscoverReference(const FObjectHandle InHandle) noexcept
{
	if (!IsHandleDiscoverable(InHandle))
	{
		return;
	}
	if (IsHandleAlreadyProcessed(InHandle))
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

	ObjectStore->CollectorSetMarked(InHandle.Index, true);
	CollectorStorage.Worklist[WorklistCount] = InHandle;
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

} // namespace MicroWorld::Engine
