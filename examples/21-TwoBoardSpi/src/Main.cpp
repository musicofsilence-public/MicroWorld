#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Platform/Esp32/Esp32OutputDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32SpiDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>
#include <MicroWorld/Platform/Esp32/SpiAddress.h>

#include <cstddef>
#include <cstdint>

// Role is chosen at build time by the platformio.ini environment, exactly as the
// two-board WiFi, LoRa, UART, and I2C examples select theirs — never with build_src_filter.
#ifndef MICROWORLD_EXAMPLE_SPI_MASTER
#error "Define MICROWORLD_EXAMPLE_SPI_MASTER=1 (master) or 0 (slave) via the master / slave build environment."
#endif

namespace
{
/** Motivation: Node ids stamped on frames: the master is 1, the slave is 2 (point-to-point). */
constexpr std::uint8_t MasterNodeId = 1;
constexpr std::uint8_t SlaveNodeId = 2;

/** Motivation: SPI2 host and the four bus GPIOs, wired straight through by signal name (Appendix B4). */
constexpr std::int32_t SpiHostNumber = 1; // SPI2_HOST
constexpr std::int32_t MosiGpioNumber = 11;
constexpr std::int32_t MisoGpioNumber = 13;
constexpr std::int32_t SclkGpioNumber = 12;
constexpr std::int32_t CsGpioNumber = 10;

/** Motivation: 1 MHz is reliable over short jumper wires; the slave takes its clock from the master. */
constexpr std::uint32_t SpiClockHz = 1000000;

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
const char* ToText(const MicroWorld::Transport::ETransportResult Result) noexcept
{
	switch (Result)
	{
		case MicroWorld::Transport::ETransportResult::Success:
			return "Success";
		case MicroWorld::Transport::ETransportResult::Full:
			return "Full";
		case MicroWorld::Transport::ETransportResult::Invalid:
			return "Invalid";
		case MicroWorld::Transport::ETransportResult::Unavailable:
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

#if MICROWORLD_EXAMPLE_SPI_MASTER
/**
 * Motivation: Lets the master build its device configuration from the fixed pins and clock in one
 *   place, so those values are never restated.
 * Responsibilities: Fill the master config with the shared host, GPIO, clock, and node id values.
 */
MicroWorld::Platform::Esp32::FEsp32SpiMasterConfig MakeMasterConfig() noexcept
{
	MicroWorld::Platform::Esp32::FEsp32SpiMasterConfig Config;
	Config.SpiHost = SpiHostNumber;
	Config.MosiGpio = MosiGpioNumber;
	Config.MisoGpio = MisoGpioNumber;
	Config.SclkGpio = SclkGpioNumber;
	Config.CsGpio = CsGpioNumber;
	Config.ClockHz = SpiClockHz;
	Config.LocalNodeId = MasterNodeId;
	return Config;
}

/**
 * Motivation: Lets the master board clock the bus and pace every volley, so the slave can stay purely
 *   reactive.
 * Responsibilities: Open the master device, send counters on a fixed cadence, and poll reads to
 *   harvest the slave's pipelined replies.
 */
void RunMaster() noexcept
{
	// Static, never on the app_main stack (the ESP32-S3 stack lesson, §2.2); its DMA buffers must live here.
	static MicroWorld::Platform::Esp32::FEsp32TimeSource TimeSource{};
	static MicroWorld::Platform::Esp32::FEsp32SpiMasterDevice Device{MakeMasterConfig()};
	MW_LOG(Log, "ex21", "master open=%d", Device.IsOpen() ? 1 : 0);
	MW_LOG(Log, "ex21", "master clocks the bus; the slave only reacts");
	if (!Device.IsOpen())
	{
		// A failed bus open cannot recover here; stop with a clear line instead of looping.
		MW_LOG(Error, "ex21", "spi master failed to open; halting");
		return;
	}

	static std::uint8_t RxBuffer[MicroWorld::Platform::Esp32::SpiMaxPayloadBytes];
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
			const MicroWorld::Transport::ETransportResult TxResult = Device.TrySend(
				MicroWorld::Platform::Esp32::MakeSpiAddress(SlaveNodeId), MicroWorld::Core::TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
			MW_LOG(Log, "ex21", "tx n=%u result=%s", static_cast<unsigned>(NextCounter), ToText(TxResult));
			if (TxResult == MicroWorld::Transport::ETransportResult::Success)
			{
				bAwaitingReply = true;
			}
		}

		// Only the master clocks the bus, so it polls reads to harvest the slave's reply (pipelined by a
		// transaction on SPI); each poll clocks one transfer until the reply arrives.
		if (bAwaitingReply)
		{
			MicroWorld::Transport::Address::FDeviceAddress From{};
			MicroWorld::Core::FReceiveResult Received{};
			const MicroWorld::Transport::ETransportResult RxResult =
				Device.TryReceive(From, MicroWorld::Core::TSpan<std::uint8_t>(RxBuffer, sizeof(RxBuffer)), Received);
			if (RxResult == MicroWorld::Transport::ETransportResult::Success && Received.BytesReceived == VolleyPayloadBytes)
			{
				const std::uint32_t Counter = ReadVolleyCounter(RxBuffer);
				const std::uint8_t FromId = MicroWorld::Platform::Esp32::SpiAddressNodeId(From);
				MW_LOG(Log, "ex21", "rx n=%u from=%u", static_cast<unsigned>(Counter), static_cast<unsigned>(FromId));
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
 * Motivation: Lets the slave build its device configuration from the fixed pins in one place, so those
 *   values are never restated.
 * Responsibilities: Fill the slave config with the shared host, GPIO, and node id values.
 */
MicroWorld::Platform::Esp32::FEsp32SpiSlaveConfig MakeSlaveConfig() noexcept
{
	MicroWorld::Platform::Esp32::FEsp32SpiSlaveConfig Config;
	Config.SpiHost = SpiHostNumber;
	Config.MosiGpio = MosiGpioNumber;
	Config.MisoGpio = MisoGpioNumber;
	Config.SclkGpio = SclkGpioNumber;
	Config.CsGpio = CsGpioNumber;
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
	// Static, never on the app_main stack (§2.2); its DMA buffers must live here.
	static MicroWorld::Platform::Esp32::FEsp32SpiSlaveDevice Device{MakeSlaveConfig()};
	MW_LOG(Log, "ex21", "slave open=%d", Device.IsOpen() ? 1 : 0);
	if (!Device.IsOpen())
	{
		// A failed bus open cannot recover here; stop with a clear line instead of looping.
		MW_LOG(Error, "ex21", "spi slave failed to open; halting");
		return;
	}

	static std::uint8_t RxBuffer[MicroWorld::Platform::Esp32::SpiMaxPayloadBytes];

	for (;;)
	{
		MicroWorld::Transport::Address::FDeviceAddress From{};
		MicroWorld::Core::FReceiveResult Received{};
		const MicroWorld::Transport::ETransportResult RxResult =
			Device.TryReceive(From, MicroWorld::Core::TSpan<std::uint8_t>(RxBuffer, sizeof(RxBuffer)), Received);
		if (RxResult == MicroWorld::Transport::ETransportResult::Success && Received.BytesReceived == VolleyPayloadBytes)
		{
			const std::uint32_t Counter = ReadVolleyCounter(RxBuffer);
			const std::uint8_t FromId = MicroWorld::Platform::Esp32::SpiAddressNodeId(From);
			MW_LOG(Log, "ex21", "rx n=%u from=%u", static_cast<unsigned>(Counter), static_cast<unsigned>(FromId));

			// Stage the reply (counter + 1) for the master's next read; the master clocks it out.
			std::uint8_t Payload[VolleyPayloadBytes];
			WriteVolleyPayload(Payload, SlaveNodeId, Counter + 1);
			const MicroWorld::Transport::ETransportResult TxResult = Device.TrySend(
				MicroWorld::Platform::Esp32::MakeSpiAddress(MasterNodeId), MicroWorld::Core::TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
			MW_LOG(Log, "ex21", "tx n=%u result=%s", static_cast<unsigned>(Counter + 1), ToText(TxResult));
		}

		MicroWorld::Platform::Esp32::SleepMilliseconds(PollPacingMilliseconds);
	}
}
#endif
} // namespace

/**
 * Motivation: Application entry point for example 21: the single ESP32 `app_main` stays a thin
 *   build-time role selector over a wired SPI bus.
 * Responsibilities: Install the output device, then run the master or slave counter volley.
 */
extern "C" void app_main(void)
{
	MicroWorld::Core::SetOutputDevice(&MicroWorld::Platform::Esp32::WriteEsp32LogRecord);
#if MICROWORLD_EXAMPLE_SPI_MASTER
	RunMaster();
#else
	RunSlave();
#endif
}
