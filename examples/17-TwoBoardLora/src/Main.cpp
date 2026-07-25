#include <MicroWorld/Log.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/PlatformEsp32/Esp32E32LoraDriver.h>
#include <MicroWorld/PlatformEsp32/Esp32OutputDevice.h>
#include <MicroWorld/PlatformEsp32/Esp32Sleep.h>
#include <MicroWorld/PlatformEsp32/Esp32TimeSource.h>
#include <MicroWorld/PlatformEsp32/LoraAddress.h>

#include <cstddef>
#include <cstdint>

// Role is chosen at build time by the platformio.ini environment, exactly as the
// two-board WiFi and UART examples select theirs — never with build_src_filter.
#ifndef MICROWORLD_EXAMPLE_NODE_ID
#error "Define MICROWORLD_EXAMPLE_NODE_ID=1 or 2 via the node-a / node-b build environment."
#endif

namespace
{
/** Single real-time source; every deadline in this example reads it. */
MicroWorld::FEsp32TimeSource GTimeSource{};

/** This board's node id, stamped on every frame it sends. */
constexpr std::uint8_t LocalNodeId = MICROWORLD_EXAMPLE_NODE_ID;

/** The only other node in this pairing; the destination the driver validates before broadcasting the frame. */
constexpr std::uint8_t PeerNodeId = (LocalNodeId == 1) ? 2 : 1;

/** Node 1 sends the opening frame; node 2 waits to receive before it replies. */
constexpr std::uint8_t VolleyInitiatorNodeId = 1;

/** UART port and the two GPIOs wired to the E32 module (TX 17 -> module RXD, RX 18 -> module TXD); identical on both boards. */
constexpr std::int32_t UartPortNumber = 1;
constexpr std::int32_t TxGpioNumber = 17;
constexpr std::int32_t RxGpioNumber = 18;

/** The E32's UART runs at its factory default of 9600 8N1 (D7); this is the module's serial baud, not a wire speed. */
constexpr std::uint32_t UartBaudRate = 9600;

/** Volley period: one second, since a LoRa frame costs hundreds of ms of airtime and 500 ms (the wired example's period) would congest the channel.
 */
constexpr std::uint64_t VolleyPeriodMilliseconds = 1000;

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

/** Builds the driver configuration for this node from the fixed pins and baud. */
MicroWorld::FEsp32E32LoraConfig MakeLoraConfig(const std::uint8_t NodeId) noexcept
{
	MicroWorld::FEsp32E32LoraConfig Config;
	Config.UartPort = UartPortNumber;
	Config.TxGpio = TxGpioNumber;
	Config.RxGpio = RxGpioNumber;
	Config.BaudRate = UartBaudRate;
	Config.LocalNodeId = NodeId;
	return Config;
}
} // namespace

/** Composition root: installs the output device, then ping-pongs a counter with the peer board over one E32 LoRa radio link. */
extern "C" void app_main(void)
{
	MicroWorld::SetOutputDevice(&MicroWorld::Esp32OutputDevice);

	// Static, never on the app_main stack (the ESP32-S3 stack lesson, §2.2).
	static MicroWorld::FEsp32E32LoraDriver Driver{MakeLoraConfig(LocalNodeId)};
	MW_LOG(Log, "ex17", "node=%u open=%d", static_cast<unsigned>(LocalNodeId), Driver.IsOpen() ? 1 : 0);
	if (!Driver.IsOpen())
	{
		// A failed UART open cannot recover here; stop with a clear line instead of looping.
		MW_LOG(Error, "ex17", "uart failed to open; halting");
		return;
	}

	static std::uint8_t RxBuffer[MicroWorld::E32MaxPayloadBytes];

	// Node 1 seeds the volley one period after boot; node 2 stays idle until it hears frame 1.
	bool bHasPendingTx = (LocalNodeId == VolleyInitiatorNodeId);
	std::uint32_t PendingCounter = 1;
	std::uint64_t PendingDueMilliseconds = GTimeSource.Now() + VolleyPeriodMilliseconds;

	for (;;)
	{
		const std::uint64_t Now = GTimeSource.Now();

		// Receive at most one frame; a completed volley schedules the reply (counter + 1).
		MicroWorld::FNetAddress From{};
		MicroWorld::FNetReceiveResult Received{};
		const MicroWorld::ENetResult RxResult = Driver.TryReceive(From, MicroWorld::TSpan<std::uint8_t>(RxBuffer, sizeof(RxBuffer)), Received);
		if (RxResult == MicroWorld::ENetResult::Success && Received.BytesReceived == VolleyPayloadBytes)
		{
			const std::uint32_t Counter = ReadVolleyCounter(RxBuffer);
			const std::uint8_t FromId = MicroWorld::LoraAddressNodeId(From);
			MW_LOG(Log, "ex17", "rx n=%u from=%u", static_cast<unsigned>(Counter), static_cast<unsigned>(FromId));
			bHasPendingTx = true;
			PendingCounter = Counter + 1;
			PendingDueMilliseconds = Now + VolleyPeriodMilliseconds;
		}

		// When the reply is due, send it; keep it pending until a send actually succeeds.
		if (bHasPendingTx && Now >= PendingDueMilliseconds)
		{
			std::uint8_t Payload[VolleyPayloadBytes];
			WriteVolleyPayload(Payload, LocalNodeId, PendingCounter);
			const MicroWorld::ENetResult TxResult =
				Driver.TrySend(MicroWorld::MakeLoraAddress(PeerNodeId), MicroWorld::TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
			MW_LOG(Log, "ex17", "tx n=%u result=%s", static_cast<unsigned>(PendingCounter), ToText(TxResult));
			if (TxResult == MicroWorld::ENetResult::Success)
			{
				bHasPendingTx = false;
			}
		}

		MicroWorld::SleepMilliseconds(PollPacingMilliseconds);
	}
}
