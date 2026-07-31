#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/ObjectStore.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace
{

/** Fixes graph size so collection work and storage evidence remain comparable. */
constexpr std::uint32_t NodeCount = 64;

/** Keeps half the graph reachable while leaving one same-sized cycle collectible. */
constexpr std::uint32_t ReachableNodeCount = NodeCount / 2;

/** Provides one explicit root for the reachable chain. */
constexpr std::uint32_t RootCapacity = 1;

/** Bounds equal-size placement storage independently from object payload size. */
constexpr std::size_t SlotSizeBytes = 128;

/** Keeps every fixed slot suitable for the benchmark's managed object. */
constexpr std::size_t SlotAlignmentBytes = 16;

/** Bounds each incremental call by observable semantic operations. */
constexpr MicroWorld::Engine::FGarbageCollectionBudget IncrementalBudget{2, 4, 8};

/** Prevents a collector regression from turning the benchmark into an unbounded loop. */
constexpr std::uint32_t MaximumIncrementalSlices = 128;

/** Records exact managed destruction for semantic validation. */
struct FBenchmarkLifetimeState final
{
	/** Counts placement constructions completed by the store. */
	std::uint32_t ConstructionCount{0};

	/** Counts exact descriptor-driven destructor calls. */
	std::uint32_t DestructionCount{0};
};

/** Provides one bounded outgoing edge for chain and cycle workloads. */
class FBenchmarkObject final : public MicroWorld::Engine::UObject
{
public:
	/** Begins one observable managed lifetime. */
	explicit FBenchmarkObject(FBenchmarkLifetimeState& InState) noexcept : State(InState) { ++State.ConstructionCount; }

	/** Records exact reclamation without allocating or logging. */
	~FBenchmarkObject() noexcept override { ++State.DestructionCount; }

	/** Selects the one descriptor-visible edge used by this graph node. */
	void SetNext(const MicroWorld::Engine::TObjectPtr<FBenchmarkObject> InNext) noexcept { Next = InNext; }

protected:
	/** Presents the bounded outgoing edge to the iterative collector. */
	void VisitReferences(MicroWorld::Engine::FReferenceCollector& InCollector) noexcept override { InCollector.AddReferencedObject(Next); }

private:
	/** Shares lifetime evidence with the benchmark invocation. */
	FBenchmarkLifetimeState& State;

	/** Retains store-qualified generation identity without caching a target address. */
	MicroWorld::Engine::TObjectPtr<FBenchmarkObject> Next{};
};

/** Owns one complete fixed store whose storage outlives all managed objects. */
class FBenchmarkStoreFixture final
{
public:
	/** Binds the store to fixed caller-owned placement, metadata, and root storage. */
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

	/** Exposes the public store used by each measured collector mode. */
	MicroWorld::Engine::FObjectStore& GetStore() noexcept { return Store; }

private:
	/** Provides aligned non-moving placement storage for the complete graph. */
	alignas(SlotAlignmentBytes) std::array<std::byte, SlotSizeBytes * NodeCount> SlotBytes{};

	/** Provides one lifecycle and collector record per object slot. */
	std::array<MicroWorld::Engine::FObjectSlotMetadata, NodeCount> Slots{};

	/** Provides the single explicit-root token used by the reachable chain. */
	std::array<MicroWorld::Engine::FObjectRootEntry, RootCapacity> Roots{};

	/** Owns all managed graph lifetimes while caller-owned storage remains valid. */
	MicroWorld::Engine::FObjectStore Store;
};

/** Captures comparable semantics and host-only cost for one collection mode. */
struct FBenchmarkObservation final
{
	/** Reports whether setup and every collection invariant passed. */
	bool bPassed{false};

	/** Counts collector calls needed to complete the cycle. */
	std::uint32_t SliceCount{0};

	/** Counts root, mark, and sweep operations across the cycle. */
	std::uint32_t OperationsPerformed{0};

	/** Counts unreachable cycle nodes reclaimed by the cycle. */
	std::uint32_t ObjectsReclaimed{0};

	/** Reports the largest total operation count from one incremental call. */
	std::uint32_t MaximumSliceOperations{0};

	/** Records elapsed host time without implying target timing. */
	std::uint64_t HostNanoseconds{0};

	/** Preserves fixed-store occupancy and fragmentation after collection. */
	MicroWorld::Engine::FObjectStoreStats StoreStats{};
};

/** Constructs one rooted chain and one unreachable cycle in fixed storage. */
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

/** Measures one full or incremental cycle over an equivalent fixed graph. */
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

/** Emits deterministic storage/work evidence and host-only timing for one mode. */
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

/** Runs equivalent full and incremental collection workloads over fixed storage. */
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
