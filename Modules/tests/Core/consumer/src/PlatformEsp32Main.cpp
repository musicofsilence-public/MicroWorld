// PlatformEsp32Main.cpp — Phase 5.2 compile/composition proof.
//
// This translation unit composes the full MicroWorld stack on ESP32-S3:
// FEsp32TimeSource (esp_timer, the single real clock) + FEsp32WifiDevice (lwIP
// non-blocking UDP) + Messaging + Network (dedicated server) bound into TEngine
// through one caller-owned device frame, then ticks it at a
// fixed 20 ms cadence from app_main. This is a composition proof: the lwIP
// stack is initialized so the UDP socket is valid, but no WiFi is associated,
// so no UDP datagram can flow. A real deployment associates WiFi first and
// requires explicit hardware authorization to flash.

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/DefaultEngineTraits.h>
#include <MicroWorld/Engine/EngineStorage.h>
#include <MicroWorld/Engine/PlaySystemSet.h>
#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Messaging/MessagingSystem.h>
#include <MicroWorld/Networking/NetworkSystem.h>
#include <MicroWorld/Engine/GarbageCollectionBudget.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Platform/Esp32/Esp32OutputDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiLink.h>
#include <MicroWorld/Core/Time.h>

#include <esp_event.h>
#include <esp_netif.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>

namespace
{

/** Motivation: Stable type id for the example's user-derived managed actor descriptor. */
constexpr MicroWorld::Engine::FTypeId DemoActorTypeId{0x00050001u};

/** Motivation: Stable type id for the example's user-derived managed component descriptor. */
constexpr MicroWorld::Engine::FTypeId DemoComponentTypeId{0x00050002u};

/**
 * Motivation: A concrete component proving the engine component base is constructible on ESP32.
 * Responsibilities: Derive UActorComponent so a descriptor can construct and destroy one user component.
 * Example:
 *   Host.RegisterClass<FDemoComponent>(DemoComponentTypeId, "DemoComponent");
 */
class FDemoComponent final : public MicroWorld::Engine::UActorComponent
{
public:
	/**
	 * Motivation: Keeps exact descriptor-driven destruction publicly instantiable.
	 * Responsibilities: Keep the destructor defaulted so the descriptor's destroy path can call it.
	 */
	~FDemoComponent() noexcept override = default;
};

/**
 * Motivation: A concrete actor proving the engine actor base is constructible on ESP32.
 * Responsibilities: Derive AActor so a descriptor can construct and destroy one user actor with component slots.
 * Example:
 *   Host.RegisterClass<FDemoActor>(DemoActorTypeId, "DemoActor");
 */
class FDemoActor final : public MicroWorld::Engine::AActor
{
public:
	/**
	 * Motivation: Initializes the managed actor base, which owns its bounded component slots.
	 * Responsibilities: Forward to the actor base so its component slots are ready before registration.
	 */
	explicit FDemoActor() noexcept : AActor() {}

	/**
	 * Motivation: Keeps exact descriptor-driven destruction publicly instantiable.
	 * Responsibilities: Keep the destructor defaulted so the descriptor's destroy path can call it.
	 */
	~FDemoActor() noexcept override = default;
};

/** Motivation: Retains the tick-loop outcome so optimization cannot erase the representative host calls. */
volatile int PlatformEsp32CompositionResult = -1;

} // namespace

/**
 * Motivation: Composes the full ESP32 stack and ticks the engine at a fixed cadence.
 * Responsibilities: Wire the dedicated-server host through the engine frame and tick at a fixed 20 ms cadence.
 */
/**
 * Motivation: Carries the exact capacities FDemoHost sized before the traits refactor, so the proof store is unchanged.
 * Responsibilities: Override the trait constants the engine template sizes its fixed storage from.
 * Example:
 *   using FDemoHost = TEngine<FDemoHostTraits>;
 */
struct FDemoHostTraits : MicroWorld::Engine::FDefaultEngineTraits
{
	static constexpr std::size_t MaxClasses = 6;
	static constexpr std::size_t MaxObjects = 8;
	static constexpr std::size_t SlotSizeBytes = 256;
	static constexpr std::size_t MaxRoots = 1;
	static constexpr std::size_t MaxActors = 2;
	static constexpr std::size_t MaxTimers = 4;
};

extern "C" void app_main()
{
	using namespace MicroWorld::Core;
	using namespace MicroWorld::Platform::Esp32;
	using namespace MicroWorld::Engine;
	using namespace MicroWorld::Transport;

	// 1. Route every surviving MW_LOG call site through ESP-IDF logging.
	SetOutputDevice(&WriteEsp32LogRecord);

	// 2. The engine consumes one caller-supplied clock; esp_timer is the only real clock here.
	FEsp32TimeSource Clock;

	// Bring up the lwIP TCP/IP stack before any socket is opened. Without the tcpip task and the
	// default event loop, FEsp32WifiDevice's socket()/bind() asserts inside lwIP ("Invalid mbox").
	// No WiFi is associated, so the socket binds but no datagram routes. The composition objects
	// below are STATIC: TEngine embeds its object storage inline (MaxObjects * SlotBytes) and
	// the UDP/transport objects hold internal buffers, together too large for the 3584-byte main task
	// stack; static .bss placement matches MicroWorld's bounded caller-owned-storage model.
	if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK)
	{
		PlatformEsp32CompositionResult = 3;
		return;
	}

	// 3. One non-blocking UDP socket on INADDR_ANY:5000 over the stack brought up above.
	static FEsp32WifiDevice Device(5000);

	// Compile/link proof for the WiFi facade (MESSAGING 1.1): constructed and queried
	// but never brought up here — this composition proof associates no WiFi.
	static FEsp32WifiLink WifiLink;
	(void)WifiLink.IsUp();

	// 4. Bind the device once; Engine drives it before its Messaging and Network systems.
	static TPlaySystemSet<1> DeviceFrames;

	// 5. The application entry point: same capacities as the Engine profile probe + the live device frame.
	using FDemoHost = TEngine<FDemoHostTraits>;
	static FDemoHost Host{FGarbageCollectionBudget{1, 4, 8}, DeviceFrames};
	if (DeviceFrames.Add(Device) != EEngineResult::Success || Host.CreateMessagingSystem({}) != ERuntimeResult::Success)
	{
		PlatformEsp32CompositionResult = 1;
		return;
	}
	MicroWorld::Messaging::FMessagingSystem* const Messaging = Host.GetMessagingSystem();
	MicroWorld::Messaging::FMessagingLinkId LinkId{};
	if (Messaging == nullptr || Messaging->RegisterLink(Device, LinkId) != MicroWorld::Messaging::EMessagingResult::Success
		|| Host.CreateNetworkSystem({MicroWorld::Networking::ENetworkRole::Server}) != MicroWorld::Networking::ENetworkResult::Success)
	{
		PlatformEsp32CompositionResult = 1;
		return;
	}

	// Register one user actor and component so CreateWorld/CreateObject have real work to do.
	(void)Host.RegisterClass<FDemoActor>(DemoActorTypeId, "DemoActor");
	(void)Host.RegisterClass<FDemoComponent>(DemoComponentTypeId, "DemoComponent");

	const TObjectPtr<UWorld> World = Host.CreateWorld();
	// The actor owns its bounded component slots directly, matching the Engine profile probe.
	const TObjectPtr<FDemoActor> Actor = Host.CreateObject<FDemoActor>(DemoActorTypeId).Object;
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

	// Compile/link proof for the sleep facade (MESSAGING 1.2): one bounded yield before the loop.
	SleepMilliseconds(1);

	// 7. Tick the canonical frame at a fixed 20 ms cadence; the loop never returns in a real app.
	for (;;)
	{
		(void)Host.Tick(Clock.Now());
		vTaskDelay(pdMS_TO_TICKS(20));
	}
}
