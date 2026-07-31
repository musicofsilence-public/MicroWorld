#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Platform/Esp32/Esp32OutputDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>
#include <MicroWorld/Platform/Esp32/Esp32UartDriver.h>
#include <MicroWorld/Platform/Esp32/UartAddress.h>

#include <cstddef>
#include <cstdint>

// Role is chosen at build time by the platformio.ini environment, exactly as the
// two-board WiFi and LoRa examples select theirs — never with build_src_filter.
#ifndef MICROWORLD_EXAMPLE_NODE_ID
#error "Define MICROWORLD_EXAMPLE_NODE_ID=1 or 2 via the node-a / node-b build environment."
#endif

namespace
{
/** Single real-time source; every deadline in this example reads it. */
MicroWorld::FEsp32TimeSource GTimeSource{};

/** This board's node id, stamped on every frame it sends. */
constexpr std::uint8_t LocalNodeId = MICROWORLD_EXAMPLE_NODE_ID;

/** The only other board on this point-to-point wire; the reply destination. */
constexpr std::uint8_t PeerNodeId = (LocalNodeId == 1) ? 2 : 1;

/** Node 1 sends the opening frame; node 2 waits to receive before it replies. */
constexpr std::uint8_t VolleyInitiatorNodeId = 1;

/** UART port and the two data GPIOs (A's TX 17 -> B's RX 18, and the mirror). */
constexpr std::int32_t UartPortNumber = 1;
constexpr std::int32_t TxGpioNumber = 17;
constexpr std::int32_t RxGpioNumber = 18;

/** A wire is fast, so 115200 baud (the E32 radio example runs the same UART at 9600). */
constexpr std::uint32_t UartBaudRate = 115200;

/** Volley period: half a second, since a 30 cm wire is fast and lossless. */
constexpr std::uint64_t VolleyPeriodMilliseconds = 500;

/** Poll far faster than the volley so the FreeRTOS idle task (and its watchdog) always runs. */
constexpr unsigned PollPacingMilliseconds = 10;

/** Volley payload layout: byte 0 is the sender node id, bytes 1..4 the counter (big-endian). */
constexpr std::size_t VolleyPayloadBytes = 5;

/** Renders one driver outcome as a short label so the serial trace reads plainly. */
const char* ToText(const MicroWorld::ETransportResult Result) noexcept
{
	switch (Result)
	{
		case MicroWorld::ETransportResult::Success:
			return "Success";
		case MicroWorld::ETransportResult::Full:
			return "Full";
		case MicroWorld::ETransportResult::Invalid:
			return "Invalid";
		case MicroWorld::ETransportResult::Unavailable:
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

/** Builds the driver configuration for this node from the fixed pins and baud. */
MicroWorld::FEsp32UartConfig MakeUartConfig(const std::uint8_t NodeId) noexcept
{
	MicroWorld::FEsp32UartConfig Config;
	Config.UartPort = UartPortNumber;
	Config.TxGpio = TxGpioNumber;
	Config.RxGpio = RxGpioNumber;
	Config.BaudRate = UartBaudRate;
	Config.LocalNodeId = NodeId;
	return Config;
}
} // namespace

/** Composition root: installs the output device, then ping-pongs a counter with the peer board over one wired UART. */
extern "C" void app_main(void)
{
	MicroWorld::SetOutputDevice(&MicroWorld::WriteEsp32LogRecord);

	// Static, never on the app_main stack (the ESP32-S3 stack lesson, §2.2).
	static MicroWorld::FEsp32UartDriver Driver{MakeUartConfig(LocalNodeId)};
	MW_LOG(Log, "ex18", "node=%u open=%d", static_cast<unsigned>(LocalNodeId), Driver.IsOpen() ? 1 : 0);
	if (!Driver.IsOpen())
	{
		// A failed UART open cannot recover here; stop with a clear line instead of looping.
		MW_LOG(Error, "ex18", "uart failed to open; halting");
		return;
	}

	static std::uint8_t RxBuffer[MicroWorld::UartMaxPayloadBytes];

	// Node 1 seeds the volley one period after boot; node 2 stays idle until it hears frame 1.
	bool bHasPendingTx = (LocalNodeId == VolleyInitiatorNodeId);
	std::uint32_t PendingCounter = 1;
	std::uint64_t PendingDueMilliseconds = GTimeSource.Now() + VolleyPeriodMilliseconds;

	for (;;)
	{
		const std::uint64_t Now = GTimeSource.Now();

		// Receive at most one frame; a completed volley schedules the reply (counter + 1).
		MicroWorld::FDeviceAddress From{};
		MicroWorld::FReceiveResult Received{};
		const MicroWorld::ETransportResult RxResult = Driver.TryReceive(From, MicroWorld::TSpan<std::uint8_t>(RxBuffer, sizeof(RxBuffer)), Received);
		if (RxResult == MicroWorld::ETransportResult::Success && Received.BytesReceived == VolleyPayloadBytes)
		{
			const std::uint32_t Counter = ReadVolleyCounter(RxBuffer);
			const std::uint8_t FromId = MicroWorld::UartAddressNodeId(From);
			MW_LOG(Log, "ex18", "rx n=%u from=%u", static_cast<unsigned>(Counter), static_cast<unsigned>(FromId));
			bHasPendingTx = true;
			PendingCounter = Counter + 1;
			PendingDueMilliseconds = Now + VolleyPeriodMilliseconds;
		}

		// When the reply is due, send it; keep it pending until a send actually succeeds.
		if (bHasPendingTx && Now >= PendingDueMilliseconds)
		{
			std::uint8_t Payload[VolleyPayloadBytes];
			WriteVolleyPayload(Payload, LocalNodeId, PendingCounter);
			const MicroWorld::ETransportResult TxResult =
				Driver.TrySend(MicroWorld::MakeUartAddress(PeerNodeId), MicroWorld::TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
			MW_LOG(Log, "ex18", "tx n=%u result=%s", static_cast<unsigned>(PendingCounter), ToText(TxResult));
			if (TxResult == MicroWorld::ETransportResult::Success)
			{
				bHasPendingTx = false;
			}
		}

		MicroWorld::SleepMilliseconds(PollPacingMilliseconds);
	}
}
