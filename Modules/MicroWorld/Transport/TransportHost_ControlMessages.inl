// Out-of-class member definitions: inbound packet routing and the channel-0
// control-message handlers for TTransportHost<MaxPeers, MaxPacketBytes>. Included
// from TransportHost.h after the class body closes; never include this file directly.
// Depends on declarations from TransportHost_PeerTable.inl
// (FindActivePeerIndexByAddress, AdmitPeer, EvictPeer, OnPeerLost, MakePeerId) and
// TransportHost_Outbound.inl (QueueControl, SendWelcome).

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
void TTransportHost<MaxPeers, MaxPacketBytes>::HandleInboundPacket(
	const Core::FDeviceAddress& InFrom, Core::TSpan<const std::uint8_t> InPacket, const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	FMessageHeader Header{};
	Core::TSpan<const std::uint8_t> Payload{};
	if (ReadMessage(InPacket, Header, Payload) != Core::ETransportResult::Success)
	{
		MW_LOG_MSG(Log, "TransportHost", "dropped malformed inbound packet");
		return;
	}
	if (Header.Channel == ControlChannel)
	{
		HandleControlMessage(InFrom, Payload, InNowMilliseconds);
		return;
	}
	const std::size_t Index = FindActivePeerIndexByAddress(InFrom);
	if (Index == MaxPeers)
	{
		MW_LOG_MSG(Log, "TransportHost", "dropped application message from unknown peer");
		return;
	}
	Peers[Index].LastReceiveMilliseconds = InNowMilliseconds;
	DispatchToHandler(MakePeerId(Index), Header.Channel, Payload);
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
void TTransportHost<MaxPeers, MaxPacketBytes>::HandleControlMessage(
	const Core::FDeviceAddress& InFrom, Core::TSpan<const std::uint8_t> InPayload, const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	FControlMessage Control{};
	if (ReadControlMessage(InPayload, Control) != Core::ETransportResult::Success)
	{
		MW_LOG_MSG(Log, "TransportHost", "dropped unknown or malformed control message");
		return;
	}
	switch (Control.Type)
	{
		case EControlMessageType::Hello:
			HandleHello(InFrom, Control, InNowMilliseconds);
			break;
		case EControlMessageType::Welcome:
			HandleWelcome(InFrom, Control, InNowMilliseconds);
			break;
		case EControlMessageType::Heartbeat:
			HandleHeartbeat(InFrom, InNowMilliseconds);
			break;
		case EControlMessageType::Bye:
			HandleBye(InFrom);
			break;
	}
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
void TTransportHost<MaxPeers, MaxPacketBytes>::HandleHello(
	const Core::FDeviceAddress& InFrom, const FControlMessage& InControl, const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	if (!IsServer())
	{
		return;
	}
	if (InControl.ProtocolVersion != Config.ProtocolVersion)
	{
		MW_LOG(
			Warning,
			"TransportHost",
			"ignored Hello: protocol version %u != %u",
			static_cast<unsigned>(InControl.ProtocolVersion),
			static_cast<unsigned>(Config.ProtocolVersion));
		return;
	}
	const std::size_t Index = AdmitPeer(InFrom, InNowMilliseconds);
	if (Index == MaxPeers)
	{
		return;
	}
	SendWelcome(Index, InFrom);
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
void TTransportHost<MaxPeers, MaxPacketBytes>::HandleWelcome(
	const Core::FDeviceAddress& InFrom, const FControlMessage& InControl, const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	if (Mode != ENetworkMode::Client)
	{
		return;
	}
	if (InControl.ProtocolVersion != Config.ProtocolVersion)
	{
		MW_LOG(
			Warning,
			"TransportHost",
			"ignored Welcome: server version %u != %u",
			static_cast<unsigned>(InControl.ProtocolVersion),
			static_cast<unsigned>(Config.ProtocolVersion));
		return;
	}
	FTransportPeerSlot& Server = Peers[ServerPeerSlotIndex];
	Server.Address = InFrom;
	Server.LastReceiveMilliseconds = InNowMilliseconds;
	Server.LastSendMilliseconds = InNowMilliseconds;
	Server.bActive = true;
	AssignedPeer = FPeerId{InControl.PeerIndex, InControl.PeerGeneration};
	State = ETransportHostState::Connected;
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
void TTransportHost<MaxPeers, MaxPacketBytes>::HandleHeartbeat(
	const Core::FDeviceAddress& InFrom, const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	const std::size_t Index = FindActivePeerIndexByAddress(InFrom);
	if (Index == MaxPeers)
	{
		MW_LOG_MSG(Log, "TransportHost", "ignored heartbeat from unknown peer");
		return;
	}
	Peers[Index].LastReceiveMilliseconds = InNowMilliseconds;
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
void TTransportHost<MaxPeers, MaxPacketBytes>::HandleBye(const Core::FDeviceAddress& InFrom) noexcept
{
	const std::size_t Index = FindActivePeerIndexByAddress(InFrom);
	if (Index == MaxPeers)
	{
		return;
	}
	EvictPeer(Index);
	OnPeerLost(Index);
}
