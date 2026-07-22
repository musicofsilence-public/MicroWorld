// Esp32BenchmarkMain.cpp — Phase 6.2 Part A on-target runtime-margin harness.
//
// This is the COMPILE-ONLY harness for §6.2: it builds the representative world
// (8 actors / 16 components / 8 timers), a standalone GC probe store, and a
// no-traffic net pump, then prints labeled measurement lines over serial at
// 115200. Part B (a separate, human-authorized flash) captures the real numbers;
// this image is never flashed or run on hardware as part of Part A.
//
// Measurements (each labeled for direct transcription):
//   1. Tick duration — Host.Tick over 1000 iterations (min/mean/max us).
//   2. GC pause per budget unit — isolated Advance slice on a standalone
//      FObjectStore + FGarbageCollector (min/mean/max us per slice).
//   3. Net pump cost — PumpReceive + PumpSend with NO netif/traffic (mean us).
//   4. Memory — free heap before/after setup, stack high-water mark after setup.
//
// GC-slice isolation uses a SEPARATE bounded store + collector, not the host's
// embedded collector (which is private). This measures the exact public-API
// cost of one bounded Advance slice, the unit the roadmap asks for, without
// adding a GetCollector() accessor that would widen engine API surface.

#include <MicroWorld/Delegates/Delegate.h>
#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/NetworkFrame.h>
#include <MicroWorld/Engine/Timer.h>
#include <MicroWorld/Log.h>
#include <MicroWorld/Net/NetHost.h>
#include <MicroWorld/Object/ClassDescriptor.h>
#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/Object.h>
#include <MicroWorld/Object/ObjectStore.h>
#include <MicroWorld/PlatformEsp32/Esp32LogSink.h>
#include <MicroWorld/PlatformEsp32/Esp32TimeSource.h>
#include <MicroWorld/PlatformEsp32/Esp32UdpDriver.h>
#include <MicroWorld/Time.h>

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

/** ESP-IDF log tag printed with every harness measurement line. */
constexpr const char* BenchmarkTag = "mwbench";

/** Stable type id for the benchmark's user-derived managed actor descriptor. */
constexpr MicroWorld::FTypeId BenchActorTypeId{0x00060010u};

/** Stable type id for the benchmark's user-derived managed component descriptor. */
constexpr MicroWorld::FTypeId BenchComponentTypeId{0x00060011u};

/** Stable type id for the standalone GC probe's unrooted garbage object descriptor. */
constexpr MicroWorld::FTypeId GcProbeObjectTypeId{0x00060012u};

/** Representative world profile: actors tick every frame, components tick every frame. */
constexpr MicroWorld::FTickConfiguration BenchTickConfiguration{true, true, MicroWorld::DurationMilliseconds{0}};

/** Representative actor count the roadmap names for the runtime-margin profile. */
constexpr std::size_t RepresentativeActorCount = 8;

/** Representative component count (two per actor) the roadmap names for the profile. */
constexpr std::size_t RepresentativeComponentCount = 16;

/** Representative timer count the roadmap names for the profile. */
constexpr std::size_t RepresentativeTimerCount = 8;

/** Iterations timed for the steady-state tick measurement (labeled on every result line). */
constexpr std::uint32_t TickMeasurementIterations = 1000;

/** Warm-up ticks before timing so caches and branch prediction settle. */
constexpr std::uint32_t TickWarmupIterations = 100;

/** Iterations timed for the no-traffic net pump measurement. */
constexpr std::uint32_t NetPumpMeasurementIterations = 1000;

/** Warm-up pump cycles before timing so the driver and host settle. */
constexpr std::uint32_t NetPumpWarmupIterations = 100;

/**
 * Concrete managed component representative of steady-state per-frame component work.
 *
 * Carries a zero-interval tick configuration so every frame produces one
 * TickComponent call across the representative component population.
 */
class FBenchComponent final : public MicroWorld::UActorComponent
{
public:
	/** Adopts the representative always-tick schedule so each frame exercises the component. */
	FBenchComponent() noexcept : UActorComponent(BenchTickConfiguration) {}

	/** Keeps exact descriptor-driven destruction publicly instantiable. */
	~FBenchComponent() noexcept override = default;
};

/**
 * Concrete managed actor representative of steady-state per-frame actor work.
 *
 * Embeds no inline registry; each instance leases a caller-owned component
 * view at construction, mirroring the proven PlatformEsp32Main composition.
 */
class FBenchActor final : public MicroWorld::AActor
{
public:
	/** Forwards the component lease and the representative always-tick schedule to the base. */
	explicit FBenchActor(MicroWorld::FActorComponentRegistryBase Components) noexcept : AActor(std::move(Components)) {}

	/** Keeps exact descriptor-driven destruction publicly instantiable. */
	~FBenchActor() noexcept override = default;
};

/**
 * Concrete managed object used as unrooted garbage in the standalone GC probe.
 *
 * Constructed into every probe slot, rooted exactly once, then left unreferenced
 * so the collector's sweep phase reclaims the rest across multiple bounded slices.
 */
class FGcProbeObject final : public MicroWorld::UObject
{
public:
	/** Keeps exact descriptor-driven destruction publicly instantiable. */
	~FGcProbeObject() noexcept override = default;
};

/** Sizes the host to hold the representative world with bounded headroom. */
using FBenchmarkHost = MicroWorld::TEngineHost<
	6,						  // MaxClasses: UWorld + AActor + UActorComponent + 2 user types + 1 spare.
	32,						  // MaxObjects: 1 world + 8 actors + 16 components = 25 live; +7 headroom.
	256,					  // SlotBytes: proven actor slot width (PlatformEsp32Main / EngineHostTests).
	16,						  // SlotAlign: proven slot alignment.
	1,						  // MaxRoots: the single rooted world.
	RepresentativeActorCount, // MaxActors: the representative actor count.
	RepresentativeTimerCount, // MaxTimers: the representative timer count.
	64>;					  // TimerCallbackBytes: proven inline delegate storage.

/** Dedicated server net host sized identically to the PlatformEsp32Main proof. */
using FBenchmarkNet = MicroWorld::TNetHost<4, 256>;

/** Delegate type matching the host's timer manager so Schedule accepts a bound callback. */
using FBenchTimerDelegate = MicroWorld::TDelegate<void(), 64>;

/**
 * Accumulates min/mean/max across one timed operation's repeated samples.
 *
 * Stores only fixed scalars so the measurement loop allocates nothing; the sum
 * is accumulated as uint64 microseconds so 1000 iterations cannot overflow.
 */
struct FBenchStats
{
	/** Smallest single-sample duration observed, or zero before the first sample. */
	std::int64_t MinMicroseconds{0};

	/** Largest single-sample duration observed, or zero before the first sample. */
	std::int64_t MaxMicroseconds{0};

	/** Running sum of all sample durations, divided by Count for the mean. */
	std::uint64_t SumMicroseconds{0};

	/** Number of samples accumulated; drives the mean denominator. */
	std::uint32_t Count{0};

	/** True once at least one sample has set Min and Max away from their zero initial state. */
	bool bSeeded{false};

	/** Records one sample and updates min/sum/count, seeding min/max on the first call. */
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

	/** Returns the arithmetic mean, or zero when no samples were recorded. */
	std::int64_t MeanMicroseconds() const noexcept { return Count == 0 ? 0 : static_cast<std::int64_t>(SumMicroseconds / Count); }
};

/** Retains the harness outcome so optimization cannot erase the representative calls. */
volatile int BenchmarkSinkResult = -1;

/**
 * Standalone bounded store plus collector used to isolate one GC Advance slice.
 *
 * Sized so MaxSweepOperations(8) is below the slot count(32), forcing a
 * multi-slice cycle: each Advance call is one measurable pause rather than a
 * whole cycle completing in a single call. One object is rooted; the rest are
 * unreferenced garbage the sweep phase reclaims slice by slice; the store and
 * collector are non-movable, so like ObjectConsumerProbe.h this probe declares
 * the backing storage as members and constructs the store/collector eagerly in
 * the member-init list in declaration order.
 */
class FGcProbe final
{
public:
	/** Slot count chosen to force a multi-slice sweep at the probe's budget. */
	static constexpr std::uint32_t SlotCount = 32;

	/** Root capacity for the single rooted object that survives the cycle. */
	static constexpr std::uint32_t RootCapacity = 1;

	/** Probe budget: a sweep budget below SlotCount produces measurable per-slice pauses. */
	static constexpr MicroWorld::FGarbageCollectionBudget ProbeBudget{1, 1, 8};

	/** Slot extent matching the largest probe object, rounded to the slot alignment. */
	static constexpr std::size_t SlotSizeBytes = 128;

	/** Power-of-two slot alignment matching the probe object's requirement. */
	static constexpr std::size_t SlotAlignmentBytes = 16;

	/** Builds the registry, store, collector, and rooted survivor the timing loop traces. */
	FGcProbe() noexcept
		: Store(MakeStorage(), MicroWorld::MakeClassRegistryView(Registry))
		, Collector(Store, MicroWorld::FGarbageCollectorStorage{Worklist.data(), SlotCount})
	{
		if (Store.ConfigurationResult() != MicroWorld::EObjectResult::Success)
		{
			return;
		}

		// Register the probe's single type against the already-constructed store.
		(void)Registry.Register(MicroWorld::MakeClassDescriptor<FGcProbeObject>(
			GcProbeObjectTypeId, "GcProbeObject", nullptr, &MicroWorld::TraceManagedObjectReferences));
		const MicroWorld::FClassDescriptor* const Descriptor = Registry.Find(GcProbeObjectTypeId);
		if (Descriptor == nullptr)
		{
			return;
		}

		// Populate every slot; root exactly one so the others are true garbage.
		for (std::uint32_t Index = 0; Index < SlotCount; ++Index)
		{
			const MicroWorld::TObjectCreationResult<FGcProbeObject> Creation = Store.NewObject<FGcProbeObject>(*Descriptor);
			if (Creation.Result != MicroWorld::EObjectResult::Success)
			{
				return;
			}
			if (Index == 0)
			{
				// Non-const so std::move selects the move-assign rather than the deleted copy-assign.
				MicroWorld::TStrongObjectPointerResult<FGcProbeObject> RootResult = Store.MakeStrongObjectPtr(Creation.Object);
				if (RootResult.Result != MicroWorld::EObjectResult::Success)
				{
					return;
				}
				Root = std::move(RootResult.Pointer);
			}
		}
		bReady = true;
	}

	/** Prevents copying the fixed storage arrays and the single store identity. */
	FGcProbe(const FGcProbe&) = delete;
	/** Prevents assigning the fixed storage arrays and the single store identity. */
	FGcProbe& operator=(const FGcProbe&) = delete;

	/** Reports whether construction populated the store, collector, and rooted survivor. */
	bool IsReady() const noexcept { return bReady; }

	/** Begins a fresh collection cycle so the timing loop starts from a known state. */
	bool StartCycle() noexcept { return Collector.RequestCollection() == MicroWorld::ERuntimeResult::Success; }

	/** Advances one bounded slice and reports whether that slice completed the cycle. */
	bool AdvanceOneSlice(bool& bCycleComplete) noexcept
	{
		const MicroWorld::FGarbageCollectionResult Result = Collector.Advance(ProbeBudget);
		bCycleComplete = Result.bCycleComplete;
		return Result.Result == MicroWorld::ERuntimeResult::Success;
	}

private:
	/** Describes this probe's complete caller-owned store storage for the store constructor. */
	MicroWorld::FObjectStoreStorage MakeStorage() noexcept
	{
		return MicroWorld::FObjectStoreStorage{
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

	/** Owns the descriptor the probe's single managed type is constructed against. */
	MicroWorld::TClassRegistry<2> Registry;

	/** Backing bytes for the equal-size, non-moving object slots. */
	alignas(SlotAlignmentBytes) std::array<std::byte, SlotSizeBytes * SlotCount> SlotStorage{};

	/** One lifecycle record per object slot, owned by the application. */
	std::array<MicroWorld::FObjectSlotMetadata, SlotCount> Slots{};

	/** Backing entries for the independent explicit-root table. */
	std::array<MicroWorld::FObjectRootEntry, RootCapacity> Roots{};

	/** Owns every managed lifetime over this probe's caller-owned storage. */
	MicroWorld::FObjectStore Store;

	/** Backing handles for the collector's reachable-object worklist. */
	std::array<MicroWorld::FObjectHandle, SlotCount> Worklist{};

	/** Performs bounded incremental mark/sweep over the probe store. */
	MicroWorld::FGarbageCollector Collector;

	/** Holds the single root token that keeps the survivor alive across the cycle. */
	MicroWorld::TStrongObjectPtr<FGcProbeObject> Root;

	/** Records whether construction reached a ready-to-measure state. */
	bool bReady{false};
};

} // namespace

/**
 * Builds the representative world, the GC probe, and the net host, then prints measurements.
 *
 * This is the COMPILE-ONLY proof: no netif/WiFi is brought up, the UDP driver
 * is constructed but never linked, and no flash or radio operation is performed.
 * Part B flashes and captures the printed numbers under explicit authorization.
 */
extern "C" void app_main()
{
	using namespace MicroWorld;

	// 0. Route every MW_LOG call site and each measurement line through ESP-IDF logging.
	SetLogSink(&Esp32LogSink);

	ESP_LOGI(BenchmarkTag, "=== MicroWorld ESP32-S3 runtime-margin harness (Phase 6.2 Part A) ===");
	ESP_LOGI(
		BenchmarkTag,
		"world: %u actors / %u components / %u timers",
		static_cast<unsigned>(RepresentativeActorCount),
		static_cast<unsigned>(RepresentativeComponentCount),
		static_cast<unsigned>(RepresentativeTimerCount));

	// 0.5. Bring up the lwIP TCP/IP stack before any socket is created. The UDP driver's
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

	// The composition objects below (UDP driver, net host, frame, engine host, and the GC probe)
	// are placed in STATIC storage, not on the stack. The ESP-IDF main task stack is only 3584
	// bytes, but TEngineHost embeds its fixed object storage inline (MaxObjects * SlotBytes) and
	// the GC probe embeds its own slot bytes, which together far exceed that; a stack frame this
	// large faults during the first register-window spill. Static .bss placement matches
	// MicroWorld's bounded caller-owned-storage model and keeps the main task stack small.

	// 2. One non-blocking UDP socket on INADDR_ANY:5000 over the TCP/IP stack brought up above;
	//    no WiFi is associated, so the socket binds and polls but no datagram can route.
	static FEsp32UdpDriver Driver(5000);

	// 3. A dedicated-server session host over that driver, started at boot time.
	static FBenchmarkNet Net(Driver);
	(void)Net.Configure(ENetMode::DedicatedServer, FNetHostConfig{});
	Net.Start(Clock.Now());

	// 4. Adapt the host to the engine's network frame seam.
	static TNetHostFrame<FBenchmarkNet> Frame(Net);

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
	static std::array<FActorComponentRegistry<2>, RepresentativeActorCount> ActorComponentStorages{};
	for (std::size_t ActorIndex = 0; ActorIndex < RepresentativeActorCount; ++ActorIndex)
	{
		const TObjectPtr<FBenchActor> Actor = Host.CreateObject<FBenchActor>(BenchActorTypeId, ActorComponentStorages[ActorIndex].MakeView()).Object;
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

	// --- Measurement 3: net pump cost (NO netif/traffic — overhead only) ---
	for (std::uint32_t Warmup = 0; Warmup < NetPumpWarmupIterations; ++Warmup)
	{
		(void)Net.PumpReceive(Clock.Now());
		(void)Net.PumpSend(Clock.Now());
	}
	FBenchStats NetPumpStats;
	for (std::uint32_t Iteration = 0; Iteration < NetPumpMeasurementIterations; ++Iteration)
	{
		const std::int64_t Begin = esp_timer_get_time();
		(void)Net.PumpReceive(Clock.Now());
		(void)Net.PumpSend(Clock.Now());
		const std::int64_t End = esp_timer_get_time();
		NetPumpStats.Record(End - Begin);
	}
	ESP_LOGI(
		BenchmarkTag,
		"net_pump: no_traffic_overhead iterations=%u mean=%lld us (live datagram cost needs a peer — out of scope)",
		static_cast<unsigned>(NetPumpMeasurementIterations),
		static_cast<long long>(NetPumpStats.MeanMicroseconds()));

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
