#include <MicroWorld/Log.h>
#include <MicroWorld/Version.h>

#include <MicroWorld/Containers/Span.h>

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineHost.h>
#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Engine/InlineTypes.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessageRouter.h>

#include <MicroWorld/Object/ClassDescriptor.h>
#include <MicroWorld/Object/ObjectPtr.h>

#include <MicroWorld/PlatformEsp32/Esp32OutputDevice.h>
#include <MicroWorld/PlatformEsp32/Esp32Sleep.h>
#include <MicroWorld/PlatformEsp32/Esp32TimeSource.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{

// ---- Message and managed-type ids (roadmap section 4.5) ------------------

/** Identifies the thermometer's broadcast reading message. */
inline constexpr MicroWorld::FMessageTypeId TemperatureReadingMessageId = 1;

/** Identifies the display's targeted calibrate message. */
inline constexpr MicroWorld::FMessageTypeId CalibrateMessageId = 2;

/** Actor id the display targets its calibrate send at, and the thermometer
 *  registers its calibrate handler under. */
inline constexpr MicroWorld::FMessageActorId ThermometerActorId = 10;

/** Actor id recorded as the sender of the calibrate message; not currently
 *  used as anyone's listener id, since the thermometer never messages it back. */
inline constexpr MicroWorld::FMessageActorId DisplayActorId = 11;

/** Stable descriptor id for the managed FThermometerActor type (0x0016 == example 22). */
constexpr MicroWorld::FTypeId ThermometerActorTypeId{0x00160001u};

/** Stable descriptor id for the managed FReadingSensorComponent type. */
constexpr MicroWorld::FTypeId ReadingSensorComponentTypeId{0x00160003u};

// ---- Shared cadence and bounds ---------------------------------------------

/**
 * Cadence shared by the sensor's tick config and the thermometer actor's own
 * tick config, so the two schedules can never drift apart (one named constant
 * instead of two copies of "500" that a future edit could desync). See
 * FThermometerActor::Tick for why same-cadence alignment matters.
 */
constexpr MicroWorld::DurationMilliseconds ReadingCadenceMilliseconds = 500;

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
class FReadingSensorComponent final : public MicroWorld::UActorComponent
{
public:
	/** Selects the 500 ms reading cadence the thermometer actor's own tick is aligned to. */
	FReadingSensorComponent() noexcept : UActorComponent(MicroWorld::FTickConfiguration::EnabledEvery(ReadingCadenceMilliseconds)) {}

	/** Returns the most recently produced reading; the owning actor packs this into its broadcast. */
	std::uint16_t GetLatestReading() const noexcept { return LatestReading; }

	/** Returns how many readings have been produced since construction or the last calibrate reset. */
	std::uint32_t GetReadingCount() const noexcept { return ReadingCount; }

	/** Resets the reading counter; called by the thermometer's calibrate handler. */
	void ResetReadingCount() noexcept { ReadingCount = 0; }

protected:
	/** Advances the counter and derives this frame's reading from it -- see the class comment for why. */
	void TickComponent(const MicroWorld::FTickContext&) noexcept override
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
class FThermometerActor final : public MicroWorld::TInlineActor<1>
{
public:
	/**
	 * Aligns this actor's own tick to the sensor's 500 ms cadence (see Tick's comment
	 * for why the alignment matters) and stores the router and sensor this actor was
	 * composed with.
	 */
	FThermometerActor(MicroWorld::IMessageRouter& InRouter, MicroWorld::TObjectPtr<FReadingSensorComponent> InSensor) noexcept
		: TInlineActor<1>(MicroWorld::FTickConfiguration::EnabledEvery(ReadingCadenceMilliseconds)), Router(InRouter), Sensor(InSensor)
	{
	}

protected:
	/** Subscribes to a calibrate message targeted at this actor's own id. */
	void BeginPlay() noexcept override
	{
		MicroWorld::FMessageHandlerBinding Handler;
		const MicroWorld::EDelegateResult BindResult =
			Handler.Bind([this](const MicroWorld::FMessageView& View) noexcept { this->OnCalibrateReceived(View); });
		if (BindResult != MicroWorld::EDelegateResult::Success)
		{
			MW_LOG(Error, "ex22", "thermometer calibrate handler bind failed");
			return;
		}

		// This bounded example never removes handlers (EndPlay tears the router and
		// its actors down together), so the returned handle is not retained.
		MicroWorld::FMessageHandlerHandle CalibrateHandlerHandle;
		const MicroWorld::EMessageResult AddResult =
			Router.AddMessageHandler(CalibrateMessageId, ThermometerActorId, std::move(Handler), CalibrateHandlerHandle);
		if (AddResult != MicroWorld::EMessageResult::Success)
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
	void Tick(const MicroWorld::FTickContext&) noexcept override
	{
		FReadingSensorComponent* const SensorPtr = Sensor.Get();
		const std::uint16_t Reading = SensorPtr->GetLatestReading();

		// Little-endian: low byte first, matching the engine's on-wire convention.
		std::uint8_t Payload[ReadingPayloadBytes];
		Payload[0] = static_cast<std::uint8_t>(Reading & 0xFFu);
		Payload[1] = static_cast<std::uint8_t>((Reading >> 8) & 0xFFu);

		const MicroWorld::EMessageResult SendResult = Router.BroadcastMessage(
			MicroWorld::LocalChannelId,
			TemperatureReadingMessageId,
			ThermometerActorId,
			MicroWorld::TSpan<const std::uint8_t>(Payload, ReadingPayloadBytes));
		if (SendResult != MicroWorld::EMessageResult::Success)
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
	void OnCalibrateReceived(const MicroWorld::FMessageView&) noexcept
	{
		MW_LOG(Log, "ex22", "thermometer calibrated (reset reading counter)");
		Sensor.Get()->ResetReadingCount();
	}

	/** Router this actor sends and subscribes through; injected at construction (D9), never a global. */
	MicroWorld::IMessageRouter& Router;

	/** Sensor this actor owns and reads each tick; registered as this actor's one inline component slot. */
	MicroWorld::TObjectPtr<FReadingSensorComponent> Sensor;
};

/**
 * Purely reactive: logs every broadcast reading and, after enough of them, sends
 * one targeted calibrate back to the thermometer to demonstrate SendMessageToActor.
 *
 * Uses TInlineActor<0> because it owns no components -- a zero-element fixed-size
 * array is valid in C++17, so the zero-capacity component registry this
 * instantiates compiles cleanly (confirmed by this file's own build).
 */
class FDisplayActor final : public MicroWorld::TInlineActor<0>
{
public:
	/** Stores the injected router (D9); this actor never ticks on its own (see the disabled tick config below). */
	explicit FDisplayActor(MicroWorld::IMessageRouter& InRouter) noexcept
		: TInlineActor<0>({/*bCanEverTick*/ false, /*bStartWithTickEnabled*/ false, /*TickIntervalMilliseconds*/ 0}), Router(InRouter)
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
		MicroWorld::FMessageHandlerBinding Handler;
		const MicroWorld::EDelegateResult BindResult =
			Handler.Bind([this](const MicroWorld::FMessageView& View) noexcept { this->OnReadingReceived(View); });
		if (BindResult != MicroWorld::EDelegateResult::Success)
		{
			MW_LOG(Error, "ex22", "display reading handler bind failed");
			return;
		}

		// This bounded example never removes handlers (EndPlay tears the router and
		// its actors down together), so the returned handle is not retained.
		MicroWorld::FMessageHandlerHandle ReadingHandlerHandle;
		const MicroWorld::EMessageResult AddResult =
			Router.AddMessageHandler(TemperatureReadingMessageId, MicroWorld::BroadcastActorId, std::move(Handler), ReadingHandlerHandle);
		if (AddResult != MicroWorld::EMessageResult::Success)
		{
			MW_LOG(Error, "ex22", "display reading handler registration failed");
		}
	}

private:
	/** Decodes, logs, and counts one reading; triggers the one-time calibrate once CalibrateAfterReadingCount is reached. */
	void OnReadingReceived(const MicroWorld::FMessageView& View) noexcept
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
		const MicroWorld::EMessageResult SendResult = Router.SendMessageToActor(
			MicroWorld::LocalChannelId, CalibrateMessageId, ThermometerActorId, DisplayActorId, MicroWorld::TSpan<const std::uint8_t>{});
		if (SendResult != MicroWorld::EMessageResult::Success)
		{
			MW_LOG(Error, "ex22", "display calibrate send failed");
			return;
		}

		bCalibrateSent = true;
		MW_LOG(Log, "ex22", "display sent calibrate to thermometer");
	}

	/** Router this actor listens and sends through; injected at construction (D9), never a global. */
	MicroWorld::IMessageRouter& Router;

	/** Counts every reading received since BeginPlay; drives both the calibrate trigger and IsDone. */
	std::uint32_t ReceivedReadingCount{0};

	/** Guards against resending calibrate once it has already been sent. */
	bool bCalibrateSent{false};
};

/** Single real-time source; every MicroWorld deadline in this example reads it. */
MicroWorld::FEsp32TimeSource GTimeSource{};

/** Poll far faster than any schedule so the FreeRTOS idle task (and its watchdog)
 *  always runs; the engine's tick schedules, not this delay, decide what fires. */
constexpr unsigned PollPacingMilliseconds = 10;

} // namespace

/**
 * Composition root: wires one router, one engine host, one sensor, and two actors,
 * then drives a bounded run that demonstrates local actor messaging end to end --
 * a broadcast reading, a display that counts them, and a targeted calibrate reply.
 */
extern "C" void app_main(void)
{
	MicroWorld::SetOutputDevice(&MicroWorld::WriteEsp32LogRecord);

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
	static MicroWorld::TMessageRouter<16, 8, 96, 1> GRouter;

	// Per-tick garbage-collection slice for this tiny graph: one root (the world),
	// a few mark steps for world -> actor -> component, and enough sweep steps to
	// scan every object slot -- matching Modules/Engine/examples/HostLifecycle/Main.cpp.
	constexpr std::uint32_t GcRootOperationsPerTick = 1;
	constexpr std::uint32_t GcMarkOperationsPerTick = 4;
	constexpr std::uint32_t GcSweepOperationsPerTick = 8;

	// Owns every managed subsystem and pumps GRouter as its network frame, so
	// Tick's step 1 (PreAdvance) and step 7 (PostAdvance) drive local delivery.
	static MicroWorld::TEngine<> GEngine{
		MicroWorld::FGarbageCollectionBudget{GcRootOperationsPerTick, GcMarkOperationsPerTick, GcSweepOperationsPerTick}, GRouter};

	if (GEngine.RegisterClass<FThermometerActor>(ThermometerActorTypeId, "ThermometerActor") != MicroWorld::EObjectResult::Success
		|| GEngine.RegisterClass<FReadingSensorComponent>(ReadingSensorComponentTypeId, "ReadingSensorComponent")
			!= MicroWorld::EObjectResult::Success)
	{
		MW_LOG(Error, "ex22", "class registration failed");
		return;
	}

	const MicroWorld::TObjectPtr<MicroWorld::UWorld> World = GEngine.CreateWorld();
	const MicroWorld::TObjectPtr<FReadingSensorComponent> Sensor = GEngine.CreateObject<FReadingSensorComponent>(ReadingSensorComponentTypeId).Object;
	const MicroWorld::TObjectPtr<FThermometerActor> Thermometer =
		GEngine.CreateObject<FThermometerActor>(ThermometerActorTypeId, GRouter, Sensor).Object;
	if (World.Get() == nullptr || Sensor.Get() == nullptr || Thermometer.Get() == nullptr)
	{
		MW_LOG(Error, "ex22", "world or object creation failed");
		return;
	}

	if (Thermometer.Get()->RegisterComponent(MicroWorld::TObjectPtr<MicroWorld::UActorComponent>{Sensor}) != MicroWorld::EEngineResult::Success
		|| GEngine.GetWorld().RegisterActor(MicroWorld::TObjectPtr<MicroWorld::AActor>{Thermometer}) != MicroWorld::EEngineResult::Success)
	{
		MW_LOG(Error, "ex22", "component or actor registration failed");
		return;
	}

	// Queue the display during composition so BeginPlay proves typed spawning also works before play starts.
	const MicroWorld::FActorSpawnRequest DisplaySpawnRequest = GEngine.GetWorld().SpawnActor<FDisplayActor>(GRouter);
	if (DisplaySpawnRequest.Result != MicroWorld::EActorSpawnRequestResult::Queued)
	{
		MW_LOG(Error, "ex22", "display spawn request failed");
		return;
	}

	// Start scheduling from a caller-supplied time point -- no hidden clock read.
	const MicroWorld::TimePointMilliseconds BootTime = GTimeSource.Now();
	if (GEngine.BeginPlay(BootTime) != MicroWorld::ERuntimeResult::Success)
	{
		MW_LOG(Error, "ex22", "engine begin play failed");
		return;
	}

	const MicroWorld::FActorSpawnStatus DisplaySpawnStatus = GEngine.GetWorld().GetSpawnStatus(DisplaySpawnRequest.Handle);
	FDisplayActor* const DisplayPtr = static_cast<FDisplayActor*>(DisplaySpawnStatus.Actor.Get());
	if (DisplaySpawnStatus.State != MicroWorld::EActorSpawnState::Spawned || DisplayPtr == nullptr)
	{
		MW_LOG(Error, "ex22", "display spawn failed during begin play");
		return;
	}

	// Bounded, deterministic run: stop as soon as the display's own query says done
	// (no side effects in the loop condition, just IsDone()).
	while (!DisplayPtr->IsDone())
	{
		if (GEngine.Tick(GTimeSource.Now()) != MicroWorld::ERuntimeResult::Success)
		{
			MW_LOG(Error, "ex22", "engine tick failed");
			break;
		}
		MicroWorld::SleepMilliseconds(PollPacingMilliseconds);
	}

	(void)GEngine.EndPlay();
	MW_LOG(Log, "ex22", "done readings=%u", static_cast<unsigned>(DisplayPtr->GetReceivedReadingCount()));
}
