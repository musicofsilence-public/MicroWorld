#include "UdpEchoShared.h"

#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/DeviceAddress.h>
#include <MicroWorld/Transport/Device.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Transport/Wifi/UdpAddressCodec.h>
#include <MicroWorld/Platform/Esp32/Esp32OutputDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32UdpDriver.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiLink.h>

#include <cstdint>

using namespace MicroWorld;
using namespace Ex15;

namespace
{
/** Readiness-wait budget per poll; long enough to idle the CPU, short enough to stay responsive. */
constexpr DurationMilliseconds PollReadinessMilliseconds = 250;
} // namespace

/**
 * Composition root: installs the output device, hosts the demo SoftAP, then echoes
 * every UDP datagram back to its sender through FEsp32UdpDriver.
 */
extern "C" void app_main(void)
{
	SetOutputDevice(&WriteEsp32LogRecord);

	static FEsp32WifiLink WifiLink;
	const ETransportResult WifiResult =
		WifiLink.StartAccessPoint(FEsp32AccessPointConfig{DemoApSsid, DemoApPassword, /*WifiChannel*/ 1, /*MaxStations*/ 4});
	if (WifiResult != ETransportResult::Success)
	{
		MW_LOG(Error, "ex15", "wifi failed result=%d; halting", static_cast<int>(WifiResult));
		return;
	}
	MW_LOG(Log, "ex15", "wifi softap up, gateway 192.168.4.1");

	// The driver is constructed only after WiFi/netif is up (lwIP must exist first).
	static FEsp32UdpDriver Driver(EchoServerPort);
	MW_LOG(Log, "ex15", "udp open=%d udp_port=%u", Driver.IsOpen() ? 1 : 0, static_cast<unsigned>(Driver.BoundPort()));
	if (!Driver.IsOpen())
	{
		MW_LOG(Error, "ex15", "socket failed; halting");
		return;
	}

	// Sized to the driver's max packet, which equals its internal peek scratch. Static,
	// never an app_main stack local (§2.2).
	static std::uint8_t RxBuffer[FEsp32UdpDriver::UdpMaxPacketBytes];
	for (;;)
	{
		if (Driver.PollReadable(PollReadinessMilliseconds))
		{
			FDeviceAddress From{};
			FReceiveResult Received{};
			const ETransportResult Result = Driver.TryReceive(From, TSpan<std::uint8_t>(RxBuffer, sizeof(RxBuffer)), Received);
			if (Result == ETransportResult::Success)
			{
				MW_LOG(
					Log,
					"ex15",
					"rx bytes=%u from_port=%u",
					static_cast<unsigned>(Received.BytesReceived),
					static_cast<unsigned>(UdpAddressPort(From)));
				const ETransportResult Sent = Driver.TrySend(From, TSpan<const std::uint8_t>(RxBuffer, Received.BytesReceived));
				MW_LOG(Log, "ex15", "echo result=%d", static_cast<int>(Sent));
			}
			else if (Result == ETransportResult::Full)
			{
				// Oversize datagram: larger than the buffer, so the driver kept it queued
				// rather than truncating. A single one can wedge later polls.
				MW_LOG(Warning, "ex15", "rx oversize: datagram larger than buffer (result=Full)");
			}
		}
		SleepMilliseconds(PollPacingMilliseconds);
	}
}
