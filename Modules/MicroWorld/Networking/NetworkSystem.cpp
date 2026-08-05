#include <MicroWorld/Networking/NetworkSystem.h>

#include <MicroWorld/Messaging/ChannelInformation.h>
#include <MicroWorld/Messaging/TypedMessageCodec.h>
#include <MicroWorld/Networking/ConnectAccepted.h>
#include <MicroWorld/Networking/ConnectRejected.h>
#include <MicroWorld/Networking/ConnectRequest.h>
#include <MicroWorld/Networking/Disconnect.h>
#include <MicroWorld/Networking/Heartbeat.h>
#include <MicroWorld/Networking/RoutedMessage.h>

#include <limits>

namespace MicroWorld::Networking
{

FNetworkSystem::FNetworkSystem(Messaging::FMessagingSystem& InMessaging, const FNetworkSystemInformation& InInformation) noexcept
	: Messaging(InMessaging), Information(InInformation)
{
}

FNetworkSystem::~FNetworkSystem() noexcept
{
	Shutdown();
}

ENetworkResult FNetworkSystem::Initialize() noexcept
{
	if (bInitialized)
	{
		return ENetworkResult::Success;
	}

	if (Information.HeartbeatIntervalMilliseconds == 0 || Information.PeerTimeoutMilliseconds <= Information.HeartbeatIntervalMilliseconds)
	{
		return ENetworkResult::Invalid;
	}

	if (Messaging.CreateChannel({BestEffortWireChannelNameId, false, nullptr, {}}) != Messaging::EMessagingResult::Success)
	{
		return ENetworkResult::Full;
	}

	if (Messaging.CreateChannel({ReliableWireChannelNameId, true, nullptr, {}}) != Messaging::EMessagingResult::Success)
	{
		(void)Messaging.DestroyChannel(BestEffortWireChannelNameId);
		return ENetworkResult::Full;
	}

	Messaging::FMessagingSystem::FSubscriberDelegate BestEffortDelegate;
	(void)BestEffortDelegate.Bind([this](const Messaging::FMessage& InMessage) noexcept { HandleWireMessage(InMessage); });
	if (Messaging.SubscribeToChannel(BestEffortWireChannelNameId, std::move(BestEffortDelegate), {}, &BestEffortSubscription)
		!= Messaging::EMessagingResult::Success)
	{
		(void)Messaging.DestroyChannel(ReliableWireChannelNameId);
		(void)Messaging.DestroyChannel(BestEffortWireChannelNameId);
		return ENetworkResult::Full;
	}

	Messaging::FMessagingSystem::FSubscriberDelegate ReliableDelegate;
	(void)ReliableDelegate.Bind([this](const Messaging::FMessage& InMessage) noexcept { HandleWireMessage(InMessage); });
	if (Messaging.SubscribeToChannel(ReliableWireChannelNameId, std::move(ReliableDelegate), {}, &ReliableSubscription)
		!= Messaging::EMessagingResult::Success)
	{
		(void)Messaging.Unsubscribe(BestEffortSubscription);
		BestEffortSubscription = {};
		(void)Messaging.DestroyChannel(ReliableWireChannelNameId);
		(void)Messaging.DestroyChannel(BestEffortWireChannelNameId);
		return ENetworkResult::Full;
	}

	bInitialized = true;
	return ENetworkResult::Success;
}

void FNetworkSystem::Shutdown() noexcept
{
	if (!bInitialized)
	{
		return;
	}

	EndPlay();
	(void)Messaging.Unsubscribe(ReliableSubscription);
	(void)Messaging.Unsubscribe(BestEffortSubscription);
	ReliableSubscription = {};
	BestEffortSubscription = {};
	(void)Messaging.CancelReliableMessagesForChannel(ReliableWireChannelNameId);
	(void)Messaging.DestroyChannel(ReliableWireChannelNameId);
	(void)Messaging.DestroyChannel(BestEffortWireChannelNameId);
	bInitialized = false;
}

ENetworkResult FNetworkSystem::ConnectToServer(
	const Messaging::FMessagingRoute& InServerRoute, const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	if (Information.Role != ENetworkRole::Client)
	{
		return ENetworkResult::WrongRole;
	}
	if (!bInitialized || !InServerRoute.IsValid() || CurrentAttemptId == std::numeric_limits<std::uint32_t>::max())
	{
		return CurrentAttemptId == std::numeric_limits<std::uint32_t>::max() ? ENetworkResult::Exhausted : ENetworkResult::Invalid;
	}

	++CurrentAttemptId;
	ServerRoute = InServerRoute;
	ServerPeer = {};
	ConnectionState = EConnectionState::Connecting;
	MostRecentTimeMilliseconds = InNowMilliseconds;
	LastClientSendMilliseconds = InNowMilliseconds;
	LastServerActivityMilliseconds = InNowMilliseconds;
	(void)ConnectionStateChanged.Broadcast(ConnectionState);
	return SendProtocolMessage(FConnectRequest{Information.ProtocolVersion, CurrentAttemptId}, BestEffortWireChannelNameId, ServerRoute);
}

ENetworkResult FNetworkSystem::DisconnectFromServer() noexcept
{
	if (Information.Role != ENetworkRole::Client)
	{
		return ENetworkResult::WrongRole;
	}
	if (ConnectionState != EConnectionState::Connected || !ServerPeer.IsValid())
	{
		return ENetworkResult::NotConnected;
	}

	const FDisconnect Disconnect{ServerPeer, CurrentAttemptId, EDisconnectReason::Requested};
	const ENetworkResult Result = SendProtocolMessage(Disconnect, BestEffortWireChannelNameId, ServerRoute);
	RetireServer(EConnectionState::Disconnected);
	return Result;
}

ENetworkResult FNetworkSystem::DisconnectPeer(const FPeerId InPeer) noexcept
{
	if (Information.Role != ENetworkRole::Server)
	{
		return ENetworkResult::WrongRole;
	}
	FPeerSlot* const Slot = FindPeer(InPeer);
	if (Slot == nullptr)
	{
		return ENetworkResult::NotConnected;
	}

	const ENetworkResult Result =
		SendProtocolMessage(FDisconnect{Slot->Peer, Slot->AttemptId, EDisconnectReason::Requested}, BestEffortWireChannelNameId, Slot->Route);
	RetirePeer(*Slot, EDisconnectReason::Requested);
	return Result;
}

ENetworkResult FNetworkSystem::SendToServer(const Messaging::FNameId InChannelNameId, const Messaging::FMessage& InMessage) noexcept
{
	if (Information.Role != ENetworkRole::Client)
	{
		return ENetworkResult::WrongRole;
	}
	if (ConnectionState != EConnectionState::Connected || !ServerPeer.IsValid())
	{
		return ENetworkResult::NotConnected;
	}
	return SendRoutedMessage(ServerPeer, ServerRoute, InChannelNameId, InMessage);
}

ENetworkResult FNetworkSystem::SendTo(const FPeerId InPeer, const Messaging::FNameId InChannelNameId, const Messaging::FMessage& InMessage) noexcept
{
	if (Information.Role != ENetworkRole::Server)
	{
		return ENetworkResult::WrongRole;
	}
	FPeerSlot* const Slot = FindPeer(InPeer);
	if (Slot == nullptr)
	{
		return ENetworkResult::NotConnected;
	}
	return SendRoutedMessage(InPeer, Slot->Route, InChannelNameId, InMessage);
}

ENetworkResult FNetworkSystem::Broadcast(const Messaging::FNameId InChannelNameId, const Messaging::FMessage& InMessage) noexcept
{
	if (Information.Role != ENetworkRole::Server)
	{
		return ENetworkResult::WrongRole;
	}

	bool bSentAny = false;
	bool bFailedAny = false;
	for (FPeerSlot& Slot : PeerSlots)
	{
		if (!Slot.bOccupied)
		{
			continue;
		}
		bSentAny = true;
		if (SendRoutedMessage(Slot.Peer, Slot.Route, InChannelNameId, InMessage) != ENetworkResult::Success)
		{
			bFailedAny = true;
		}
	}
	if (!bSentAny)
	{
		return ENetworkResult::NotConnected;
	}
	return bFailedAny ? ENetworkResult::Partial : ENetworkResult::Success;
}

FPeerId FNetworkSystem::ResolveSenderPeer(const Messaging::FMessage& InMessage) const noexcept
{
	const FPeerId Candidate = ReadSourceId(InMessage.GetSourceId());
	if (!Candidate.IsValid())
	{
		return {};
	}
	if (Information.Role == ENetworkRole::Client)
	{
		return Candidate == ServerPeer && ConnectionState == EConnectionState::Connected ? Candidate : FPeerId{};
	}
	for (const FPeerSlot& Slot : PeerSlots)
	{
		if (Slot.bOccupied && Slot.Peer == Candidate)
		{
			return Candidate;
		}
	}
	return {};
}

void FNetworkSystem::PreAdvance(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	MostRecentTimeMilliseconds = InNowMilliseconds;
	if (!bInitialized)
	{
		return;
	}

	if (Information.Role == ENetworkRole::Client)
	{
		if (ConnectionState == EConnectionState::Disconnected)
		{
			return;
		}
		if (InNowMilliseconds - LastClientSendMilliseconds >= Information.HeartbeatIntervalMilliseconds)
		{
			if (ConnectionState == EConnectionState::Connecting)
			{
				(void)SendProtocolMessage(FConnectRequest{Information.ProtocolVersion, CurrentAttemptId}, BestEffortWireChannelNameId, ServerRoute);
			}
			else
			{
				(void)SendProtocolMessage(FHeartbeat{ServerPeer, CurrentAttemptId}, BestEffortWireChannelNameId, ServerRoute);
			}
			LastClientSendMilliseconds = InNowMilliseconds;
		}
		if (ConnectionState == EConnectionState::Connected
			&& InNowMilliseconds - LastServerActivityMilliseconds > Information.PeerTimeoutMilliseconds)
		{
			RetireServer(EConnectionState::Disconnected);
		}
		return;
	}

	for (FPeerSlot& Slot : PeerSlots)
	{
		if (!Slot.bOccupied)
		{
			continue;
		}
		if (InNowMilliseconds - Slot.LastActivityMilliseconds > Information.PeerTimeoutMilliseconds)
		{
			RetirePeer(Slot, EDisconnectReason::Timeout);
			continue;
		}
		if (InNowMilliseconds - Slot.LastHeartbeatMilliseconds >= Information.HeartbeatIntervalMilliseconds)
		{
			(void)SendProtocolMessage(FHeartbeat{Slot.Peer, Slot.AttemptId}, BestEffortWireChannelNameId, Slot.Route);
			Slot.LastHeartbeatMilliseconds = InNowMilliseconds;
		}
	}
}

void FNetworkSystem::PostAdvance(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	MostRecentTimeMilliseconds = InNowMilliseconds;
}

void FNetworkSystem::EndPlay() noexcept
{
	if (Information.Role == ENetworkRole::Client)
	{
		RetireServer(EConnectionState::Disconnected);
		return;
	}
	for (FPeerSlot& Slot : PeerSlots)
	{
		if (Slot.bOccupied)
		{
			RetirePeer(Slot, EDisconnectReason::Requested);
		}
	}
}

void FNetworkSystem::HandleWireMessage(const Messaging::FMessage& InMessage) noexcept
{
	const Messaging::FNameId NameId = InMessage.GetMessageNameId();
	if (NameId == GetMessageNameId(FConnectRequest{}))
	{
		HandleConnectRequest(InMessage);
		return;
	}
	if (NameId == GetMessageNameId(FConnectAccepted{}))
	{
		HandleConnectAccepted(InMessage);
		return;
	}
	if (NameId == GetMessageNameId(FConnectRejected{}))
	{
		HandleConnectRejected(InMessage);
		return;
	}
	if (NameId == GetMessageNameId(FHeartbeat{}))
	{
		HandleHeartbeat(InMessage);
		return;
	}
	if (NameId == GetMessageNameId(FDisconnect{}))
	{
		HandleDisconnect(InMessage);
		return;
	}
	if (NameId == GetMessageNameId(FRoutedMessage{}))
	{
		HandleRoutedMessage(InMessage);
	}
}

void FNetworkSystem::HandleConnectRequest(const Messaging::FMessage& InMessage) noexcept
{
	if (Information.Role != ENetworkRole::Server || !InMessage.GetSenderRoute().IsValid())
	{
		return;
	}
	FConnectRequest Request{};
	if (Messaging::DecodeTypedMessage(InMessage, Request) != Messaging::EMessagingResult::Success)
	{
		return;
	}
	if (Request.ProtocolVersion != Information.ProtocolVersion)
	{
		(void)SendProtocolMessage(
			FConnectRejected{Request.AttemptId, EConnectionRejectReason::ProtocolMismatch}, BestEffortWireChannelNameId, InMessage.GetSenderRoute());
		return;
	}

	FPeerSlot* const Slot = FindAdmissionSlot(InMessage.GetSenderRoute());
	if (Slot == nullptr)
	{
		(void)SendProtocolMessage(
			FConnectRejected{Request.AttemptId, EConnectionRejectReason::Full}, BestEffortWireChannelNameId, InMessage.GetSenderRoute());
		return;
	}
	if (Slot->bOccupied && Request.AttemptId < Slot->AttemptId)
	{
		return;
	}
	if (Slot->bOccupied && Slot->AttemptId == Request.AttemptId)
	{
		(void)SendProtocolMessage(FConnectAccepted{Request.AttemptId, Slot->Peer}, BestEffortWireChannelNameId, Slot->Route);
		return;
	}
	if (Slot->bOccupied)
	{
		RetirePeer(*Slot, EDisconnectReason::Requested);
	}
	if (Slot->bRetired)
	{
		(void)SendProtocolMessage(
			FConnectRejected{Request.AttemptId, EConnectionRejectReason::Full}, BestEffortWireChannelNameId, InMessage.GetSenderRoute());
		return;
	}
	if (Slot->Peer.Generation == 0)
	{
		Slot->Peer.Generation = 1;
	}
	Slot->Peer.Index = static_cast<std::uint8_t>(Slot - PeerSlots);
	Slot->Route = InMessage.GetSenderRoute();
	Slot->AttemptId = Request.AttemptId;
	Slot->LastActivityMilliseconds = MostRecentTimeMilliseconds;
	Slot->LastHeartbeatMilliseconds = MostRecentTimeMilliseconds;
	Slot->bOccupied = true;
	(void)SendProtocolMessage(FConnectAccepted{Request.AttemptId, Slot->Peer}, BestEffortWireChannelNameId, Slot->Route);
	(void)PeerConnected.Broadcast(Slot->Peer);
}

void FNetworkSystem::HandleConnectAccepted(const Messaging::FMessage& InMessage) noexcept
{
	if (Information.Role != ENetworkRole::Client || ConnectionState != EConnectionState::Connecting || InMessage.GetSenderRoute() != ServerRoute)
	{
		return;
	}
	FConnectAccepted Accepted{};
	if (Messaging::DecodeTypedMessage(InMessage, Accepted) != Messaging::EMessagingResult::Success || Accepted.AttemptId != CurrentAttemptId
		|| !Accepted.Peer.IsValid())
	{
		return;
	}
	ServerPeer = Accepted.Peer;
	ConnectionState = EConnectionState::Connected;
	LastClientSendMilliseconds = MostRecentTimeMilliseconds;
	LastServerActivityMilliseconds = MostRecentTimeMilliseconds;
	(void)ConnectionStateChanged.Broadcast(ConnectionState);
}

void FNetworkSystem::HandleConnectRejected(const Messaging::FMessage& InMessage) noexcept
{
	if (Information.Role != ENetworkRole::Client || ConnectionState != EConnectionState::Connecting || InMessage.GetSenderRoute() != ServerRoute)
	{
		return;
	}
	FConnectRejected Rejected{};
	if (Messaging::DecodeTypedMessage(InMessage, Rejected) == Messaging::EMessagingResult::Success && Rejected.AttemptId == CurrentAttemptId)
	{
		RetireServer(EConnectionState::Disconnected);
	}
}

void FNetworkSystem::HandleHeartbeat(const Messaging::FMessage& InMessage) noexcept
{
	FHeartbeat Heartbeat{};
	if (Messaging::DecodeTypedMessage(InMessage, Heartbeat) != Messaging::EMessagingResult::Success)
	{
		return;
	}
	if (Information.Role == ENetworkRole::Client)
	{
		if (ConnectionState == EConnectionState::Connected && InMessage.GetSenderRoute() == ServerRoute && Heartbeat.Peer == ServerPeer
			&& Heartbeat.AttemptId == CurrentAttemptId)
		{
			LastServerActivityMilliseconds = MostRecentTimeMilliseconds;
		}
		return;
	}
	FPeerSlot* const Slot = FindPeerByRoute(InMessage.GetSenderRoute());
	if (Slot != nullptr && Slot->Peer == Heartbeat.Peer && Slot->AttemptId == Heartbeat.AttemptId)
	{
		Slot->LastActivityMilliseconds = MostRecentTimeMilliseconds;
	}
}

void FNetworkSystem::HandleDisconnect(const Messaging::FMessage& InMessage) noexcept
{
	FDisconnect Disconnect{};
	if (Messaging::DecodeTypedMessage(InMessage, Disconnect) != Messaging::EMessagingResult::Success)
	{
		return;
	}
	if (Information.Role == ENetworkRole::Client)
	{
		if (ConnectionState == EConnectionState::Connected && InMessage.GetSenderRoute() == ServerRoute && Disconnect.Peer == ServerPeer
			&& Disconnect.AttemptId == CurrentAttemptId)
		{
			RetireServer(EConnectionState::Disconnected);
		}
		return;
	}
	FPeerSlot* const Slot = FindPeerByRoute(InMessage.GetSenderRoute());
	if (Slot != nullptr && Slot->Peer == Disconnect.Peer && Slot->AttemptId == Disconnect.AttemptId)
	{
		RetirePeer(*Slot, Disconnect.Reason);
	}
}

void FNetworkSystem::HandleRoutedMessage(const Messaging::FMessage& InMessage) noexcept
{
	FRoutedMessage Routed{};
	if (Messaging::DecodeTypedMessage(InMessage, Routed) != Messaging::EMessagingResult::Success)
	{
		return;
	}
	if (Information.Role == ENetworkRole::Client)
	{
		if (ConnectionState != EConnectionState::Connected || InMessage.GetSenderRoute() != ServerRoute || Routed.Peer != ServerPeer)
		{
			return;
		}
		LastServerActivityMilliseconds = MostRecentTimeMilliseconds;
	}
	else
	{
		FPeerSlot* const Slot = FindPeerByRoute(InMessage.GetSenderRoute());
		if (Slot == nullptr || Routed.Peer != Slot->Peer)
		{
			return;
		}
		Slot->LastActivityMilliseconds = MostRecentTimeMilliseconds;
	}
	if (GetWireChannelNameId(Routed.ChannelNameId) == Messaging::InvalidNameId)
	{
		return;
	}
	Messaging::FMessage ApplicationMessage;
	ApplicationMessage.SetMessageNameId(Routed.MessageNameId);
	ApplicationMessage.SetPayload(Routed.GetPayload());
	ApplicationMessage.ClearSenderContext();
	ApplicationMessage.SetSourceId(MakeSourceId(Routed.Peer));
	(void)Messaging.DeliverMessageLocally(ApplicationMessage, Routed.ChannelNameId);
}

ENetworkResult FNetworkSystem::MapMessagingResult(const Messaging::EMessagingResult InResult) noexcept
{
	if (InResult == Messaging::EMessagingResult::Success)
	{
		return ENetworkResult::Success;
	}
	if (InResult == Messaging::EMessagingResult::Full)
	{
		return ENetworkResult::Full;
	}
	return ENetworkResult::Invalid;
}

Messaging::FMessageSourceId FNetworkSystem::MakeSourceId(const FPeerId InPeer) noexcept
{
	return InPeer.IsValid()
		? Messaging::
			  FMessageSourceId{(static_cast<std::uint64_t>(InPeer.Generation) << SourceIdPeerGenerationShift) | InPeer.Index | SourceIdNetworkMarkerMask}
		: Messaging::FMessageSourceId{};
}

FPeerId FNetworkSystem::ReadSourceId(const Messaging::FMessageSourceId InSourceId) noexcept
{
	const std::uint64_t SourceIdLayoutMask =
		SourceIdNetworkMarkerMask | SourceIdPeerIndexMask | (SourceIdPeerGenerationMask << SourceIdPeerGenerationShift);
	if (!InSourceId.IsValid() || (InSourceId.Value & SourceIdNetworkMarkerMask) == 0 || (InSourceId.Value & ~SourceIdLayoutMask) != 0)
	{
		return {};
	}
	return FPeerId{
		static_cast<std::uint8_t>(InSourceId.Value & SourceIdPeerIndexMask),
		static_cast<std::uint32_t>((InSourceId.Value >> SourceIdPeerGenerationShift) & SourceIdPeerGenerationMask)};
}

Messaging::FNameId FNetworkSystem::GetWireChannelNameId(const Messaging::FNameId InApplicationChannelNameId) const noexcept
{
	Messaging::FChannelTraits Traits{};
	if (InApplicationChannelNameId == BestEffortWireChannelNameId || InApplicationChannelNameId == ReliableWireChannelNameId
		|| Messaging.GetChannelTraits(InApplicationChannelNameId, Traits) != Messaging::EMessagingResult::Success || Traits.bHasDefaultRoute)
	{
		return Messaging::InvalidNameId;
	}
	return Traits.bIsReliable ? ReliableWireChannelNameId : BestEffortWireChannelNameId;
}

FNetworkSystem::FPeerSlot* FNetworkSystem::FindPeer(const FPeerId InPeer) noexcept
{
	if (!InPeer.IsValid() || InPeer.Index >= MaxPeers)
	{
		return nullptr;
	}
	FPeerSlot& Slot = PeerSlots[InPeer.Index];
	return Slot.bOccupied && Slot.Peer == InPeer ? &Slot : nullptr;
}

FNetworkSystem::FPeerSlot* FNetworkSystem::FindPeerByRoute(const Messaging::FMessagingRoute& InRoute) noexcept
{
	for (FPeerSlot& Slot : PeerSlots)
	{
		if (Slot.bOccupied && Slot.Route == InRoute)
		{
			return &Slot;
		}
	}
	return nullptr;
}

FNetworkSystem::FPeerSlot* FNetworkSystem::FindAdmissionSlot(const Messaging::FMessagingRoute& InRoute) noexcept
{
	FPeerSlot* FreeSlot = nullptr;
	for (FPeerSlot& Slot : PeerSlots)
	{
		if (Slot.bOccupied && Slot.Route == InRoute)
		{
			return &Slot;
		}
		if (!Slot.bOccupied && !Slot.bRetired && FreeSlot == nullptr)
		{
			FreeSlot = &Slot;
		}
	}
	return FreeSlot;
}

void FNetworkSystem::RetirePeer(FPeerSlot& InSlot, const EDisconnectReason InReason) noexcept
{
	const FPeerId RetiredPeer = InSlot.Peer;
	InSlot.bOccupied = false;
	InSlot.Route = {};
	InSlot.AttemptId = 0;
	if (InSlot.Peer.Generation == std::numeric_limits<std::uint32_t>::max())
	{
		InSlot.bRetired = true;
	}
	else
	{
		++InSlot.Peer.Generation;
	}
	(void)PeerDisconnected.Broadcast(RetiredPeer, InReason);
}

void FNetworkSystem::RetireServer(const EConnectionState InNextState) noexcept
{
	if (ConnectionState == InNextState && !ServerPeer.IsValid())
	{
		return;
	}
	ServerRoute = {};
	ServerPeer = {};
	ConnectionState = InNextState;
	(void)ConnectionStateChanged.Broadcast(ConnectionState);
}

template<typename MessageType>
ENetworkResult FNetworkSystem::SendProtocolMessage(
	const MessageType& InMessage, const Messaging::FNameId InWireChannel, const Messaging::FMessagingRoute& InRoute) noexcept
{
	return MapMessagingResult(Messaging.SendTypedMessageToRemoteChannel(InMessage, InWireChannel, InRoute));
}

ENetworkResult FNetworkSystem::SendRoutedMessage(
	const FPeerId InPeer,
	const Messaging::FMessagingRoute& InRoute,
	const Messaging::FNameId InChannelNameId,
	const Messaging::FMessage& InMessage) noexcept
{
	const Messaging::FNameId WireChannel = GetWireChannelNameId(InChannelNameId);
	if (WireChannel == Messaging::InvalidNameId || InMessage.GetMessageNameId() == Messaging::InvalidNameId
		|| InMessage.GetPayload().Size() > MaxRoutedMessageBytes)
	{
		return ENetworkResult::Invalid;
	}
	FRoutedMessage Routed{};
	Routed.Peer = InPeer;
	Routed.ChannelNameId = InChannelNameId;
	Routed.MessageNameId = InMessage.GetMessageNameId();
	Routed.PayloadSize = static_cast<std::uint8_t>(InMessage.GetPayload().Size());
	for (std::size_t Index = 0; Index < Routed.PayloadSize; ++Index)
	{
		Routed.Payload[Index] = InMessage.GetPayload()[Index];
	}
	return SendProtocolMessage(Routed, WireChannel, InRoute);
}

} // namespace MicroWorld::Networking
