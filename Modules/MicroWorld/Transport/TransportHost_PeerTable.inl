// Out-of-class member definitions: peer table, liveness timing, and slot management
// for TTransportHost<MaxPeers, MaxPacketBytes>. Included from TransportHost.h after
// the class body closes; never include this file directly.

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
constexpr Core::DurationMilliseconds TTransportHost<MaxPeers, MaxPacketBytes>::ElapsedSince(
	const Core::TimePointMilliseconds InNowMilliseconds, const Core::TimePointMilliseconds InPastMilliseconds) noexcept
{
	if (InNowMilliseconds <= InPastMilliseconds)
	{
		return 0;
	}
	const Core::TimePointMilliseconds Delta = InNowMilliseconds - InPastMilliseconds;
	// DurationMilliseconds is u32 (~49 days max), so clamp a wider gap to that
	// ceiling instead of overflowing when it is narrowed.
	constexpr Core::TimePointMilliseconds MaxDuration = std::numeric_limits<Core::DurationMilliseconds>::max();
	return Delta > MaxDuration ? static_cast<Core::DurationMilliseconds>(MaxDuration) : static_cast<Core::DurationMilliseconds>(Delta);
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
bool TTransportHost<MaxPeers, MaxPacketBytes>::IsPeerTimedOut(
	const FTransportPeerSlot& InSlot, const Core::TimePointMilliseconds InNowMilliseconds) const noexcept
{
	return ElapsedSince(InNowMilliseconds, InSlot.LastReceiveMilliseconds) > Config.PeerTimeoutMilliseconds;
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
bool TTransportHost<MaxPeers, MaxPacketBytes>::IsHeartbeatDue(
	const FTransportPeerSlot& InSlot, const Core::TimePointMilliseconds InNowMilliseconds) const noexcept
{
	return ElapsedSince(InNowMilliseconds, InSlot.LastSendMilliseconds) >= Config.HeartbeatIntervalMilliseconds;
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
std::size_t TTransportHost<MaxPeers, MaxPacketBytes>::FindActivePeerIndexByAddress(const Core::FDeviceAddress& InAddress) const noexcept
{
	for (std::size_t Index = 0; Index < MaxPeers; ++Index)
	{
		if (Peers[Index].bActive && Peers[Index].Address == InAddress)
		{
			return Index;
		}
	}
	return MaxPeers;
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
std::size_t TTransportHost<MaxPeers, MaxPacketBytes>::FindFreePeerSlot() const noexcept
{
	for (std::size_t Index = 0; Index < MaxPeers; ++Index)
	{
		if (!Peers[Index].bActive)
		{
			return Index;
		}
	}
	return MaxPeers;
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
const typename TTransportHost<MaxPeers, MaxPacketBytes>::FTransportPeerSlot* TTransportHost<MaxPeers, MaxPacketBytes>::ResolvePeer(
	const FPeerId InPeer) const noexcept
{
	if (InPeer.Index >= MaxPeers)
	{
		return nullptr;
	}
	const FTransportPeerSlot& Slot = Peers[InPeer.Index];
	if (!Slot.bActive || Slot.Generation != InPeer.Generation)
	{
		return nullptr;
	}
	return &Slot;
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
void TTransportHost<MaxPeers, MaxPacketBytes>::EvictPeer(const std::size_t InIndex) noexcept
{
	FTransportPeerSlot& Slot = Peers[InIndex];
	Slot.bActive = false;
	// Generation is u8, so it wraps after 256 evictions of this slot; a stale
	// id from exactly 256 evictions ago would re-match -- an accepted,
	// practically-unreachable window.
	Slot.Generation = static_cast<std::uint8_t>(Slot.Generation + 1);
	Slot.Address = Core::FDeviceAddress{};
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
void TTransportHost<MaxPeers, MaxPacketBytes>::OnPeerLost(const std::size_t InIndex) noexcept
{
	if (Mode == ENetworkMode::Client && InIndex == ServerPeerSlotIndex)
	{
		State = ETransportHostState::Connecting;
		bHelloDue = true;
	}
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
void TTransportHost<MaxPeers, MaxPacketBytes>::EvictTimedOutPeers(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	for (std::size_t Index = 0; Index < MaxPeers; ++Index)
	{
		FTransportPeerSlot& Slot = Peers[Index];
		if (!Slot.bActive)
		{
			continue;
		}
		if (IsPeerTimedOut(Slot, InNowMilliseconds))
		{
			MW_LOG(Log, "TransportHost", "evicting peer %u (timeout)", static_cast<unsigned>(Index));
			EvictPeer(Index);
			OnPeerLost(Index);
		}
	}
}

template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
std::size_t TTransportHost<MaxPeers, MaxPacketBytes>::AdmitPeer(
	const Core::FDeviceAddress& InFrom, const Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	std::size_t Index = FindActivePeerIndexByAddress(InFrom);
	if (Index == MaxPeers)
	{
		Index = FindFreePeerSlot();
		if (Index == MaxPeers)
		{
			MW_LOG_MSG(Warning, "TransportHost", "rejected Hello: peer table full");
			return MaxPeers;
		}
		FTransportPeerSlot& Slot = Peers[Index];
		Slot.Address = InFrom;
		Slot.LastReceiveMilliseconds = InNowMilliseconds;
		Slot.LastSendMilliseconds = InNowMilliseconds;
		Slot.bActive = true;
	}
	else
	{
		Peers[Index].LastReceiveMilliseconds = InNowMilliseconds;
	}
	return Index;
}
