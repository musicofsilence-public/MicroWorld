#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Platform/Esp32/Esp32OutputDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>
#include <MicroWorld/Platform/Esp32/Esp32UartDevice.h>
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
/** Motivation: Single real-time source; every deadline in this example reads it. */
MicroWorld::Platform::Esp32::FEsp32TimeSource GTimeSource{};

/** Motivation: This board's node id, stamped on every frame it sends. */
constexpr std::uint8_t LocalNodeId = MICROWORLD_EXAMPLE_NODE_ID;

/** Motivation: The only other board on this point-to-point wire; the reply destination. */
constexpr std::uint8_t PeerNodeId = (LocalNodeId == 1) ? 2 : 1;

/** Motivation: Node 1 sends the opening frame; node 2 waits to receive before it replies. */
constexpr std::uint8_t VolleyInitiatorNodeId = 1;

/** Motivation: UART port and the two data GPIOs (A's TX 17 -> B's RX 18, and the mirror). */
constexpr std::int32_t UartPortNumber = 1;
constexpr std::int32_t TxGpioNumber = 17;
constexpr std::int32_t RxGpioNumber = 18;

/** Motivation: A wire is fast, so 115200 baud (the E32 radio example runs the same UART at 9600). */
constexpr std::uint32_t UartBaudRate = 115200;

/** Motivation: Volley period: half a second, since a 30 cm wire is fast and lossless. */
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

/**
 * Motivation: Lets one place build the device configuration from the fixed pins and baud, so a node's
 *   config is never restated across the example.
 * Responsibilities: Fill the UART config with the shared port, GPIO, baud, and node id values.
 */
MicroWorld::Platform::Esp32::FEsp32UartConfig MakeUartConfig(const std::uint8_t NodeId) noexcept
{
	MicroWorld::Platform::Esp32::FEsp32UartConfig Config;
	Config.UartPort = UartPortNumber;
	Config.TxGpio = TxGpioNumber;
	Config.RxGpio = RxGpioNumber;
	Config.BaudRate = UartBaudRate;
	Config.LocalNodeId = NodeId;
	return Config;
}
} // namespace

/**
 * Motivation: Application entry point for example 18: the single ESP32 `app_main` stays a thin driver
 *   that ping-pongs a counter with the peer board over one wired UART.
 * Responsibilities: Install the output device, open the UART, and run the counter volley loop.
 */
extern "C" void app_main(void)
{
	MicroWorld::Core::SetOutputDevice(&MicroWorld::Platform::Esp32::WriteEsp32LogRecord);

	// Static, never on the app_main stack (the ESP32-S3 stack lesson, §2.2).
	static MicroWorld::Platform::Esp32::FEsp32UartDevice Device{MakeUartConfig(LocalNodeId)};
	MW_LOG(Log, "ex18", "node=%u open=%d", static_cast<unsigned>(LocalNodeId), Device.IsOpen() ? 1 : 0);
	if (!Device.IsOpen())
	{
		// A failed UART open cannot recover here; stop with a clear line instead of looping.
		MW_LOG(Error, "ex18", "uart failed to open; halting");
		return;
	}

	static std::uint8_t RxBuffer[MicroWorld::Platform::Esp32::UartMaxPayloadBytes];

	// Node 1 seeds the volley one period after boot; node 2 stays idle until it hears frame 1.
	bool bHasPendingTx = (LocalNodeId == VolleyInitiatorNodeId);
	std::uint32_t PendingCounter = 1;
	std::uint64_t PendingDueMilliseconds = GTimeSource.Now() + VolleyPeriodMilliseconds;

	for (;;)
	{
		const std::uint64_t Now = GTimeSource.Now();

		// Receive at most one frame; a completed volley schedules the reply (counter + 1).
		MicroWorld::Transport::Address::FDeviceAddress From{};
		MicroWorld::Core::FReceiveResult Received{};
		const MicroWorld::Transport::ETransportResult RxResult =
			Device.TryReceive(From, MicroWorld::Core::TSpan<std::uint8_t>(RxBuffer, sizeof(RxBuffer)), Received);
		if (RxResult == MicroWorld::Transport::ETransportResult::Success && Received.BytesReceived == VolleyPayloadBytes)
		{
			const std::uint32_t Counter = ReadVolleyCounter(RxBuffer);
			const std::uint8_t FromId = MicroWorld::Platform::Esp32::UartAddressNodeId(From);
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
			const MicroWorld::Transport::ETransportResult TxResult = Device.TrySend(
				MicroWorld::Platform::Esp32::MakeUartAddress(PeerNodeId), MicroWorld::Core::TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
			MW_LOG(Log, "ex18", "tx n=%u result=%s", static_cast<unsigned>(PendingCounter), ToText(TxResult));
			if (TxResult == MicroWorld::Transport::ETransportResult::Success)
			{
				bHasPendingTx = false;
			}
		}

		MicroWorld::Platform::Esp32::SleepMilliseconds(PollPacingMilliseconds);
	}
}
