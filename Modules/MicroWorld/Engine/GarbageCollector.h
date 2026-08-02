#pragma once

#include <MicroWorld/Engine/ObjectHandle.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Core/RuntimeResult.h>
#include <MicroWorld/Core/Time.h>

#include <cstdint>

namespace MicroWorld::Engine
{

class FObjectStore;

/**
 * Motivation: Names the current bounded stage of one explicit mark/sweep cycle so the collector and its callers branch
 *   on progress without re-deriving it from internal state.
 * Responsibilities: Distinguish idle, root-seeding, mark, and sweep phases.
 * Example:
 *   if (Collector.Phase() == EGarbageCollectionPhase::Idle) { Collector.RequestCollection(); }
 */
enum class EGarbageCollectionPhase : std::uint8_t
{
	/** Motivation: Reports that no collection is requested or in progress. */
	Idle,

	/** Motivation: Scans fixed root-table entries before graph traversal begins. */
	SeedRoots,

	/** Motivation: Iteratively traces reachable objects through caller-owned worklist storage. */
	Mark,

	/** Motivation: Inspects fixed object slots and reclaims each unreachable live object. */
	Sweep,
};

/**
 * Motivation: Supplies caller-owned iterative bookkeeping with no collector heap fallback, so collection stays
 *   allocation-free and bounded.
 * Responsibilities: Carry the worklist buffer and its capacity, which must cover the configured object-slot count.
 * Example:
 *   FGarbageCollectorStorage Storage{Worklist.data(), N};
 */
struct FGarbageCollectorStorage
{
	/** Motivation: Holds generation-checked reachable identities awaiting one finite visitor run. */
	FObjectHandle* Worklist{nullptr};

	/** Motivation: Bounds worklist occupancy and must cover the configured object-slot count. */
	std::uint32_t WorklistCapacity{0};
};

/**
 * Motivation: Limits one incremental call by semantic operations rather than hidden time, so collection progress stays
 *   predictable and caller-driven.
 * Responsibilities: Bound root entries scanned, reachable-object visitor executions, and object slots inspected;
 *   reference enqueue and deduplication stay inside the bounded class visitor.
 * Example:
 *   FGarbageCollectionBudget Budget{8, 8, 16};
 *   Collector.Advance(Budget);
 */
struct FGarbageCollectionBudget
{
	/** Motivation: Limits root-table entries inspected while seeding reachability. */
	std::uint32_t MaxRootOperations{0};

	/** Motivation: Limits complete reachable-object visitor executions. */
	std::uint32_t MaxMarkOperations{0};

	/** Motivation: Limits object slots inspected for reclamation. */
	std::uint32_t MaxSweepOperations{0};
};

/**
 * Motivation: Reports exact work and reclamation performed by one collector call so a caller can observe incremental
 *   progress without logging or hidden clocks.
 * Responsibilities: Carry the result, current phase, per-phase and total operation counts, reclaimed count, and
 *   cycle-complete signal for the call.
 * Example:
 *   FGarbageCollectionResult R = Collector.Advance(Budget);
 *   if (R.bCycleComplete) { Done(); }
 */
struct FGarbageCollectionResult
{
	/** Motivation: Reports invalid lifecycle or caller-storage capacity without throwing. */
	Core::ERuntimeResult Result{Core::ERuntimeResult::Success};

	/** Motivation: Exposes the phase waiting for the next caller-provided budget. */
	EGarbageCollectionPhase Phase{EGarbageCollectionPhase::Idle};

	/** Motivation: Reports the sum of root, mark, and sweep operations performed this call. */
	std::uint32_t OperationsPerformed{0};

	/** Motivation: Reports root-table entries inspected during this call. */
	std::uint32_t RootOperations{0};

	/** Motivation: Reports reachable objects whose finite visitor completed during this call. */
	std::uint32_t MarkOperations{0};

	/** Motivation: Reports object slots inspected during this call. */
	std::uint32_t SweepOperations{0};

	/** Motivation: Reports objects reclaimed during this call rather than over the whole cycle. */
	std::uint32_t ObjectsReclaimed{0};

	/** Motivation: Signals the exact call that returned the collector to Idle. */
	bool bCycleComplete{false};
};

/**
 * Motivation: Exposes cumulative collector outcomes without logging or hidden clocks so a caller can observe collector
 *   health over many cycles.
 * Responsibilities: Count completed cycles, reclaimed objects, rejected requests, and worklist overflows.
 * Example:
 *   FGarbageCollectionStats S = Collector.Stats();
 *   if (S.WorklistOverflows > 0) { GrowWorklist(); }
 */
struct FGarbageCollectionStats
{
	/** Motivation: Counts complete explicit collection cycles. */
	std::uint32_t CompletedCycles{0};

	/** Motivation: Counts objects reclaimed across complete and incremental calls. */
	std::uint32_t ReclaimedObjects{0};

	/** Motivation: Counts requests rejected because a cycle was active or storage was invalid. */
	std::uint32_t RejectedRequests{0};

	/** Motivation: Counts traces that could not enqueue a reachable object in caller storage. */
	std::uint32_t WorklistOverflows{0};
};

class FGarbageCollector;

/**
 * Motivation: Presents descriptor-visible handles to the active non-recursive mark traversal so traced references reach
 *   the collector through one narrow type.
 * Responsibilities: Mark and enqueue same-store referenced handles during the active mark phase without exposing a raw
 *   public bypass.
 * Example:
 *   Collector.VisitReferences(RefCollector);
 *   RefCollector.AddReferencedObject(Child);
 */
class FReferenceCollector final
{
public:
	/**
	 * Motivation: Marks one typed traced reference while preserving its generation identity.
	 * Responsibilities: Enqueue the reference's handle only when it belongs to the expected store.
	 */
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

	/**
	 * Motivation: Marks one validated same-store identity without exposing a raw public bypass.
	 * Responsibilities: Forward the handle to the active collector's discovery path.
	 */
	void AddReferencedHandle(FObjectHandle InHandle) noexcept;

	/**
	 * Motivation: Restricts discovery to one active visitor and its owning object store.
	 * Responsibilities: Bind the collector and expected store for the visitor's lifetime.
	 */
	FReferenceCollector(FGarbageCollector& InGarbageCollector, FObjectStore& InStore) noexcept
		: Collector(&InGarbageCollector), ExpectedStore(&InStore)
	{
	}

	/** Motivation: Identifies the collector that owns mark state and worklist capacity. */
	FGarbageCollector* Collector{nullptr};

	/** Motivation: Prevents same-valued handles from another object store entering this graph. */
	FObjectStore* ExpectedStore{nullptr};
};

/**
 * Motivation: Performs explicit-root, non-moving mark/sweep through caller-budgeted slices so collection reclaims
 *   unreachable managed objects without allocating or blocking a whole frame.
 * Responsibilities: Drive seed-roots, mark, and sweep phases within caller budgets, own the active store traversal
 *   exclusively, and stay resumable across calls.
 * Example:
 *   FGarbageCollector Collector(Store, Storage);
 *   (void)Collector.RequestCollection();
 *   Collector.Advance(Budget);
 */
class FGarbageCollector final
{
public:
	/**
	 * Motivation: Binds one object store to caller-owned iterative worklist storage.
	 * Responsibilities: Record the store and worklist storage for the collector's lifetime.
	 */
	FGarbageCollector(FObjectStore& InStore, FGarbageCollectorStorage InStorage) noexcept;

	/**
	 * Motivation: Ensures any active cycle is cancelled and store ownership released before destruction.
	 * Responsibilities: Cancel an in-progress cycle and release the store ownership token.
	 */
	~FGarbageCollector() noexcept;

	/**
	 * Motivation: Preserves the unique store-cycle ownership held by this collector.
	 * Responsibilities: Reject copy construction so one collector owns at most one cycle.
	 */
	FGarbageCollector(const FGarbageCollector&) = delete;

	/**
	 * Motivation: Prevents assigning two collectors to one active store-cycle identity.
	 * Responsibilities: Reject copy assignment so one collector owns at most one cycle.
	 */
	FGarbageCollector& operator=(const FGarbageCollector&) = delete;

	/**
	 * Motivation: Preserves the address registered as the store's active collector.
	 * Responsibilities: Reject move construction so the store's active-collector pointer never dangles.
	 */
	FGarbageCollector(FGarbageCollector&&) = delete;

	/**
	 * Motivation: Prevents moving active cycle state behind the store ownership token.
	 * Responsibilities: Reject move assignment so the store's active-collector pointer never dangles.
	 */
	FGarbageCollector& operator=(FGarbageCollector&&) = delete;

	/**
	 * Motivation: Begins one cycle only while idle and only with sufficient caller storage.
	 * Responsibilities: Reject a busy collector or undersized storage, then acquire store ownership and seed the cycle.
	 */
	Core::ERuntimeResult RequestCollection() noexcept;

	/**
	 * Motivation: Performs no more than each phase's caller-provided operation budget.
	 * Responsibilities: Advance the current phase up to its budget and report the work done.
	 */
	FGarbageCollectionResult Advance(FGarbageCollectionBudget InBudget) noexcept;

	/**
	 * Motivation: Explicitly completes a cycle without imposing a hidden allocation-time collection.
	 * Responsibilities: Run phases to completion, bounded by the supplied budgets, and return the final result.
	 */
	FGarbageCollectionResult CollectFull() noexcept;

	/**
	 * Motivation: Abandons one active cycle, clears marks, and releases public store mutation.
	 * Responsibilities: Reset cycle state and the store ownership token; stay a no-op when idle.
	 */
	Core::ERuntimeResult CancelCollection() noexcept;

	/**
	 * Motivation: Reports the phase that will consume the next relevant budget.
	 * Responsibilities: Return the current phase.
	 */
	EGarbageCollectionPhase Phase() const noexcept { return CurrentPhase; }

	/**
	 * Motivation: Returns cumulative diagnostics without changing collector progress.
	 * Responsibilities: Return the cumulative collection stats.
	 */
	FGarbageCollectionStats Stats() const noexcept { return CollectionStats; }

private:
	friend class FReferenceCollector;

	/**
	 * Motivation: Marks and queues one live non-pending object once during the active cycle.
	 * Responsibilities: Deduplicate the handle and enqueue it when worklist space remains.
	 */
	void DiscoverReference(FObjectHandle InHandle) noexcept;

	/**
	 * Motivation: Reports whether the cycle is in a discovery phase and the handle names a live same-store slot.
	 * Responsibilities: Return true only during mark for a live, same-store handle.
	 */
	bool IsHandleDiscoverable(FObjectHandle InHandle) const noexcept;

	/**
	 * Motivation: Reports whether the handle's slot is already pending, marked, or held under another generation.
	 * Responsibilities: Return true when the slot is already processed so it is not re-enqueued.
	 */
	bool IsHandleAlreadyProcessed(FObjectHandle InHandle) const noexcept;

	/**
	 * Motivation: Clears partial marks/cursors and releases the store after completion or abort.
	 * Responsibilities: Reset phase, cursors, worklist, and store ownership to idle.
	 */
	void ResetCycle() noexcept;

	/**
	 * Motivation: Releases a normally swept cycle without adding unbudgeted slot work.
	 * Responsibilities: Return the collector to Idle and record the completed cycle.
	 */
	void CompleteCycle() noexcept;

	/**
	 * Motivation: Reports why a collection cannot start, or acquires store ownership for the cycle and returns Success.
	 * Responsibilities: Reject a busy cycle or locked store, else reserve the store for this collector.
	 */
	Core::ERuntimeResult ClassifyStartFailure() noexcept;

	/**
	 * Motivation: Rejects an Advance call whose lifecycle or store ownership is not ready.
	 * Responsibilities: Return a result naming why Advance cannot proceed or Success.
	 */
	Core::ERuntimeResult ValidateAdvancePreconditions() const noexcept;

	/**
	 * Motivation: Scans root-table entries into the worklist within the root budget.
	 * Responsibilities: Advance the root cursor and enqueue reachable roots up to the budget.
	 */
	bool AdvanceSeedRootsPhase(const FGarbageCollectionBudget& InBudget, FGarbageCollectionResult& OutResult) noexcept;

	/**
	 * Motivation: Drains reachable objects through their finite visitors within the mark budget.
	 * Responsibilities: Pop worklist entries and trace their references up to the budget.
	 */
	bool AdvanceMarkPhase(const FGarbageCollectionBudget& InBudget, FGarbageCollectionResult& OutResult) noexcept;

	/**
	 * Motivation: Inspects object slots and reclaims unreachable objects within the sweep budget.
	 * Responsibilities: Advance the sweep cursor and reclaim unmarked live slots up to the budget.
	 */
	bool AdvanceSweepPhase(const FGarbageCollectionBudget& InBudget, FGarbageCollectionResult& OutResult) noexcept;

	/**
	 * Motivation: Returns the swept cycle to Idle and records its completion.
	 * Responsibilities: Mark the result cycle-complete and reset the cycle.
	 */
	void FinalizeCompletedCycle(FGarbageCollectionResult& OutResult) noexcept;

	/**
	 * Motivation: Folds the per-phase operation counts into the reported total.
	 * Responsibilities: Sum root, mark, and sweep operations into OperationsPerformed.
	 */
	static void AccumulateOperations(FGarbageCollectionResult& OutResult) noexcept;

	/** Motivation: Identifies the fixed object store whose roots, marks, and slots are traversed. */
	FObjectStore* ObjectStore{nullptr};

	/** Motivation: Holds caller-owned iterative traversal storage without recursion or heap fallback. */
	FGarbageCollectorStorage CollectorStorage{};

	/** Motivation: Exposes the deterministic phase waiting for caller budget. */
	EGarbageCollectionPhase CurrentPhase{EGarbageCollectionPhase::Idle};

	/** Motivation: Resumes root-table scanning without repeating already charged entries. */
	std::uint32_t RootCursor{0};

	/** Motivation: Tracks occupied worklist entries awaiting a finite visitor run. */
	std::uint32_t WorklistCount{0};

	/** Motivation: Resumes fixed-slot sweep without repeating already charged slots. */
	std::uint32_t SweepCursor{0};

	/** Motivation: Retains bounded collector diagnostics across explicit cycles. */
	FGarbageCollectionStats CollectionStats{};

	/** Motivation: Rejects recursive Advance calls from managed reference visitors. */
	bool bAdvanceActive{false};

	/** Motivation: Set by DiscoverReference when the fixed worklist cannot take one more entry, read by Advance at each phase head so it does not
	 * re-derive the abort from CurrentPhase. */
	bool bWorklistOverflowed{false};
};

} // namespace MicroWorld::Engine
