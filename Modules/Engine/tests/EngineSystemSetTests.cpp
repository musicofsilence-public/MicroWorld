#include "TestSupport.h"

#include <MicroWorld/EngineSystem.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/EngineSystem.h>
#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/ObjectPtr.h>
#include <MicroWorld/Time.h>

#include <cstddef>
#include <cstdint>

namespace
{
using MicroWorld::EEngineResult;
using MicroWorld::ERuntimeResult;
using MicroWorld::FDefaultEngineTraits;
using MicroWorld::FGarbageCollectionBudget;
using MicroWorld::IEngineSystem;
using MicroWorld::TEngine;
using MicroWorld::TEngineSystemSet;
using MicroWorld::TimePointMilliseconds;

/** Carries the exact capacities FHost sized before the traits refactor, so the test store is unchanged. */
struct FHostTraits : FDefaultEngineTraits
{
	static constexpr std::size_t MaxClasses = 6;
	static constexpr std::size_t MaxObjects = 8;
	static constexpr std::size_t SlotSizeBytes = 256;
	static constexpr std::size_t MaxRoots = 1;
	static constexpr std::size_t MaxActors = 2;
	static constexpr std::size_t MaxTimers = 4;
};

/** Engine profile sized for a bare rooted world, matching EngineMessageChannelTests.cpp's profile; this suite never spawns actors. */
using FHost = TEngine<FHostTraits>;

/** Monotonic call-order source every recording frame in a test stamps from, so several frames' relative order is observable. */
class FSharedFrameSequence final
{
public:
	/** Returns the next stamp, incrementing so no two calls across every sharing frame return the same value. */
	std::uint32_t Next() noexcept { return ++Counter; }

private:
	/** Backing counter; only Next() may advance it. */
	std::uint32_t Counter{0};
};

/** Records how many times one frame's two slots ran and their stamps from a shared sequence. */
struct FFrameCallRecord
{
	/** Number of play-start lifecycle invocations observed. */
	int BeginCount{0};

	/** Number of play-end lifecycle invocations observed. */
	int EndCount{0};

	/** Number of inbound-dispatch slot invocations observed. */
	int DispatchCount{0};

	/** Number of outbound-flush slot invocations observed. */
	int FlushCount{0};

	/** This frame's stamp from the shared sequence at its most recent dispatch. */
	std::uint32_t DispatchOrder{0};

	/** This frame's stamp from the shared sequence at its most recent flush. */
	std::uint32_t FlushOrder{0};

	/** This frame's stamp from the shared sequence at its most recent play start. */
	std::uint32_t BeginOrder{0};

	/** This frame's stamp from the shared sequence at its most recent play end. */
	std::uint32_t EndOrder{0};
};

/** A network frame that only records its two slot calls, isolating TEngineSystemSet's pump order from any real transport. */
class FRecordingEngineSystem final : public IEngineSystem
{
public:
	/** Binds this stub to the caller-owned record it stamps and the sequence every recording frame in the test shares. */
	FRecordingEngineSystem(FFrameCallRecord& InRecord, FSharedFrameSequence& InSequence) noexcept : Record(InRecord), Sequence(InSequence) {}

	/** Stamps the play-start turn so lifecycle add-order is observable. */
	void BeginPlay(const TimePointMilliseconds) noexcept override
	{
		++Record.BeginCount;
		Record.BeginOrder = Sequence.Next();
	}

	/** Stamps the inbound-dispatch slot's count and shared-sequence order. */
	void PreAdvance(const TimePointMilliseconds) noexcept override
	{
		++Record.DispatchCount;
		Record.DispatchOrder = Sequence.Next();
	}

	/** Stamps the outbound-flush slot's count and shared-sequence order. */
	void PostAdvance(const TimePointMilliseconds) noexcept override
	{
		++Record.FlushCount;
		Record.FlushOrder = Sequence.Next();
	}

	/** Stamps the play-end turn so lifecycle reverse add-order is observable. */
	void EndPlay() noexcept override
	{
		++Record.EndCount;
		Record.EndOrder = Sequence.Next();
	}

private:
	/** Receives this stub's observed slot counts and ordering; never owned here. */
	FFrameCallRecord& Record;

	/** Shared monotonic source every recording frame in the owning test stamps from; never owned here. */
	FSharedFrameSequence& Sequence;
};

} // namespace

/**
 * Scenario: Add three recording frames to a system set, then run BeginPlay and EndPlay.
 * Expected: Lifecycle turns preserve add-order at begin and reverse add-order at end.
 */
MW_TEST_CASE(EngineSystemSet_BeginPlayRunsAddOrderAndEndPlayRunsReverseOrder)
{
	// Arrange
	FSharedFrameSequence Sequence;
	FFrameCallRecord RecordA{};
	FFrameCallRecord RecordB{};
	FFrameCallRecord RecordC{};
	FRecordingEngineSystem FrameA{RecordA, Sequence};
	FRecordingEngineSystem FrameB{RecordB, Sequence};
	FRecordingEngineSystem FrameC{RecordC, Sequence};
	TEngineSystemSet<3> SystemSet;

	// Act
	const EEngineResult AddAResult = SystemSet.Add(FrameA);
	const EEngineResult AddBResult = SystemSet.Add(FrameB);
	const EEngineResult AddCResult = SystemSet.Add(FrameC);

	SystemSet.BeginPlay(10);
	SystemSet.EndPlay();

	// Assert
	MW_EXPECT_EQ(Test, EEngineResult::Success, AddAResult, "Adding the first lifecycle system must succeed");
	MW_EXPECT_EQ(Test, EEngineResult::Success, AddBResult, "Adding the second lifecycle system must succeed");
	MW_EXPECT_EQ(Test, EEngineResult::Success, AddCResult, "Adding the third lifecycle system must succeed");
	MW_EXPECT_EQ(Test, 1, RecordA.BeginCount, "Each added system must receive one BeginPlay turn");
	MW_EXPECT_EQ(Test, 1, RecordB.BeginCount, "Each added system must receive one BeginPlay turn");
	MW_EXPECT_EQ(Test, 1, RecordC.BeginCount, "Each added system must receive one BeginPlay turn");
	MW_EXPECT_TRUE(Test, RecordA.BeginOrder < RecordB.BeginOrder, "BeginPlay must run the first-added system before the second");
	MW_EXPECT_TRUE(Test, RecordB.BeginOrder < RecordC.BeginOrder, "BeginPlay must run the second-added system before the third");
	MW_EXPECT_EQ(Test, 1, RecordA.EndCount, "Each added system must receive one EndPlay turn");
	MW_EXPECT_EQ(Test, 1, RecordB.EndCount, "Each added system must receive one EndPlay turn");
	MW_EXPECT_EQ(Test, 1, RecordC.EndCount, "Each added system must receive one EndPlay turn");
	MW_EXPECT_TRUE(Test, RecordC.EndOrder < RecordB.EndOrder, "EndPlay must run the third-added system before the second");
	MW_EXPECT_TRUE(Test, RecordB.EndOrder < RecordA.EndOrder, "EndPlay must run the second-added system before the first");
}

/**
 * Scenario: Add three recording frames to a system set, then call PreAdvance and PostAdvance directly.
 * Expected: TEngineSystemSet's PreAdvance runs its frames in add-order and PostAdvance runs them in reverse add-order, called directly.
 */
MW_TEST_CASE(EngineSystemSet_PreAdvanceRunsAddOrderPostAdvanceRunsReverseOrder)
{
	// Arrange
	FSharedFrameSequence Sequence;
	FFrameCallRecord RecordA{};
	FFrameCallRecord RecordB{};
	FFrameCallRecord RecordC{};
	FRecordingEngineSystem FrameA{RecordA, Sequence};
	FRecordingEngineSystem FrameB{RecordB, Sequence};
	FRecordingEngineSystem FrameC{RecordC, Sequence};
	TEngineSystemSet<3> SystemSet;

	// Act - add three frames under capacity
	MW_EXPECT_EQ(Test, EEngineResult::Success, SystemSet.Add(FrameA), "Adding the first frame under capacity must succeed");
	MW_EXPECT_EQ(Test, EEngineResult::Success, SystemSet.Add(FrameB), "Adding the second frame under capacity must succeed");
	MW_EXPECT_EQ(Test, EEngineResult::Success, SystemSet.Add(FrameC), "Adding the third frame under capacity must succeed");

	// Act - run the dispatch and flush slots directly
	SystemSet.PreAdvance(10);
	SystemSet.PostAdvance(10);

	// Assert
	MW_EXPECT_TRUE(Test, RecordA.DispatchOrder < RecordB.DispatchOrder, "PreAdvance must run the first-added frame before the second");
	MW_EXPECT_TRUE(Test, RecordB.DispatchOrder < RecordC.DispatchOrder, "PreAdvance must run the second-added frame before the third");
	MW_EXPECT_TRUE(Test, RecordC.FlushOrder < RecordB.FlushOrder, "PostAdvance must run the third-added frame before the second (reverse add-order)");
	MW_EXPECT_TRUE(Test, RecordB.FlushOrder < RecordA.FlushOrder, "PostAdvance must run the second-added frame before the first (reverse add-order)");
	MW_EXPECT_TRUE(
		Test, RecordC.DispatchOrder < RecordC.FlushOrder, "Every added frame's dispatch must complete before any added frame's flush begins");
}

/**
 * Scenario: Add two recording frames to a system set, bind it to a host, create the world, begin play, and run a single tick.
 * Expected: TEngine::Tick pumps a bound TEngineSystemSet at its step 1 (dispatch, add-order) and step 7 (flush, reverse add-order).
 */
MW_TEST_CASE(EngineSystemSet_TEngineTickPumpsBoundSetAtPreAdvanceAndPostAdvanceSteps)
{
	// Arrange
	FSharedFrameSequence Sequence;
	FFrameCallRecord NetRecord{};
	FFrameCallRecord RouterRecord{};
	FRecordingEngineSystem NetFrame{NetRecord, Sequence};
	FRecordingEngineSystem RouterFrame{RouterRecord, Sequence};

	TEngineSystemSet<2> SystemSet;

	// Act - add the net-like frame first and the router-like frame last
	MW_EXPECT_EQ(Test, EEngineResult::Success, SystemSet.Add(NetFrame), "The net-like frame must be added first (D3 order: net before router)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, SystemSet.Add(RouterFrame), "The router-like frame must be added last (D3 order: net before router)");

	// Act - build the host, then run a single tick
	FHost Host{FGarbageCollectionBudget{1, 4, 8}, SystemSet};
	MW_EXPECT_TRUE(Test, Host.CreateWorld().Get() != nullptr, "CreateWorld roots the world before the frame-driven tick");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "BeginPlay reports success at the canonical baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(10), "The tick reports success");

	// Assert
	MW_EXPECT_EQ(Test, 1, NetRecord.DispatchCount, "The engine's step 1 must dispatch the net-like frame exactly once");
	MW_EXPECT_EQ(Test, 1, RouterRecord.DispatchCount, "The engine's step 1 must dispatch the router-like frame exactly once");
	MW_EXPECT_EQ(Test, 1, NetRecord.FlushCount, "The engine's step 7 must flush the net-like frame exactly once");
	MW_EXPECT_EQ(Test, 1, RouterRecord.FlushCount, "The engine's step 7 must flush the router-like frame exactly once");
	MW_EXPECT_TRUE(Test, NetRecord.DispatchOrder < RouterRecord.DispatchOrder, "Dispatch must run the net-like frame before the router-like frame");
	MW_EXPECT_TRUE(
		Test, RouterRecord.FlushOrder < NetRecord.FlushOrder, "Flush must run the router-like frame before the net-like frame (reverse add-order)");
	MW_EXPECT_TRUE(Test, RouterRecord.DispatchOrder < RouterRecord.FlushOrder, "Every dispatch this tick must complete before any flush begins");
}

/**
 * Scenario: Add two frames under a capacity-two set, then attempt a third Add past capacity.
 * Expected: An Add past a set's fixed capacity must report CapacityExceeded and leave FrameCount unchanged.
 */
MW_TEST_CASE(EngineSystemSet_AddPastCapacityReportsCapacityExceededAndLeavesFrameCountUnchanged)
{
	// Arrange
	FSharedFrameSequence Sequence;
	FFrameCallRecord RecordA{};
	FFrameCallRecord RecordB{};
	FFrameCallRecord RecordC{};
	FRecordingEngineSystem FrameA{RecordA, Sequence};
	FRecordingEngineSystem FrameB{RecordB, Sequence};
	FRecordingEngineSystem FrameC{RecordC, Sequence};

	TEngineSystemSet<2> SystemSet;

	// Act - add two frames under capacity, then attempt a third past capacity
	MW_EXPECT_EQ(Test, EEngineResult::Success, SystemSet.Add(FrameA), "The first Add under capacity must succeed");
	MW_EXPECT_EQ(Test, EEngineResult::Success, SystemSet.Add(FrameB), "The second Add under capacity must succeed");
	MW_EXPECT_EQ(Test, EEngineResult::CapacityExceeded, SystemSet.Add(FrameC), "A third Add on a set already at capacity must be rejected");

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{2}, SystemSet.FrameCount(), "A rejected Add must leave FrameCount unchanged");
}

/**
 * Scenario: Add one frame to a set, then add the same frame pointer again.
 * Expected: Adding the same frame pointer twice must report Duplicate on the second call and count the frame only once.
 */
MW_TEST_CASE(EngineSystemSet_AddSameFramePointerTwiceReportsDuplicateAndCountsItOnce)
{
	// Arrange
	FSharedFrameSequence Sequence;
	FFrameCallRecord Record{};
	FRecordingEngineSystem Frame{Record, Sequence};

	TEngineSystemSet<2> SystemSet;

	// Act - add the frame, then add the same pointer again
	MW_EXPECT_EQ(Test, EEngineResult::Success, SystemSet.Add(Frame), "The first Add of a frame must succeed");
	MW_EXPECT_EQ(Test, EEngineResult::Duplicate, SystemSet.Add(Frame), "Adding the same frame pointer again must be rejected as Duplicate");

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{1}, SystemSet.FrameCount(), "A duplicate Add must not grow FrameCount");
}

/**
 * Scenario: Construct an empty system set, then call PreAdvance and PostAdvance on it.
 * Expected: An empty set's PreAdvance and PostAdvance must both be inert: no crash, and FrameCount stays zero.
 */
MW_TEST_CASE(EngineSystemSet_EmptySetTicksInertly)
{
	// Arrange
	TEngineSystemSet<2> SystemSet;

	// Assert - a freshly constructed set starts empty
	MW_EXPECT_EQ(Test, std::size_t{0}, SystemSet.FrameCount(), "A freshly constructed set must start empty");

	// Act
	SystemSet.PreAdvance(10);
	SystemSet.PostAdvance(10);

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{0}, SystemSet.FrameCount(), "Ticking an empty set must not change FrameCount");
}
