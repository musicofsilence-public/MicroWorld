// Esp32BenchmarkMain.cpp — Phase 6.2 Part A on-target runtime-margin harness.
//
// This is the COMPILE-ONLY harness for §6.2: it builds the representative world
// (8 actors / 16 components / 8 timers), a standalone GC probe store, and a
// no-traffic transport pump, then prints labeled measurement lines over serial at
// 115200. Part B (a separate, human-authorized flash) captures the real numbers;
// this image is never flashed or run on hardware as part of Part A.
//
// Measurements (each labeled for direct transcription):
//   1. Tick duration — Host.Tick over 1000 iterations (min/mean/max us).
//   2. GC pause per budget unit — isolated Advance slice on a standalone
//      FObjectStore + FGarbageCollector (min/mean/max us per slice).
//   3. Transport pump cost — PumpReceive + PumpSend with NO netif/traffic (mean us).
//   4. Memory — free heap before/after setup, stack high-water mark after setup.
//
// GC-slice isolation uses a SEPARATE bounded store + collector, not the host's
// embedded collector (which is private). This measures the exact public-API
// cost of one bounded Advance slice, the unit the roadmap asks for, without
// adding a GetCollector() accessor that would widen engine API surface.

#include <MicroWorld/Core/Delegates/Delegate.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/EngineSystem.h>
#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/TransportHost.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/GarbageCollector.h>
#include <MicroWorld/Engine/Object.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Platform/Esp32/Esp32OutputDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiDevice.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Core/Timer.h>

#include <esp_event.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_timer.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

/** Motivation: ESP-IDF log tag printed with every harness measurement line. */
constexpr const char* BenchmarkTag = "mwbench";

/** Motivation: Stable type id for the benchmark's user-derived managed actor descriptor. */
constexpr MicroWorld::Engine::FTypeId BenchActorTypeId{0x00060010u};

/** Motivation: Stable type id for the benchmark's user-derived managed component descriptor. */
constexpr MicroWorld::Engine::FTypeId BenchComponentTypeId{0x00060011u};

/** Motivation: Stable type id for the standalone GC probe's unrooted garbage object descriptor. */
constexpr MicroWorld::Engine::FTypeId GcProbeObjectTypeId{0x00060012u};

/** Motivation: Representative world profile: actors tick every frame, components tick every frame. */
constexpr MicroWorld::Core::FTickConfiguration BenchTickConfiguration{true, true, MicroWorld::Core::DurationMilliseconds{0}};

/** Motivation: Representative actor count the roadmap names for the runtime-margin profile. */
constexpr std::size_t RepresentativeActorCount = 8;

/** Motivation: Representative component count (two per actor) the roadmap names for the profile. */
constexpr std::size_t RepresentativeComponentCount = 16;

/** Motivation: Representative timer count the roadmap names for the profile. */
constexpr std::size_t RepresentativeTimerCount = 8;

/** Motivation: Iterations timed for the steady-state tick measurement (labeled on every result line). */
constexpr std::uint32_t TickMeasurementIterations = 1000;

/** Motivation: Warm-up ticks before timing so caches and branch prediction settle. */
constexpr std::uint32_t TickWarmupIterations = 100;

/** Motivation: Iterations timed for the no-traffic transport pump measurement. */
constexpr std::uint32_t TransportPumpMeasurementIterations = 1000;

/** Motivation: Warm-up pump cycles before timing so the device and host settle. */
constexpr std::uint32_t TransportPumpWarmupIterations = 100;

/**
 * Motivation: Concrete managed component representative of steady-state per-frame component work.
 * Responsibilities: Carry a zero-interval tick so every frame ticks the representative component population.
 * Example:
 *   Host.RegisterClass<FBenchComponent>(BenchComponentTypeId, "BenchComponent");
 */
class FBenchComponent final : public MicroWorld::Engine::UActorComponent
{
public:
	/**
	 * Motivation: Adopts the representative always-tick schedule so each frame exercises the component.
	 * Responsibilities: Forward the always-tick configuration to the component base.
	 */
	FBenchComponent() noexcept : UActorComponent(BenchTickConfiguration) {}

	/**
	 * Motivation: Keeps exact descriptor-driven destruction publicly instantiable.
	 * Responsibilities: Keep the destructor defaulted so the descriptor's destroy path can call it.
	 */
	~FBenchComponent() noexcept override = default;
};

/**
 * Motivation: Concrete managed actor representative of steady-state per-frame actor work.
 * Responsibilities: Own bounded component slots directly, mirroring the proven PlatformEsp32Main composition.
 * Example:
 *   Host.RegisterClass<FBenchActor>(BenchActorTypeId, "BenchActor");
 */
class FBenchActor final : public MicroWorld::Engine::AActor
{
public:
	/**
	 * Motivation: Initializes the managed actor base, which owns its bounded component slots.
	 * Responsibilities: Forward to the actor base so its component slots are ready before registration.
	 */
	explicit FBenchActor() noexcept : AActor() {}

	/**
	 * Motivation: Keeps exact descriptor-driven destruction publicly instantiable.
	 * Responsibilities: Keep the destructor defaulted so the descriptor's destroy path can call it.
	 */
	~FBenchActor() noexcept override = default;
};

/**
 * Motivation: Concrete managed object used as unrooted garbage in the standalone GC probe.
 * Responsibilities: Construct into every probe slot so the collector reclaims all but the one rooted survivor across multiple bounded slices.
 * Example:
 *   Host.RegisterClass<FGcProbeObject>(GcProbeObjectTypeId, "GcProbeObject");
 */
class FGcProbeObject final : public MicroWorld::Engine::UObject
{
public:
	/**
	 * Motivation: Keeps exact descriptor-driven destruction publicly instantiable.
	 * Responsibilities: Keep the destructor defaulted so the descriptor's destroy path can call it.
	 */
	~FGcProbeObject() noexcept override = default;
};

/**
 * Motivation: Carries the exact capacities FBenchmarkHost sized before the traits refactor, so the benchmark store is unchanged.
 * Responsibilities: Override the trait constants the engine template sizes its fixed storage from.
 * Example:
 *   using FBenchmarkHost = MicroWorld::Engine::TEngine<FBenchmarkHostTraits>;
 */
struct FBenchmarkHostTraits : MicroWorld::Engine::FDefaultEngineTraits
{
	static constexpr std::size_t MaxClasses = 6;					   // UWorld + AActor + UActorComponent + 2 user types + 1 spare.
	static constexpr std::size_t MaxObjects = 32;					   // 1 world + 8 actors + 16 components = 25 live; +7 headroom.
	static constexpr std::size_t SlotSizeBytes = 256;				   // proven actor slot width (PlatformEsp32Main / EngineHostTests).
	static constexpr std::size_t MaxRoots = 1;						   // the single rooted world.
	static constexpr std::size_t MaxActors = RepresentativeActorCount; // the representative actor count.
	static constexpr std::size_t MaxTimers = RepresentativeTimerCount; // the representative timer count.
};

/** Motivation: Sizes the engine to hold the representative world with bounded headroom. */
using FBenchmarkHost = MicroWorld::Engine::TEngine<FBenchmarkHostTraits>;

/** Motivation: Dedicated server transport host sized identically to the PlatformEsp32Main proof. */
using FBenchmarkTransport = MicroWorld::Transport::TTransportHost<4, 256>;

/** Motivation: Delegate type matching the host's timer manager so Schedule accepts a bound callback. */
using FBenchTimerDelegate = MicroWorld::Core::TDelegate<void(), 64>;

/**
 * Motivation: Accumulates min/mean/max across one timed operation's repeated samples.
 * Responsibilities: Store only fixed scalars and accumulate the sum so 1000 iterations cannot overflow.
 * Example:
 *   FBenchStats Stats;
 *   Stats.Record(End - Begin);
 */
struct FBenchStats
{
	/** Motivation: Smallest single-sample duration observed, or zero before the first sample. */
	std::int64_t MinMicroseconds{0};

	/** Motivation: Largest single-sample duration observed, or zero before the first sample. */
	std::int64_t MaxMicroseconds{0};

	/** Motivation: Running sum of all sample durations, divided by Count for the mean. */
	std::uint64_t SumMicroseconds{0};

	/** Motivation: Number of samples accumulated; drives the mean denominator. */
	std::uint32_t Count{0};

	/** Motivation: True once at least one sample has set Min and Max away from their zero initial state. */
	bool bSeeded{false};

	/**
	 * Motivation: Records one sample and updates min/sum/count, seeding min/max on the first call.
	 * Responsibilities: Fold the sample into the running min/max/sum/count without allocating.
	 */
	void Record(const std::int64_t Microseconds) noexcept
	{
		SumMicroseconds += static_cast<std::uint64_t>(Microseconds);
		++Count;
		if (!bSeeded)
		{
			MinMicroseconds = Microseconds;
			MaxMicroseconds = Microseconds;
			bSeeded = true;
		}
		else
		{
			if (Microseconds < MinMicroseconds)
			{
				MinMicroseconds = Microseconds;
			}
			if (Microseconds > MaxMicroseconds)
			{
				MaxMicroseconds = Microseconds;
			}
		}
	}

	/**
	 * Motivation: Returns the arithmetic mean, or zero when no samples were recorded.
	 * Responsibilities: Divide the accumulated sum by the sample count, or report zero with no samples.
	 */
	std::int64_t MeanMicroseconds() const noexcept { return Count == 0 ? 0 : static_cast<std::int64_t>(SumMicroseconds / Count); }
};

/** Motivation: Retains the harness outcome so optimization cannot erase the representative calls. */
volatile int BenchmarkSinkResult = -1;

/**
 * Motivation: Standalone bounded store plus collector used to isolate one GC Advance slice.
 * Responsibilities: Force a multi-slice cycle (sweep budget below slot count) so each Advance call is one measurable pause, with one rooted survivor
 * and the rest reclaimed slice by slice, declaring backing storage as members and constructing the store/collector eagerly in declaration order.
 * Example:
 *   static FGcProbe GcProbe;
 *   GcProbe.AdvanceOneSlice(bCycleComplete);
 */
class FGcProbe final
{
public:
	/** Motivation: Slot count chosen to force a multi-slice sweep at the probe's budget. */
	static constexpr std::uint32_t SlotCount = 32;

	/** Motivation: Root capacity for the single rooted object that survives the cycle. */
	static constexpr std::uint32_t RootCapacity = 1;

	/** Motivation: Probe budget: a sweep budget below SlotCount produces measurable per-slice pauses. */
	static constexpr MicroWorld::Engine::FGarbageCollectionBudget ProbeBudget{1, 1, 8};

	/** Motivation: Slot extent matching the largest probe object, rounded to the slot alignment. */
	static constexpr std::size_t SlotSizeBytes = 128;

	/** Motivation: Power-of-two slot alignment matching the probe object's requirement. */
	static constexpr std::size_t SlotAlignmentBytes = 16;

	/**
	 * Motivation: Builds the registry, store, collector, and rooted survivor the timing loop traces.
	 * Responsibilities: Populate every slot, root exactly one, and reach a ready-to-measure state or leave bReady false.
	 */
	FGcProbe() noexcept
		: Store(MakeStorage(), MicroWorld::Engine::MakeClassRegistryView(Registry))
		, Collector(Store, MicroWorld::Engine::FGarbageCollectorStorage{Worklist.data(), SlotCount})
	{
		if (Store.ConfigurationResult() != MicroWorld::Engine::EObjectResult::Success)
		{
			return;
		}

		// Register the probe's single type against the already-constructed store.
		(void)Registry.Register(MicroWorld::Engine::MakeClassDescriptor<FGcProbeObject>(
			GcProbeObjectTypeId, "GcProbeObject", nullptr, &MicroWorld::Engine::TraceManagedObjectReferences));
		const MicroWorld::Engine::FClassDescriptor* const Descriptor = Registry.Find(GcProbeObjectTypeId);
		if (Descriptor == nullptr)
		{
			return;
		}

		// Populate every slot; root exactly one so the others are true garbage.
		for (std::uint32_t Index = 0; Index < SlotCount; ++Index)
		{
			const MicroWorld::Engine::TObjectCreationResult<FGcProbeObject> Creation = Store.NewObject<FGcProbeObject>(*Descriptor);
			if (Creation.Result != MicroWorld::Engine::EObjectResult::Success)
			{
				return;
			}
			if (Index == 0)
			{
				// Non-const so std::move selects the move-assign rather than the deleted copy-assign.
				MicroWorld::Engine::TStrongObjectPointerResult<FGcProbeObject> RootResult = Store.MakeStrongObjectPtr(Creation.Object);
				if (RootResult.Result != MicroWorld::Engine::EObjectResult::Success)
				{
					return;
				}
				Root = std::move(RootResult.Pointer);
			}
		}
		bReady = true;
	}

	/**
	 * Motivation: Prevents copying the fixed storage arrays and the single store identity.
	 * Responsibilities: Delete the copy constructor so two probes cannot alias the same store.
	 */
	FGcProbe(const FGcProbe&) = delete;
	/**
	 * Motivation: Prevents assigning the fixed storage arrays and the single store identity.
	 * Responsibilities: Delete the copy-assignment operator so a probe cannot be reassigned over another store.
	 */
	FGcProbe& operator=(const FGcProbe&) = delete;

	/**
	 * Motivation: Reports whether construction populated the store, collector, and rooted survivor.
	 * Responsibilities: Return the ready flag set only when construction fully succeeded.
	 */
	bool IsReady() const noexcept { return bReady; }

	/**
	 * Motivation: Begins a fresh collection cycle so the timing loop starts from a known state.
	 * Responsibilities: Request a collection cycle and report whether the collector accepted it.
	 */
	bool StartCycle() noexcept { return Collector.RequestCollection() == MicroWorld::Core::ERuntimeResult::Success; }

	/**
	 * Motivation: Advances one bounded slice and reports whether that slice completed the cycle.
	 * Responsibilities: Run one Advance at the probe budget and surface both its success and cycle completion.
	 */
	bool AdvanceOneSlice(bool& bCycleComplete) noexcept
	{
		const MicroWorld::Engine::FGarbageCollectionResult Result = Collector.Advance(ProbeBudget);
		bCycleComplete = Result.bCycleComplete;
		return Result.Result == MicroWorld::Core::ERuntimeResult::Success;
	}

private:
	/**
	 * Motivation: Describes this probe's complete caller-owned store storage for the store constructor.
	 * Responsibilities: Return one FObjectStoreStorage aggregating every fixed backing array and capacity.
	 */
	MicroWorld::Engine::FObjectStoreStorage MakeStorage() noexcept
	{
		return MicroWorld::Engine::FObjectStoreStorage{
			SlotStorage.data(),
			SlotStorage.size(),
			Slots.data(),
			SlotCount,
			SlotSizeBytes,
			SlotAlignmentBytes,
			Roots.data(),
			RootCapacity,
		};
	}

	/** Motivation: Owns the descriptor the probe's single managed type is constructed against. */
	MicroWorld::Engine::TClassRegistry<2> Registry;

	/** Motivation: Backing bytes for the equal-size, non-moving object slots. */
	alignas(SlotAlignmentBytes) std::array<std::byte, SlotSizeBytes * SlotCount> SlotStorage{};

	/** Motivation: One lifecycle record per object slot, owned by the application. */
	std::array<MicroWorld::Engine::FObjectSlotMetadata, SlotCount> Slots{};

	/** Motivation: Backing entries for the independent explicit-root table. */
	std::array<MicroWorld::Engine::FObjectRootEntry, RootCapacity> Roots{};

	/** Motivation: Owns every managed lifetime over this probe's caller-owned storage. */
	MicroWorld::Engine::FObjectStore Store;

	/** Motivation: Backing handles for the collector's reachable-object worklist. */
	std::array<MicroWorld::Engine::FObjectHandle, SlotCount> Worklist{};

	/** Motivation: Performs bounded incremental mark/sweep over the probe store. */
	MicroWorld::Engine::FGarbageCollector Collector;

	/** Motivation: Holds the single root token that keeps the survivor alive across the cycle. */
	MicroWorld::Engine::TStrongObjectPtr<FGcProbeObject> Root;

	/** Motivation: Records whether construction reached a ready-to-measure state. */
	bool bReady{false};
};

} // namespace

/**
 * Motivation: Builds the representative world, the GC probe, and the transport host, then prints measurements.
 * Responsibilities: Run the compile-only proof that brings up no WiFi, performs no flash or radio operation, and prints the labeled measurement lines
 * Part B captures under explicit authorization.
 */
extern "C" void app_main()
{
	using namespace MicroWorld::Core;
	using namespace MicroWorld::Platform::Esp32;
	using namespace MicroWorld::Engine;

	// 0. Route every MW_LOG call site and each measurement line through ESP-IDF logging.
	SetOutputDevice(&WriteEsp32LogRecord);

	ESP_LOGI(BenchmarkTag, "=== MicroWorld ESP32-S3 runtime-margin harness (Phase 6.2 Part A) ===");
	ESP_LOGI(
		BenchmarkTag,
		"world: %u actors / %u components / %u timers",
		static_cast<unsigned>(RepresentativeActorCount),
		static_cast<unsigned>(RepresentativeComponentCount),
		static_cast<unsigned>(RepresentativeTimerCount));

	// 0.5. Bring up the lwIP TCP/IP stack before any socket is created. The UDP device's
	//      socket()/bind() asserts inside lwIP ("Invalid mbox") if the tcpip task and the
	//      default event loop are not running first; no WiFi association is needed for a
	//      bound, pollable socket. Initialized before the heap baseline so this ESP-IDF
	//      infrastructure is not charged to the world-setup heap delta measured below.
	if (esp_netif_init() != ESP_OK)
	{
		ESP_LOGE(BenchmarkTag, "setup FAILED: esp_netif_init rejected");
		BenchmarkSinkResult = 11;
		return;
	}
	if (esp_event_loop_create_default() != ESP_OK)
	{
		ESP_LOGE(BenchmarkTag, "setup FAILED: esp_event_loop_create_default rejected");
		BenchmarkSinkResult = 12;
		return;
	}

	const std::uint32_t FreeHeapBeforeSetup = esp_get_free_heap_size();
	ESP_LOGI(BenchmarkTag, "mem: free_heap_before_setup=%lu bytes", static_cast<unsigned long>(FreeHeapBeforeSetup));

	// 1. The single real clock; esp_timer feeds the engine's caller-supplied monotonic time.
	FEsp32TimeSource Clock;

	// The composition objects below (UDP device, transport host, frame, engine, and the GC probe)
	// are placed in STATIC storage, not on the stack. The ESP-IDF main task stack is only 3584
	// bytes, but TEngine embeds its fixed object storage inline (MaxObjects * SlotBytes) and
	// the GC probe embeds its own slot bytes, which together far exceed that; a stack frame this
	// large faults during the first register-window spill. Static .bss placement matches
	// MicroWorld's bounded caller-owned-storage model and keeps the main task stack small.

	// 2. One non-blocking UDP socket on INADDR_ANY:5000 over the TCP/IP stack brought up above;
	//    no WiFi is associated, so the socket binds and polls but no datagram can route.
	static FEsp32WifiDevice Device(5000);

	// 3. A dedicated-server session host over that device, started at boot time.
	static FBenchmarkTransport Transport(Device);
	(void)Transport.Configure(ENetworkMode::DedicatedServer, FTransportHostConfig{});
	Transport.Start(Clock.Now());

	// 4. Adapt the host to the engine's `THostPlaySystem` interface.
	static THostPlaySystem<FBenchmarkTransport> Frame(Transport);

	// 5. Composition root. Budget {1,4,32}: MaxSweepOperations(32) >= MaxObjects(32) so one
	//    Tick completes a full GC cycle each frame — no mid-cycle mutation lock during the
	//    measured loop (safe because all spawning happens in this setup phase).
	static FBenchmarkHost Host{FGarbageCollectionBudget{1, 4, 32}, Frame};

	(void)Host.RegisterClass<FBenchActor>(BenchActorTypeId, "BenchActor");
	(void)Host.RegisterClass<FBenchComponent>(BenchComponentTypeId, "BenchComponent");

	const TObjectPtr<UWorld> World = Host.CreateWorld();
	if (World.Get() == nullptr)
	{
		ESP_LOGE(BenchmarkTag, "setup FAILED: CreateWorld returned null");
		BenchmarkSinkResult = 1;
		return;
	}

	// 6. Spawn the representative population: 8 actors, each leasing a 2-component view,
	//    with 16 components attached two-per-actor. All spawning finishes before BeginPlay.
	for (std::size_t ActorIndex = 0; ActorIndex < RepresentativeActorCount; ++ActorIndex)
	{
		const TObjectPtr<FBenchActor> Actor = Host.CreateObject<FBenchActor>(BenchActorTypeId).Object;
		if (Actor.Get() == nullptr)
		{
			ESP_LOGE(BenchmarkTag, "setup FAILED: actor %u creation returned null", static_cast<unsigned>(ActorIndex));
			BenchmarkSinkResult = 2;
			return;
		}
		for (std::size_t ComponentIndex = 0; ComponentIndex < 2; ++ComponentIndex)
		{
			const TObjectPtr<FBenchComponent> Component = Host.CreateObject<FBenchComponent>(BenchComponentTypeId).Object;
			if (Component.Get() == nullptr)
			{
				ESP_LOGE(
					BenchmarkTag,
					"setup FAILED: component %u.%u creation returned null",
					static_cast<unsigned>(ActorIndex),
					static_cast<unsigned>(ComponentIndex));
				BenchmarkSinkResult = 3;
				return;
			}
			if (Actor.Get()->RegisterComponent(Component) != EEngineResult::Success)
			{
				ESP_LOGE(
					BenchmarkTag,
					"setup FAILED: RegisterComponent %u.%u rejected",
					static_cast<unsigned>(ActorIndex),
					static_cast<unsigned>(ComponentIndex));
				BenchmarkSinkResult = 4;
				return;
			}
		}
		if (Host.GetWorld().RegisterActor(TObjectPtr<AActor>{Actor}) != EEngineResult::Success)
		{
			ESP_LOGE(BenchmarkTag, "setup FAILED: RegisterActor %u rejected", static_cast<unsigned>(ActorIndex));
			BenchmarkSinkResult = 5;
			return;
		}
	}

	// 7. Schedule the representative timer set (8 looping timers) before BeginPlay.
	for (std::size_t TimerIndex = 0; TimerIndex < RepresentativeTimerCount; ++TimerIndex)
	{
		FBenchTimerDelegate Callback;
		(void)Callback.Bind([]() noexcept {});
		FTimerHandle Handle{};
		if (Host.GetTimerManager().Schedule(std::move(Callback), 100, ETimerMode::Looping, Handle) != ETimerResult::Success)
		{
			ESP_LOGE(BenchmarkTag, "setup FAILED: timer %u schedule rejected", static_cast<unsigned>(TimerIndex));
			BenchmarkSinkResult = 6;
			return;
		}
	}

	if (Host.BeginPlay(Clock.Now()) != ERuntimeResult::Success)
	{
		ESP_LOGE(BenchmarkTag, "setup FAILED: BeginPlay rejected");
		BenchmarkSinkResult = 7;
		return;
	}

	// 8. Construct the standalone GC probe used to isolate one Advance slice.
	static FGcProbe GcProbe;
	if (!GcProbe.IsReady())
	{
		ESP_LOGE(BenchmarkTag, "setup FAILED: GC probe not ready");
		BenchmarkSinkResult = 8;
		return;
	}

	const std::uint32_t FreeHeapAfterSetup = esp_get_free_heap_size();
	const std::uint32_t StackHighWaterMark = uxTaskGetStackHighWaterMark(nullptr);
	ESP_LOGI(BenchmarkTag, "mem: free_heap_after_setup=%lu bytes", static_cast<unsigned long>(FreeHeapAfterSetup));
	ESP_LOGI(BenchmarkTag, "mem: heap_consumed_by_setup=%lu bytes", static_cast<unsigned long>(FreeHeapBeforeSetup - FreeHeapAfterSetup));
	ESP_LOGI(BenchmarkTag, "mem: stack_high_water_mark_after_setup=%lu bytes", static_cast<unsigned long>(StackHighWaterMark));

	// --- Measurement 1: steady-state Tick duration ---
	for (std::uint32_t Warmup = 0; Warmup < TickWarmupIterations; ++Warmup)
	{
		(void)Host.Tick(Clock.Now());
	}
	FBenchStats TickStats;
	for (std::uint32_t Iteration = 0; Iteration < TickMeasurementIterations; ++Iteration)
	{
		const std::int64_t Begin = esp_timer_get_time();
		(void)Host.Tick(Clock.Now());
		const std::int64_t End = esp_timer_get_time();
		TickStats.Record(End - Begin);
	}
	ESP_LOGI(
		BenchmarkTag,
		"tick: iterations=%u min=%lld us mean=%lld us max=%lld us",
		static_cast<unsigned>(TickMeasurementIterations),
		static_cast<long long>(TickStats.MinMicroseconds),
		static_cast<long long>(TickStats.MeanMicroseconds()),
		static_cast<long long>(TickStats.MaxMicroseconds));

	// --- Measurement 2: GC pause per budget unit (isolated Advance slice) ---
	if (!GcProbe.StartCycle())
	{
		ESP_LOGE(BenchmarkTag, "gc: FAILED to start collection cycle");
		BenchmarkSinkResult = 9;
		return;
	}
	FBenchStats GcSliceStats;
	std::uint32_t GcSlicesInCycle = 0;
	for (;;)
	{
		bool bCycleComplete = false;
		const std::int64_t Begin = esp_timer_get_time();
		const bool bAdvanced = GcProbe.AdvanceOneSlice(bCycleComplete);
		const std::int64_t End = esp_timer_get_time();
		if (!bAdvanced)
		{
			ESP_LOGE(BenchmarkTag, "gc: Advance returned non-success mid-cycle");
			BenchmarkSinkResult = 10;
			return;
		}
		GcSliceStats.Record(End - Begin);
		++GcSlicesInCycle;
		if (bCycleComplete)
		{
			break;
		}
	}
	ESP_LOGI(
		BenchmarkTag,
		"gc: budget={root=%u,mark=%u,sweep=%u} slices_in_cycle=%u min=%lld us mean=%lld us max=%lld us",
		static_cast<unsigned>(FGcProbe::ProbeBudget.MaxRootOperations),
		static_cast<unsigned>(FGcProbe::ProbeBudget.MaxMarkOperations),
		static_cast<unsigned>(FGcProbe::ProbeBudget.MaxSweepOperations),
		static_cast<unsigned>(GcSlicesInCycle),
		static_cast<long long>(GcSliceStats.MinMicroseconds),
		static_cast<long long>(GcSliceStats.MeanMicroseconds()),
		static_cast<long long>(GcSliceStats.MaxMicroseconds));

	// --- Measurement 3: transport pump cost (NO netif/traffic — overhead only) ---
	for (std::uint32_t Warmup = 0; Warmup < TransportPumpWarmupIterations; ++Warmup)
	{
		(void)Transport.PumpReceive(Clock.Now());
		(void)Transport.PumpSend(Clock.Now());
	}
	FBenchStats TransportPumpStats;
	for (std::uint32_t Iteration = 0; Iteration < TransportPumpMeasurementIterations; ++Iteration)
	{
		const std::int64_t Begin = esp_timer_get_time();
		(void)Transport.PumpReceive(Clock.Now());
		(void)Transport.PumpSend(Clock.Now());
		const std::int64_t End = esp_timer_get_time();
		TransportPumpStats.Record(End - Begin);
	}
	ESP_LOGI(
		BenchmarkTag,
		"net_pump: no_traffic_overhead iterations=%u mean=%lld us (live datagram cost needs a peer — out of scope)",
		static_cast<unsigned>(TransportPumpMeasurementIterations),
		static_cast<long long>(TransportPumpStats.MeanMicroseconds()));

	// --- Measurement 4 (static): RAM/flash are cited from the build output in the deliverable. ---
	ESP_LOGI(BenchmarkTag, "image: static RAM/Flash figures are read from the pio build summary, not measured in code");

	ESP_LOGI(BenchmarkTag, "=== harness complete ===");
	BenchmarkSinkResult = 0;

	// A real deployment flashes and reads the lines above; Part A never reaches hardware.
	for (;;)
	{
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}
