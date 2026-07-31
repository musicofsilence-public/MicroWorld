#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Delegates/Delegate.h>
#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/ByteWriter.h>
#include <MicroWorld/Transport/DeviceAddress.h>
#include <MicroWorld/Transport/Device.h>
#include <MicroWorld/Transport/TransportManager.h>
#include <MicroWorld/Transport/TransportPacketStorage.h>
#include <MicroWorld/Transport/TransportProtocol.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Core/Time.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace MicroWorld::Transport
{

/** The UE5-style role this host plays; selects which session traffic it originates and accepts. */
enum class ENetworkMode : std::uint8_t
{
	/** Runs no device traffic; every send reports `Unavailable`. */
	Standalone,

	/** Holds exactly one peer (the server) and sends `Hello` until admitted. */
	Client,

	/** Admits remote peers and additionally owns a directly dispatched local peer. */
	ListenServer,

	/** Admits remote peers with no local peer of its own. */
	DedicatedServer,
};

/** Observable session state, primarily meaningful to a `Client` tracking admission. */
enum class ETransportHostState : std::uint8_t
{
	/** Not started, standalone, or stopped; no session is in progress. */
	Idle,

	/** A client has sent `Hello` and is awaiting `Welcome`. */
	Connecting,

	/** A client has been admitted and heartbeats are flowing. */
	Connected,

	/** A server is started and accepting `Hello` up to its peer capacity. */
	Listening,
};

/** Default heartbeat cadence used when a caller does not override `FTransportHostConfig`. */
inline constexpr Core::DurationMilliseconds DefaultHeartbeatIntervalMilliseconds = 1000;

/** Default peer eviction window used when a caller does not override `FTransportHostConfig`. */
inline constexpr Core::DurationMilliseconds DefaultPeerTimeoutMilliseconds = 5000;

/** Default protocol version advertised in `Hello`/`Welcome` when a caller does not override it. */
inline constexpr std::uint8_t DefaultProtocolVersion = 1;

/** Session timing and identity supplied once before `Start`. */
struct FTransportHostConfig
{
	/** Interval between outgoing heartbeats (and client `Hello` retries while connecting). */
	Core::DurationMilliseconds HeartbeatIntervalMilliseconds{DefaultHeartbeatIntervalMilliseconds};

	/** Silence window after which a peer is evicted; must exceed the heartbeat interval. */
	Core::DurationMilliseconds PeerTimeoutMilliseconds{DefaultPeerTimeoutMilliseconds};

	/** Address the client greets with `Hello`; ignored by every non-client mode. */
	::MicroWorld::Transport::Address::FDeviceAddress ServerAddress{};

	/** Protocol version advertised in `Hello`/`Welcome`; a mismatch is ignored, not admitted. */
	std::uint8_t ProtocolVersion{DefaultProtocolVersion};
};

/** Generation-checked identity of one peer, so a reused slot never answers to a stale id. */
struct FPeerId
{
	/** Reserved index that names no peer; the default identity is deliberately invalid. */
	static constexpr std::uint8_t InvalidIndex = 0xFF;

	/** Peer slot index, or `InvalidIndex`; the host also reserves `0xFE` for a local peer. */
	std::uint8_t Index{InvalidIndex};

	/** Slot generation at the time of issue; a later eviction bumps it so this id goes stale. */
	std::uint8_t Generation{0};

	/** Reports whether the identity names a routable peer rather than the invalid default. */
	constexpr bool IsValid() const noexcept { return Index != InvalidIndex; }
};

/** Compares the complete generation-checked peer identity. */
constexpr bool operator==(const FPeerId InLeft, const FPeerId InRight) noexcept
{
	return InLeft.Index == InRight.Index && InLeft.Generation == InRight.Generation;
}

/** Negates `operator==` so callers can test peer inequality directly. */
constexpr bool operator!=(const FPeerId InLeft, const FPeerId InRight) noexcept
{
	return !(InLeft == InRight);
}

/**
 * Bounded session host over one `::MicroWorld::Transport::Device::IDevice`, delivering the UE5 dedicated/listen/client roles.
 *
 * Owns a fixed peer table, an outbound FIFO, and one message handler; drives the
 * protocol only through explicit `PumpReceive`/`PumpSend` ticks so it samples no
 * clock and allocates nothing. Channel 0 is handled internally (admission,
 * heartbeats, timeout eviction); channels 1..255 dispatch to the registered handler.
 */
template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
class TTransportHost final
{
	static_assert(MaxPeers > 0, "TTransportHost requires at least one peer slot.");
	static_assert(MaxPeers < 0xFE, "TTransportHost reserves peer indices 0xFE (local) and 0xFF (invalid).");
	static_assert(
		MaxPacketBytes >= MessageHeaderBytes + MaxControlPayloadBytes,
		"TTransportHost packets must fit the largest control frame (header + Welcome).");

public:
	/** Number of packets one full peer broadcast occupies in the outbound FIFO. */
	static constexpr std::size_t BroadcastPacketsPerPeer = 1;

	/** Packets reserved per peer for heartbeats and replies between pumps. */
	static constexpr std::size_t HeartbeatPacketsPerPeer = 1;

	/** Extra outbound/inbound packet headroom so a burst of control traffic between pumps is not dropped. */
	static constexpr std::size_t PumpSlackPackets = 4;

	/** Outbound FIFO depth: one full broadcast plus pending heartbeats plus slack. */
	static constexpr std::size_t SendQueueDepth = (BroadcastPacketsPerPeer + HeartbeatPacketsPerPeer) * MaxPeers + PumpSlackPackets;

	/** Fixed number of message-handler bindings; small because one dispatcher usually suffices. */
	static constexpr std::size_t MaxMessageHandlers = 4;

	/** Inline bytes reserved per handler callable, sized for a small capture. */
	static constexpr std::size_t MessageHandlerInlineBytes = 32;

	/** Largest encoded message one packet can carry: the packet budget minus the
	 *  4-byte message header. A channel binding exposes this as its send ceiling. */
	static constexpr std::size_t MaxMessageBytes = MaxPacketBytes - MessageHeaderBytes;

	/** Reserved peer index that routes a message to the listen server's local peer. */
	static constexpr std::uint8_t LocalPeerIndex = 0xFE;

	/** The listen server's local peer is never evicted, so its generation never advances past this initial value. */
	static constexpr std::uint8_t LocalPeerGeneration = 1;

	/** A client keeps exactly one peer — the server — in slot 0; this index is only meaningful in Client mode. */
	static constexpr std::uint8_t ServerPeerSlotIndex = 0;

	/** Multicast dispatcher type for application (channel >= 1) messages. */
	using FMessageHandler =
		Core::TMulticastDelegate<void(FPeerId, std::uint8_t, Core::TSpan<const std::uint8_t>), MaxMessageHandlers, MessageHandlerInlineBytes>;

	/** One bindable handler callable matching `FMessageHandler`'s signature. */
	using FMessageHandlerBinding = Core::TDelegate<void(FPeerId, std::uint8_t, Core::TSpan<const std::uint8_t>), MessageHandlerInlineBytes>;

	/** Binds the host to one externally owned device; mode and config follow via `Configure`. */
	explicit TTransportHost(::MicroWorld::Transport::Device::IDevice& InDevice) noexcept
		: Device(InDevice), OutboundManager(InDevice, OutboundStorage)
	{
	}

	/** Prevents copying so one host value binds one device, table, and handler. */
	TTransportHost(const TTransportHost&) = delete;

	/** Prevents copying so one host value binds one device, table, and handler. */
	TTransportHost& operator=(const TTransportHost&) = delete;

	/** Prevents moving so the owned manager's device reference and handler slots stay fixed. */
	TTransportHost(TTransportHost&&) = delete;

	/** Prevents moving so the owned manager's device reference and handler slots stay fixed. */
	TTransportHost& operator=(TTransportHost&&) = delete;

	/** Defaulted; the host holds only fixed inline storage and no external resource. */
	~TTransportHost() noexcept = default;

	/**
	 * Sets the role and session parameters before the host starts.
	 * Returns `Invalid` without changing anything when the host is not `Idle`, so a
	 * running session cannot be silently reconfigured.
	 */
	ETransportResult Configure(const ENetworkMode InMode, const FTransportHostConfig& InConfig) noexcept
	{
		if (State != ETransportHostState::Idle)
		{
			return ETransportResult::Invalid;
		}
		Mode = InMode;
		Config = InConfig;
		return ETransportResult::Success;
	}

	/**
	 * Begins the session: a client enters `Connecting` (and greets on the next `PumpSend`),
	 * a server enters `Listening`, standalone stays `Idle`. Returns `Invalid` when already started.
	 */
	ETransportResult Start(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
	{
		if (State != ETransportHostState::Idle)
		{
			return ETransportResult::Invalid;
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
		return ETransportResult::Success;
	}

	/**
	 * Ends the session: best-effort `Bye` queueing to every active peer, then evicts all and returns to `Idle`.
	 *
	 * This operation never waits
	 * for a physical transport drain; a caller that needs one must keep pumping before
	 * destroying the externally owned device.
	 * The
	 * generation of each evicted slot is bumped so any outstanding `FPeerId` goes stale.
	 */
	void Stop() noexcept
	{
		SendByeToAllActivePeers();
		EvictAllPeers();
		State = ETransportHostState::Idle;
		bHelloDue = false;
	}

	/**
	 * Drains inbound packets (at most `MaxPeers + 4`), handling control internally and
	 * dispatching application messages, then evicts peers past the timeout window.
	 * A standalone host does no device traffic and returns immediately.
	 */
	ETransportResult PumpReceive(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
	{
		if (Mode == ENetworkMode::Standalone)
		{
			return ETransportResult::Success;
		}
		DrainInboundPackets(InNowMilliseconds);
		EvictTimedOutPeers(InNowMilliseconds);
		return ETransportResult::Success;
	}

	/**
	 * Emits due heartbeats (and client `Hello` retries), then drains the outbound FIFO.
	 * A standalone host does no device traffic and returns immediately.
	 */
	ETransportResult PumpSend(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
	{
		if (Mode == ENetworkMode::Standalone)
		{
			return ETransportResult::Success;
		}
		SendClientHelloIfDue(InNowMilliseconds);
		SendDueHeartbeats(InNowMilliseconds);
		DrainOutbound();
		Device.AdvanceTransmit();
		return ETransportResult::Success;
	}

	/**
	 * Queues one application message (channel 1..255) to a single peer.
	 * A message to the listen server's local peer dispatches directly to the handler
	 * without the device. Returns `Unavailable` for a standalone host, `Invalid` for
	 * channel 0 or an unresolved peer, or the framing/queue result otherwise.
	 */
	ETransportResult SendTo(const FPeerId InPeer, const std::uint8_t InChannel, Core::TSpan<const std::uint8_t> InPayload) noexcept
	{
		if (Mode == ENetworkMode::Standalone)
		{
			return ETransportResult::Unavailable;
		}
		if (InChannel == ControlChannel)
		{
			return ETransportResult::Invalid;
		}
		if (InPeer.Index == LocalPeerIndex)
		{
			return SendToLocalPeer(InChannel, InPayload);
		}
		const FTransportPeerSlot* const Slot = ResolvePeer(InPeer);
		if (Slot == nullptr)
		{
			return ETransportResult::Invalid;
		}
		return QueueAppMessage(Slot->Address, InChannel, InPayload);
	}

	/**
	 * Queues one application message (channel 1..255) to every active peer.
	 * A listen server also dispatches to its local peer directly. Best-effort: returns
	 * `Success` when every active peer queued, otherwise the first failure result.
	 */
	ETransportResult Broadcast(const std::uint8_t InChannel, Core::TSpan<const std::uint8_t> InPayload) noexcept
	{
		if (Mode == ENetworkMode::Standalone)
		{
			return ETransportResult::Unavailable;
		}
		if (InChannel == ControlChannel)
		{
			return ETransportResult::Invalid;
		}
		if (Mode == ENetworkMode::ListenServer)
		{
			DispatchToHandler(GetLocalPeer(), InChannel, InPayload);
		}
		ETransportResult Outcome = ETransportResult::Success;
		for (std::size_t Index = 0; Index < MaxPeers; ++Index)
		{
			if (!Peers[Index].bActive)
			{
				continue;
			}
			const ETransportResult SlotResult = QueueAppMessage(Peers[Index].Address, InChannel, InPayload);
			if (SlotResult != ETransportResult::Success && Outcome == ETransportResult::Success)
			{
				Outcome = SlotResult;
			}
		}
		return Outcome;
	}

	/** Registers one message handler; forwards the multicast delegate's own result. */
	Core::EDelegateResult AddMessageHandler(FMessageHandlerBinding&& InBinding, Core::FDelegateHandle& OutHandle) noexcept
	{
		return MessageHandler.Add(std::move(InBinding), OutHandle);
	}

	/** Removes a previously registered message handler by its generation-checked handle. */
	Core::EDelegateResult RemoveMessageHandler(const Core::FDelegateHandle InHandle) noexcept { return MessageHandler.Remove(InHandle); }

	/** Reports the observable session state. */
	ETransportHostState GetState() const noexcept { return State; }

	/** Reports the configured role. */
	ENetworkMode GetMode() const noexcept { return Mode; }

	/** Reports the listen server's local-peer identity; only meaningful in `ListenServer` mode. */
	constexpr FPeerId GetLocalPeer() const noexcept { return FPeerId{LocalPeerIndex, LocalPeerGeneration}; }

	/** Reports a connected client's server-peer identity, or an invalid id when not connected. */
	FPeerId GetServerPeer() const noexcept
	{
		if (Mode != ENetworkMode::Client || State != ETransportHostState::Connected)
		{
			return FPeerId{};
		}
		return FPeerId{ServerPeerSlotIndex, Peers[ServerPeerSlotIndex].Generation};
	}

	/** Reports the identity a `Welcome` assigned to this client within the server's table. */
	FPeerId GetAssignedPeer() const noexcept { return AssignedPeer; }

	/** Reports how many remote peer slots are currently active. */
	std::size_t ActivePeerCount() const noexcept
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

private:
	/** One remote peer's address, liveness timestamps, and reuse generation. */
	struct FTransportPeerSlot
	{
		/** Transport address to reach this peer; empty while the slot is free. */
		::MicroWorld::Transport::Address::FDeviceAddress Address{};

		/** Time of the last packet received from this peer; drives timeout eviction. */
		Core::TimePointMilliseconds LastReceiveMilliseconds{0};

		/** Time of the last packet sent to this peer; paces outgoing heartbeats. */
		Core::TimePointMilliseconds LastSendMilliseconds{0};

		/** Reuse counter bumped on eviction so a stale `FPeerId` cannot match a later occupant. */
		std::uint8_t Generation{0};

		/** Distinguishes a live peer from a free, reusable slot. */
		bool bActive{false};
	};

	/** Returns monotonic elapsed milliseconds, clamped so a backward clock reads as zero elapsed. */
	static constexpr Core::DurationMilliseconds ElapsedSince(
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

	/** Reports whether an active peer has been silent past the configured eviction window. */
	bool IsPeerTimedOut(const FTransportPeerSlot& InSlot, const Core::TimePointMilliseconds InNowMilliseconds) const noexcept
	{
		return ElapsedSince(InNowMilliseconds, InSlot.LastReceiveMilliseconds) > Config.PeerTimeoutMilliseconds;
	}

	/** Reports whether a peer's last send is older than the heartbeat cadence. */
	bool IsHeartbeatDue(const FTransportPeerSlot& InSlot, const Core::TimePointMilliseconds InNowMilliseconds) const noexcept
	{
		return ElapsedSince(InNowMilliseconds, InSlot.LastSendMilliseconds) >= Config.HeartbeatIntervalMilliseconds;
	}

	/** Reports whether this host admits remote peers. */
	constexpr bool IsServer() const noexcept { return Mode == ENetworkMode::ListenServer || Mode == ENetworkMode::DedicatedServer; }

	/** Finds the active peer at `InAddress`, or `MaxPeers` when none matches. */
	std::size_t FindActivePeerIndexByAddress(const ::MicroWorld::Transport::Address::FDeviceAddress& InAddress) const noexcept
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

	/** Finds the lowest free peer slot, or `MaxPeers` when the table is full. */
	std::size_t FindFreePeerSlot() const noexcept
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

	/** Builds the current generation-checked identity of the peer at `InIndex`. */
	FPeerId MakePeerId(const std::size_t InIndex) const noexcept { return FPeerId{static_cast<std::uint8_t>(InIndex), Peers[InIndex].Generation}; }

	/** Resolves a remote peer id to its live slot, or `nullptr` when unknown or stale. */
	const FTransportPeerSlot* ResolvePeer(const FPeerId InPeer) const noexcept
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

	/** Frees the slot at `InIndex` and bumps its generation so outstanding ids go stale. */
	void EvictPeer(const std::size_t InIndex) noexcept
	{
		FTransportPeerSlot& Slot = Peers[InIndex];
		Slot.bActive = false;
		// Generation is u8, so it wraps after 256 evictions of this slot; a stale
		// id from exactly 256 evictions ago would re-match -- an accepted,
		// practically-unreachable window.
		Slot.Generation = static_cast<std::uint8_t>(Slot.Generation + 1);
		Slot.Address = ::MicroWorld::Transport::Address::FDeviceAddress{};
	}

	/** Returns a disconnected client to `Connecting` so it re-greets the server on the next send. */
	void OnPeerLost(const std::size_t InIndex) noexcept
	{
		if (Mode == ENetworkMode::Client && InIndex == ServerPeerSlotIndex)
		{
			State = ETransportHostState::Connecting;
			bHelloDue = true;
		}
	}

	/** Evicts every active peer whose last receive is older than the timeout window. */
	void EvictTimedOutPeers(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
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

	/** Parses one inbound packet, routing control internally and application messages to the handler. */
	void HandleInboundPacket(
		const ::MicroWorld::Transport::Address::FDeviceAddress& InFrom,
		Core::TSpan<const std::uint8_t> InPacket,
		const Core::TimePointMilliseconds InNowMilliseconds) noexcept
	{
		FMessageHeader Header{};
		Core::TSpan<const std::uint8_t> Payload{};
		if (ReadMessage(InPacket, Header, Payload) != ETransportResult::Success)
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

	/** Decodes one channel-0 control payload and dispatches it by type. */
	void HandleControlMessage(
		const ::MicroWorld::Transport::Address::FDeviceAddress& InFrom,
		Core::TSpan<const std::uint8_t> InPayload,
		const Core::TimePointMilliseconds InNowMilliseconds) noexcept
	{
		FControlMessage Control{};
		if (ReadControlMessage(InPayload, Control) != ETransportResult::Success)
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

	/** Admits a client on `Hello` (idempotent per address), or ignores it on wrong version or a full table. */
	void HandleHello(
		const ::MicroWorld::Transport::Address::FDeviceAddress& InFrom,
		const FControlMessage& InControl,
		const Core::TimePointMilliseconds InNowMilliseconds) noexcept
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

	/** Records the server as a connected client's single peer and enters `Connected`. */
	void HandleWelcome(
		const ::MicroWorld::Transport::Address::FDeviceAddress& InFrom,
		const FControlMessage& InControl,
		const Core::TimePointMilliseconds InNowMilliseconds) noexcept
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

	/** Refreshes a known peer's liveness on `Heartbeat`; ignores heartbeats from strangers. */
	void HandleHeartbeat(const ::MicroWorld::Transport::Address::FDeviceAddress& InFrom, const Core::TimePointMilliseconds InNowMilliseconds) noexcept
	{
		const std::size_t Index = FindActivePeerIndexByAddress(InFrom);
		if (Index == MaxPeers)
		{
			MW_LOG_MSG(Log, "TransportHost", "ignored heartbeat from unknown peer");
			return;
		}
		Peers[Index].LastReceiveMilliseconds = InNowMilliseconds;
	}

	/** Evicts a peer on `Bye`; a client whose server departs returns to `Connecting`. */
	void HandleBye(const ::MicroWorld::Transport::Address::FDeviceAddress& InFrom) noexcept
	{
		const std::size_t Index = FindActivePeerIndexByAddress(InFrom);
		if (Index == MaxPeers)
		{
			return;
		}
		EvictPeer(Index);
		OnPeerLost(Index);
	}

	/** Queues a `Welcome` carrying the assigned index and generation to a newly admitted client. */
	void SendWelcome(const std::size_t InPeerIndex, const ::MicroWorld::Transport::Address::FDeviceAddress& InTo) noexcept
	{
		FControlMessage Welcome{};
		Welcome.Type = EControlMessageType::Welcome;
		Welcome.ProtocolVersion = Config.ProtocolVersion;
		Welcome.PeerIndex = static_cast<std::uint8_t>(InPeerIndex);
		Welcome.PeerGeneration = Peers[InPeerIndex].Generation;
		if (QueueControl(InTo, Welcome) != ETransportResult::Success)
		{
			MW_LOG_MSG(Warning, "TransportHost", "Welcome not queued: outbound queue full");
		}
	}

	/** Queues a client `Hello` to the configured server address. */
	void QueueHello() noexcept
	{
		FControlMessage Hello{};
		Hello.Type = EControlMessageType::Hello;
		Hello.ProtocolVersion = Config.ProtocolVersion;
		if (QueueControl(Config.ServerAddress, Hello) != ETransportResult::Success)
		{
			MW_LOG_MSG(Warning, "TransportHost", "Hello not queued: outbound queue full");
		}
	}

	/** Frames one control message and queues it to `InTo`; returns the framing or queue result. */
	ETransportResult QueueControl(const ::MicroWorld::Transport::Address::FDeviceAddress& InTo, const FControlMessage& InControl) noexcept
	{
		std::array<std::uint8_t, MessageHeaderBytes + MaxControlPayloadBytes> FrameBuffer{};
		FByteWriter Writer(Core::TSpan<std::uint8_t>(FrameBuffer.data(), FrameBuffer.size()));
		const ETransportResult WriteResult = WriteControlMessage(Writer, InControl);
		if (WriteResult != ETransportResult::Success)
		{
			return WriteResult;
		}
		return OutboundManager.QueueSend(InTo, Writer.WrittenBytes());
	}

	/** Frames one application message and queues it to `InTo`; returns the framing or queue result. */
	ETransportResult QueueAppMessage(
		const ::MicroWorld::Transport::Address::FDeviceAddress& InTo,
		const std::uint8_t InChannel,
		Core::TSpan<const std::uint8_t> InPayload) noexcept
	{
		std::array<std::uint8_t, MaxPacketBytes> FrameBuffer{};
		FByteWriter Writer(Core::TSpan<std::uint8_t>(FrameBuffer.data(), FrameBuffer.size()));
		const ETransportResult WriteResult = WriteMessage(Writer, InChannel, InPayload);
		if (WriteResult != ETransportResult::Success)
		{
			return WriteResult;
		}
		return OutboundManager.QueueSend(InTo, Writer.WrittenBytes());
	}

	/** Delivers one application message to every registered handler; a dispatch failure is best-effort ignored. */
	void DispatchToHandler(const FPeerId InFrom, const std::uint8_t InChannel, Core::TSpan<const std::uint8_t> InPayload) noexcept
	{
		(void)MessageHandler.Broadcast(InFrom, InChannel, InPayload);
	}

	/** Sends outbound FIFO entries until it empties or the device stops accepting; bounded by depth. */
	void DrainOutbound() noexcept
	{
		for (std::size_t Count = 0; Count < SendQueueDepth; ++Count)
		{
			if (OutboundManager.AdvanceSend() != ETransportResult::Success)
			{
				// Unavailable means the FIFO is empty; a device failure retains the head for a later drain.
				break;
			}
		}
	}

	/** Receives up to `MaxPeers + PumpSlackPackets` packets this pump, routing each to `HandleInboundPacket`. */
	void DrainInboundPackets(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
	{
		std::array<std::uint8_t, MaxPacketBytes> ReceiveBuffer{};
		const std::size_t MaxReceives = MaxPeers + PumpSlackPackets;
		for (std::size_t Count = 0; Count < MaxReceives; ++Count)
		{
			::MicroWorld::Transport::Address::FDeviceAddress From{};
			::MicroWorld::Transport::Device::FReceiveResult Result{};
			const ETransportResult ReceiveResult =
				OutboundManager.Receive(From, Core::TSpan<std::uint8_t>(ReceiveBuffer.data(), ReceiveBuffer.size()), Result);
			if (ReceiveResult != ETransportResult::Success)
			{
				// Unavailable means the transport is drained; any other failure cannot make progress now.
				break;
			}
			HandleInboundPacket(From, Core::TSpan<const std::uint8_t>(ReceiveBuffer.data(), Result.BytesReceived), InNowMilliseconds);
		}
	}

	/** Client-only: greets the server on the first connecting pump and on each heartbeat interval afterward. */
	void SendClientHelloIfDue(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
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

	/** Queues a heartbeat to every active peer whose last send is older than the heartbeat interval. */
	void SendDueHeartbeats(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
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

	/** Best-effort `Bye` to every active peer, then drains the outbound FIFO without waiting for physical transmission. */
	void SendByeToAllActivePeers() noexcept
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

	/** Evicts every active peer so each outstanding `FPeerId` goes stale. */
	void EvictAllPeers() noexcept
	{
		for (std::size_t Index = 0; Index < MaxPeers; ++Index)
		{
			if (Peers[Index].bActive)
			{
				EvictPeer(Index);
			}
		}
	}

	/** Dispatches an application message directly to the listen server's local peer; `Invalid` in any other mode. */
	ETransportResult SendToLocalPeer(const std::uint8_t InChannel, Core::TSpan<const std::uint8_t> InPayload) noexcept
	{
		if (Mode != ENetworkMode::ListenServer)
		{
			return ETransportResult::Invalid;
		}
		DispatchToHandler(GetLocalPeer(), InChannel, InPayload);
		return ETransportResult::Success;
	}

	/** Finds this address's peer or allocates a free slot, refreshing liveness; returns `MaxPeers` when the table is full. */
	std::size_t AdmitPeer(
		const ::MicroWorld::Transport::Address::FDeviceAddress& InFrom, const Core::TimePointMilliseconds InNowMilliseconds) noexcept
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

	/** Device borrowed for one host lifetime; progresses pending physical transmission after each outbound pump. */
	::MicroWorld::Transport::Device::IDevice& Device;

	/** Owns the outbound packet bytes, lengths, and destinations for the FIFO. */
	TTransportPacketStorage<SendQueueDepth, MaxPacketBytes> OutboundStorage{};

	/** Owns the outbound FIFO over the device; reused rather than re-implementing queue mechanics. */
	TTransportManager<SendQueueDepth, MaxPacketBytes> OutboundManager;

	/** Dispatches application messages to every registered handler. */
	FMessageHandler MessageHandler{};

	/** Fixed table of remote peer slots. */
	std::array<FTransportPeerSlot, MaxPeers> Peers{};

	/** Session timing and identity set by `Configure`. */
	FTransportHostConfig Config{};

	/** Configured role. */
	ENetworkMode Mode{ENetworkMode::Standalone};

	/** Observable session state. */
	ETransportHostState State{ETransportHostState::Idle};

	/** Identity a `Welcome` assigned to this client within the server's table. */
	FPeerId AssignedPeer{};

	/** Time the client last sent `Hello`; paces connecting retries. */
	Core::TimePointMilliseconds LastHelloSendMilliseconds{0};

	/** Forces the next connecting `PumpSend` to greet immediately after start or reconnect. */
	bool bHelloDue{false};
};

} // namespace MicroWorld::Transport
