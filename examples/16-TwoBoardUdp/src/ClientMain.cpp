#include "UdpMessagingShared.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Messaging/ChannelInformation.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessagingRoute.h>
#include <MicroWorld/Messaging/MessagingSystem.h>
#include <MicroWorld/Networking/ConnectionState.h>
#include <MicroWorld/Networking/NetworkSystem.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32StationConfig.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32WifiLink.h>
#include <MicroWorld/Transport/Wifi/UdpAddressCodec.h>

#include <cstdint>

using namespace MicroWorld::Core;
using namespace MicroWorld::Messaging;
using namespace MicroWorld::Networking;
using namespace MicroWorld::Platform::Esp32;
using namespace Ex16;

namespace
{
/** Motivation: Supplies the client board's monotonic session time. */
FEsp32TimeSource GTimeSource{};
/** Motivation: Retains the last server actor count delivered through Network. */
int GLastServerActors = -1;

/**
 * Motivation: Decodes state only after Network has validated the live server session.
 * Responsibilities: Ignore malformed payloads and record the advertised count.
 */
void HandleStateUpdate(const FMessage& InMessage) noexcept
{
	if (InMessage.GetPayload().Size() != 2)
	{
		return;
	}
	GLastServerActors = static_cast<int>(InMessage.GetPayload()[1]);
	MW_LOG(Log, "ex16", "client rx state tick=%d actors=%d", static_cast<int>(InMessage.GetPayload()[0]), GLastServerActors);
}

/**
 * Motivation: Builds the client composition after it joins the demonstration access point.
 * Responsibilities: Register the device, initialize Networking, and install the application state subscriber.
 */
bool SetupClient(FEsp32WifiDevice& Device, FMessagingSystem& Messaging, FNetworkSystem& Network, FMessagingLinkId& OutLinkId) noexcept
{
	if (Messaging.RegisterLink(Device, OutLinkId) != EMessagingResult::Success || Network.Initialize() != ENetworkResult::Success
		|| Messaging.CreateChannel({InputEventChannel, false, nullptr, {}}) != EMessagingResult::Success
		|| Messaging.CreateChannel({StateBroadcastChannel, false, nullptr, {}}) != EMessagingResult::Success)
	{
		MW_LOG(Error, "ex16", "client network setup failed; halting");
		return false;
	}

	FMessagingSystem::FSubscriberDelegate StateSubscriber;
	if (StateSubscriber.Bind(HandleStateUpdate) != EDelegateResult::Success
		|| Messaging.SubscribeToChannel(StateBroadcastChannel, StateMessageName, std::move(StateSubscriber)) != EMessagingResult::Success)
	{
		MW_LOG(Error, "ex16", "client subscription setup failed; halting");
		return false;
	}

	return true;
}

/**
 * Motivation: Starts the composed systems before the client begins its one server session.
 * Responsibilities: Preserve the device-to-Messaging-to-Network start order and request the fixed server route.
 */
bool StartClient(FEsp32WifiDevice& Device, FMessagingSystem& Messaging, FNetworkSystem& Network, const FMessagingLinkId LinkId) noexcept
{
	const TimePointMilliseconds StartTime = GTimeSource.Now();
	const FMessagingRoute ServerRoute{
		LinkId, MicroWorld::Transport::MakeUdpAddress(ServerIpv4[0], ServerIpv4[1], ServerIpv4[2], ServerIpv4[3], ServerPort)};
	Device.BeginPlay(StartTime);
	Messaging.BeginPlay(StartTime);
	Network.BeginPlay(StartTime);
	if (Network.ConnectToServer(ServerRoute, StartTime) != ENetworkResult::Success)
	{
		MW_LOG(Error, "ex16", "client connect setup failed; halting");
		return false;
	}

	return true;
}

/**
 * Motivation: Keeps the demonstrated client traffic within its fixed request and pacing limits.
 * Responsibilities: Advance the composition in order, issue at most MaxSpawns requests, and report the observed completion.
 */
void RunClientMessageLoop(FEsp32WifiDevice& Device, FMessagingSystem& Messaging, FNetworkSystem& Network) noexcept
{
	bool bConnectedAnnounced = false;
	bool bDoneAnnounced = false;
	int SpawnRequestsSent = 0;
	TimePointMilliseconds NextSpawnDueMilliseconds = 0;

	for (;;)
	{
		const TimePointMilliseconds Now = GTimeSource.Now();
		Device.PreAdvance(Now);
		Messaging.PreAdvance(Now);
		Network.PreAdvance(Now);
		if (Network.GetConnectionState() == EConnectionState::Connected)
		{
			if (!bConnectedAnnounced)
			{
				MW_LOG(Log, "ex16", "client connected");
				bConnectedAnnounced = true;
				NextSpawnDueMilliseconds = Now;
			}
			if (SpawnRequestsSent < MaxSpawns && Now >= NextSpawnDueMilliseconds)
			{
				const std::uint8_t Payload[1] = {SpawnRequestOpcode};
				FMessage Request;
				Request.SetMessageNameId(SpawnRequestMessageName);
				Request.SetPayload(TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
				if (Network.SendToServer(InputEventChannel, Request) == ENetworkResult::Success)
				{
					++SpawnRequestsSent;
					MW_LOG(Log, "ex16", "client sent spawn request %d", SpawnRequestsSent);
					NextSpawnDueMilliseconds = Now + 1000;
				}
			}
		}
		Network.PostAdvance(Now);
		Messaging.PostAdvance(Now);
		Device.PostAdvance(Now);
		if (!bDoneAnnounced && GLastServerActors >= MaxSpawns)
		{
			MW_LOG(Log, "ex16", "done (observed actor count %d)", GLastServerActors);
			bDoneAnnounced = true;
		}
		SleepMilliseconds(PollPacingMilliseconds);
	}
}
} // namespace

/**
 * Motivation: Runs the example's client role without giving application code a transport route.
 * Responsibilities: Join the AP, connect once, issue bounded requests, and report delivered state.
 */
void RunClient() noexcept
{
	static FEsp32WifiLink WifiLink;
	if (WifiLink.JoinAccessPoint(FEsp32StationConfig{DemoApSsid, DemoApPassword, 15000}) != ETransportResult::Success)
	{
		MW_LOG(Error, "ex16", "wifi failed; halting");
		return;
	}

	static FEsp32WifiDevice Device(0);
	if (!Device.IsOpen())
	{
		MW_LOG(Error, "ex16", "socket failed; halting");
		return;
	}

	static FMessagingSystem Messaging;
	static FNetworkSystem Network{Messaging, MakeNetworkInformation(ENetworkRole::Client)};
	static FMessagingLinkId LinkId{};
	if (!SetupClient(Device, Messaging, Network, LinkId))
	{
		return;
	}

	if (!StartClient(Device, Messaging, Network, LinkId))
	{
		return;
	}

	RunClientMessageLoop(Device, Messaging, Network);
}
