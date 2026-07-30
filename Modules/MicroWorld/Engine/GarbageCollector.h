#pragma once

#include <MicroWorld/Engine/ObjectHandle.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Core/Time.h>

#include <cstdint>

namespace MicroWorld
{

class FObjectStore;

/** Identifies the current bounded stage of one explicit mark/sweep cycle. */
enum class EGarbageCollectionPhase : std::uint8_t
{
	/** Reports that no collection is requested or in progress. */
	Idle,

	/** Scans fixed root-table entries before graph traversal begins. */
	SeedRoots,

	/** Iteratively traces reachable objects through caller-owned worklist storage. */
	Mark,

	/** Inspects fixed object slots and reclaims each unreachable live object. */
	Sweep,
};

/** Supplies caller-owned iterative bookkeeping with no collector heap fallback. */
struct FGarbageCollectorStorage
{
	/** Holds generation-checked reachable identities awaiting one finite visitor run. */
	FObjectHandle* Worklist{nullptr};

	/** Bounds worklist occupancy and must cover the configured object-slot count. */
	std::uint32_t WorklistCapacity{0};
};

/**
 * Limits one incremental call by semantic operations rather than hidden time.
 *
 * One operation means one root entry scanned, one reachable object popped with
 * its finite visitor completed, or one object slot inspected; reference
 * enqueue and deduplication stay inside the bounded class visitor.
 */
struct FGarbageCollectionBudget
{
	/** Limits root-table entries inspected while seeding reachability. */
	std::uint32_t MaxRootOperations{0};

	/** Limits complete reachable-object visitor executions. */
	std::uint32_t MaxMarkOperations{0};

	/** Limits object slots inspected for reclamation. */
	std::uint32_t MaxSweepOperations{0};
};

/** Reports exact work and reclamation performed by one collector call. */
struct FGarbageCollectionResult
{
	/** Reports invalid lifecycle or caller-storage capacity without throwing. */
	ERuntimeResult Result{ERuntimeResult::Success};

	/** Exposes the phase waiting for the next caller-provided budget. */
	EGarbageCollectionPhase Phase{EGarbageCollectionPhase::Idle};

	/** Reports the sum of root, mark, and sweep operations performed this call. */
	std::uint32_t OperationsPerformed{0};

	/** Reports root-table entries inspected during this call. */
	std::uint32_t RootOperations{0};

	/** Reports reachable objects whose finite visitor completed during this call. */
	std::uint32_t MarkOperations{0};

	/** Reports object slots inspected during this call. */
	std::uint32_t SweepOperations{0};

	/** Reports objects reclaimed during this call rather than over the whole cycle. */
	std::uint32_t ObjectsReclaimed{0};

	/** Signals the exact call that returned the collector to Idle. */
	bool bCycleComplete{false};
};

/** Exposes cumulative collector outcomes without logging or hidden clocks. */
struct FGarbageCollectionStats
{
	/** Counts complete explicit collection cycles. */
	std::uint32_t CompletedCycles{0};

	/** Counts objects reclaimed across complete and incremental calls. */
	std::uint32_t ReclaimedObjects{0};

	/** Counts requests rejected because a cycle was active or storage was invalid. */
	std::uint32_t RejectedRequests{0};

	/** Counts traces that could not enqueue a reachable object in caller storage. */
	std::uint32_t WorklistOverflows{0};
};

class FGarbageCollector;

/** Presents descriptor-visible handles to the active non-recursive mark traversal. */
class FReferenceCollector final
{
public:
	/** Marks one typed traced reference while preserving its generation identity. */
	template<typename T>
	void AddReferencedObject(const TObjectPtr<T> InObject) noexcept
	{
		if (ExpectedStore != nullptr && InObject.BelongsTo(*ExpectedStore))
		{
			AddReferencedHandle(InObject.Handle());
		}
	}

private:
	friend class FGarbageCollector;

	/** Marks one validated same-store identity without exposing a raw public bypass. */
	void AddReferencedHandle(FObjectHandle InHandle) noexcept;

	/** Restricts discovery to one active visitor and its owning object store. */
	FReferenceCollector(FGarbageCollector& InGarbageCollector, FObjectStore& InStore) noexcept
		: Collector(&InGarbageCollector), ExpectedStore(&InStore)
	{
	}

	/** Identifies the collector that owns mark state and worklist capacity. */
	FGarbageCollector* Collector{nullptr};

	/** Prevents same-valued handles from another object store entering this graph. */
	FObjectStore* ExpectedStore{nullptr};
};

/** Performs explicit-root, non-moving mark/sweep through caller-budgeted slices. */
class FGarbageCollector final
{
public:
	/** Binds one object store to caller-owned iterative worklist storage. */
	FGarbageCollector(FObjectStore& InStore, FGarbageCollectorStorage InStorage) noexcept;

	/** Cancels any active cycle and releases store ownership before destruction. */
	~FGarbageCollector() noexcept;

	/** Preserves the unique store-cycle ownership held by this collector. */
	FGarbageCollector(const FGarbageCollector&) = delete;

	/** Prevents assigning two collectors to one active store-cycle identity. */
	FGarbageCollector& operator=(const FGarbageCollector&) = delete;

	/** Preserves the address registered as the store's active collector. */
	FGarbageCollector(FGarbageCollector&&) = delete;

	/** Prevents moving active cycle state behind the store ownership token. */
	FGarbageCollector& operator=(FGarbageCollector&&) = delete;

	/** Begins one cycle only while idle and only with sufficient caller storage. */
	ERuntimeResult RequestCollection() noexcept;

	/** Performs no more than each phase's caller-provided operation budget. */
	FGarbageCollectionResult Advance(FGarbageCollectionBudget InBudget) noexcept;

	/** Explicitly completes a cycle without imposing a hidden allocation-time collection. */
	FGarbageCollectionResult CollectFull() noexcept;

	/** Abandons one active cycle, clears marks, and releases public store mutation. */
	ERuntimeResult CancelCollection() noexcept;

	/** Reports the phase that will consume the next relevant budget. */
	EGarbageCollectionPhase Phase() const noexcept { return CurrentPhase; }

	/** Returns cumulative diagnostics without changing collector progress. */
	FGarbageCollectionStats Stats() const noexcept { return CollectionStats; }

private:
	friend class FReferenceCollector;

	/** Marks and queues one live non-pending object once during the active cycle. */
	void DiscoverReference(FObjectHandle InHandle) noexcept;

	/** Reports whether the cycle is in a discovery phase and the handle names a live same-store slot. */
	bool IsHandleDiscoverable(FObjectHandle InHandle) const noexcept;

	/** Reports whether the handle's slot is already pending, marked, or held under another generation. */
	bool IsHandleAlreadyProcessed(FObjectHandle InHandle) const noexcept;

	/** Clears partial marks/cursors and releases the store after completion or abort. */
	void ResetCycle() noexcept;

	/** Releases a normally swept cycle without adding unbudgeted slot work. */
	void CompleteCycle() noexcept;

	/** Reports why a collection cannot start, or acquires store ownership for the cycle and returns Success. */
	ERuntimeResult ClassifyStartFailure() noexcept;

	/** Rejects an Advance call whose lifecycle or store ownership is not ready. */
	ERuntimeResult ValidateAdvancePreconditions() const noexcept;

	/** Scans root-table entries into the worklist within the root budget. */
	bool AdvanceSeedRootsPhase(const FGarbageCollectionBudget& InBudget, FGarbageCollectionResult& OutResult) noexcept;

	/** Drains reachable objects through their finite visitors within the mark budget. */
	bool AdvanceMarkPhase(const FGarbageCollectionBudget& InBudget, FGarbageCollectionResult& OutResult) noexcept;

	/** Inspects object slots and reclaims unreachable objects within the sweep budget. */
	bool AdvanceSweepPhase(const FGarbageCollectionBudget& InBudget, FGarbageCollectionResult& OutResult) noexcept;

	/** Returns the swept cycle to Idle and records its completion. */
	void FinalizeCompletedCycle(FGarbageCollectionResult& OutResult) noexcept;

	/** Folds the per-phase operation counts into the reported total. */
	static void AccumulateOperations(FGarbageCollectionResult& OutResult) noexcept;

	/** Identifies the fixed object store whose roots, marks, and slots are traversed. */
	FObjectStore* ObjectStore{nullptr};

	/** Holds caller-owned iterative traversal storage without recursion or heap fallback. */
	FGarbageCollectorStorage CollectorStorage{};

	/** Exposes the deterministic phase waiting for caller budget. */
	EGarbageCollectionPhase CurrentPhase{EGarbageCollectionPhase::Idle};

	/** Resumes root-table scanning without repeating already charged entries. */
	std::uint32_t RootCursor{0};

	/** Tracks occupied worklist entries awaiting a finite visitor run. */
	std::uint32_t WorklistCount{0};

	/** Resumes fixed-slot sweep without repeating already charged slots. */
	std::uint32_t SweepCursor{0};

	/** Retains bounded collector diagnostics across explicit cycles. */
	FGarbageCollectionStats CollectionStats{};

	/** Rejects recursive Advance calls from managed reference visitors. */
	bool bAdvanceActive{false};

	/** Set by DiscoverReference when the fixed worklist cannot take one more entry; Advance reads it at each phase head instead of re-deriving the
	 * abort from CurrentPhase. */
	bool bWorklistOverflowed{false};
};

} // namespace MicroWorld
