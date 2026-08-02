#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/ReceiveResult.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Platform/Esp32/Esp32I2cDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32OutputDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>
#include <MicroWorld/Platform/Esp32/I2cAddress.h>

#include <cstddef>
#include <cstdint>

// Role is chosen at build time by the platformio.ini environment, exactly as the
// two-board WiFi, LoRa, and UART examples select theirs — never with build_src_filter.
#ifndef MICROWORLD_EXAMPLE_I2C_MASTER
#error "Define MICROWORLD_EXAMPLE_I2C_MASTER=1 (master) or 0 (slave) via the master / slave build environment."
#endif

namespace
{
/** Motivation: Node ids stamped on frames: the master is 1, the slave is 2 (point-to-point). */
constexpr std::uint8_t MasterNodeId = 1;
constexpr std::uint8_t SlaveNodeId = 2;

/** Motivation: I2C port and the two bus GPIOs shared crossover-free (SDA<->SDA, SCL<->SCL). */
constexpr std::int32_t I2cPortNumber = 0;
constexpr std::int32_t SdaGpioNumber = 8;
constexpr std::int32_t SclGpioNumber = 9;

/** Motivation: 100 kHz standard mode is reliable over short jumper wires with the external pull-ups. */
constexpr std::uint32_t I2cSclSpeedHz = 100000;

/** Motivation: The slave's 7-bit bus address the master clocks; not the frame node id. */
constexpr std::uint8_t I2cSlaveBusAddress = 0x28;

/** Motivation: Volley period: half a second, since a short wired bus is fast. */
constexpr std::uint64_t VolleyPeriodMilliseconds = 500;

/** Motivation: Poll far faster than the volley so the FreeRTOS idle task (and its watchdog) always runs. */
constexpr unsigned PollPacingMilliseconds = 10;

/** Motivation: Volley payload layout: byte 0 is the sender node id, bytes 1..4 the counter (big-endian). */
constexpr std::size_t VolleyPayloadBytes = 5;

/**
 * Motivation: Lets the serial trace render one device outcome as a short label, so logs read plainly
 *   without restating the enum-to-text mapping at each call site.
 * Responsibilities: Map each transport result to one stable label string.
 */
const char* ToText(const MicroWorld::Core::ETransportResult Result) noexcept
{
	switch (Result)
	{
		case MicroWorld::Core::ETransportResult::Success:
			return "Success";
		case MicroWorld::Core::ETransportResult::Full:
			return "Full";
		case MicroWorld::Core::ETransportResult::Invalid:
			return "Invalid";
		case MicroWorld::Core::ETransportResult::Unavailable:
			return "Unavailable";
		default:
			return "unknown";
	}
}

/**
 * Motivation: Lets one side pack the sender id and counter into the five-byte volley payload so the
 *   wire layout is stated in a single place.
 * Responsibilities: Write the sender id at byte 0 and the counter big-endian into bytes 1..4.
 */
void WriteVolleyPayload(std::uint8_t* const Out, const std::uint8_t SenderId, const std::uint32_t Counter) noexcept
{
	Out[0] = SenderId;
	Out[1] = static_cast<std::uint8_t>((Counter >> 24) & 0xFFu);
	Out[2] = static_cast<std::uint8_t>((Counter >> 16) & 0xFFu);
	Out[3] = static_cast<std::uint8_t>((Counter >> 8) & 0xFFu);
	Out[4] = static_cast<std::uint8_t>(Counter & 0xFFu);
}

/**
 * Motivation: Lets the receiver read the big-endian counter back out of a received volley payload,
 *   mirroring the writer so the two halves agree on the layout.
 * Responsibilities: Reassemble the counter from bytes 1..4 in big-endian order.
 */
std::uint32_t ReadVolleyCounter(const std::uint8_t* const In) noexcept
{
	return (static_cast<std::uint32_t>(In[1]) << 24) | (static_cast<std::uint32_t>(In[2]) << 16) | (static_cast<std::uint32_t>(In[3]) << 8)
		| static_cast<std::uint32_t>(In[4]);
}

#if MICROWORLD_EXAMPLE_I2C_MASTER
/**
 * Motivation: Lets the master build its device configuration from the fixed pins, speed, and peer
 *   address in one place, so those values are never restated.
 * Responsibilities: Fill the master config with the shared port, GPIO, speed, slave address, and node id.
 */
MicroWorld::Platform::Esp32::FEsp32I2cMasterConfig MakeMasterConfig() noexcept
{
	MicroWorld::Platform::Esp32::FEsp32I2cMasterConfig Config;
	Config.I2cPort = I2cPortNumber;
	Config.SdaGpio = SdaGpioNumber;
	Config.SclGpio = SclGpioNumber;
	Config.SclSpeedHz = I2cSclSpeedHz;
	Config.SlaveAddress = I2cSlaveBusAddress;
	Config.LocalNodeId = MasterNodeId;
	return Config;
}

/**
 * Motivation: Lets the master board clock the bus and pace every volley, so the slave can stay purely
 *   reactive.
 * Responsibilities: Open the master device, send counters on a fixed cadence, and poll reads to
 *   harvest the slave's staged replies.
 */
void RunMaster() noexcept
{
	// Static, never on the app_main stack (the ESP32-S3 stack lesson, §2.2).
	static MicroWorld::Platform::Esp32::FEsp32TimeSource TimeSource{};
	static MicroWorld::Platform::Esp32::FEsp32I2cMasterDevice Device{MakeMasterConfig()};
	MW_LOG(Log, "ex20", "master open=%d", Device.IsOpen() ? 1 : 0);
	MW_LOG(Log, "ex20", "master clocks the bus; the slave only reacts");
	if (!Device.IsOpen())
	{
		// A failed bus open cannot recover here; stop with a clear line instead of looping.
		MW_LOG(Error, "ex20", "i2c master failed to open; halting");
		return;
	}

	static std::uint8_t RxBuffer[MicroWorld::Platform::Esp32::I2cMaxPayloadBytes];
	bool bAwaitingReply = false;
	std::uint32_t NextCounter = 1;
	std::uint64_t NextSendDueMilliseconds = TimeSource.Now() + VolleyPeriodMilliseconds;

	for (;;)
	{
		const std::uint64_t Now = TimeSource.Now();

		// Send the next counter when it is due and no reply is still outstanding.
		if (!bAwaitingReply && Now >= NextSendDueMilliseconds)
		{
			std::uint8_t Payload[VolleyPayloadBytes];
			WriteVolleyPayload(Payload, MasterNodeId, NextCounter);
			const MicroWorld::Core::ETransportResult TxResult = Device.TrySend(
				MicroWorld::Platform::Esp32::MakeI2cAddress(SlaveNodeId), MicroWorld::Core::TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
			MW_LOG(Log, "ex20", "tx n=%u result=%s", static_cast<unsigned>(NextCounter), ToText(TxResult));
			if (TxResult == MicroWorld::Core::ETransportResult::Success)
			{
				bAwaitingReply = true;
			}
		}

		// Only the master clocks the bus, so it must poll reads to harvest the slave's staged reply.
		if (bAwaitingReply)
		{
			MicroWorld::Core::FDeviceAddress From{};
			MicroWorld::Core::FReceiveResult Received{};
			const MicroWorld::Core::ETransportResult RxResult =
				Device.TryReceive(From, MicroWorld::Core::TSpan<std::uint8_t>(RxBuffer, sizeof(RxBuffer)), Received);
			if (RxResult == MicroWorld::Core::ETransportResult::Success && Received.BytesReceived == VolleyPayloadBytes)
			{
				const std::uint32_t Counter = ReadVolleyCounter(RxBuffer);
				const std::uint8_t FromId = MicroWorld::Platform::Esp32::I2cAddressNodeId(From);
				MW_LOG(Log, "ex20", "rx n=%u from=%u", static_cast<unsigned>(Counter), static_cast<unsigned>(FromId));
				NextCounter = Counter + 1;
				bAwaitingReply = false;
				NextSendDueMilliseconds = Now + VolleyPeriodMilliseconds;
			}
		}

		MicroWorld::Platform::Esp32::SleepMilliseconds(PollPacingMilliseconds);
	}
}
#else
/**
 * Motivation: Lets the slave build its device configuration from the fixed pins and its own bus
 *   address in one place, so those values are never restated.
 * Responsibilities: Fill the slave config with the shared port, GPIO, slave address, and node id.
 */
MicroWorld::Platform::Esp32::FEsp32I2cSlaveConfig MakeSlaveConfig() noexcept
{
	MicroWorld::Platform::Esp32::FEsp32I2cSlaveConfig Config;
	Config.I2cPort = I2cPortNumber;
	Config.SdaGpio = SdaGpioNumber;
	Config.SclGpio = SclGpioNumber;
	Config.SlaveAddress = I2cSlaveBusAddress;
	Config.LocalNodeId = SlaveNodeId;
	return Config;
}

/**
 * Motivation: Lets the slave board stay purely reactive, so on each received counter it stages a reply
 *   for the master's next read.
 * Responsibilities: Open the slave device, receive counters, and stage counter-plus-one replies.
 */
void RunSlave() noexcept
{
	static MicroWorld::Platform::Esp32::FEsp32I2cSlaveDevice Device{MakeSlaveConfig()};
	MW_LOG(Log, "ex20", "slave open=%d", Device.IsOpen() ? 1 : 0);
	if (!Device.IsOpen())
	{
		// A failed bus open cannot recover here; stop with a clear line instead of looping.
		MW_LOG(Error, "ex20", "i2c slave failed to open; halting");
		return;
	}

	static std::uint8_t RxBuffer[MicroWorld::Platform::Esp32::I2cMaxPayloadBytes];

	for (;;)
	{
		MicroWorld::Core::FDeviceAddress From{};
		MicroWorld::Core::FReceiveResult Received{};
		const MicroWorld::Core::ETransportResult RxResult =
			Device.TryReceive(From, MicroWorld::Core::TSpan<std::uint8_t>(RxBuffer, sizeof(RxBuffer)), Received);
		if (RxResult == MicroWorld::Core::ETransportResult::Success && Received.BytesReceived == VolleyPayloadBytes)
		{
			const std::uint32_t Counter = ReadVolleyCounter(RxBuffer);
			const std::uint8_t FromId = MicroWorld::Platform::Esp32::I2cAddressNodeId(From);
			MW_LOG(Log, "ex20", "rx n=%u from=%u", static_cast<unsigned>(Counter), static_cast<unsigned>(FromId));

			// Stage the reply (counter + 1) for the master's next read; the master clocks it out.
			std::uint8_t Payload[VolleyPayloadBytes];
			WriteVolleyPayload(Payload, SlaveNodeId, Counter + 1);
			const MicroWorld::Core::ETransportResult TxResult = Device.TrySend(
				MicroWorld::Platform::Esp32::MakeI2cAddress(MasterNodeId), MicroWorld::Core::TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
			MW_LOG(Log, "ex20", "tx n=%u result=%s", static_cast<unsigned>(Counter + 1), ToText(TxResult));
		}

		MicroWorld::Platform::Esp32::SleepMilliseconds(PollPacingMilliseconds);
	}
}
#endif
} // namespace

/**
 * Motivation: Application entry point for example 20: the single ESP32 `app_main` stays a thin
 *   build-time role selector over a wired I2C bus.
 * Responsibilities: Install the output device, then run the master or slave counter volley.
 */
extern "C" void app_main(void)
{
	MicroWorld::Core::SetOutputDevice(&MicroWorld::Platform::Esp32::WriteEsp32LogRecord);
#if MICROWORLD_EXAMPLE_I2C_MASTER
	RunMaster();
#else
	RunSlave();
#endif
}
