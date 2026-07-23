#include "NetworkConfig.h"
#include "WifiStation.h"

#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetDriver.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/Net/UdpAddressCodec.h>
#include <MicroWorld/PlatformEsp32/Esp32UdpDriver.h>

#include <cstdint>
#include <cstdio>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

using namespace MicroWorld;

namespace
{
/** Readiness-wait budget per poll; long enough to idle the CPU, short enough to stay responsive. */
constexpr DurationMilliseconds PollReadinessMilliseconds = 250;

/** Pacing between polls so the FreeRTOS idle task (and its watchdog) always runs. */
constexpr unsigned PollPacingMilliseconds = 10;
} // namespace

/**
 * Composition root: joins WiFi, opens one UDP socket, and echoes every datagram
 * back to its sender. Uses the raw FEsp32UdpDriver directly — no TNetHost, no
 * engine — so it is the narrowest possible proof of the driver on real WiFi.
 */
extern "C" void app_main(void)
{
	if (!ConnectWifiStation("ex15"))
	{
		std::printf("[ex15] wifi failed; halting\n");
		return;
	}

	// The driver is constructed only after WiFi/netif is up (lwIP must exist first).
	static FEsp32UdpDriver Driver(kServerPort);
	std::printf("[ex15] listening port=%u open=%d\n", static_cast<unsigned>(Driver.BoundPort()), Driver.IsOpen() ? 1 : 0);
	if (!Driver.IsOpen())
	{
		std::printf("[ex15] socket failed; halting\n");
		return;
	}

	// Sized to the driver's max packet, which equals its internal peek scratch. An
	// oversize datagram therefore surfaces as EITHER Full (when MSG_TRUNC lets the
	// peek see the true length) OR a silently truncated Success (when it cannot);
	// the hardware run records which. Static, never an app_main stack local (§2.2).
	static std::uint8_t RxBuffer[FEsp32UdpDriver::UdpMaxPacketBytes];
	for (;;)
	{
		if (Driver.PollReadable(PollReadinessMilliseconds))
		{
			FNetAddress From{};
			FNetReceiveResult Received{};
			const ENetResult Result = Driver.TryReceive(From, TSpan<std::uint8_t>(RxBuffer, sizeof(RxBuffer)), Received);
			if (Result == ENetResult::Success)
			{
				std::printf(
					"[ex15] rx bytes=%u from_port=%u\n", static_cast<unsigned>(Received.BytesReceived), static_cast<unsigned>(UdpAddressPort(From)));
				const ENetResult Sent = Driver.TrySend(From, TSpan<const std::uint8_t>(RxBuffer, Received.BytesReceived));
				std::printf("[ex15] echo result=%d\n", static_cast<int>(Sent));
			}
			else if (Result == ENetResult::Full)
			{
				// Oversize head datagram: larger than the buffer, so the driver kept it
				// queued rather than truncating. A single one can wedge later polls.
				std::printf("[ex15] rx oversize: datagram larger than buffer (result=Full)\n");
			}
		}
		vTaskDelay(pdMS_TO_TICKS(PollPacingMilliseconds));
	}
}
