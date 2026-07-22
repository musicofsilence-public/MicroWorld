// PlatformEsp32Main.cpp — Phase 5.2 + 5.3 compile/composition proof.
//
// This translation unit composes the full MicroWorld stack on ESP32-S3:
// FEsp32TimeSource (esp_timer, the single real clock) + FEsp32UdpDriver (lwIP
// non-blocking UDP) + TNetHost<4,256> (dedicated server) bound into TEngineHost
// via the TNetHostFrame/INetworkFrame seam from Phase 4.4, then ticks it at a
// fixed 20 ms cadence from app_main. Phase 5.3 additionally constructs one
// FEsp32E32LoraDriver so its portable FrameCodec + UART glue compile and link
// into the image; that driver is never ticked here. It is a COMPOSITION proof:
// the lwIP stack is initialized so the UDP socket is valid, but no WiFi is
// associated, so no UDP datagram can flow, the LoRa UART is never opened at
// runtime in this proof, and no firmware upload or radio transmit is performed
// in this milestone. A real deployment associates WiFi first and requires
// explicit hardware authorization to flash.

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Log.h>
#include <MicroWorld/Net/NetHost.h>
#include <MicroWorld/Object/GarbageCollector.h>
#include <MicroWorld/Object/ObjectStore.h>
#include <MicroWorld/PlatformEsp32/Esp32E32LoraDriver.h>
#include <MicroWorld/PlatformEsp32/Esp32LogSink.h>
#include <MicroWorld/PlatformEsp32/Esp32TimeSource.h>
#include <MicroWorld/PlatformEsp32/Esp32UdpDriver.h>
#include <MicroWorld/Time.h>

#include <driver/uart.h>

#include <esp_event.h>
#include <esp_netif.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>

namespace
{

/** Stable type id for the example's user-derived managed actor descriptor. */
constexpr MicroWorld::FTypeId DemoActorTypeId{0x00050001u};

/** Stable type id for the example's user-derived managed component descriptor. */
constexpr MicroWorld::FTypeId DemoComponentTypeId{0x00050002u};

/** A concrete component proving the engine component base is constructible on ESP32. */
class FDemoComponent final : public MicroWorld::UActorComponent
{
public:
	/** Keeps exact descriptor-driven destruction publicly instantiable. */
	~FDemoComponent() noexcept override = default;
};

/** A concrete actor proving the engine actor base is constructible on ESP32. */
class FDemoActor final : public MicroWorld::AActor
{
public:
	/** Forwards store and component storage to the managed actor base. */
	explicit FDemoActor(MicroWorld::FActorComponentRegistryBase Components) noexcept : AActor(std::move(Components)) {}

	/** Keeps exact descriptor-driven destruction publicly instantiable. */
	~FDemoActor() noexcept override = default;
};

/** Retains the tick-loop outcome so optimization cannot erase the representative host calls. */
volatile int PlatformEsp32CompositionResult = -1;

} // namespace

/**
 * Composes the full ESP32 stack and ticks the engine host at a fixed cadence.
 *
 * This is a compile/composition proof: the dedicated-server network host is
 * wired through the frame seam but no netif/WiFi is brought up, so the loop
 * exercises only the host's plumbing. Flashing this image to hardware requires
 * explicit authorization that is out of scope for Phase 5.2.
 */
extern "C" void app_main()
{
	using namespace MicroWorld;

	// 1. Route every surviving MW_LOG call site through ESP-IDF logging.
	SetLogSink(&Esp32LogSink);

	// 2. The engine consumes one caller-supplied clock; esp_timer is the only real clock here.
	FEsp32TimeSource Clock;

	// Bring up the lwIP TCP/IP stack before any socket is opened. Without the tcpip task and the
	// default event loop, FEsp32UdpDriver's socket()/bind() asserts inside lwIP ("Invalid mbox").
	// No WiFi is associated, so the socket binds but no datagram routes. The composition objects
	// below are STATIC: TEngineHost embeds its object storage inline (MaxObjects * SlotBytes) and
	// the UDP/net objects hold internal buffers, together too large for the 3584-byte main task
	// stack; static .bss placement matches MicroWorld's bounded caller-owned-storage model.
	if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK)
	{
		PlatformEsp32CompositionResult = 3;
		return;
	}

	// 3. One non-blocking UDP socket on INADDR_ANY:5000 over the stack brought up above.
	static FEsp32UdpDriver Driver(5000);

	// 3b. Compile-only E32 LoRa UART driver (Phase 5.3): the object is linked into the image
	//     so its portable FrameCodec + UART glue compile, but it is never ticked here. No UART
	//     traffic, no radio, and no upload is performed in this proof.
	static FEsp32E32LoraDriver LoraDriver(FEsp32E32LoraConfig{UART_NUM_1, /*TxGpio*/ 17, /*RxGpio*/ 18, /*Baud*/ 9600, /*NodeId*/ 1});
	(void)LoraDriver;

	// 4. A dedicated-server session host over that driver, started at the current boot time.
	static TNetHost<4, 256> Net(Driver);
	(void)Net.Configure(ENetMode::DedicatedServer, FNetHostConfig{});
	Net.Start(Clock.Now());

	// 5. Adapt the host to the engine's network frame seam (Phase 4.4).
	static TNetHostFrame<TNetHost<4, 256>> Frame(Net);

	// 6. The composition root: same template args as the Engine profile probe + the live frame.
	using FDemoHost = TEngineHost<6, 8, 256, 16, 1, 2, 4, 64>;
	static FDemoHost Host{FGarbageCollectionBudget{1, 4, 8}, Frame};

	// Register one user actor and component so CreateWorld/CreateObject have real work to do.
	(void)Host.RegisterClass<FDemoActor>(DemoActorTypeId, "DemoActor");
	(void)Host.RegisterClass<FDemoComponent>(DemoComponentTypeId, "DemoComponent");

	const TObjectPtr<UWorld> World = Host.CreateWorld();
	// The actor embeds no inline registry; lease one caller-owned view at construction,
	// mirroring the Engine profile probe (EngineConsumerProbe.h) so the slot stays 256 bytes.
	static FActorComponentRegistry<1> ActorComponents;
	const TObjectPtr<FDemoActor> Actor = Host.CreateObject<FDemoActor>(DemoActorTypeId, ActorComponents.MakeView()).Object;
	const TObjectPtr<FDemoComponent> Component = Host.CreateObject<FDemoComponent>(DemoComponentTypeId).Object;
	if (World.Get() == nullptr || Actor.Get() == nullptr || Component.Get() == nullptr)
	{
		PlatformEsp32CompositionResult = 1;
		return;
	}
	if (Actor.Get()->RegisterComponent(Component) != EEngineResult::Success
		|| Host.GetWorld().RegisterActor(TObjectPtr<AActor>{Actor}) != EEngineResult::Success
		|| Host.BeginPlay(Clock.Now()) != ERuntimeResult::Success)
	{
		PlatformEsp32CompositionResult = 2;
		return;
	}

	// 7. Tick the canonical frame at a fixed 20 ms cadence; the loop never returns in a real app.
	for (;;)
	{
		(void)Host.Tick(Clock.Now());
		vTaskDelay(pdMS_TO_TICKS(20));
	}
}
