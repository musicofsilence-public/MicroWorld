#include "UdpEchoShared.h"
#include "WifiLink.h"

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
using namespace Ex15;

namespace
{
/** Readiness-wait budget per poll; long enough to idle the CPU, short enough to stay responsive. */
constexpr DurationMilliseconds PollReadinessMilliseconds = 250;
} // namespace

/**
 * Echo-server board: hosts the SoftAP and echoes every datagram back to its
 * sender using the raw FEsp32UdpDriver. This is the board to capture — its
 * console shows the receive outcome, including how an oversize datagram (fired
 * by the probe's raw socket) resolves: Full, or a silently truncated Success.
 */
void RunEchoServer() noexcept
{
	if (!StartSoftAccessPoint("ex15", DemoApSsid, DemoApPassword))
	{
		std::printf("[ex15] wifi failed; halting\n");
		return;
	}

	// The driver is constructed only after WiFi/netif is up (lwIP must exist first).
	static FEsp32UdpDriver Driver(EchoServerPort);
	std::printf("[ex15] echo server open=%d udp_port=%u\n", Driver.IsOpen() ? 1 : 0, static_cast<unsigned>(Driver.BoundPort()));
	if (!Driver.IsOpen())
	{
		std::printf("[ex15] socket failed; halting\n");
		return;
	}

	// Sized to the driver's max packet, which equals its internal peek scratch. An
	// oversize datagram therefore surfaces as EITHER Full (MSG_TRUNC present) OR a
	// silently truncated Success. Static, never an app_main stack local (§2.2).
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
