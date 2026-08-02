#pragma once

#include <MicroWorld/Core/RuntimeResult.h>
#include <MicroWorld/Engine/GarbageCollectionBudget.h>
#include <MicroWorld/Engine/GarbageCollectionPhase.h>
#include <MicroWorld/Engine/GarbageCollectionResult.h>
#include <MicroWorld/Engine/GarbageCollectionStats.h>
#include <MicroWorld/Engine/GarbageCollectorStorage.h>
#include <MicroWorld/Engine/ObjectHandle.h>

#include <cstdint>

namespace MicroWorld::Engine
{

class FObjectStore;
class FReferenceCollector;

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
