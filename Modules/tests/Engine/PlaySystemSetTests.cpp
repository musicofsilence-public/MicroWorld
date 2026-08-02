#include "TestSupport.h"

#include <MicroWorld/Core/PlaySystem.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/PlaySystemSet.h>
#include <MicroWorld/Engine/GarbageCollectionBudget.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Engine/World.h>
#include <MicroWorld/Core/Time.h>

#include <cstddef>
#include <cstdint>

namespace
{
using MicroWorld::Core::ERuntimeResult;
using MicroWorld::Core::FTickConfiguration;
using MicroWorld::Core::IPlaySystem;
using MicroWorld::Core::TimePointMilliseconds;
using MicroWorld::Engine::AActor;
using MicroWorld::Engine::EEngineResult;
using MicroWorld::Engine::EObjectResult;
using MicroWorld::Engine::FDefaultEngineTraits;
using MicroWorld::Engine::FGarbageCollectionBudget;
using MicroWorld::Engine::TEngine;
using MicroWorld::Engine::TObjectPtr;
using MicroWorld::Engine::TPlaySystemSet;
using MicroWorld::Engine::UWorld;

/**
 * Motivation: Carries the exact capacities FHost sized before the traits refactor, so the test store is unchanged.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FHostTraits : FDefaultEngineTraits
{
	static constexpr std::size_t MaxClasses = 6;
	static constexpr std::size_t MaxObjects = 8;
	static constexpr std::size_t SlotSizeBytes = 256;
	static constexpr std::size_t MaxRoots = 1;
	static constexpr std::size_t MaxActors = 2;
	static constexpr std::size_t MaxTimers = 4;
};

/** Motivation: Engine profile sized for a bare rooted world, matching EngineMessageChannelTests.cpp's profile. */
using FHost = TEngine<FHostTraits>;

/** Motivation: Stable descriptor id for the world actor that stamps its own lifecycle turns. */
constexpr MicroWorld::Engine::FTypeId LifecycleRecordingActorTypeId{0x00070003u};

/**
 * Motivation: Monotonic call-order source every recording frame in a test stamps from, so several frames' relative
 *   order is observable.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FSharedFrameSequence final
{
public:
	/**
	 * Motivation: No two calls across every sharing frame return the same value.
	 * Responsibilities: Returns the next stamp, incrementing.
	 */
	std::uint32_t Next() noexcept { return ++Counter; }

private:
	/** Motivation: Backing counter; only Next() may advance it. */
	std::uint32_t Counter{0};
};

/**
 * Motivation: Records how many times one frame's two slots ran and their stamps from a shared sequence.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FFrameCallRecord
{
	/** Motivation: Number of play-start lifecycle invocations observed. */
	int BeginCount{0};

	/** Motivation: Number of play-end lifecycle invocations observed. */
	int EndCount{0};

	/** Motivation: Number of inbound-dispatch slot invocations observed. */
	int DispatchCount{0};

	/** Motivation: Number of outbound-flush slot invocations observed. */
	int FlushCount{0};

	/** Motivation: This frame's stamp from the shared sequence at its most recent dispatch. */
	std::uint32_t DispatchOrder{0};

	/** Motivation: This frame's stamp from the shared sequence at its most recent flush. */
	std::uint32_t FlushOrder{0};

	/** Motivation: This frame's stamp from the shared sequence at its most recent play start. */
	std::uint32_t BeginOrder{0};

	/** Motivation: This frame's stamp from the shared sequence at its most recent play end. */
	std::uint32_t EndOrder{0};
};

/**
 * Motivation: A network frame that only records its two slot calls, isolating TPlaySystemSet's pump order from any
 *   real transport.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FRecordingPlaySystem final : public IPlaySystem
{
public:
	/**
	 * Motivation: Binds this stub to the caller-owned record it stamps and the sequence every recording frame in the
	 *   test shares.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FRecordingPlaySystem(FFrameCallRecord& InRecord, FSharedFrameSequence& InSequence) noexcept : Record(InRecord), Sequence(InSequence) {}

	/**
	 * Motivation: Lifecycle add-order is observable.
	 * Responsibilities: Stamps the play-start turn.
	 */
	void BeginPlay(const TimePointMilliseconds) noexcept override
	{
		++Record.BeginCount;
		Record.BeginOrder = Sequence.Next();
	}

	/**
	 * Motivation: Stamps the inbound-dispatch slot's count and shared-sequence order.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void PreAdvance(const TimePointMilliseconds) noexcept override
	{
		++Record.DispatchCount;
		Record.DispatchOrder = Sequence.Next();
	}

	/**
	 * Motivation: Stamps the outbound-flush slot's count and shared-sequence order.
	 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
	 */
	void PostAdvance(const TimePointMilliseconds) noexcept override
	{
		++Record.FlushCount;
		Record.FlushOrder = Sequence.Next();
	}

	/**
	 * Motivation: Lifecycle reverse add-order is observable.
	 * Responsibilities: Stamps the play-end turn.
	 */
	void EndPlay() noexcept override
	{
		++Record.EndCount;
		Record.EndOrder = Sequence.Next();
	}

private:
	/** Motivation: Receives this stub's observed slot counts and ordering; never owned here. */
	FFrameCallRecord& Record;

	/** Motivation: Shared monotonic source every recording frame in the owning test stamps from; never owned here. */
	FSharedFrameSequence& Sequence;
};

/**
 * Motivation: A world actor that stamps its own lifecycle turns from the same sequence a play system stamps from, so
 *   system-versus-world ordering is observable without either side knowing about the other.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FLifecycleRecordingActor final : public AActor
{
public:
	/**
	 * Motivation: Binds this actor to the caller-owned stamps it writes and the sequence the test shares; it never ticks.
	 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
	 */
	FLifecycleRecordingActor(FSharedFrameSequence& InSequence, std::uint32_t& InBeginOrder, std::uint32_t& InEndOrder) noexcept
		: AActor(FTickConfiguration{/*bCanEverTick*/ false, /*bStartWithTickEnabled*/ false, /*TickIntervalMilliseconds*/ 0})
		, Sequence(InSequence)
		, BeginOrder(InBeginOrder)
		, EndOrder(InEndOrder)
	{
	}

protected:
	/**
	 * Motivation: World begin order relative to the bound system is observable.
	 * Responsibilities: Stamps this actor's play-start turn.
	 */
	void BeginPlay() noexcept override { BeginOrder = Sequence.Next(); }

	/**
	 * Motivation: World end order relative to the bound system is observable.
	 * Responsibilities: Stamps this actor's play-end turn.
	 */
	void EndPlay() noexcept override { EndOrder = Sequence.Next(); }

private:
	/** Motivation: Shared monotonic source the bound play system stamps from too; never owned here. */
	FSharedFrameSequence& Sequence;

	/** Motivation: Receives this actor's play-start stamp; never owned here. */
	std::uint32_t& BeginOrder;

	/** Motivation: Receives this actor's play-end stamp; never owned here. */
	std::uint32_t& EndOrder;
};

} // namespace

/**
 * Motivation: Add three recording frames to a system set, then run BeginPlay and EndPlay.
 * Responsibilities: Lifecycle turns preserve add-order at begin and reverse add-order at end.
 */
MW_TEST_CASE(PlaySystemSet_BeginPlayRunsAddOrderAndEndPlayRunsReverseOrder)
{
	// Arrange
	FSharedFrameSequence Sequence;
	FFrameCallRecord RecordA{};
	FFrameCallRecord RecordB{};
	FFrameCallRecord RecordC{};
	FRecordingPlaySystem FrameA{RecordA, Sequence};
	FRecordingPlaySystem FrameB{RecordB, Sequence};
	FRecordingPlaySystem FrameC{RecordC, Sequence};
	TPlaySystemSet<3> SystemSet;

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
 * Motivation: Add three recording frames to a system set, then call PreAdvance and PostAdvance directly.
 * Responsibilities: TPlaySystemSet's PreAdvance runs its frames in add-order and PostAdvance runs them in reverse
 *   add-order, called directly.
 */
MW_TEST_CASE(PlaySystemSet_PreAdvanceRunsAddOrderPostAdvanceRunsReverseOrder)
{
	// Arrange
	FSharedFrameSequence Sequence;
	FFrameCallRecord RecordA{};
	FFrameCallRecord RecordB{};
	FFrameCallRecord RecordC{};
	FRecordingPlaySystem FrameA{RecordA, Sequence};
	FRecordingPlaySystem FrameB{RecordB, Sequence};
	FRecordingPlaySystem FrameC{RecordC, Sequence};
	TPlaySystemSet<3> SystemSet;

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
 * Motivation: Add two recording frames to a system set, bind it to a host, create the world, begin play, and run a
 *   single tick.
 * Responsibilities: TEngine::Tick pumps a bound TPlaySystemSet at its step 1 (dispatch, add-order) and step 7 (flush,
 *   reverse add-order).
 */
MW_TEST_CASE(PlaySystemSet_TEngineTickPumpsBoundSetAtPreAdvanceAndPostAdvanceSteps)
{
	// Arrange
	FSharedFrameSequence Sequence;
	FFrameCallRecord TransportRecord{};
	FFrameCallRecord RouterRecord{};
	FRecordingPlaySystem TransportFrame{TransportRecord, Sequence};
	FRecordingPlaySystem RouterFrame{RouterRecord, Sequence};

	TPlaySystemSet<2> SystemSet;

	// Act - add the transport-like frame first and the router-like frame last
	MW_EXPECT_EQ(
		Test,
		EEngineResult::Success,
		SystemSet.Add(TransportFrame),
		"The transport-like frame must be added first (D3 order: transport before router)");
	MW_EXPECT_EQ(
		Test, EEngineResult::Success, SystemSet.Add(RouterFrame), "The router-like frame must be added last (D3 order: transport before router)");

	// Act - build the host, then run a single tick
	FHost Host{FGarbageCollectionBudget{1, 4, 8}, SystemSet};
	MW_EXPECT_TRUE(Test, Host.CreateWorld().Get() != nullptr, "CreateWorld roots the world before the frame-driven tick");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.BeginPlay(0), "BeginPlay reports success at the canonical baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, Host.Tick(10), "The tick reports success");

	// Assert
	MW_EXPECT_EQ(Test, 1, TransportRecord.DispatchCount, "The engine's step 1 must dispatch the transport-like frame exactly once");
	MW_EXPECT_EQ(Test, 1, RouterRecord.DispatchCount, "The engine's step 1 must dispatch the router-like frame exactly once");
	MW_EXPECT_EQ(Test, 1, TransportRecord.FlushCount, "The engine's step 7 must flush the transport-like frame exactly once");
	MW_EXPECT_EQ(Test, 1, RouterRecord.FlushCount, "The engine's step 7 must flush the router-like frame exactly once");
	MW_EXPECT_TRUE(
		Test, TransportRecord.DispatchOrder < RouterRecord.DispatchOrder, "Dispatch must run the transport-like frame before the router-like frame");
	MW_EXPECT_TRUE(
		Test,
		RouterRecord.FlushOrder < TransportRecord.FlushOrder,
		"Flush must run the router-like frame before the transport-like frame (reverse add-order)");
	MW_EXPECT_TRUE(Test, RouterRecord.DispatchOrder < RouterRecord.FlushOrder, "Every dispatch this tick must complete before any flush begins");
}

/**
 * Motivation: Bind one recording system to a host, root a world holding a lifecycle-recording actor, and drive one full
 *   BeginPlay/EndPlay turn.
 * Responsibilities: The bound system begins before any world actor and ends only after every world actor has ended, so a
 *   system is live for the whole span in which actors can use it.
 */
MW_TEST_CASE(PlaySystemSet_BoundSystemBeginsBeforeTheWorldAndEndsAfterIt)
{
	// Arrange
	FSharedFrameSequence Sequence;
	FFrameCallRecord SystemRecord{};
	FRecordingPlaySystem System{SystemRecord, Sequence};
	std::uint32_t ActorBeginOrder = 0;
	std::uint32_t ActorEndOrder = 0;

	// Act - root a world holding the recording actor, then run one full lifecycle
	FHost Host{FGarbageCollectionBudget{1, 4, 8}, System};
	const EObjectResult RegisterResult = Host.RegisterClass<FLifecycleRecordingActor>(LifecycleRecordingActorTypeId, "LifecycleRecordingActor");
	UWorld* const World = Host.CreateWorld().Get();
	const TObjectPtr<FLifecycleRecordingActor> Actor =
		Host.CreateObject<FLifecycleRecordingActor>(LifecycleRecordingActorTypeId, Sequence, ActorBeginOrder, ActorEndOrder).Object;
	const EEngineResult ActorRegistration = World == nullptr ? EEngineResult::InvalidReference : World->RegisterActor(TObjectPtr<AActor>{Actor});
	const ERuntimeResult BeginResult = Host.BeginPlay(0);
	const ERuntimeResult EndResult = Host.EndPlay();

	// Assert
	MW_EXPECT_EQ(Test, EObjectResult::Success, RegisterResult, "The engine must register the lifecycle-recording actor type");
	MW_EXPECT_EQ(Test, EEngineResult::Success, ActorRegistration, "The world must register the lifecycle-recording actor");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, BeginResult, "BeginPlay reports success at the canonical baseline");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, EndResult, "EndPlay reports success after ending the rooted world");
	MW_EXPECT_EQ(Test, 1, SystemRecord.BeginCount, "The engine must begin the bound system exactly once");
	MW_EXPECT_EQ(Test, 1, SystemRecord.EndCount, "The engine must end the bound system exactly once");
	MW_EXPECT_TRUE(Test, SystemRecord.BeginOrder < ActorBeginOrder, "The bound system must begin before any world actor receives BeginPlay");
	MW_EXPECT_TRUE(Test, ActorEndOrder < SystemRecord.EndOrder, "The bound system must end only after every world actor has received EndPlay");
}

/**
 * Motivation: Add two frames under a capacity-two set, then attempt a third Add past capacity.
 * Responsibilities: An Add past a set's fixed capacity must report CapacityExceeded and leave FrameCount unchanged.
 */
MW_TEST_CASE(PlaySystemSet_AddPastCapacityReportsCapacityExceededAndLeavesFrameCountUnchanged)
{
	// Arrange
	FSharedFrameSequence Sequence;
	FFrameCallRecord RecordA{};
	FFrameCallRecord RecordB{};
	FFrameCallRecord RecordC{};
	FRecordingPlaySystem FrameA{RecordA, Sequence};
	FRecordingPlaySystem FrameB{RecordB, Sequence};
	FRecordingPlaySystem FrameC{RecordC, Sequence};

	TPlaySystemSet<2> SystemSet;

	// Act - add two frames under capacity, then attempt a third past capacity
	MW_EXPECT_EQ(Test, EEngineResult::Success, SystemSet.Add(FrameA), "The first Add under capacity must succeed");
	MW_EXPECT_EQ(Test, EEngineResult::Success, SystemSet.Add(FrameB), "The second Add under capacity must succeed");
	MW_EXPECT_EQ(Test, EEngineResult::CapacityExceeded, SystemSet.Add(FrameC), "A third Add on a set already at capacity must be rejected");

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{2}, SystemSet.FrameCount(), "A rejected Add must leave FrameCount unchanged");
}

/**
 * Motivation: Add one frame to a set, then add the same frame pointer again.
 * Responsibilities: Adding the same frame pointer twice must report Duplicate on the second call and count the frame
 *   only once.
 */
MW_TEST_CASE(PlaySystemSet_AddSameFramePointerTwiceReportsDuplicateAndCountsItOnce)
{
	// Arrange
	FSharedFrameSequence Sequence;
	FFrameCallRecord Record{};
	FRecordingPlaySystem Frame{Record, Sequence};

	TPlaySystemSet<2> SystemSet;

	// Act - add the frame, then add the same pointer again
	MW_EXPECT_EQ(Test, EEngineResult::Success, SystemSet.Add(Frame), "The first Add of a frame must succeed");
	MW_EXPECT_EQ(Test, EEngineResult::Duplicate, SystemSet.Add(Frame), "Adding the same frame pointer again must be rejected as Duplicate");

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{1}, SystemSet.FrameCount(), "A duplicate Add must not grow FrameCount");
}

/**
 * Motivation: Construct an empty system set, then call PreAdvance and PostAdvance on it.
 * Responsibilities: An empty set's PreAdvance and PostAdvance must both be inert: no crash, and FrameCount stays zero.
 */
MW_TEST_CASE(PlaySystemSet_EmptySetTicksInertly)
{
	// Arrange
	TPlaySystemSet<2> SystemSet;

	// Assert - a freshly constructed set starts empty
	MW_EXPECT_EQ(Test, std::size_t{0}, SystemSet.FrameCount(), "A freshly constructed set must start empty");

	// Act
	SystemSet.PreAdvance(10);
	SystemSet.PostAdvance(10);

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{0}, SystemSet.FrameCount(), "Ticking an empty set must not change FrameCount");
}
