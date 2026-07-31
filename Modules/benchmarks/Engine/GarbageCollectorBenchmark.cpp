#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/ObjectStore.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace
{

/** Motivation: Fixes graph size so collection work and storage evidence remain comparable. */
constexpr std::uint32_t NodeCount = 64;

/** Motivation: Keeps half the graph reachable while leaving one same-sized cycle collectible. */
constexpr std::uint32_t ReachableNodeCount = NodeCount / 2;

/** Motivation: Provides one explicit root for the reachable chain. */
constexpr std::uint32_t RootCapacity = 1;

/** Motivation: Bounds equal-size placement storage independently from object payload size. */
constexpr std::size_t SlotSizeBytes = 128;

/** Motivation: Keeps every fixed slot suitable for the benchmark's managed object. */
constexpr std::size_t SlotAlignmentBytes = 16;

/** Motivation: Bounds each incremental call by observable semantic operations. */
constexpr MicroWorld::Engine::FGarbageCollectionBudget IncrementalBudget{2, 4, 8};

/** Motivation: Prevents a collector regression from turning the benchmark into an unbounded loop. */
constexpr std::uint32_t MaximumIncrementalSlices = 128;

/**
 * Motivation: Records exact managed construction and destruction so the benchmark can validate collection semantics.
 * Responsibilities: Hold the two counters the workload asserts against after one collection cycle.
 * Example:
 *   FBenchmarkLifetimeState Lifetime;
 *   Store.NewObject<FBenchmarkObject>(Descriptor, Lifetime);
 */
struct FBenchmarkLifetimeState final
{
	/** Motivation: Counts placement constructions completed by the store. */
	std::uint32_t ConstructionCount{0};

	/** Motivation: Counts exact descriptor-driven destructor calls. */
	std::uint32_t DestructionCount{0};
};

/**
 * Motivation: Gives the collector one managed object with a bounded outgoing edge so a chain and a cycle can be built from it.
 * Responsibilities: Record construction and destruction into the shared lifetime state and expose one descriptor-visible edge to the collector.
 * Example:
 *   FBenchmarkLifetimeState Lifetime;
 *   FBenchmarkObject Object(Lifetime);
 */
class FBenchmarkObject final : public MicroWorld::Engine::UObject
{
public:
	/**
	 * Motivation: Begins one observable managed lifetime for a graph node.
	 * Responsibilities: Forward to the base and increment the shared construction counter.
	 */
	explicit FBenchmarkObject(FBenchmarkLifetimeState& InState) noexcept : State(InState) { ++State.ConstructionCount; }

	/**
	 * Motivation: Records exact reclamation without allocating or logging.
	 * Responsibilities: Increment the shared destruction counter exactly once.
	 */
	~FBenchmarkObject() noexcept override { ++State.DestructionCount; }

	/**
	 * Motivation: Lets the workload wire the one descriptor-visible edge each node owns.
	 * Responsibilities: Store the next-pointer the collector will trace.
	 */
	void SetNext(const MicroWorld::Engine::TObjectPtr<FBenchmarkObject> InNext) noexcept { Next = InNext; }

protected:
	/**
	 * Motivation: Presents the bounded outgoing edge to the iterative collector.
	 * Responsibilities: Add the stored next-pointer to the collector's reference set.
	 */
	void VisitReferences(MicroWorld::Engine::FReferenceCollector& InCollector) noexcept override { InCollector.AddReferencedObject(Next); }

private:
	/** Motivation: Shares lifetime evidence with the benchmark invocation. */
	FBenchmarkLifetimeState& State;

	/** Motivation: Retains store-qualified generation identity without caching a target address. */
	MicroWorld::Engine::TObjectPtr<FBenchmarkObject> Next{};
};

/**
 * Motivation: Owns one complete fixed store whose storage outlives all managed objects, so the measured work is collection, not allocation.
 * Responsibilities: Hold aligned placement bytes, per-slot metadata, the root token, and the store itself at stable addresses.
 * Example:
 *   FBenchmarkStoreFixture Fixture(MakeClassRegistryView(Registry));
 *   FObjectStore& Store = Fixture.GetStore();
 */
class FBenchmarkStoreFixture final
{
public:
	/**
	 * Motivation: Binds the store to fixed caller-owned placement, metadata, and root storage.
	 * Responsibilities: Construct the FObjectStore over the fixture's stable arrays.
	 */
	explicit FBenchmarkStoreFixture(const MicroWorld::Engine::FClassRegistryView InClasses) noexcept
		: Store(
			  MicroWorld::Engine::FObjectStoreStorage{
				  SlotBytes.data(),
				  SlotBytes.size(),
				  Slots.data(),
				  NodeCount,
				  SlotSizeBytes,
				  SlotAlignmentBytes,
				  Roots.data(),
				  RootCapacity,
			  },
			  InClasses)
	{
	}

	/**
	 * Motivation: Exposes the public store used by each measured collector mode.
	 * Responsibilities: Return a reference to the constructed store without transferring ownership.
	 */
	MicroWorld::Engine::FObjectStore& GetStore() noexcept { return Store; }

private:
	/** Motivation: Provides aligned non-moving placement storage for the complete graph. */
	alignas(SlotAlignmentBytes) std::array<std::byte, SlotSizeBytes * NodeCount> SlotBytes{};

	/** Motivation: Provides one lifecycle and collector record per object slot. */
	std::array<MicroWorld::Engine::FObjectSlotMetadata, NodeCount> Slots{};

	/** Motivation: Provides the single explicit-root token used by the reachable chain. */
	std::array<MicroWorld::Engine::FObjectRootEntry, RootCapacity> Roots{};

	/** Motivation: Owns all managed graph lifetimes while caller-owned storage remains valid. */
	MicroWorld::Engine::FObjectStore Store;
};

/**
 * Motivation: Captures comparable semantics and host-only cost for one collection mode in one value.
 * Responsibilities: Hold the pass flag, slice/operation/reclaim counts, peak slice work, host timing, and post-collection store stats.
 * Example:
 *   FBenchmarkObservation Obs = RunCollection(false);
 *   if (Obs.bPassed) { PrintObservation("full", Obs); }
 */
struct FBenchmarkObservation final
{
	/** Motivation: Reports whether setup and every collection invariant passed. */
	bool bPassed{false};

	/** Motivation: Counts collector calls needed to complete the cycle. */
	std::uint32_t SliceCount{0};

	/** Motivation: Counts root, mark, and sweep operations across the cycle. */
	std::uint32_t OperationsPerformed{0};

	/** Motivation: Counts unreachable cycle nodes reclaimed by the cycle. */
	std::uint32_t ObjectsReclaimed{0};

	/** Motivation: Reports the largest total operation count from one incremental call. */
	std::uint32_t MaximumSliceOperations{0};

	/** Motivation: Records elapsed host time without implying target timing. */
	std::uint64_t HostNanoseconds{0};

	/** Motivation: Preserves fixed-store occupancy and fragmentation after collection. */
	MicroWorld::Engine::FObjectStoreStats StoreStats{};
};

/**
 * Motivation: Constructs one rooted chain and one unreachable cycle in fixed storage so the collector sees a representative graph.
 * Responsibilities: Create every node, wire the reachable chain and the unreachable cycle, and publish the chain root; report failure without partial
 * wiring.
 */
bool BuildRepresentativeGraph(
	MicroWorld::Engine::FObjectStore& InStore,
	const MicroWorld::Engine::FClassDescriptor& InDescriptor,
	FBenchmarkLifetimeState& InLifetime,
	std::array<MicroWorld::Engine::TObjectPtr<FBenchmarkObject>, NodeCount>& InNodes,
	MicroWorld::Engine::TStrongObjectPointerResult<FBenchmarkObject>& OutRoot) noexcept
{
	for (std::uint32_t NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex)
	{
		const MicroWorld::Engine::TObjectCreationResult<FBenchmarkObject> Creation = InStore.NewObject<FBenchmarkObject>(InDescriptor, InLifetime);
		if (Creation.Result != MicroWorld::Engine::EObjectResult::Success)
		{
			return false;
		}
		InNodes[NodeIndex] = Creation.Object;
	}

	for (std::uint32_t NodeIndex = 1; NodeIndex < ReachableNodeCount; ++NodeIndex)
	{
		InNodes[NodeIndex - 1].Get()->SetNext(InNodes[NodeIndex]);
	}
	for (std::uint32_t NodeIndex = ReachableNodeCount; NodeIndex < NodeCount; ++NodeIndex)
	{
		const std::uint32_t NextIndex = NodeIndex + 1 < NodeCount ? NodeIndex + 1 : ReachableNodeCount;
		InNodes[NodeIndex].Get()->SetNext(InNodes[NextIndex]);
	}

	OutRoot = InStore.MakeStrongObjectPtr(InNodes[0]);
	return OutRoot.Result == MicroWorld::Engine::EObjectResult::Success;
}

/**
 * Motivation: Measures one full or incremental cycle over an equivalent fixed graph so the two modes are directly comparable.
 * Responsibilities: Build the graph, run the chosen collector mode under a timing sample, and assert the semantic and budget invariants into bPassed.
 */
FBenchmarkObservation RunCollection(const bool bIncremental) noexcept
{
	using namespace MicroWorld::Core;
	using namespace MicroWorld::Engine;

	FBenchmarkLifetimeState Lifetime{};
	TClassRegistry<1> Registry;
	const FClassDescriptor Descriptor = MakeClassDescriptor<FBenchmarkObject>(1, "BenchmarkObject", nullptr, &TraceManagedObjectReferences);
	const EObjectResult RegistrationResult = Registry.Register(Descriptor);
	const FClassDescriptor* const RegisteredDescriptor = Registry.Find(Descriptor.TypeId);
	FBenchmarkStoreFixture Fixture(MakeClassRegistryView(Registry));
	FObjectStore& Store = Fixture.GetStore();
	std::array<TObjectPtr<FBenchmarkObject>, NodeCount> Nodes{};
	TStrongObjectPointerResult<FBenchmarkObject> Root{};
	const bool bGraphBuilt = RegisteredDescriptor != nullptr && BuildRepresentativeGraph(Store, *RegisteredDescriptor, Lifetime, Nodes, Root);

	std::array<FObjectHandle, NodeCount> Worklist{};
	FGarbageCollector Collector(Store, FGarbageCollectorStorage{Worklist.data(), NodeCount});
	FBenchmarkObservation Observation{};
	if (RegistrationResult != EObjectResult::Success || Store.ConfigurationResult() != EObjectResult::Success || !bGraphBuilt)
	{
		return Observation;
	}

	const std::chrono::steady_clock::time_point StartTime = std::chrono::steady_clock::now();
	if (!bIncremental)
	{
		const FGarbageCollectionResult Result = Collector.CollectFull();
		Observation.SliceCount = 1;
		Observation.OperationsPerformed = Result.OperationsPerformed;
		Observation.ObjectsReclaimed = Result.ObjectsReclaimed;
		Observation.MaximumSliceOperations = Result.OperationsPerformed;
		Observation.bPassed = Result.Result == ERuntimeResult::Success && Result.bCycleComplete;
	}
	else
	{
		Observation.bPassed = Collector.RequestCollection() == ERuntimeResult::Success;
		while (Observation.bPassed && Collector.Phase() != EGarbageCollectionPhase::Idle && Observation.SliceCount < MaximumIncrementalSlices)
		{
			const FGarbageCollectionResult Result = Collector.Advance(IncrementalBudget);
			++Observation.SliceCount;
			Observation.OperationsPerformed += Result.OperationsPerformed;
			Observation.ObjectsReclaimed += Result.ObjectsReclaimed;
			if (Result.OperationsPerformed > Observation.MaximumSliceOperations)
			{
				Observation.MaximumSliceOperations = Result.OperationsPerformed;
			}
			Observation.bPassed = Result.Result == ERuntimeResult::Success;
		}
		Observation.bPassed = Observation.bPassed && Collector.Phase() == EGarbageCollectionPhase::Idle;
	}
	const std::chrono::steady_clock::time_point EndTime = std::chrono::steady_clock::now();
	Observation.HostNanoseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(EndTime - StartTime).count());
	Observation.StoreStats = Store.Stats();

	const std::uint32_t ExpectedReclaimedObjects = NodeCount - ReachableNodeCount;
	Observation.bPassed = Observation.bPassed && Lifetime.ConstructionCount == NodeCount && Lifetime.DestructionCount == ExpectedReclaimedObjects
		&& Observation.ObjectsReclaimed == ExpectedReclaimedObjects && Observation.StoreStats.OccupiedSlots == ReachableNodeCount
		&& Observation.MaximumSliceOperations
			<= (bIncremental ? IncrementalBudget.MaxRootOperations + IncrementalBudget.MaxMarkOperations + IncrementalBudget.MaxSweepOperations
							 : Observation.MaximumSliceOperations);
	return Observation;
}

/**
 * Motivation: Emits deterministic storage/work evidence and host-only timing for one mode as one parseable line.
 * Responsibilities: Format the observation and the fixed sizing constants to stdout without implying target timing.
 */
void PrintObservation(const char* const InMode, const FBenchmarkObservation& InObservation) noexcept
{
	std::printf(
		"%s,passed=%u,nodes=%u,reachable=%u,reclaimed=%u,slices=%u,operations=%u,max_slice_operations=%u,"
		"slot_bytes=%zu,payload_bytes=%zu,fragmentation_bytes=%zu,host_ns=%llu\n",
		InMode,
		InObservation.bPassed ? 1U : 0U,
		NodeCount,
		ReachableNodeCount,
		InObservation.ObjectsReclaimed,
		InObservation.SliceCount,
		InObservation.OperationsPerformed,
		InObservation.MaximumSliceOperations,
		InObservation.StoreStats.SlotSizeBytes * InObservation.StoreStats.SlotCapacity,
		InObservation.StoreStats.ObjectPayloadBytes,
		InObservation.StoreStats.InternalFragmentationBytes,
		static_cast<unsigned long long>(InObservation.HostNanoseconds));
}

} // namespace

/**
 * Motivation: Runs equivalent full and incremental collection workloads over fixed storage and reports evidence plus a pass/fail exit code.
 * Responsibilities: Run both modes, print their observations and the storage sizing, and return non-zero if either mode failed semantic or work
 * equivalence.
 */
int main()
{
	static_assert(sizeof(FBenchmarkObject) <= SlotSizeBytes);
	static_assert(alignof(FBenchmarkObject) <= SlotAlignmentBytes);

	const FBenchmarkObservation FullObservation = RunCollection(false);
	const FBenchmarkObservation IncrementalObservation = RunCollection(true);
	PrintObservation("full_gc_host_only", FullObservation);
	PrintObservation("incremental_gc_host_only", IncrementalObservation);
	std::printf(
		"storage,object_bytes=%zu,slot_storage_bytes=%zu,metadata_bytes=%zu,root_bytes=%zu,worklist_bytes=%zu,"
		"incremental_budget=%u/%u/%u,target_timing_claimed=0\n",
		sizeof(FBenchmarkObject),
		SlotSizeBytes * NodeCount,
		sizeof(MicroWorld::Engine::FObjectSlotMetadata) * NodeCount,
		sizeof(MicroWorld::Engine::FObjectRootEntry) * RootCapacity,
		sizeof(MicroWorld::Engine::FObjectHandle) * NodeCount,
		IncrementalBudget.MaxRootOperations,
		IncrementalBudget.MaxMarkOperations,
		IncrementalBudget.MaxSweepOperations);

	return FullObservation.bPassed && IncrementalObservation.bPassed
			&& FullObservation.OperationsPerformed == IncrementalObservation.OperationsPerformed
			&& FullObservation.ObjectsReclaimed == IncrementalObservation.ObjectsReclaimed
		? 0
		: 1;
}
