#include "LoraMessagingShared.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Delegates/DelegateHandle.h>
#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/NetworkMode.h>
#include <MicroWorld/Transport/PeerId.h>
#include <MicroWorld/Transport/TransportHost.h>
#include <MicroWorld/Transport/TransportHostConfig.h>
#include <MicroWorld/Transport/TransportHostState.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Platform/Esp32/Esp32LoraDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>
#include <MicroWorld/Platform/Esp32/LoraAddress.h>

#include <cstdint>

using namespace MicroWorld::Core;
using namespace MicroWorld::Platform::Esp32;
using namespace MicroWorld::Transport;
using namespace Ex26;

namespace
{
/** Motivation: Single real-time source for the client board. */
FEsp32TimeSource GTimeSource{};

/** Motivation: Client session host; one peer slot holds the single server. */
using FClientTransport = TTransportHost<1, 58>;

/** Motivation: Most recent actor count decoded from a server broadcast; -1 before the first. */
int GLastServerActors = -1;
} // namespace

/**
 * Motivation: Lets one board act as the bare client half of example 26 over a single E32 LoRa radio, so
 *   the client-side transport can be reasoned about with no engine and no WiFi.
 * Responsibilities: Open the radio, run a TTransportHost client, issue spawn requests at airtime-paced
 *   intervals, and observe the server's broadcast state until the expected actor count appears.
 */
void RunClient() noexcept
{
	static FEsp32LoraDevice Device{MakeLoraConfig(ClientNodeId)};
	MW_LOG(Log, "ex26", "client node=%u open=%d", static_cast<unsigned>(ClientNodeId), Device.IsOpen() ? 1 : 0);
	if (!Device.IsOpen())
	{
		MW_LOG(Error, "ex26", "uart failed to open; halting");
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
			MW_LOG(Log, "ex26", "client rx state tick=%d actors=%d", static_cast<int>(Payload[0]), GLastServerActors);
		});
	FDelegateHandle Handle{};
	(void)ClientTransport.AddMessageHandler(std::move(Binding), Handle);

	FTransportHostConfig Config = MakeHostConfig();
	Config.ServerAddress = MakeLoraAddress(ServerNodeId);
	(void)ClientTransport.Configure(ENetworkMode::Client, Config);
	(void)ClientTransport.Start(GTimeSource.Now());
	MW_LOG(Log, "ex26", "client connecting (no WiFi -- LoRa radio only)");

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
				MW_LOG(Log, "ex26", "client connected");
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
					MW_LOG(Log, "ex26", "client sent spawn request %d", SpawnRequestsSent);
					NextSpawnDueMilliseconds = Now + 1000; // >= 1 s airtime pacing (D8)
				}
			}
		}

		if (!bDoneAnnounced && GLastServerActors >= MaxSpawns)
		{
			MW_LOG(Log, "ex26", "done (observed actor count %d)", GLastServerActors);
			bDoneAnnounced = true;
		}
		SleepMilliseconds(PollPacingMilliseconds);
	}
}
