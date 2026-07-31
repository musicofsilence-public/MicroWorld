#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Core/Version.h>

#include <MicroWorld/Core/Containers/Span.h>

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessageRouter.h>

#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/ObjectPtr.h>

#include <MicroWorld/Platform/Esp32/Esp32OutputDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

namespace
{

// ---- Message and managed-type ids (roadmap section 4.5) ------------------

/** Identifies the thermometer's broadcast reading message. */
inline constexpr MicroWorld::Messaging::FMessageTypeId TemperatureReadingMessageId = 1;

/** Identifies the display's targeted calibrate message. */
inline constexpr MicroWorld::Messaging::FMessageTypeId CalibrateMessageId = 2;

/** Actor id the display targets its calibrate send at, and the thermometer
 *  registers its calibrate handler under. */
inline constexpr MicroWorld::Messaging::FMessageActorId ThermometerActorId = 10;

/** Actor id recorded as the sender of the calibrate message; not currently
 *  used as anyone's listener id, since the thermometer never messages it back. */
inline constexpr MicroWorld::Messaging::FMessageActorId DisplayActorId = 11;

/** Stable descriptor id for the managed FThermometerActor type (0x0016 == example 22). */
constexpr MicroWorld::Engine::FTypeId ThermometerActorTypeId{0x00160001u};

/** Stable descriptor id for the managed FReadingSensorComponent type. */
constexpr MicroWorld::Engine::FTypeId ReadingSensorComponentTypeId{0x00160003u};

// ---- Shared cadence and bounds ---------------------------------------------

/**
 * Cadence shared by the sensor's tick config and the thermometer actor's own
 * tick config, so the two schedules can never drift apart (one named constant
 * instead of two copies of "500" that a future edit could desync). See
 * FThermometerActor::Tick for why same-cadence alignment matters.
 */
constexpr MicroWorld::Core::DurationMilliseconds ReadingCadenceMilliseconds = 500;

/** Number of readings the display waits for before it sends the one calibrate message. */
constexpr std::uint32_t CalibrateAfterReadingCount = 5;

/** Bounds the run: the loop stops once the display has logged this many readings --
 *  5 to trigger calibrate, then a couple more to show the reset counter climbing again. */
constexpr std::uint32_t TargetDisplayedReadingCount = 7;

/** Byte width of the little-endian uint16 reading payload packed and decoded below. */
constexpr std::size_t ReadingPayloadBytes = 2;

/**
 * Produces a deterministic synthetic temperature reading every 500 ms.
 *
 * No peripheral is read (ADR 0003 keeps device buses out of engine-first
 * examples) and no RNG is used (unavailable under this engine's constraints,
 * and non-deterministic anyway) -- the reading is a named base plus a bounded
 * ramp, so the trace is byte-for-byte reproducible run to run.
 */
class FReadingSensorComponent final : public MicroWorld::Engine::UActorComponent
{
public:
	/** Selects the 500 ms reading cadence the thermometer actor's own tick is aligned to. */
	FReadingSensorComponent() noexcept : UActorComponent(MicroWorld::Core::FTickConfiguration::EnabledEvery(ReadingCadenceMilliseconds)) {}

	/** Returns the most recently produced reading; the owning actor packs this into its broadcast. */
	std::uint16_t GetLatestReading() const noexcept { return LatestReading; }

	/** Returns how many readings have been produced since construction or the last calibrate reset. */
	std::uint32_t GetReadingCount() const noexcept { return ReadingCount; }

	/** Resets the reading counter; called by the thermometer's calibrate handler. */
	void ResetReadingCount() noexcept { ReadingCount = 0; }

protected:
	/** Advances the counter and derives this frame's reading from it -- see the class comment for why. */
	void TickComponent(const MicroWorld::Core::FTickContext&) noexcept override
	{
		++ReadingCount;
		LatestReading = static_cast<std::uint16_t>(BaseReadingValue + (ReadingCount % ReadingSpan));
	}

private:
	/** Offset so the synthetic reading reads like a plausible tenths-of-a-degree value, not a raw counter. */
	static constexpr std::uint16_t BaseReadingValue = 200;

	/** Bounds the ramp so the synthetic reading cycles instead of growing without limit. */
	static constexpr std::uint16_t ReadingSpan = 50;

	/** Counts readings since construction or the last calibrate reset; also the "reading N" trace value. */
	std::uint32_t ReadingCount{0};

	/** Holds the most recent synthetic reading for the owning actor to read and broadcast. */
	std::uint16_t LatestReading{0};
};

/**
 * Owns the reading sensor and broadcasts its value once per frame; also accepts a
 * targeted calibrate message that resets the sensor's reading counter.
 *
 * Takes the router and its sensor by constructor injection (D9) instead of
 * reaching into a global -- see the constructor below.
 */
class FThermometerActor final : public MicroWorld::Engine::AActor
{
public:
	/**
	 * Aligns this actor's own tick to the sensor's 500 ms cadence (see Tick's comment
	 * for why the alignment matters) and stores the router and sensor this actor was
	 * composed with.
	 */
	FThermometerActor(MicroWorld::Messaging::IMessageRouter& InRouter, MicroWorld::Engine::TObjectPtr<FReadingSensorComponent> InSensor) noexcept
		: AActor(MicroWorld::Core::FTickConfiguration::EnabledEvery(ReadingCadenceMilliseconds)), Router(InRouter), Sensor(InSensor)
	{
	}

protected:
	/** Subscribes to a calibrate message targeted at this actor's own id. */
	void BeginPlay() noexcept override
	{
		MicroWorld::Messaging::FMessageHandlerBinding Handler;
		const MicroWorld::Core::EDelegateResult BindResult =
			Handler.Bind([this](const MicroWorld::Messaging::FMessageView& View) noexcept { this->OnCalibrateReceived(View); });
		if (BindResult != MicroWorld::Core::EDelegateResult::Success)
		{
			MW_LOG(Error, "ex22", "thermometer calibrate handler bind failed");
			return;
		}

		// This bounded example never removes handlers (EndPlay tears the router and
		// its actors down together), so the returned handle is not retained.
		MicroWorld::Messaging::FMessageHandlerHandle CalibrateHandlerHandle;
		const MicroWorld::Messaging::EMessageResult AddResult =
			Router.AddMessageHandler(CalibrateMessageId, ThermometerActorId, std::move(Handler), CalibrateHandlerHandle);
		if (AddResult != MicroWorld::Messaging::EMessageResult::Success)
		{
			MW_LOG(Error, "ex22", "thermometer calibrate handler registration failed");
		}
	}

	/**
	 * Broadcasts the current reading.
	 *
	 * Reading Sensor's latest value here is always safe: this actor's Tick runs
	 * after its own components tick within the same Advance (see Actor.h -- "Runs
	 * ... after this actor's components have ticked"), and both the sensor and this
	 * actor's primary tick share the identical ReadingCadenceMilliseconds schedule
	 * started from the same BootTime, so they are due on exactly the same frames.
	 * The sensor has therefore already produced this frame's reading by the time
	 * this runs.
	 */
	void Tick(const MicroWorld::Core::FTickContext&) noexcept override
	{
		FReadingSensorComponent* const SensorPtr = Sensor.Get();
		const std::uint16_t Reading = SensorPtr->GetLatestReading();

		// Little-endian: low byte first, matching the engine's on-wire convention.
		std::uint8_t Payload[ReadingPayloadBytes];
		Payload[0] = static_cast<std::uint8_t>(Reading & 0xFFu);
		Payload[1] = static_cast<std::uint8_t>((Reading >> 8) & 0xFFu);

		const MicroWorld::Messaging::EMessageResult SendResult = Router.BroadcastMessage(
			MicroWorld::Messaging::LocalChannelId,
			TemperatureReadingMessageId,
			ThermometerActorId,
			MicroWorld::Core::TSpan<const std::uint8_t>(Payload, ReadingPayloadBytes));
		if (SendResult != MicroWorld::Messaging::EMessageResult::Success)
		{
			MW_LOG(Error, "ex22", "thermometer broadcast failed");
			return;
		}

		MW_LOG(
			Log,
			"ex22",
			"thermometer broadcast reading N=%u value=%u",
			static_cast<unsigned>(SensorPtr->GetReadingCount()),
			static_cast<unsigned>(Reading));
	}

private:
	/** Logs receipt and resets the sensor's reading counter; this is the calibrate handler bound in BeginPlay. */
	void OnCalibrateReceived(const MicroWorld::Messaging::FMessageView&) noexcept
	{
		MW_LOG(Log, "ex22", "thermometer calibrated (reset reading counter)");
		Sensor.Get()->ResetReadingCount();
	}

	/** Router this actor sends and subscribes through; injected at construction (D9), never a global. */
	MicroWorld::Messaging::IMessageRouter& Router;

	/** Sensor this actor owns and reads each tick; registered as this actor's one inline component slot. */
	MicroWorld::Engine::TObjectPtr<FReadingSensorComponent> Sensor;
};

/**
 * Purely reactive: logs every broadcast reading and, after enough of them, sends
 * one targeted calibrate back to the thermometer to demonstrate SendMessageToActor.
 *
 * Uses AActor directly: it reserves bounded component slots but registers
 * none, which keeps this display-only actor deliberately simple.
 */
class FDisplayActor final : public MicroWorld::Engine::AActor
{
public:
	/** Stores the injected router (D9); this actor never ticks on its own (see the disabled tick config below). */
	explicit FDisplayActor(MicroWorld::Messaging::IMessageRouter& InRouter) noexcept
		: AActor({/*bCanEverTick*/ false, /*bStartWithTickEnabled*/ false, /*TickIntervalMilliseconds*/ 0}), Router(InRouter)
	{
	}

	/** Reports whether the bounded run loop can stop: true once every target reading has been logged. Pure query. */
	bool IsDone() const noexcept { return ReceivedReadingCount >= TargetDisplayedReadingCount; }

	/** Reports how many readings this actor has logged since BeginPlay; used only for the final trace line. */
	std::uint32_t GetReceivedReadingCount() const noexcept { return ReceivedReadingCount; }

protected:
	/** Subscribes to every broadcast reading (ListenerActorId = BroadcastActorId receives broadcasts). */
	void BeginPlay() noexcept override
	{
		MicroWorld::Messaging::FMessageHandlerBinding Handler;
		const MicroWorld::Core::EDelegateResult BindResult =
			Handler.Bind([this](const MicroWorld::Messaging::FMessageView& View) noexcept { this->OnReadingReceived(View); });
		if (BindResult != MicroWorld::Core::EDelegateResult::Success)
		{
			MW_LOG(Error, "ex22", "display reading handler bind failed");
			return;
		}

		// This bounded example never removes handlers (EndPlay tears the router and
		// its actors down together), so the returned handle is not retained.
		MicroWorld::Messaging::FMessageHandlerHandle ReadingHandlerHandle;
		const MicroWorld::Messaging::EMessageResult AddResult =
			Router.AddMessageHandler(TemperatureReadingMessageId, MicroWorld::Messaging::BroadcastActorId, std::move(Handler), ReadingHandlerHandle);
		if (AddResult != MicroWorld::Messaging::EMessageResult::Success)
		{
			MW_LOG(Error, "ex22", "display reading handler registration failed");
		}
	}

private:
	/** Decodes, logs, and counts one reading; triggers the one-time calibrate once CalibrateAfterReadingCount is reached. */
	void OnReadingReceived(const MicroWorld::Messaging::FMessageView& View) noexcept
	{
		if (View.Payload.Size() < ReadingPayloadBytes)
		{
			MW_LOG(Error, "ex22", "display received undersized reading payload");
			return;
		}

		// Little-endian: low byte first, mirroring FThermometerActor::Tick's pack.
		const std::uint8_t* const PayloadBytes = View.Payload.Data();
		const std::uint16_t Reading =
			static_cast<std::uint16_t>(static_cast<std::uint16_t>(PayloadBytes[0]) | (static_cast<std::uint16_t>(PayloadBytes[1]) << 8));

		++ReceivedReadingCount;
		MW_LOG(
			Log, "ex22", "display received reading value=%u (count=%u)", static_cast<unsigned>(Reading), static_cast<unsigned>(ReceivedReadingCount));

		if (ReceivedReadingCount == CalibrateAfterReadingCount && !bCalibrateSent)
		{
			SendCalibrate();
		}
	}

	/**
	 * Sends the targeted calibrate exactly once.
	 *
	 * Sending from inside this handler is legal (D5): it appends to the outbound
	 * queue and is delivered to the thermometer next frame, exactly like any other
	 * send. bCalibrateSent guards against resending on readings 6, 7, ...
	 */
	void SendCalibrate() noexcept
	{
		const MicroWorld::Messaging::EMessageResult SendResult = Router.SendMessageToActor(
			MicroWorld::Messaging::LocalChannelId,
			CalibrateMessageId,
			ThermometerActorId,
			DisplayActorId,
			MicroWorld::Core::TSpan<const std::uint8_t>{});
		if (SendResult != MicroWorld::Messaging::EMessageResult::Success)
		{
			MW_LOG(Error, "ex22", "display calibrate send failed");
			return;
		}

		bCalibrateSent = true;
		MW_LOG(Log, "ex22", "display sent calibrate to thermometer");
	}

	/** Router this actor listens and sends through; injected at construction (D9), never a global. */
	MicroWorld::Messaging::IMessageRouter& Router;

	/** Counts every reading received since BeginPlay; drives both the calibrate trigger and IsDone. */
	std::uint32_t ReceivedReadingCount{0};

	/** Guards against resending calibrate once it has already been sent. */
	bool bCalibrateSent{false};
};

/** Single real-time source; every MicroWorld deadline in this example reads it. */
MicroWorld::Platform::Esp32::FEsp32TimeSource GTimeSource{};

/** Poll far faster than any schedule so the FreeRTOS idle task (and its watchdog)
 *  always runs; the engine's tick schedules, not this delay, decide what fires. */
constexpr unsigned PollPacingMilliseconds = 10;

} // namespace

/**
 * Composition root: wires one router, one engine, one sensor, and two actors,
 * then drives a bounded run that demonstrates local actor messaging end to end --
 * a broadcast reading, a display that counts them, and a targeted calibrate reply.
 */
extern "C" void app_main(void)
{
	MicroWorld::Core::SetOutputDevice(&MicroWorld::Platform::Esp32::WriteEsp32LogRecord);

	// Announce the exact package contract this image was built against.
	MW_LOG(
		Log,
		"ex22",
		"microworld %u.%u.%u",
		static_cast<unsigned>(MicroWorld::Version.Major),
		static_cast<unsigned>(MicroWorld::Version.Minor),
		static_cast<unsigned>(MicroWorld::Version.Patch));

	// Static, never on the app_main stack (the ESP32-S3 stack lesson, example 01
	// section 2.2) -- both objects below are sized in the hundreds of bytes.

	// Local actor-message router. Passed by reference into each actor's constructor
	// below (D9); neither actor ever reaches into a global to find it.
	static MicroWorld::Messaging::TMessageRouter<16, 8, 96, 1> GRouter;

	// Per-tick garbage-collection slice for this tiny graph: one root (the world),
	// a few mark steps for world -> actor -> component, and enough sweep steps to
	// scan every object slot -- matching Modules/Engine/examples/HostLifecycle/Main.cpp.
	constexpr std::uint32_t GcRootOperationsPerTick = 1;
	constexpr std::uint32_t GcMarkOperationsPerTick = 4;
	constexpr std::uint32_t GcSweepOperationsPerTick = 8;

	// Owns every managed subsystem and pumps GRouter as its network frame, so
	// Tick's step 1 (PreAdvance) and step 7 (PostAdvance) drive local delivery.
	static MicroWorld::Engine::TEngine<> GEngine{
		MicroWorld::Engine::FGarbageCollectionBudget{GcRootOperationsPerTick, GcMarkOperationsPerTick, GcSweepOperationsPerTick}, GRouter};

	if (GEngine.RegisterClass<FThermometerActor>(ThermometerActorTypeId, "ThermometerActor") != MicroWorld::Engine::EObjectResult::Success
		|| GEngine.RegisterClass<FReadingSensorComponent>(ReadingSensorComponentTypeId, "ReadingSensorComponent")
			!= MicroWorld::Engine::EObjectResult::Success)
	{
		MW_LOG(Error, "ex22", "class registration failed");
		return;
	}

	const MicroWorld::Engine::TObjectPtr<MicroWorld::Engine::UWorld> World = GEngine.CreateWorld();
	const MicroWorld::Engine::TObjectPtr<FReadingSensorComponent> Sensor =
		GEngine.CreateObject<FReadingSensorComponent>(ReadingSensorComponentTypeId).Object;
	const MicroWorld::Engine::TObjectPtr<FThermometerActor> Thermometer =
		GEngine.CreateObject<FThermometerActor>(ThermometerActorTypeId, GRouter, Sensor).Object;
	if (World.Get() == nullptr || Sensor.Get() == nullptr || Thermometer.Get() == nullptr)
	{
		MW_LOG(Error, "ex22", "world or object creation failed");
		return;
	}

	if (Thermometer.Get()->RegisterComponent(MicroWorld::Engine::TObjectPtr<MicroWorld::Engine::UActorComponent>{Sensor})
			!= MicroWorld::Engine::EEngineResult::Success
		|| GEngine.GetWorld().RegisterActor(MicroWorld::Engine::TObjectPtr<MicroWorld::Engine::AActor>{Thermometer})
			!= MicroWorld::Engine::EEngineResult::Success)
	{
		MW_LOG(Error, "ex22", "component or actor registration failed");
		return;
	}

	// Queue the display during composition so BeginPlay proves typed spawning also works before play starts.
	// Deferred spawning stores arguments by value; std::ref preserves the router injection without copying it.
	const MicroWorld::Engine::FActorSpawnRequest DisplaySpawnRequest = GEngine.GetWorld().SpawnActor<FDisplayActor>(std::ref(GRouter));
	if (DisplaySpawnRequest.Result != MicroWorld::Engine::EActorSpawnRequestResult::Queued)
	{
		MW_LOG(Error, "ex22", "display spawn request failed");
		return;
	}

	// Start scheduling from a caller-supplied time point -- no hidden clock read.
	const MicroWorld::Core::TimePointMilliseconds BootTime = GTimeSource.Now();
	if (GEngine.BeginPlay(BootTime) != MicroWorld::Core::ERuntimeResult::Success)
	{
		MW_LOG(Error, "ex22", "engine begin play failed");
		return;
	}

	const MicroWorld::Engine::FActorSpawnStatus DisplaySpawnStatus = GEngine.GetWorld().GetSpawnStatus(DisplaySpawnRequest.Handle);
	FDisplayActor* const DisplayPtr = static_cast<FDisplayActor*>(DisplaySpawnStatus.Actor.Get());
	if (DisplaySpawnStatus.State != MicroWorld::Engine::EActorSpawnState::Spawned || DisplayPtr == nullptr)
	{
		MW_LOG(Error, "ex22", "display spawn failed during begin play");
		return;
	}

	// Bounded, deterministic run: stop as soon as the display's own query says done
	// (no side effects in the loop condition, just IsDone()).
	while (!DisplayPtr->IsDone())
	{
		if (GEngine.Tick(GTimeSource.Now()) != MicroWorld::Core::ERuntimeResult::Success)
		{
			MW_LOG(Error, "ex22", "engine tick failed");
			break;
		}
		MicroWorld::Platform::Esp32::SleepMilliseconds(PollPacingMilliseconds);
	}

	(void)GEngine.EndPlay();
	MW_LOG(Log, "ex22", "done readings=%u", static_cast<unsigned>(DisplayPtr->GetReceivedReadingCount()));
}
