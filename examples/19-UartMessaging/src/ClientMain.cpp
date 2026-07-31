#include "UartMessagingShared.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Delegates/Delegate.h>
#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/TransportHost.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>
#include <MicroWorld/Platform/Esp32/Esp32UartDevice.h>
#include <MicroWorld/Platform/Esp32/UartAddress.h>

#include <cstdint>

using namespace MicroWorld::Core;
using namespace MicroWorld::Platform::Esp32;
using namespace MicroWorld::Transport;
using namespace Ex19;

namespace
{
/** Single real-time source for the client board. */
FEsp32TimeSource GTimeSource{};

/** Client session host; one peer slot holds the single server. */
using FClientTransport = TTransportHost<1, 120>;

/** Most recent actor count decoded from a server broadcast; -1 before the first. */
int GLastServerActors = -1;
} // namespace

/** Client board: a bare TTransportHost (Client) over one UART, no engine, no WiFi. */
void RunClient() noexcept
{
	static FEsp32UartDevice Device{MakeUartConfig(ClientNodeId)};
	MW_LOG(Log, "ex19", "client node=%u open=%d", static_cast<unsigned>(ClientNodeId), Device.IsOpen() ? 1 : 0);
	if (!Device.IsOpen())
	{
		MW_LOG(Error, "ex19", "uart failed to open; halting");
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
			MW_LOG(Log, "ex19", "client rx state tick=%d actors=%d", static_cast<int>(Payload[0]), GLastServerActors);
		});
	FDelegateHandle Handle{};
	(void)ClientTransport.AddMessageHandler(std::move(Binding), Handle);

	FTransportHostConfig Config = MakeHostConfig();
	Config.ServerAddress = MakeUartAddress(ServerNodeId);
	(void)ClientTransport.Configure(ENetworkMode::Client, Config);
	(void)ClientTransport.Start(GTimeSource.Now());
	MW_LOG(Log, "ex19", "client connecting (no WiFi -- UART only)");

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
				MW_LOG(Log, "ex19", "client connected");
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
					MW_LOG(Log, "ex19", "client sent spawn request %d", SpawnRequestsSent);
					NextSpawnDueMilliseconds = Now + 1000;
				}
			}
		}

		if (!bDoneAnnounced && GLastServerActors >= MaxSpawns)
		{
			MW_LOG(Log, "ex19", "done (observed actor count %d)", GLastServerActors);
			bDoneAnnounced = true;
		}
		SleepMilliseconds(PollPacingMilliseconds);
	}
}
