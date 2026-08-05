#include "LoraMessagingShared.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Messaging/ChannelInformation.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessagingRoute.h>
#include <MicroWorld/Messaging/MessagingSystem.h>
#include <MicroWorld/Networking/ConnectionState.h>
#include <MicroWorld/Networking/NetworkSystem.h>
#include <MicroWorld/Platform/Esp32/Esp32LoraDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>
#include <MicroWorld/Platform/Esp32/LoraAddress.h>

#include <cstdint>

using namespace MicroWorld::Core;
using namespace MicroWorld::Messaging;
using namespace MicroWorld::Networking;
using namespace MicroWorld::Platform::Esp32;
using namespace Ex26;

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
	MW_LOG(Log, "ex26", "client rx state tick=%d actors=%d", static_cast<int>(InMessage.GetPayload()[0]), GLastServerActors);
}

/**
 * Motivation: Builds the LoRa client composition before it starts a server session.
 * Responsibilities: Register the device, initialize Networking, and install the application state subscriber.
 */
bool SetupClient(FEsp32LoraDevice& Device, FMessagingSystem& Messaging, FNetworkSystem& Network, FMessagingLinkId& OutLinkId) noexcept
{
	if (Messaging.RegisterLink(Device, OutLinkId) != EMessagingResult::Success || Network.Initialize() != ENetworkResult::Success
		|| Messaging.CreateChannel({InputEventChannel, false, nullptr, {}}) != EMessagingResult::Success
		|| Messaging.CreateChannel({StateBroadcastChannel, false, nullptr, {}}) != EMessagingResult::Success)
	{
		MW_LOG(Error, "ex26", "client network setup failed; halting");
		return false;
	}

	FMessagingSystem::FSubscriberDelegate StateSubscriber;
	if (StateSubscriber.Bind(HandleStateUpdate) != EDelegateResult::Success
		|| Messaging.SubscribeToChannel(StateBroadcastChannel, StateMessageName, std::move(StateSubscriber)) != EMessagingResult::Success)
	{
		MW_LOG(Error, "ex26", "client subscription setup failed; halting");
		return false;
	}

	return true;
}

/**
 * Motivation: Starts the composed systems before the client begins its one server session.
 * Responsibilities: Preserve the device-to-Messaging-to-Network start order and request the fixed server route.
 */
bool StartClient(FEsp32LoraDevice& Device, FMessagingSystem& Messaging, FNetworkSystem& Network, const FMessagingLinkId LinkId) noexcept
{
	const TimePointMilliseconds StartTime = GTimeSource.Now();
	Device.BeginPlay(StartTime);
	Messaging.BeginPlay(StartTime);
	Network.BeginPlay(StartTime);
	if (Network.ConnectToServer({LinkId, MicroWorld::Transport::MakeLoraAddress(ServerNodeId)}, StartTime) != ENetworkResult::Success)
	{
		MW_LOG(Error, "ex26", "client connect setup failed; halting");
		return false;
	}

	return true;
}

/**
 * Motivation: Keeps the demonstrated client traffic within its fixed request and airtime limits.
 * Responsibilities: Advance the composition in order, issue at most MaxSpawns requests, and report the observed completion.
 */
void RunClientMessageLoop(FEsp32LoraDevice& Device, FMessagingSystem& Messaging, FNetworkSystem& Network) noexcept
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
				MW_LOG(Log, "ex26", "client connected");
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
					NextSpawnDueMilliseconds = Now + StateBroadcastPeriodMilliseconds;
				}
			}
		}
		Network.PostAdvance(Now);
		Messaging.PostAdvance(Now);
		Device.PostAdvance(Now);
		if (!bDoneAnnounced && GLastServerActors >= MaxSpawns)
		{
			MW_LOG(Log, "ex26", "done (observed actor count %d)", GLastServerActors);
			bDoneAnnounced = true;
		}
		SleepMilliseconds(PollPacingMilliseconds);
	}
}
} // namespace

/**
 * Motivation: Runs the LoRa client role without exposing the server route to application operations.
 * Responsibilities: Connect once, pace bounded requests, and observe state.
 */
void RunClient() noexcept
{
	static FEsp32LoraDevice Device{MakeLoraConfig(ClientNodeId)};
	if (!Device.IsOpen())
	{
		MW_LOG(Error, "ex26", "radio failed to open; halting");
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
