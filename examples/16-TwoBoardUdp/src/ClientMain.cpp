#include "UdpMessagingShared.h"
#include "WifiLink.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Delegates/Delegate.h>
#include <MicroWorld/Net/NetHost.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/Net/UdpAddressCodec.h>
#include <MicroWorld/PlatformEsp32/Esp32TimeSource.h>
#include <MicroWorld/PlatformEsp32/Esp32UdpDriver.h>

#include <cstdint>
#include <cstdio>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

using namespace MicroWorld;
using namespace Ex16;

namespace
{
/** Single real-time source for the client board. */
FEsp32TimeSource GTimeSource{};

/** Client session host; one peer slot holds the single server. 256-byte packet
 *  capacity matches the server and the host TwoNodeDemo. */
using FClientNet = TNetHost<1, 256>;

/** Most recent actor count decoded from a server broadcast; -1 before the first. */
int GLastServerActors = -1;
} // namespace

/** Client board: a bare TNetHost (Client) over one UDP socket, no engine. */
void RunClient() noexcept
{
	if (!JoinAccessPoint("ex16", DemoApSsid, DemoApPassword))
	{
		std::printf("[ex16] wifi failed; halting\n");
		return;
	}

	// The client binds an ephemeral local port (0): it only needs to reach the
	// server, and TNetHost learns the client's address server-side from its Hello.
	static FEsp32UdpDriver Driver(0);
	std::printf("[ex16] client open=%d\n", Driver.IsOpen() ? 1 : 0);
	if (!Driver.IsOpen())
	{
		std::printf("[ex16] socket failed; halting\n");
		return;
	}

	static FClientNet ClientNet{Driver};

	// Channel-2 state handler; the no-capture lambda names the static capture directly.
	FClientNet::FMessageHandlerBinding Binding;
	Binding.Bind(
		[](const FPeerId, const std::uint8_t Channel, TSpan<const std::uint8_t> Payload) noexcept
		{
			if (Channel != StateBroadcastChannel || Payload.Size() < 2)
			{
				return;
			}
			GLastServerActors = static_cast<int>(Payload[1]);
			std::printf("[ex16] client rx state tick=%d actors=%d\n", static_cast<int>(Payload[0]), GLastServerActors);
		});
	FDelegateHandle Handle{};
	(void)ClientNet.AddMessageHandler(std::move(Binding), Handle);

	FNetHostConfig Config = MakeHostConfig();
	Config.ServerAddress = MakeUdpAddress(ServerIpv4[0], ServerIpv4[1], ServerIpv4[2], ServerIpv4[3], ServerPort);
	(void)ClientNet.Configure(ENetMode::Client, Config);
	(void)ClientNet.Start(GTimeSource.Now());
	std::printf("[ex16] client connecting (udp)\n");

	bool bConnectedAnnounced = false;
	bool bDoneAnnounced = false;
	int SpawnRequestsSent = 0;
	std::uint64_t NextSpawnDueMilliseconds = 0;
	for (;;)
	{
		const std::uint64_t Now = GTimeSource.Now();
		(void)ClientNet.PumpReceive(Now);
		(void)ClientNet.PumpSend(Now);

		if (ClientNet.GetState() == ENetHostState::Connected)
		{
			if (!bConnectedAnnounced)
			{
				std::printf("[ex16] client connected\n");
				bConnectedAnnounced = true;
				NextSpawnDueMilliseconds = Now; // first request now, the second one second later
			}
			if (SpawnRequestsSent < MaxSpawns && Now >= NextSpawnDueMilliseconds)
			{
				const std::uint8_t RequestPayload[1] = {SpawnRequestOpcode};
				if (ClientNet.SendTo(ClientNet.GetServerPeer(), InputEventChannel, TSpan<const std::uint8_t>(RequestPayload, 1))
					== ENetResult::Success)
				{
					++SpawnRequestsSent;
					std::printf("[ex16] client sent spawn request %d\n", SpawnRequestsSent);
					NextSpawnDueMilliseconds = Now + 1000;
				}
			}
		}

		if (!bDoneAnnounced && GLastServerActors >= MaxSpawns)
		{
			std::printf("[ex16] done (observed actor count %d)\n", GLastServerActors);
			bDoneAnnounced = true;
		}
		vTaskDelay(pdMS_TO_TICKS(PollPacingMilliseconds));
	}
}
