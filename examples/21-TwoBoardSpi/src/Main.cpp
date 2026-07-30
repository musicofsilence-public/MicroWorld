#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/NetResult.h>
#include <MicroWorld/Platform/Esp32/Esp32OutputDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32SpiDriver.h>
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
/** Node ids stamped on frames: the master is 1, the slave is 2 (point-to-point). */
constexpr std::uint8_t MasterNodeId = 1;
constexpr std::uint8_t SlaveNodeId = 2;

/** SPI2 host and the four bus GPIOs, wired straight through by signal name (Appendix B4). */
constexpr std::int32_t SpiHostNumber = 1; // SPI2_HOST
constexpr std::int32_t MosiGpioNumber = 11;
constexpr std::int32_t MisoGpioNumber = 13;
constexpr std::int32_t SclkGpioNumber = 12;
constexpr std::int32_t CsGpioNumber = 10;

/** 1 MHz is reliable over short jumper wires; the slave takes its clock from the master. */
constexpr std::uint32_t SpiClockHz = 1000000;

/** Volley period: half a second, since a short wired bus is fast. */
constexpr std::uint64_t VolleyPeriodMilliseconds = 500;

/** Poll far faster than the volley so the FreeRTOS idle task (and its watchdog) always runs. */
constexpr unsigned PollPacingMilliseconds = 10;

/** Volley payload layout: byte 0 is the sender node id, bytes 1..4 the counter (big-endian). */
constexpr std::size_t VolleyPayloadBytes = 5;

/** Renders one driver outcome as a short label so the serial trace reads plainly. */
const char* ToText(const MicroWorld::ENetResult Result) noexcept
{
	switch (Result)
	{
		case MicroWorld::ENetResult::Success:
			return "Success";
		case MicroWorld::ENetResult::Full:
			return "Full";
		case MicroWorld::ENetResult::Invalid:
			return "Invalid";
		case MicroWorld::ENetResult::Unavailable:
			return "Unavailable";
		default:
			return "unknown";
	}
}

/** Packs the sender id and counter into the five-byte volley payload. */
void WriteVolleyPayload(std::uint8_t* const Out, const std::uint8_t SenderId, const std::uint32_t Counter) noexcept
{
	Out[0] = SenderId;
	Out[1] = static_cast<std::uint8_t>((Counter >> 24) & 0xFFu);
	Out[2] = static_cast<std::uint8_t>((Counter >> 16) & 0xFFu);
	Out[3] = static_cast<std::uint8_t>((Counter >> 8) & 0xFFu);
	Out[4] = static_cast<std::uint8_t>(Counter & 0xFFu);
}

/** Reads the big-endian counter back out of a received volley payload. */
std::uint32_t ReadVolleyCounter(const std::uint8_t* const In) noexcept
{
	return (static_cast<std::uint32_t>(In[1]) << 24) | (static_cast<std::uint32_t>(In[2]) << 16) | (static_cast<std::uint32_t>(In[3]) << 8)
		| static_cast<std::uint32_t>(In[4]);
}

#if MICROWORLD_EXAMPLE_SPI_MASTER
/** Builds the master driver configuration from the fixed pins and clock. */
MicroWorld::FEsp32SpiMasterConfig MakeMasterConfig() noexcept
{
	MicroWorld::FEsp32SpiMasterConfig Config;
	Config.SpiHost = SpiHostNumber;
	Config.MosiGpio = MosiGpioNumber;
	Config.MisoGpio = MisoGpioNumber;
	Config.SclkGpio = SclkGpioNumber;
	Config.CsGpio = CsGpioNumber;
	Config.ClockHz = SpiClockHz;
	Config.LocalNodeId = MasterNodeId;
	return Config;
}

/** Master composition root: clocks the bus, so it paces every volley and the slave only reacts. */
void RunMaster() noexcept
{
	// Static, never on the app_main stack (the ESP32-S3 stack lesson, §2.2); its DMA buffers must live here.
	static MicroWorld::FEsp32TimeSource TimeSource{};
	static MicroWorld::FEsp32SpiMasterDriver Driver{MakeMasterConfig()};
	MW_LOG(Log, "ex21", "master open=%d", Driver.IsOpen() ? 1 : 0);
	MW_LOG(Log, "ex21", "master clocks the bus; the slave only reacts");
	if (!Driver.IsOpen())
	{
		// A failed bus open cannot recover here; stop with a clear line instead of looping.
		MW_LOG(Error, "ex21", "spi master failed to open; halting");
		return;
	}

	static std::uint8_t RxBuffer[MicroWorld::SpiMaxPayloadBytes];
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
			const MicroWorld::ENetResult TxResult =
				Driver.TrySend(MicroWorld::MakeSpiAddress(SlaveNodeId), MicroWorld::TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
			MW_LOG(Log, "ex21", "tx n=%u result=%s", static_cast<unsigned>(NextCounter), ToText(TxResult));
			if (TxResult == MicroWorld::ENetResult::Success)
			{
				bAwaitingReply = true;
			}
		}

		// Only the master clocks the bus, so it polls reads to harvest the slave's reply (pipelined by a
		// transaction on SPI); each poll clocks one transfer until the reply arrives.
		if (bAwaitingReply)
		{
			MicroWorld::FNetAddress From{};
			MicroWorld::FNetReceiveResult Received{};
			const MicroWorld::ENetResult RxResult = Driver.TryReceive(From, MicroWorld::TSpan<std::uint8_t>(RxBuffer, sizeof(RxBuffer)), Received);
			if (RxResult == MicroWorld::ENetResult::Success && Received.BytesReceived == VolleyPayloadBytes)
			{
				const std::uint32_t Counter = ReadVolleyCounter(RxBuffer);
				const std::uint8_t FromId = MicroWorld::SpiAddressNodeId(From);
				MW_LOG(Log, "ex21", "rx n=%u from=%u", static_cast<unsigned>(Counter), static_cast<unsigned>(FromId));
				NextCounter = Counter + 1;
				bAwaitingReply = false;
				NextSendDueMilliseconds = Now + VolleyPeriodMilliseconds;
			}
		}

		MicroWorld::SleepMilliseconds(PollPacingMilliseconds);
	}
}
#else
/** Builds the slave driver configuration from the fixed pins. */
MicroWorld::FEsp32SpiSlaveConfig MakeSlaveConfig() noexcept
{
	MicroWorld::FEsp32SpiSlaveConfig Config;
	Config.SpiHost = SpiHostNumber;
	Config.MosiGpio = MosiGpioNumber;
	Config.MisoGpio = MisoGpioNumber;
	Config.SclkGpio = SclkGpioNumber;
	Config.CsGpio = CsGpioNumber;
	Config.LocalNodeId = SlaveNodeId;
	return Config;
}

/** Slave composition root: purely reactive — on each received counter it stages counter + 1 for the master's next read. */
void RunSlave() noexcept
{
	// Static, never on the app_main stack (§2.2); its DMA buffers must live here.
	static MicroWorld::FEsp32SpiSlaveDriver Driver{MakeSlaveConfig()};
	MW_LOG(Log, "ex21", "slave open=%d", Driver.IsOpen() ? 1 : 0);
	if (!Driver.IsOpen())
	{
		// A failed bus open cannot recover here; stop with a clear line instead of looping.
		MW_LOG(Error, "ex21", "spi slave failed to open; halting");
		return;
	}

	static std::uint8_t RxBuffer[MicroWorld::SpiMaxPayloadBytes];

	for (;;)
	{
		MicroWorld::FNetAddress From{};
		MicroWorld::FNetReceiveResult Received{};
		const MicroWorld::ENetResult RxResult = Driver.TryReceive(From, MicroWorld::TSpan<std::uint8_t>(RxBuffer, sizeof(RxBuffer)), Received);
		if (RxResult == MicroWorld::ENetResult::Success && Received.BytesReceived == VolleyPayloadBytes)
		{
			const std::uint32_t Counter = ReadVolleyCounter(RxBuffer);
			const std::uint8_t FromId = MicroWorld::SpiAddressNodeId(From);
			MW_LOG(Log, "ex21", "rx n=%u from=%u", static_cast<unsigned>(Counter), static_cast<unsigned>(FromId));

			// Stage the reply (counter + 1) for the master's next read; the master clocks it out.
			std::uint8_t Payload[VolleyPayloadBytes];
			WriteVolleyPayload(Payload, SlaveNodeId, Counter + 1);
			const MicroWorld::ENetResult TxResult =
				Driver.TrySend(MicroWorld::MakeSpiAddress(MasterNodeId), MicroWorld::TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
			MW_LOG(Log, "ex21", "tx n=%u result=%s", static_cast<unsigned>(Counter + 1), ToText(TxResult));
		}

		MicroWorld::SleepMilliseconds(PollPacingMilliseconds);
	}
}
#endif
} // namespace

/** Composition root: installs the output device, then ping-pongs a counter with the peer board over one wired SPI bus. */
extern "C" void app_main(void)
{
	MicroWorld::SetOutputDevice(&MicroWorld::WriteEsp32LogRecord);
#if MICROWORLD_EXAMPLE_SPI_MASTER
	RunMaster();
#else
	RunSlave();
#endif
}
