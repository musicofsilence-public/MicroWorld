#include "UdpMessagingShared.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Delegates/DelegateHandle.h>
#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/NetworkMode.h>
#include <MicroWorld/Transport/PeerId.h>
#include <MicroWorld/Transport/TransportHost.h>
#include <MicroWorld/Transport/TransportHostConfig.h>
#include <MicroWorld/Transport/TransportHostState.h>
#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Transport/Wifi/UdpAddressCodec.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiLink.h>

#include <cstdint>

using namespace MicroWorld::Core;
using namespace MicroWorld::Platform::Esp32;
using namespace MicroWorld::Transport;
using namespace Ex16;

namespace
{
/** Motivation: Single real-time source for the client board. */
FEsp32TimeSource GTimeSource{};

/** Motivation: Client session host; one peer slot holds the single server. 256-byte packet
 *  capacity matches the server and the host TwoNodeDemo. */
using FClientTransport = TTransportHost<1, 256>;

/** Motivation: Most recent actor count decoded from a server broadcast; -1 before the first. */
int GLastServerActors = -1;
} // namespace

/**
 * Motivation: Lets one board act as the bare client half of example 16 over a single UDP socket,
 *   so the client-side transport can be reasoned about without an engine.
 * Responsibilities: Join the demo SoftAP, run a TTransportHost client, issue spawn requests, and
 *   observe the server's broadcast state until the expected actor count appears.
 */
void RunClient() noexcept
{
	static FEsp32WifiLink WifiLink;
	if (WifiLink.JoinAccessPoint(FEsp32StationConfig{DemoApSsid, DemoApPassword, /*ConnectTimeoutMilliseconds*/ 15000}) != ETransportResult::Success)
	{
		MW_LOG(Error, "ex16", "wifi failed; halting");
		return;
	}
	MW_LOG(Log, "ex16", "wifi joined AP");

	// The client binds an ephemeral local port (0): it only needs to reach the
	// server, and TTransportHost learns the client's address server-side from its Hello.
	static FEsp32WifiDevice Device(0);
	MW_LOG(Log, "ex16", "client open=%d", Device.IsOpen() ? 1 : 0);
	if (!Device.IsOpen())
	{
		MW_LOG(Error, "ex16", "socket failed; halting");
		return;
	}

	static FClientTransport ClientTransport{Device};

	// Channel-2 state handler; the no-capture lambda names the static capture directly.
	FClientTransport::FMessageHandlerBinding Binding;
	Binding.Bind(
		[](const FPeerId, const std::uint8_t Channel, TSpan<const std::uint8_t> Payload) noexcept
		{
			if (Channel != StateBroadcastChannel || Payload.Size() < 2)
			{
				return;
			}
			GLastServerActors = static_cast<int>(Payload[1]);
			MW_LOG(Log, "ex16", "client rx state tick=%d actors=%d", static_cast<int>(Payload[0]), GLastServerActors);
		});
	FDelegateHandle Handle{};
	(void)ClientTransport.AddMessageHandler(std::move(Binding), Handle);

	FTransportHostConfig Config = MakeHostConfig();
	Config.ServerAddress = MakeUdpAddress(ServerIpv4[0], ServerIpv4[1], ServerIpv4[2], ServerIpv4[3], ServerPort);
	(void)ClientTransport.Configure(ENetworkMode::Client, Config);
	(void)ClientTransport.Start(GTimeSource.Now());
	MW_LOG(Log, "ex16", "client connecting (udp)");

	bool bConnectedAnnounced = false;
	bool bDoneAnnounced = false;
	int SpawnRequestsSent = 0;
	std::uint64_t NextSpawnDueMilliseconds = 0;
	for (;;)
	{
		const std::uint64_t Now = GTimeSource.Now();
		(void)ClientTransport.PumpReceive(Now);
		(void)ClientTransport.PumpSend(Now);

		if (ClientTransport.GetState() == ETransportHostState::Connected)
		{
			if (!bConnectedAnnounced)
			{
				MW_LOG(Log, "ex16", "client connected");
				bConnectedAnnounced = true;
				NextSpawnDueMilliseconds = Now; // first request now, the second one second later
			}
			if (SpawnRequestsSent < MaxSpawns && Now >= NextSpawnDueMilliseconds)
			{
				const std::uint8_t RequestPayload[1] = {SpawnRequestOpcode};
				if (ClientTransport.SendTo(ClientTransport.GetServerPeer(), InputEventChannel, TSpan<const std::uint8_t>(RequestPayload, 1))
					== ETransportResult::Success)
				{
					++SpawnRequestsSent;
					MW_LOG(Log, "ex16", "client sent spawn request %d", SpawnRequestsSent);
					NextSpawnDueMilliseconds = Now + 1000;
				}
			}
		}

		if (!bDoneAnnounced && GLastServerActors >= MaxSpawns)
		{
			MW_LOG(Log, "ex16", "done (observed actor count %d)", GLastServerActors);
			bDoneAnnounced = true;
		}
		SleepMilliseconds(PollPacingMilliseconds);
	}
}
