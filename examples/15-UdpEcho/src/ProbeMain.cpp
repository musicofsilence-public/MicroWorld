#include "UdpEchoShared.h"
#include "WifiLink.h"

#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetDriver.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/Net/UdpAddressCodec.h>
#include <MicroWorld/PlatformEsp32/Esp32UdpDriver.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <sys/time.h>

#include <lwip/sockets.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

using namespace MicroWorld;
using namespace Ex15;

namespace
{
/** Attempts to read a driver echo of the just-sent normal payload; caps the poll count. */
constexpr int NormalEchoPollAttempts = 25;

/** Builds the echo server's UDP address for the MicroWorld driver send path. */
FNetAddress EchoServerAddress() noexcept
{
	return MakeUdpAddress(EchoServerIpv4[0], EchoServerIpv4[1], EchoServerIpv4[2], EchoServerIpv4[3], EchoServerPort);
}

/**
 * Sends the normal payload through the MicroWorld driver and reports whether the
 * echoed byte count matches. Proves the driver's send AND receive over WiFi.
 */
void SendNormalAndCheck(FEsp32UdpDriver& Driver) noexcept
{
	const std::uint8_t* const Payload = reinterpret_cast<const std::uint8_t*>(NormalPayload);
	const std::size_t Length = sizeof(NormalPayload) - 1;
	const ENetResult Sent = Driver.TrySend(EchoServerAddress(), TSpan<const std::uint8_t>(Payload, Length));
	std::printf("[ex15] probe sent normal bytes=%u result=%d\n", static_cast<unsigned>(Length), static_cast<int>(Sent));
	if (Sent != ENetResult::Success)
	{
		return;
	}

	static std::uint8_t RxBuffer[FEsp32UdpDriver::UdpMaxPacketBytes];
	for (int Attempt = 0; Attempt < NormalEchoPollAttempts; ++Attempt)
	{
		if (Driver.PollReadable(200))
		{
			FNetAddress From{};
			FNetReceiveResult Received{};
			if (Driver.TryReceive(From, TSpan<std::uint8_t>(RxBuffer, sizeof(RxBuffer)), Received) == ENetResult::Success)
			{
				std::printf(
					"[ex15] probe echo normal bytes=%u %s\n",
					static_cast<unsigned>(Received.BytesReceived),
					Received.BytesReceived == Length ? "MATCH" : "MISMATCH");
				return;
			}
		}
	}
	std::printf("[ex15] probe echo normal: none\n");
}

/**
 * Fires one oversize datagram through a RAW lwIP socket. The MicroWorld driver
 * refuses to send more than UdpMaxPacketBytes (TrySend returns Invalid), so the
 * only way to exercise the echo server's oversize RECEIVE path is a sender that
 * bypasses that cap — exactly the role the original PC client played. Reports
 * what, if anything, the raw socket gets echoed back.
 */
void SendOversizeRaw() noexcept
{
	const int Socket = lwip_socket(AF_INET, SOCK_DGRAM, 0);
	if (Socket < 0)
	{
		std::printf("[ex15] probe oversize: raw socket open failed\n");
		return;
	}

	struct sockaddr_in Destination{};
	Destination.sin_family = AF_INET;
	Destination.sin_port = htons(EchoServerPort);
	const std::uint32_t HostOrderIp = (static_cast<std::uint32_t>(EchoServerIpv4[0]) << 24) | (static_cast<std::uint32_t>(EchoServerIpv4[1]) << 16)
		| (static_cast<std::uint32_t>(EchoServerIpv4[2]) << 8) | static_cast<std::uint32_t>(EchoServerIpv4[3]);
	Destination.sin_addr.s_addr = htonl(HostOrderIp);

	static std::uint8_t Oversize[OversizePayloadBytes];
	std::memset(Oversize, 'X', sizeof(Oversize));
	const int SentBytes = lwip_sendto(Socket, Oversize, sizeof(Oversize), 0, reinterpret_cast<struct sockaddr*>(&Destination), sizeof(Destination));
	std::printf("[ex15] probe sent oversize bytes=%d (raw socket, bypasses driver 1200 cap)\n", SentBytes);

	fd_set ReadSet;
	FD_ZERO(&ReadSet);
	FD_SET(Socket, &ReadSet);
	struct timeval Timeout{};
	Timeout.tv_sec = 3;
	static std::uint8_t RawRx[OversizePayloadBytes];
	if (lwip_select(Socket + 1, &ReadSet, nullptr, nullptr, &Timeout) > 0)
	{
		const int GotBytes = lwip_recvfrom(Socket, RawRx, sizeof(RawRx), 0, nullptr, nullptr);
		std::printf(
			"[ex15] probe oversize echo bytes=%d -> %s\n", GotBytes, GotBytes == static_cast<int>(sizeof(Oversize)) ? "FULL-ECHO" : "TRUNCATED");
	}
	else
	{
		std::printf("[ex15] probe oversize echo: none (server reported Full and dropped it)\n");
	}
	(void)lwip_close(Socket);
}
} // namespace

/**
 * Probe board: joins the SoftAP, proves a normal round trip through the
 * MicroWorld driver, then fires a raw oversize datagram to exercise the echo
 * server's oversize receive path. The definitive oversize outcome is on the echo
 * server's console; this board drives it.
 */
void RunProbe() noexcept
{
	if (!JoinAccessPoint("ex15", DemoApSsid, DemoApPassword))
	{
		std::printf("[ex15] wifi failed; halting\n");
		return;
	}

	static FEsp32UdpDriver Driver(0);
	std::printf("[ex15] probe open=%d\n", Driver.IsOpen() ? 1 : 0);
	if (!Driver.IsOpen())
	{
		std::printf("[ex15] socket failed; halting\n");
		return;
	}

	// Small settle so the echo server is listening after our join.
	vTaskDelay(pdMS_TO_TICKS(500));

	SendNormalAndCheck(Driver);
	SendOversizeRaw();

	std::printf("[ex15] done (probe sent normal + oversize)\n");
	for (;;)
	{
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}
