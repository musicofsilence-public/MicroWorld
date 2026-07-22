#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Containers/StaticVector.h>
#include <MicroWorld/Delegates/Delegate.h>
#include <MicroWorld/Memory/FixedArena.h>
#include <MicroWorld/Memory/SharedPtr.h>
#include <MicroWorld/Memory/UniquePtr.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

namespace
{

/** Counts host allocations so fixed-resource workloads can expose hidden heap use. */
std::size_t GlobalAllocationCount = 0;

} // namespace

/** Intercepts scalar host allocation solely for benchmark attribution. */
void* operator new(const std::size_t RequestedSize)
{
	++GlobalAllocationCount;
	const std::size_t AllocationSize = RequestedSize == 0 ? 1 : RequestedSize;
	if (void* const Memory = std::malloc(AllocationSize))
	{
		return Memory;
	}
	throw std::bad_alloc();
}

/** Routes array allocation through the same benchmark counter. */
void* operator new[](const std::size_t RequestedSize)
{
	return ::operator new(RequestedSize);
}

/** Completes scalar benchmark allocation without adding another attribution path. */
void operator delete(void* const Memory) noexcept
{
	std::free(Memory);
}

/** Completes array benchmark allocation without adding another attribution path. */
void operator delete[](void* const Memory) noexcept
{
	std::free(Memory);
}

/** Supports implementations that select sized scalar deletion. */
void operator delete(void* const Memory, const std::size_t) noexcept
{
	std::free(Memory);
}

/** Supports implementations that select sized array deletion. */
void operator delete[](void* const Memory, const std::size_t) noexcept
{
	std::free(Memory);
}

namespace
{

/** Fixes the public-operation workload so repeated runs remain comparable. */
constexpr std::uint32_t OperationCount = 100000;

/** Supplies enough caller-owned storage for every isolated pointer workload. */
constexpr std::size_t PointerArenaBytes = 512;

/** Captures the injected allocation activity visible at the Memory boundary. */
class FCountingMemoryResource final : public MicroWorld::IMemoryResource
{
public:
	/** Records an allocation attempt before forwarding it to fixed caller-owned storage. */
	MicroWorld::EMemoryResult TryAllocate(
		const std::size_t SizeBytes, const std::size_t AlignmentBytes, MicroWorld::FMemoryBlock& OutBlock) noexcept override
	{
		++AllocationAttempts;
		LastRequestedBytes = SizeBytes;
		const MicroWorld::EMemoryResult Result = Arena.TryAllocate(SizeBytes, AlignmentBytes, OutBlock);
		if (Result == MicroWorld::EMemoryResult::Success)
		{
			++SuccessfulAllocations;
		}
		return Result;
	}

	/** Records successful exact-block returns while preserving the arena's validation. */
	MicroWorld::EMemoryResult Deallocate(const MicroWorld::FMemoryBlock Block) noexcept override
	{
		const MicroWorld::EMemoryResult Result = Arena.Deallocate(Block);
		if (Result == MicroWorld::EMemoryResult::Success)
		{
			++SuccessfulDeallocations;
		}
		return Result;
	}

	/** Reports fixed caller-usable payload capacity without benchmark counters. */
	std::size_t CapacityBytes() const noexcept override { return Arena.CapacityBytes(); }

	/** Reports active payload bytes attributed to current owners. */
	std::size_t UsedBytes() const noexcept override { return Arena.UsedBytes(); }

	/** Counts every request so failure and success remain distinguishable. */
	std::size_t AllocationAttempts{0};

	/** Counts blocks whose lifetime began successfully. */
	std::size_t SuccessfulAllocations{0};

	/** Counts exact blocks successfully returned to this resource. */
	std::size_t SuccessfulDeallocations{0};

	/** Preserves the most recent requested extent for combined-layout evidence. */
	std::size_t LastRequestedBytes{0};

private:
	/** Owns every byte used by measured custom pointer allocations. */
	MicroWorld::TFixedArena<PointerArenaBytes, alignof(std::max_align_t)> Arena;
};

/** Gives unique/shared workloads observable construction and destruction semantics. */
struct FBenchmarkValue final
{
	/** Retains the expected payload and the counter proving exact destruction. */
	FBenchmarkValue(const std::uint32_t InValue, std::uint32_t& InDestructionCount) noexcept : Value(InValue), DestructionCount(&InDestructionCount)
	{
	}

	/** Makes final-owner behavior visible without allocating or logging. */
	~FBenchmarkValue() noexcept { ++(*DestructionCount); }

	/** Carries one public value checked by every ownership prototype. */
	std::uint32_t Value{0};

	/** Observes caller-owned result state that outlives the measured value. */
	std::uint32_t* DestructionCount{nullptr};
};

/** Recreates the resource-aware standard unique deleter for a direct layout comparison. */
struct FStandardResourceDeleter final
{
	/** Destroys one value before returning its exact block to its originating resource. */
	void operator()(FBenchmarkValue* const Value) noexcept
	{
		if (Value == nullptr)
		{
			return;
		}
		Value->~FBenchmarkValue();
		static_cast<void>(Resource->Deallocate(Allocation));
	}

	/** Identifies the resource selected before standard unique ownership began. */
	MicroWorld::IMemoryResource* Resource{nullptr};

	/** Retains the unchanged block identity required by exact deallocation. */
	MicroWorld::FMemoryBlock Allocation{};
};

/** Names the standard exclusive owner used to compare the thin MicroWorld wrapper. */
using FStandardResourceUniquePtr = std::unique_ptr<FBenchmarkValue, FStandardResourceDeleter>;

/** Attributes one successful standard shared allocation without inducing OOM. */
struct FStandardAllocationCounters final
{
	/** Counts allocator calls made by the standard shared prototype. */
	std::size_t AllocationCount{0};

	/** Counts allocator returns after all standard owners and observers expire. */
	std::size_t DeallocationCount{0};

	/** Records the implementation-selected allocation extent. */
	std::size_t AllocatedBytes{0};
};

/** Supplies a standard-conforming attributed allocator for the shared prototype. */
template<typename ElementType>
class TCountingStandardAllocator final
{
public:
	/** Names the element type required by allocator traits. */
	using value_type = ElementType;

	/** Binds allocations to counters that outlive every rebound allocator copy. */
	explicit TCountingStandardAllocator(FStandardAllocationCounters& InCounters) noexcept : Counters(&InCounters) {}

	/** Preserves attribution when `allocate_shared` rebinds this allocator. */
	template<typename OtherElementType>
	TCountingStandardAllocator(const TCountingStandardAllocator<OtherElementType>& Other) noexcept : Counters(Other.Counters)
	{
	}

	/** Allocates one implementation-selected block through the measured host heap. */
	ElementType* allocate(const std::size_t Count)
	{
		const std::size_t SizeBytes = Count * sizeof(ElementType);
		++Counters->AllocationCount;
		Counters->AllocatedBytes += SizeBytes;
		return static_cast<ElementType*>(::operator new(SizeBytes));
	}

	/** Returns an attributed standard block through the matching host heap. */
	void deallocate(ElementType* const Address, const std::size_t) noexcept
	{
		++Counters->DeallocationCount;
		::operator delete(Address);
	}

	/** Lets allocator traits compare rebound allocators by attribution identity. */
	template<typename OtherElementType>
	bool operator==(const TCountingStandardAllocator<OtherElementType>& Other) const noexcept
	{
		return Counters == Other.Counters;
	}

	/** Distinguishes allocators whose measurement state differs. */
	template<typename OtherElementType>
	bool operator!=(const TCountingStandardAllocator<OtherElementType>& Other) const noexcept
	{
		return !(*this == Other);
	}

	/** Allows rebound allocator copies to preserve the same counters. */
	template<typename>
	friend class TCountingStandardAllocator;

private:
	/** Observes benchmark-owned attribution state for the full shared lifetime. */
	FStandardAllocationCounters* Counters;
};

/** Reports one invariant failure while allowing all benchmark sections to run. */
bool Check(const bool bCondition, const char* const Message) noexcept
{
	if (!bCondition)
	{
		std::printf("failure,%s\n", Message);
	}
	return bCondition;
}

/** Measures elapsed host time around a fixed operation count. */
template<typename OperationType>
std::uint64_t MeasureOperations(OperationType&& Operation) noexcept
{
	const auto StartTime = std::chrono::steady_clock::now();
	for (std::uint32_t OperationIndex = 0; OperationIndex < OperationCount; ++OperationIndex)
	{
		Operation(OperationIndex);
	}
	const auto EndTime = std::chrono::steady_clock::now();
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(EndTime - StartTime).count());
}

/** Compares thin and direct standard unique ownership through the same resource contract. */
bool RunUniquePointerComparison() noexcept
{
	bool bPassed = true;
	std::uint32_t ThinDestructionCount = 0;
	FCountingMemoryResource ThinResource;
	const std::size_t GlobalAllocationsBeforeThin = GlobalAllocationCount;
	{
		auto ThinResult = MicroWorld::MakeUnique<FBenchmarkValue>(ThinResource, 17U, ThinDestructionCount);
		bPassed &= Check(ThinResult.Result == MicroWorld::EMemoryResult::Success, "thin unique construction");
		bPassed &= Check(ThinResult.Pointer.IsValid() && ThinResult.Pointer.Get()->Value == 17U, "thin unique value");
	}
	const std::size_t ThinGlobalAllocationDelta = GlobalAllocationCount - GlobalAllocationsBeforeThin;
	bPassed &= Check(ThinDestructionCount == 1 && ThinResource.UsedBytes() == 0, "thin unique lifetime");
	bPassed &= Check(ThinResource.SuccessfulAllocations == 1 && ThinResource.SuccessfulDeallocations == 1, "thin unique attribution");
	bPassed &= Check(ThinGlobalAllocationDelta == 0, "thin unique hidden allocation");

	std::uint32_t StandardDestructionCount = 0;
	FCountingMemoryResource StandardResource;
	MicroWorld::FMemoryBlock StandardAllocation{};
	const std::size_t GlobalAllocationsBeforeStandard = GlobalAllocationCount;
	const MicroWorld::EMemoryResult AllocationResult =
		StandardResource.TryAllocate(sizeof(FBenchmarkValue), alignof(FBenchmarkValue), StandardAllocation);
	bPassed &= Check(AllocationResult == MicroWorld::EMemoryResult::Success, "standard unique resource allocation");
	if (AllocationResult == MicroWorld::EMemoryResult::Success)
	{
		FBenchmarkValue* const Value = ::new (StandardAllocation.Address) FBenchmarkValue(23U, StandardDestructionCount);
		FStandardResourceUniquePtr Pointer(Value, FStandardResourceDeleter{&StandardResource, StandardAllocation});
		bPassed &= Check(Pointer->Value == 23U, "standard unique value");
	}
	const std::size_t StandardGlobalAllocationDelta = GlobalAllocationCount - GlobalAllocationsBeforeStandard;
	bPassed &= Check(StandardDestructionCount == 1 && StandardResource.UsedBytes() == 0, "standard unique lifetime");
	bPassed &= Check(StandardResource.SuccessfulAllocations == 1 && StandardResource.SuccessfulDeallocations == 1, "standard unique attribution");
	bPassed &= Check(StandardGlobalAllocationDelta == 0, "standard unique hidden allocation");

	std::printf(
		"unique,TUniquePtr_bytes=%zu,std_unique_resource_bytes=%zu,value_bytes=%zu,resource_bytes=%zu,"
		"thin_global_allocations=%zu,std_global_allocations=%zu\n",
		sizeof(MicroWorld::TUniquePtr<FBenchmarkValue>),
		sizeof(FStandardResourceUniquePtr),
		sizeof(FBenchmarkValue),
		ThinResource.LastRequestedBytes,
		ThinGlobalAllocationDelta,
		StandardGlobalAllocationDelta);
	return bPassed;
}

/** Measures custom combined shared storage and validates weak expiry with one allocation. */
bool RunCustomSharedPointerComparison() noexcept
{
	bool bPassed = true;
	std::uint32_t DestructionCount = 0;
	FCountingMemoryResource Resource;
	const std::size_t GlobalAllocationsBefore = GlobalAllocationCount;
	{
		auto SharedResult = MicroWorld::MakeShared<FBenchmarkValue>(Resource, 31U, DestructionCount);
		bPassed &= Check(SharedResult.Result == MicroWorld::ESharedPointerResult::Success, "custom shared construction");
		auto WeakResult = SharedResult.Pointer.TryAcquireWeak();
		auto SecondStrongResult = SharedResult.Pointer.TryShare();
		bPassed &= Check(WeakResult.Result == MicroWorld::ESharedPointerResult::Success, "custom weak acquisition");
		bPassed &= Check(SecondStrongResult.Result == MicroWorld::ESharedPointerResult::Success, "custom strong acquisition");
		SharedResult.Pointer.Reset();
		bPassed &= Check(!WeakResult.Pointer.IsExpired(), "custom weak remains live");
		SecondStrongResult.Pointer.Reset();
		bPassed &= Check(WeakResult.Pointer.IsExpired() && DestructionCount == 1, "custom weak expiry");
		bPassed &= Check(Resource.UsedBytes() > 0, "custom weak retains combined block");
	}
	const std::size_t GlobalAllocationDelta = GlobalAllocationCount - GlobalAllocationsBefore;
	bPassed &= Check(Resource.UsedBytes() == 0, "custom shared combined release");
	bPassed &= Check(Resource.SuccessfulAllocations == 1 && Resource.SuccessfulDeallocations == 1, "custom shared one allocation");
	bPassed &= Check(GlobalAllocationDelta == 0, "custom shared hidden allocation");

	std::printf(
		"custom_shared,TSharedPtr_bytes=%zu,TWeakPtr_bytes=%zu,combined_resource_bytes=%zu,"
		"resource_allocations=%zu,global_allocations=%zu\n",
		sizeof(MicroWorld::TSharedPtr<FBenchmarkValue>),
		sizeof(MicroWorld::TWeakPtr<FBenchmarkValue>),
		Resource.LastRequestedBytes,
		Resource.SuccessfulAllocations,
		GlobalAllocationDelta);
	return bPassed;
}

/** Records the successful standard shared size and allocator attribution prototype. */
bool RunStandardSharedPointerPrototype()
{
	bool bPassed = true;
	std::uint32_t DestructionCount = 0;
	FStandardAllocationCounters Counters;
	const std::size_t GlobalAllocationsBefore = GlobalAllocationCount;
	{
		const TCountingStandardAllocator<FBenchmarkValue> Allocator(Counters);
		std::shared_ptr<FBenchmarkValue> Shared = std::allocate_shared<FBenchmarkValue>(Allocator, 37U, DestructionCount);
		std::weak_ptr<FBenchmarkValue> Weak = Shared;
		bPassed &= Check(Shared->Value == 37U && !Weak.expired(), "standard shared value");
		Shared.reset();
		bPassed &= Check(Weak.expired() && DestructionCount == 1, "standard shared weak expiry");
	}
	const std::size_t GlobalAllocationDelta = GlobalAllocationCount - GlobalAllocationsBefore;
	bPassed &= Check(Counters.AllocationCount == 1 && Counters.DeallocationCount == 1, "standard shared attribution");
	bPassed &= Check(GlobalAllocationDelta == Counters.AllocationCount, "standard shared global attribution");

	std::printf(
		"standard_shared,std_shared_bytes=%zu,std_weak_bytes=%zu,attributed_bytes=%zu,"
		"allocator_allocations=%zu,global_allocations=%zu,exceptions=enabled,no_oom_probe=1\n",
		sizeof(std::shared_ptr<FBenchmarkValue>),
		sizeof(std::weak_ptr<FBenchmarkValue>),
		Counters.AllocatedBytes,
		Counters.AllocationCount,
		GlobalAllocationDelta);
	return bPassed;
}

/** Measures bounded container work and validates its exact semantic total. */
bool RunContainerWorkload() noexcept
{
	bool bPassed = true;
	std::uint64_t VectorSum = 0;
	const std::size_t GlobalAllocationsBeforeVector = GlobalAllocationCount;
	const std::uint64_t VectorNanoseconds = MeasureOperations(
		[&VectorSum](const std::uint32_t OperationIndex) noexcept
		{
			MicroWorld::TStaticVector<std::uint32_t, 4> Values;
			static_cast<void>(Values.Add(OperationIndex));
			static_cast<void>(Values.Add(1U));
			static_cast<void>(Values.Add(2U));
			static_cast<void>(Values.Add(3U));
			for (const std::uint32_t Value : Values)
			{
				VectorSum += Value;
			}
		});
	const std::size_t VectorAllocationDelta = GlobalAllocationCount - GlobalAllocationsBeforeVector;
	const std::uint64_t ExpectedVectorSum =
		(static_cast<std::uint64_t>(OperationCount - 1U) * OperationCount) / 2U + static_cast<std::uint64_t>(OperationCount) * 6U;
	bPassed &= Check(VectorSum == ExpectedVectorSum, "static vector semantic count");
	bPassed &= Check(VectorAllocationDelta == 0, "static vector hidden allocation");

	std::uint32_t SpanValues[]{2U, 3U, 5U, 7U};
	const MicroWorld::TSpan<const std::uint32_t> ValuesView(SpanValues);
	std::uint64_t SpanSum = 0;
	const std::size_t GlobalAllocationsBeforeSpan = GlobalAllocationCount;
	const std::uint64_t SpanNanoseconds = MeasureOperations(
		[&ValuesView, &SpanSum](const std::uint32_t) noexcept
		{
			for (const std::uint32_t Value : ValuesView)
			{
				SpanSum += Value;
			}
		});
	const std::size_t SpanAllocationDelta = GlobalAllocationCount - GlobalAllocationsBeforeSpan;
	bPassed &= Check(SpanSum == static_cast<std::uint64_t>(OperationCount) * 17U, "span semantic count");
	bPassed &= Check(SpanAllocationDelta == 0, "span hidden allocation");

	std::printf(
		"containers,TStaticVector4_bytes=%zu,TSpan_bytes=%zu,operations=%u,vector_ns=%llu,span_ns=%llu,"
		"vector_semantic_sum=%llu,span_semantic_sum=%llu,global_allocations=%zu\n",
		sizeof(MicroWorld::TStaticVector<std::uint32_t, 4>),
		sizeof(MicroWorld::TSpan<const std::uint32_t>),
		static_cast<unsigned int>(OperationCount),
		static_cast<unsigned long long>(VectorNanoseconds),
		static_cast<unsigned long long>(SpanNanoseconds),
		static_cast<unsigned long long>(VectorSum),
		static_cast<unsigned long long>(SpanSum),
		VectorAllocationDelta + SpanAllocationDelta);
	return bPassed;
}

/** Measures single/multicast bounded dispatch and validates exact callback counts. */
bool RunDelegateWorkload() noexcept
{
	bool bPassed = true;
	std::uint64_t SingleCount = 0;
	MicroWorld::TDelegate<void(std::uint32_t), 32> SingleDelegate;
	bPassed &= Check(
		SingleDelegate.Bind([&SingleCount](const std::uint32_t Value) noexcept { SingleCount += Value; }) == MicroWorld::EDelegateResult::Success,
		"single delegate bind");
	const std::size_t GlobalAllocationsBeforeSingle = GlobalAllocationCount;
	const std::uint64_t SingleNanoseconds =
		MeasureOperations([&SingleDelegate](const std::uint32_t) noexcept { static_cast<void>(SingleDelegate.Execute(1U)); });
	const std::size_t SingleAllocationDelta = GlobalAllocationCount - GlobalAllocationsBeforeSingle;
	bPassed &= Check(SingleCount == OperationCount, "single delegate semantic count");
	bPassed &= Check(SingleAllocationDelta == 0, "single delegate hidden allocation");

	std::uint64_t MulticastCount = 0;
	MicroWorld::TMulticastDelegate<void(std::uint32_t), 2, 32> MulticastDelegate;
	MicroWorld::TDelegate<void(std::uint32_t), 32> FirstBinding;
	MicroWorld::TDelegate<void(std::uint32_t), 32> SecondBinding;
	static_cast<void>(FirstBinding.Bind([&MulticastCount](const std::uint32_t Value) noexcept { MulticastCount += Value; }));
	static_cast<void>(SecondBinding.Bind([&MulticastCount](const std::uint32_t Value) noexcept { MulticastCount += Value * 2U; }));
	MicroWorld::FDelegateHandle FirstHandle;
	MicroWorld::FDelegateHandle SecondHandle;
	bPassed &= Check(MulticastDelegate.Add(std::move(FirstBinding), FirstHandle) == MicroWorld::EDelegateResult::Success, "multicast first add");
	bPassed &= Check(MulticastDelegate.Add(std::move(SecondBinding), SecondHandle) == MicroWorld::EDelegateResult::Success, "multicast second add");
	const std::size_t GlobalAllocationsBeforeMulticast = GlobalAllocationCount;
	const std::uint64_t MulticastNanoseconds =
		MeasureOperations([&MulticastDelegate](const std::uint32_t) noexcept { static_cast<void>(MulticastDelegate.Broadcast(1U)); });
	const std::size_t MulticastAllocationDelta = GlobalAllocationCount - GlobalAllocationsBeforeMulticast;
	bPassed &= Check(MulticastCount == static_cast<std::uint64_t>(OperationCount) * 3U, "multicast semantic count");
	bPassed &= Check(MulticastAllocationDelta == 0, "multicast hidden allocation");

	std::printf(
		"delegates,TDelegate32_bytes=%zu,TMulticast2x32_bytes=%zu,operations=%u,single_ns=%llu,"
		"multicast_ns=%llu,single_callbacks=%llu,multicast_callbacks_weighted=%llu,global_allocations=%zu\n",
		sizeof(SingleDelegate),
		sizeof(MulticastDelegate),
		static_cast<unsigned int>(OperationCount),
		static_cast<unsigned long long>(SingleNanoseconds),
		static_cast<unsigned long long>(MulticastNanoseconds),
		static_cast<unsigned long long>(SingleCount),
		static_cast<unsigned long long>(MulticastCount),
		SingleAllocationDelta + MulticastAllocationDelta);
	return bPassed;
}

/** Records fixed arena payload and complete object overhead without private access. */
bool RunArenaLayout() noexcept
{
	using FMeasuredArena = MicroWorld::TFixedArena<256, alignof(std::max_align_t)>;
	FMeasuredArena Arena;
	const std::size_t ArenaObjectBytes = sizeof(FMeasuredArena);
	const std::size_t PayloadBytes = Arena.CapacityBytes();
	const std::size_t ObjectOverheadBytes = ArenaObjectBytes - PayloadBytes;
	const bool bPassed = Check(Arena.UsedBytes() == 0, "fixed arena initial usage");
	std::printf(
		"arena,object_bytes=%zu,payload_bytes=%zu,object_overhead_bytes=%zu,alignment=%zu\n",
		ArenaObjectBytes,
		PayloadBytes,
		ObjectOverheadBytes,
		alignof(FMeasuredArena));
	return bPassed;
}

} // namespace

/** Runs every independent public-API workload and fails on any semantic mismatch. */
int main()
{
	bool bPassed = true;
	bPassed &= RunArenaLayout();
	bPassed &= RunUniquePointerComparison();
	bPassed &= RunCustomSharedPointerComparison();
	bPassed &= RunStandardSharedPointerPrototype();
	bPassed &= RunContainerWorkload();
	bPassed &= RunDelegateWorkload();
	std::printf("summary,passed=%u,total_global_allocations=%zu\n", bPassed ? 1U : 0U, GlobalAllocationCount);
	return bPassed ? 0 : 1;
}
