#include "UartMessagingShared.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Delegates/Delegate.h>
#include <MicroWorld/Log.h>
#include <MicroWorld/Net/NetHost.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/PlatformEsp32/Esp32Sleep.h>
#include <MicroWorld/PlatformEsp32/Esp32TimeSource.h>
#include <MicroWorld/PlatformEsp32/Esp32UartDriver.h>
#include <MicroWorld/PlatformEsp32/UartAddress.h>

#include <cstdint>

using namespace MicroWorld;
using namespace Ex19;

namespace
{
/** Single real-time source for the client board. */
FEsp32TimeSource GTimeSource{};

/** Client session host; one peer slot holds the single server. */
using FClientNet = TNetHost<1, 120>;

/** Most recent actor count decoded from a server broadcast; -1 before the first. */
int GLastServerActors = -1;
} // namespace

/** Client board: a bare TNetHost (Client) over one UART, no engine, no WiFi. */
void RunClient() noexcept
{
	static FEsp32UartDriver Driver{MakeUartConfig(ClientNodeId)};
	MW_LOG(Log, "ex19", "client node=%u open=%d", static_cast<unsigned>(ClientNodeId), Driver.IsOpen() ? 1 : 0);
	if (!Driver.IsOpen())
	{
		MW_LOG(Error, "ex19", "uart failed to open; halting");
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
			MW_LOG(Log, "ex19", "client rx state tick=%d actors=%d", static_cast<int>(Payload[0]), GLastServerActors);
		});
	FDelegateHandle Handle{};
	(void)ClientNet.AddMessageHandler(std::move(Binding), Handle);

	FNetHostConfig Config = MakeHostConfig();
	Config.ServerAddress = MakeUartAddress(ServerNodeId);
	(void)ClientNet.Configure(ENetMode::Client, Config);
	(void)ClientNet.Start(GTimeSource.Now());
	MW_LOG(Log, "ex19", "client connecting (no WiFi -- UART only)");

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
				MW_LOG(Log, "ex19", "client connected");
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
