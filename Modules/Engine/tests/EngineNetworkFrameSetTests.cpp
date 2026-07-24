#include "TestSupport.h"

#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/NetworkFrame.h>
#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/ObjectPtr.h>
#include <MicroWorld/Time.h>

#include <cstddef>
#include <cstdint>

namespace
{
using MicroWorld::EEngineResult;
using MicroWorld::ERuntimeResult;
using MicroWorld::FGarbageCollectionBudget;
using MicroWorld::INetworkFrame;
using MicroWorld::TEngineHost;
using MicroWorld::TimePointMilliseconds;
using MicroWorld::TNetworkFrameSet;

/** Host profile sized for a bare rooted world, matching EngineMessageChannelTests.cpp's profile; this suite never spawns actors. */
using FHost = TEngineHost<6, 8, 256, 16, 1, 2, 4, 64>;

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
	/** Number of inbound-dispatch slot invocations observed. */
	int DispatchCount{0};

	/** Number of outbound-flush slot invocations observed. */
	int FlushCount{0};

	/** This frame's stamp from the shared sequence at its most recent dispatch. */
	std::uint32_t DispatchOrder{0};

	/** This frame's stamp from the shared sequence at its most recent flush. */
	std::uint32_t FlushOrder{0};
};

/** A network frame that only records its two slot calls, isolating TNetworkFrameSet's pump order from any real transport. */
class FRecordingNetworkFrame final : public INetworkFrame
{
public:
	/** Binds this stub to the caller-owned record it stamps and the sequence every recording frame in the test shares. */
	FRecordingNetworkFrame(FFrameCallRecord& InRecord, FSharedFrameSequence& InSequence) noexcept : Record(InRecord), Sequence(InSequence) {}

	/** Stamps the inbound-dispatch slot's count and shared-sequence order. */
	void TickDispatch(const TimePointMilliseconds) noexcept override
	{
		++Record.DispatchCount;
		Record.DispatchOrder = Sequence.Next();
	}

	/** Stamps the outbound-flush slot's count and shared-sequence order. */
	void TickFlush(const TimePointMilliseconds) noexcept override
	{
		++Record.FlushCount;
		Record.FlushOrder = Sequence.Next();
	}

private:
	/** Receives this stub's observed slot counts and ordering; never owned here. */
	FFrameCallRecord& Record;

	/** Shared monotonic source every recording frame in the owning test stamps from; never owned here. */
	FSharedFrameSequence& Sequence;
};

} // namespace

/** Proves TNetworkFrameSet's TickDispatch runs its frames in add-order and TickFlush runs them in reverse add-order, called directly. */
MW_TEST_CASE(EngineNetworkFrameSet_TickDispatchRunsAddOrderTickFlushRunsReverseOrder)
{
	FSharedFrameSequence Sequence;
	FFrameCallRecord RecordA{};
	FFrameCallRecord RecordB{};
	FFrameCallRecord RecordC{};
	FRecordingNetworkFrame FrameA{RecordA, Sequence};
	FRecordingNetworkFrame FrameB{RecordB, Sequence};
	FRecordingNetworkFrame FrameC{RecordC, Sequence};

	TNetworkFrameSet<3> Set;
	MW_EXPECT_EQ(Test, EEngineResult::Success, Set.Add(FrameA), "Adding the first frame under capacity must succeed");
	MW_EXPECT_EQ(Test, EEngineResult::Success, Set.Add(FrameB), "Adding the second frame under capacity must succeed");
	MW_EXPECT_EQ(Test, EEngineResult::Success, Set.Add(FrameC), "Adding the third frame under capacity must succeed");

	Set.TickDispatch(10);
	Set.TickFlush(10);

	MW_EXPECT_TRUE(Test, RecordA.DispatchOrder < RecordB.DispatchOrder, "TickDispatch must run the first-added frame before the second");
	MW_EXPECT_TRUE(Test, RecordB.DispatchOrder < RecordC.DispatchOrder, "TickDispatch must run the second-added frame before the third");
	MW_EXPECT_TRUE(Test, RecordC.FlushOrder < RecordB.FlushOrder, "TickFlush must run the third-added frame before the second (reverse add-order)");
	MW_EXPECT_TRUE(Test, RecordB.FlushOrder < RecordA.FlushOrder, "TickFlush must run the second-added frame before the first (reverse add-order)");
	MW_EXPECT_TRUE(
		Test, RecordC.DispatchOrder < RecordC.FlushOrder, "Every added frame's dispatch must complete before any added frame's flush begins");
}

/** Proves TEngineHost::Tick pumps a bound TNetworkFrameSet at its step 1 (dispatch, add-order) and step 7 (flush, reverse add-order). */
MW_TEST_CASE(EngineNetworkFrameSet_TEngineHostTickPumpsBoundSetAtDispatchAndFlushSteps)
{
	FSharedFrameSequence Sequence;
	FFrameCallRecord NetRecord{};
	FFrameCallRecord RouterRecord{};
	FRecordingNetworkFrame NetFrame{NetRecord, Sequence};
	FRecordingNetworkFrame RouterFrame{RouterRecord, Sequence};

	TNetworkFrameSet<2> Set;
	MW_EXPECT_EQ(Test, EEngineResult::Success, Set.Add(NetFrame), "The net-like frame must be added first (D3 order: net before router)");
	MW_EXPECT_EQ(Test, EEngineResult::Success, Set.Add(RouterFrame), "The router-like frame must be added last (D3 order: net before router)");

	FHost Host{FGarbageCollectionBudget{1, 4, 8}, Set};
	MW_EXPECT_TRUE(Test, Host.CreateWorld().Get() != nullptr, "CreateWorld roots the world before the frame-driven tick");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "BeginPlay reports success at the canonical baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(10), "The tick reports success");

	MW_EXPECT_EQ(Test, 1, NetRecord.DispatchCount, "The engine's step 1 must dispatch the net-like frame exactly once");
	MW_EXPECT_EQ(Test, 1, RouterRecord.DispatchCount, "The engine's step 1 must dispatch the router-like frame exactly once");
	MW_EXPECT_EQ(Test, 1, NetRecord.FlushCount, "The engine's step 7 must flush the net-like frame exactly once");
	MW_EXPECT_EQ(Test, 1, RouterRecord.FlushCount, "The engine's step 7 must flush the router-like frame exactly once");
	MW_EXPECT_TRUE(Test, NetRecord.DispatchOrder < RouterRecord.DispatchOrder, "Dispatch must run the net-like frame before the router-like frame");
	MW_EXPECT_TRUE(
		Test, RouterRecord.FlushOrder < NetRecord.FlushOrder, "Flush must run the router-like frame before the net-like frame (reverse add-order)");
	MW_EXPECT_TRUE(Test, RouterRecord.DispatchOrder < RouterRecord.FlushOrder, "Every dispatch this tick must complete before any flush begins");
}

/** An Add past a set's fixed capacity must report CapacityExceeded and leave FrameCount unchanged. */
MW_TEST_CASE(EngineNetworkFrameSet_AddPastCapacityReportsCapacityExceededAndLeavesFrameCountUnchanged)
{
	FSharedFrameSequence Sequence;
	FFrameCallRecord RecordA{};
	FFrameCallRecord RecordB{};
	FFrameCallRecord RecordC{};
	FRecordingNetworkFrame FrameA{RecordA, Sequence};
	FRecordingNetworkFrame FrameB{RecordB, Sequence};
	FRecordingNetworkFrame FrameC{RecordC, Sequence};

	TNetworkFrameSet<2> Set;
	MW_EXPECT_EQ(Test, EEngineResult::Success, Set.Add(FrameA), "The first Add under capacity must succeed");
	MW_EXPECT_EQ(Test, EEngineResult::Success, Set.Add(FrameB), "The second Add under capacity must succeed");
	MW_EXPECT_EQ(Test, EEngineResult::CapacityExceeded, Set.Add(FrameC), "A third Add on a set already at capacity must be rejected");
	MW_EXPECT_EQ(Test, std::size_t{2}, Set.FrameCount(), "A rejected Add must leave FrameCount unchanged");
}

/** Adding the same frame pointer twice must report Duplicate on the second call and count the frame only once. */
MW_TEST_CASE(EngineNetworkFrameSet_AddSameFramePointerTwiceReportsDuplicateAndCountsItOnce)
{
	FSharedFrameSequence Sequence;
	FFrameCallRecord Record{};
	FRecordingNetworkFrame Frame{Record, Sequence};

	TNetworkFrameSet<2> Set;
	MW_EXPECT_EQ(Test, EEngineResult::Success, Set.Add(Frame), "The first Add of a frame must succeed");
	MW_EXPECT_EQ(Test, EEngineResult::Duplicate, Set.Add(Frame), "Adding the same frame pointer again must be rejected as Duplicate");
	MW_EXPECT_EQ(Test, std::size_t{1}, Set.FrameCount(), "A duplicate Add must not grow FrameCount");
}

/** An empty set's TickDispatch and TickFlush must both be inert: no crash, and FrameCount stays zero. */
MW_TEST_CASE(EngineNetworkFrameSet_EmptySetTicksInertly)
{
	TNetworkFrameSet<2> Set;
	MW_EXPECT_EQ(Test, std::size_t{0}, Set.FrameCount(), "A freshly constructed set must start empty");

	Set.TickDispatch(10);
	Set.TickFlush(10);

	MW_EXPECT_EQ(Test, std::size_t{0}, Set.FrameCount(), "Ticking an empty set must not change FrameCount");
}
