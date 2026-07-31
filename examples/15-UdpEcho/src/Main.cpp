#include "UdpEchoShared.h"

#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/DeviceAddress.h>
#include <MicroWorld/Transport/Device.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Transport/Wifi/UdpAddressCodec.h>
#include <MicroWorld/Platform/Esp32/Esp32OutputDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiLink.h>

#include <cstdint>

using namespace MicroWorld::Core;
using namespace MicroWorld::Platform::Esp32;
using namespace MicroWorld::Transport;
using namespace MicroWorld::Transport::Address;
using namespace MicroWorld::Transport::Device;
using namespace Ex15;

namespace
{
/** Motivation: Readiness-wait budget per poll; long enough to idle the CPU, short enough to stay responsive. */
constexpr DurationMilliseconds PollReadinessMilliseconds = 250;
} // namespace

/**
 * Motivation: Composition root for example 15 that keeps the platform adapter to one place,
 *   so the echo behavior can be reasoned about without scattered setup.
 * Responsibilities: Install the output device, host the demo SoftAP, then echo every UDP
 *   datagram back to its sender through FEsp32WifiDevice.
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

	// The device is constructed only after WiFi/netif is up (lwIP must exist first).
	static FEsp32WifiDevice Device(EchoServerPort);
	MW_LOG(Log, "ex15", "udp open=%d udp_port=%u", Device.IsOpen() ? 1 : 0, static_cast<unsigned>(Device.BoundPort()));
	if (!Device.IsOpen())
	{
		MW_LOG(Error, "ex15", "socket failed; halting");
		return;
	}

	// Sized to the device's max packet, which equals its internal peek scratch. Static,
	// never an app_main stack local (§2.2).
	static std::uint8_t RxBuffer[FEsp32WifiDevice::UdpMaxPacketBytes];
	for (;;)
	{
		if (Device.PollReadable(PollReadinessMilliseconds))
		{
			FDeviceAddress From{};
			FReceiveResult Received{};
			const ETransportResult Result = Device.TryReceive(From, TSpan<std::uint8_t>(RxBuffer, sizeof(RxBuffer)), Received);
			if (Result == ETransportResult::Success)
			{
				MW_LOG(
					Log,
					"ex15",
					"rx bytes=%u from_port=%u",
					static_cast<unsigned>(Received.BytesReceived),
					static_cast<unsigned>(UdpAddressPort(From)));
				const ETransportResult Sent = Device.TrySend(From, TSpan<const std::uint8_t>(RxBuffer, Received.BytesReceived));
				MW_LOG(Log, "ex15", "echo result=%d", static_cast<int>(Sent));
			}
			else if (Result == ETransportResult::Full)
			{
				// Oversize datagram: larger than the buffer, so the device kept it queued
				// rather than truncating. A single one can wedge later polls.
				MW_LOG(Warning, "ex15", "rx oversize: datagram larger than buffer (result=Full)");
			}
		}
		SleepMilliseconds(PollPacingMilliseconds);
	}
}
