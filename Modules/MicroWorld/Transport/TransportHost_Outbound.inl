// Out-of-class member definitions: outbound framing, queue, heartbeat, bye, and
// local-peer dispatch for TTransportHost<MaxPeers, MaxPacketBytes>. Included from
// TransportHost.h after the class body closes; never include this file directly.
// Depends on declarations from TransportHost_PeerTable.inl (IsHeartbeatDue,
// EvictPeer, ElapsedSince) and is included before TransportHost_ControlMessages.inl,
// whose HandleHello/HandleBye rely on QueueControl and SendWelcome defined here.

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
Core::ETransportResult TTransportHost<MaxPeers, MaxPacketBytes>::Configure(const ENetworkMode InMode, const FTransportHostConfig& InConfig) noexcept
{
	if (State != ETransportHostState::Idle)
	{
		return Core::ETransportResult::Invalid;
	}
	Mode = InMode;
	Config = InConfig;
	return Core::ETransportResult::Success;
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
Core::ETransportResult TTransportHost<MaxPeers, MaxPacketBytes>::Start(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	if (State != ETransportHostState::Idle)
	{
		return Core::ETransportResult::Invalid;
	}
	switch (Mode)
	{
		case ENetworkMode::Client:
			State = ETransportHostState::Connecting;
			LastHelloSendMilliseconds = InNowMilliseconds;
			bHelloDue = true;
			break;
		case ENetworkMode::ListenServer:
		case ENetworkMode::DedicatedServer:
			State = ETransportHostState::Listening;
			break;
		case ENetworkMode::Standalone:
			break;
	}
	return Core::ETransportResult::Success;
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
void TTransportHost<MaxPeers, MaxPacketBytes>::Stop() noexcept
{
	SendByeToAllActivePeers();
	EvictAllPeers();
	State = ETransportHostState::Idle;
	bHelloDue = false;
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
Core::ETransportResult TTransportHost<MaxPeers, MaxPacketBytes>::PumpReceive(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	if (Mode == ENetworkMode::Standalone)
	{
		return Core::ETransportResult::Success;
	}
	DrainInboundPackets(InNowMilliseconds);
	EvictTimedOutPeers(InNowMilliseconds);
	return Core::ETransportResult::Success;
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
Core::ETransportResult TTransportHost<MaxPeers, MaxPacketBytes>::PumpSend(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	if (Mode == ENetworkMode::Standalone)
	{
		return Core::ETransportResult::Success;
	}
	SendClientHelloIfDue(InNowMilliseconds);
	SendDueHeartbeats(InNowMilliseconds);
	DrainOutbound();
	Device.PreAdvance(InNowMilliseconds);
	return Core::ETransportResult::Success;
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
Core::ETransportResult TTransportHost<MaxPeers, MaxPacketBytes>::SendTo(
	const FPeerId InPeer, const std::uint8_t InChannel, Core::TSpan<const std::uint8_t> InPayload) noexcept
{
	if (Mode == ENetworkMode::Standalone)
	{
		return Core::ETransportResult::Unavailable;
	}
	if (InChannel == ControlChannel)
	{
		return Core::ETransportResult::Invalid;
	}
	if (InPeer.Index == LocalPeerIndex)
	{
		return SendToLocalPeer(InChannel, InPayload);
	}
	const FTransportPeerSlot* const Slot = ResolvePeer(InPeer);
	if (Slot == nullptr)
	{
		return Core::ETransportResult::Invalid;
	}
	return QueueAppMessage(Slot->Address, InChannel, InPayload);
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
Core::ETransportResult TTransportHost<MaxPeers, MaxPacketBytes>::Broadcast(
	const std::uint8_t InChannel, Core::TSpan<const std::uint8_t> InPayload) noexcept
{
	if (Mode == ENetworkMode::Standalone)
	{
		return Core::ETransportResult::Unavailable;
	}
	if (InChannel == ControlChannel)
	{
		return Core::ETransportResult::Invalid;
	}
	if (Mode == ENetworkMode::ListenServer)
	{
		DispatchToHandler(GetLocalPeer(), InChannel, InPayload);
	}
	Core::ETransportResult Outcome = Core::ETransportResult::Success;
	for (std::size_t Index = 0; Index < MaxPeers; ++Index)
	{
		if (!Peers[Index].bActive)
		{
			continue;
		}
		const Core::ETransportResult SlotResult = QueueAppMessage(Peers[Index].Address, InChannel, InPayload);
		if (SlotResult != Core::ETransportResult::Success && Outcome == Core::ETransportResult::Success)
		{
			Outcome = SlotResult;
		}
	}
	return Outcome;
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
FPeerId TTransportHost<MaxPeers, MaxPacketBytes>::GetServerPeer() const noexcept
{
	if (Mode != ENetworkMode::Client || State != ETransportHostState::Connected)
	{
		return FPeerId{};
	}
	return FPeerId{ServerPeerSlotIndex, Peers[ServerPeerSlotIndex].Generation};
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
std::size_t TTransportHost<MaxPeers, MaxPacketBytes>::ActivePeerCount() const noexcept
{
	std::size_t ActiveCount = 0;
	for (std::size_t Index = 0; Index < MaxPeers; ++Index)
	{
		if (Peers[Index].bActive)
		{
			++ActiveCount;
		}
	}
	return ActiveCount;
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
void TTransportHost<MaxPeers, MaxPacketBytes>::SendWelcome(const std::size_t InPeerIndex, const Core::FDeviceAddress& InTo) noexcept
{
	FControlMessage Welcome{};
	Welcome.Type = EControlMessageType::Welcome;
	Welcome.ProtocolVersion = Config.ProtocolVersion;
	Welcome.PeerIndex = static_cast<std::uint8_t>(InPeerIndex);
	Welcome.PeerGeneration = Peers[InPeerIndex].Generation;
	if (QueueControl(InTo, Welcome) != Core::ETransportResult::Success)
	{
		MW_LOG_MSG(Warning, "TransportHost", "Welcome not queued: outbound queue full");
	}
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
void TTransportHost<MaxPeers, MaxPacketBytes>::QueueHello() noexcept
{
	FControlMessage Hello{};
	Hello.Type = EControlMessageType::Hello;
	Hello.ProtocolVersion = Config.ProtocolVersion;
	if (QueueControl(Config.ServerAddress, Hello) != Core::ETransportResult::Success)
	{
		MW_LOG_MSG(Warning, "TransportHost", "Hello not queued: outbound queue full");
	}
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
Core::ETransportResult TTransportHost<MaxPeers, MaxPacketBytes>::QueueControl(
	const Core::FDeviceAddress& InTo, const FControlMessage& InControl) noexcept
{
	std::array<std::uint8_t, MessageHeaderBytes + MaxControlPayloadBytes> FrameBuffer{};
	FByteWriter Writer(Core::TSpan<std::uint8_t>(FrameBuffer.data(), FrameBuffer.size()));
	const Core::ETransportResult WriteResult = WriteControlMessage(Writer, InControl);
	if (WriteResult != Core::ETransportResult::Success)
	{
		return WriteResult;
	}
	return OutboundManager.QueueSend(InTo, Writer.WrittenBytes());
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
Core::ETransportResult TTransportHost<MaxPeers, MaxPacketBytes>::QueueAppMessage(
	const Core::FDeviceAddress& InTo, const std::uint8_t InChannel, Core::TSpan<const std::uint8_t> InPayload) noexcept
{
	std::array<std::uint8_t, MaxPacketBytes> FrameBuffer{};
	FByteWriter Writer(Core::TSpan<std::uint8_t>(FrameBuffer.data(), FrameBuffer.size()));
	const Core::ETransportResult WriteResult = WriteMessage(Writer, InChannel, InPayload);
	if (WriteResult != Core::ETransportResult::Success)
	{
		return WriteResult;
	}
	return OutboundManager.QueueSend(InTo, Writer.WrittenBytes());
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
void TTransportHost<MaxPeers, MaxPacketBytes>::DispatchToHandler(
	const FPeerId InFrom, const std::uint8_t InChannel, Core::TSpan<const std::uint8_t> InPayload) noexcept
{
	(void)MessageHandler.Broadcast(InFrom, InChannel, InPayload);
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
void TTransportHost<MaxPeers, MaxPacketBytes>::DrainOutbound() noexcept
{
	for (std::size_t Count = 0; Count < SendQueueDepth; ++Count)
	{
		if (OutboundManager.AdvanceSend() != Core::ETransportResult::Success)
		{
			// Unavailable means the FIFO is empty; a device failure retains the head for a later drain.
			break;
		}
	}
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
void TTransportHost<MaxPeers, MaxPacketBytes>::DrainInboundPackets(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	std::array<std::uint8_t, MaxPacketBytes> ReceiveBuffer{};
	const std::size_t MaxReceives = MaxPeers + PumpSlackPackets;
	for (std::size_t Count = 0; Count < MaxReceives; ++Count)
	{
		Core::FDeviceAddress From{};
		Core::FReceiveResult Result{};
		const Core::ETransportResult ReceiveResult =
			OutboundManager.Receive(From, Core::TSpan<std::uint8_t>(ReceiveBuffer.data(), ReceiveBuffer.size()), Result);
		if (ReceiveResult != Core::ETransportResult::Success)
		{
			// Unavailable means the transport is drained; any other failure cannot make progress now.
			break;
		}
		HandleInboundPacket(From, Core::TSpan<const std::uint8_t>(ReceiveBuffer.data(), Result.BytesReceived), InNowMilliseconds);
	}
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
void TTransportHost<MaxPeers, MaxPacketBytes>::SendClientHelloIfDue(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	if (Mode != ENetworkMode::Client || State != ETransportHostState::Connecting)
	{
		return;
	}
	if (bHelloDue || ElapsedSince(InNowMilliseconds, LastHelloSendMilliseconds) >= Config.HeartbeatIntervalMilliseconds)
	{
		QueueHello();
		LastHelloSendMilliseconds = InNowMilliseconds;
		bHelloDue = false;
	}
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
void TTransportHost<MaxPeers, MaxPacketBytes>::SendDueHeartbeats(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	for (std::size_t Index = 0; Index < MaxPeers; ++Index)
	{
		FTransportPeerSlot& Slot = Peers[Index];
		if (Slot.bActive && IsHeartbeatDue(Slot, InNowMilliseconds))
		{
			FControlMessage HeartbeatMessage{};
			HeartbeatMessage.Type = EControlMessageType::Heartbeat;
			(void)QueueControl(Slot.Address, HeartbeatMessage);
			Slot.LastSendMilliseconds = InNowMilliseconds;
		}
	}
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
void TTransportHost<MaxPeers, MaxPacketBytes>::SendByeToAllActivePeers() noexcept
{
	if (Mode == ENetworkMode::Standalone)
	{
		return;
	}
	FControlMessage ByeMessage{};
	ByeMessage.Type = EControlMessageType::Bye;
	for (std::size_t Index = 0; Index < MaxPeers; ++Index)
	{
		if (Peers[Index].bActive)
		{
			(void)QueueControl(Peers[Index].Address, ByeMessage);
		}
	}
	DrainOutbound();
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
void TTransportHost<MaxPeers, MaxPacketBytes>::EvictAllPeers() noexcept
{
	for (std::size_t Index = 0; Index < MaxPeers; ++Index)
	{
		if (Peers[Index].bActive)
		{
			EvictPeer(Index);
		}
	}
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
Core::ETransportResult TTransportHost<MaxPeers, MaxPacketBytes>::SendToLocalPeer(
	const std::uint8_t InChannel, Core::TSpan<const std::uint8_t> InPayload) noexcept
{
	if (Mode != ENetworkMode::ListenServer)
	{
		return Core::ETransportResult::Invalid;
	}
	DispatchToHandler(GetLocalPeer(), InChannel, InPayload);
	return Core::ETransportResult::Success;
}
