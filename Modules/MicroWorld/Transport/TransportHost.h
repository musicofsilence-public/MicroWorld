#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Delegates/MulticastDelegate.h>
#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/ReceiveResult.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Transport/NetworkMode.h>
#include <MicroWorld/Transport/PeerId.h>
#include <MicroWorld/Transport/TransportHostConfig.h>
#include <MicroWorld/Transport/TransportHostState.h>
#include <MicroWorld/Transport/ByteWriter.h>
#include <MicroWorld/Transport/TransportManager.h>
#include <MicroWorld/Transport/TransportPacketStorage.h>
#include <MicroWorld/Transport/EControlMessageType.h>
#include <MicroWorld/Transport/FControlMessage.h>
#include <MicroWorld/Transport/FMessageHeader.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace MicroWorld::Transport
{

/**
 * Motivation: Delivers the UE5 dedicated/listen/client roles over one bounded session host so an application runs a networked
 *   session without its own protocol code.
 * Responsibilities: Own a fixed peer table, an outbound FIFO, and one message handler; drive the protocol only through explicit
 *   PumpReceive/PumpSend ticks so the host samples no clock and allocates nothing; handle channel 0 internally (admission,
 *   heartbeats, timeout eviction); and dispatch channels 1..255 to the registered handler.
 * Example:
 *   TTransportHost<4, 64> Host(Device);
 *   Host.Configure(ENetworkMode::ListenServer, Config);
 *   Host.Start(Now);
 *   Host.PumpReceive(Now);
 *   Host.PumpSend(Now);
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
	/** Motivation: Counts the packets one full peer broadcast occupies in the outbound FIFO. */
	static constexpr std::size_t BroadcastPacketsPerPeer = 1;

	/** Motivation: Reserves per-peer packets for heartbeats and replies between pumps. */
	static constexpr std::size_t HeartbeatPacketsPerPeer = 1;

	/** Motivation: Adds outbound/inbound packet headroom so a burst of control traffic between pumps is not dropped. */
	static constexpr std::size_t PumpSlackPackets = 4;

	/** Motivation: Fixes the outbound FIFO depth as one full broadcast plus pending heartbeats plus slack. */
	static constexpr std::size_t SendQueueDepth = (BroadcastPacketsPerPeer + HeartbeatPacketsPerPeer) * MaxPeers + PumpSlackPackets;

	/** Motivation: Bounds the message-handler bindings to a small fixed count since one dispatcher usually suffices. */
	static constexpr std::size_t MaxMessageHandlers = 4;

	/** Motivation: Reserves inline bytes per handler callable, sized for a small capture. */
	static constexpr std::size_t MessageHandlerInlineBytes = 32;

	/** Motivation: Fixes the largest encoded message one packet can carry (packet budget minus the four-byte header). */
	static constexpr std::size_t MaxMessageBytes = MaxPacketBytes - MessageHeaderBytes;

	/** Motivation: Reserves the peer index that routes a message to the listen server's local peer. */
	static constexpr std::uint8_t LocalPeerIndex = 0xFE;

	/** Motivation: Pins the local peer's generation, which never advances since the local peer is never evicted. */
	static constexpr std::uint8_t LocalPeerGeneration = 1;

	/** Motivation: Names the slot a client keeps for its single server peer; meaningful only in Client mode. */
	static constexpr std::uint8_t ServerPeerSlotIndex = 0;

	/** Motivation: Names the multicast dispatcher type for application (channel >= 1) messages. */
	using FMessageHandler =
		Core::TMulticastDelegate<void(FPeerId, std::uint8_t, Core::TSpan<const std::uint8_t>), MaxMessageHandlers, MessageHandlerInlineBytes>;

	/** Motivation: Names the bindable handler callable type matching FMessageHandler's signature. */
	using FMessageHandlerBinding = Core::TDelegate<void(FPeerId, std::uint8_t, Core::TSpan<const std::uint8_t>), MessageHandlerInlineBytes>;

	/**
	 * Motivation: Binds the host to one externally owned device at construction, with mode and config following via Configure.
	 * Responsibilities: Store the device reference and construct the outbound manager over the device and its storage.
	 */
	explicit TTransportHost(Core::ITransportDevice& InDevice) noexcept : Device(InDevice), OutboundManager(InDevice, OutboundStorage) {}

	/**
	 * Motivation: Prevents copying so one host value binds one device, table, and handler.
	 * Responsibilities: Reject copy construction so the host stays a single owner.
	 */
	TTransportHost(const TTransportHost&) = delete;

	/**
	 * Motivation: Prevents copying so one host value binds one device, table, and handler.
	 * Responsibilities: Reject copy assignment so the host stays a single owner.
	 */
	TTransportHost& operator=(const TTransportHost&) = delete;

	/**
	 * Motivation: Prevents moving so the owned manager's device reference and handler slots stay fixed.
	 * Responsibilities: Reject move construction so embedded references and slots do not relocate.
	 */
	TTransportHost(TTransportHost&&) = delete;

	/**
	 * Motivation: Prevents moving so the owned manager's device reference and handler slots stay fixed.
	 * Responsibilities: Reject move assignment so embedded references and slots do not relocate.
	 */
	TTransportHost& operator=(TTransportHost&&) = delete;

	/**
	 * Motivation: Keeps the host side-effect free on destruction since it holds only fixed inline storage.
	 * Responsibilities: Default the destructor because the host owns no external resource.
	 */
	~TTransportHost() noexcept = default;

	/**
	 * Motivation: Sets the role and session parameters before the host starts so a running session cannot be silently reconfigured.
	 * Responsibilities: Return Invalid without changing anything when the host is not Idle; otherwise store the mode and config.
	 */
	Core::ETransportResult Configure(const ENetworkMode InMode, const FTransportHostConfig& InConfig) noexcept
	{
		if (State != ETransportHostState::Idle)
		{
			return Core::ETransportResult::Invalid;
		}
		Mode = InMode;
		Config = InConfig;
		return Core::ETransportResult::Success;
	}

	/**
	 * Motivation: Begins the session in the configured role so traffic starts only after an explicit call.
	 * Responsibilities: Move a client to Connecting (greeting on the next PumpSend), a server to Listening, leave standalone Idle,
	 *   and return Invalid when already started.
	 */
	Core::ETransportResult Start(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
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

	/**
	 * Motivation: Ends the session cleanly so outstanding peer ids go stale and the host returns to Idle.
	 * Responsibilities: Best-effort queue a Bye to every active peer, evict all peers (bumping each generation), and return to
	 *   Idle without waiting for a physical transport drain.
	 */
	void Stop() noexcept
	{
		SendByeToAllActivePeers();
		EvictAllPeers();
		State = ETransportHostState::Idle;
		bHelloDue = false;
	}

	/**
	 * Motivation: Drains inbound traffic and reaps dead peers in one bounded tick so the host needs no background thread.
	 * Responsibilities: Receive at most MaxPeers + 4 packets (handling control internally and dispatching application messages),
	 *   evict peers past the timeout window, and return immediately for a standalone host.
	 */
	Core::ETransportResult PumpReceive(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
	{
		if (Mode == ENetworkMode::Standalone)
		{
			return Core::ETransportResult::Success;
		}
		DrainInboundPackets(InNowMilliseconds);
		EvictTimedOutPeers(InNowMilliseconds);
		return Core::ETransportResult::Success;
	}

	/**
	 * Motivation: Emits due heartbeats and drains the outbound FIFO in one bounded tick so the host needs no background thread.
	 * Responsibilities: Send client Hello retries and due heartbeats, drain the outbound FIFO, run the device's pre-advance
	 *   progress, and return immediately for a standalone host.
	 */
	Core::ETransportResult PumpSend(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
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

	/**
	 * Motivation: Sends one application message to a single peer, routing the listen server's local peer directly without the device.
	 * Responsibilities: Return Unavailable for a standalone host, Invalid for channel 0 or an unresolved peer, and otherwise the
	 *   framing or queue result.
	 */
	Core::ETransportResult SendTo(const FPeerId InPeer, const std::uint8_t InChannel, Core::TSpan<const std::uint8_t> InPayload) noexcept
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

	/**
	 * Motivation: Sends one application message to every active peer so a host can fan out without per-peer calls.
	 * Responsibilities: Best-effort queue to each active peer (a listen server also dispatches to its local peer directly),
	 *   return Success when every active peer queued, and otherwise the first failure result.
	 */
	Core::ETransportResult Broadcast(const std::uint8_t InChannel, Core::TSpan<const std::uint8_t> InPayload) noexcept
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

	/**
	 * Motivation: Lets an application register one handler for application messages.
	 * Responsibilities: Forward the binding to the multicast delegate and return its result.
	 */
	Core::EDelegateResult AddMessageHandler(FMessageHandlerBinding&& InBinding, Core::FDelegateHandle& OutHandle) noexcept
	{
		return MessageHandler.Add(std::move(InBinding), OutHandle);
	}

	/**
	 * Motivation: Lets an application remove a previously registered handler.
	 * Responsibilities: Forward the generation-checked handle to the multicast delegate and return its result.
	 */
	Core::EDelegateResult RemoveMessageHandler(const Core::FDelegateHandle InHandle) noexcept { return MessageHandler.Remove(InHandle); }

	/**
	 * Motivation: Lets a caller branch on the observable session state.
	 * Responsibilities: Report the stored state.
	 */
	ETransportHostState GetState() const noexcept { return State; }

	/**
	 * Motivation: Lets a caller branch on the configured role.
	 * Responsibilities: Report the stored mode.
	 */
	ENetworkMode GetMode() const noexcept { return Mode; }

	/**
	 * Motivation: Gives a caller the listen server's local-peer identity for direct dispatch.
	 * Responsibilities: Report the local peer identity, meaningful only in ListenServer mode.
	 */
	constexpr FPeerId GetLocalPeer() const noexcept { return FPeerId{LocalPeerIndex, LocalPeerGeneration}; }

	/**
	 * Motivation: Lets a connected client address its server peer.
	 * Responsibilities: Report the server-peer identity when connected, or an invalid id otherwise.
	 */
	FPeerId GetServerPeer() const noexcept
	{
		if (Mode != ENetworkMode::Client || State != ETransportHostState::Connected)
		{
			return FPeerId{};
		}
		return FPeerId{ServerPeerSlotIndex, Peers[ServerPeerSlotIndex].Generation};
	}

	/**
	 * Motivation: Lets a client learn the identity a Welcome assigned it within the server's table.
	 * Responsibilities: Report the assigned peer identity.
	 */
	FPeerId GetAssignedPeer() const noexcept { return AssignedPeer; }

	/**
	 * Motivation: Lets a caller observe how many remote peers are active.
	 * Responsibilities: Count and report active peer slots.
	 */
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
	/**
	 * Motivation: Holds one remote peer's address, liveness timestamps, and reuse generation so the table can evict and reuse slots.
	 * Responsibilities: Carry the peer address, last receive and send times, generation, and active flag.
	 * Example:
	 *   // Internal slot type owned by the host's peer table.
	 */
	struct FTransportPeerSlot
	{
		/** Motivation: Holds the transport address to reach this peer; empty while the slot is free. */
		Core::FDeviceAddress Address{};

		/** Motivation: Records the last packet received from this peer; drives timeout eviction. */
		Core::TimePointMilliseconds LastReceiveMilliseconds{0};

		/** Motivation: Records the last packet sent to this peer; paces outgoing heartbeats. */
		Core::TimePointMilliseconds LastSendMilliseconds{0};

		/** Motivation: Bumps the reuse counter on eviction so a stale FPeerId cannot match a later occupant. */
		std::uint8_t Generation{0};

		/** Motivation: Distinguishes a live peer from a free, reusable slot. */
		bool bActive{false};
	};

	/**
	 * Motivation: Computes elapsed time without overflowing when a wide gap narrows into DurationMilliseconds.
	 * Responsibilities: Return monotonic elapsed milliseconds, clamped so a backward clock reads as zero elapsed and a gap wider
	 *   than DurationMilliseconds max saturates at that ceiling.
	 */
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

	/**
	 * Motivation: Decides whether a peer should be evicted for silence.
	 * Responsibilities: Report whether an active peer's last receive is older than the configured eviction window.
	 */
	bool IsPeerTimedOut(const FTransportPeerSlot& InSlot, const Core::TimePointMilliseconds InNowMilliseconds) const noexcept
	{
		return ElapsedSince(InNowMilliseconds, InSlot.LastReceiveMilliseconds) > Config.PeerTimeoutMilliseconds;
	}

	/**
	 * Motivation: Decides whether a peer is due a heartbeat.
	 * Responsibilities: Report whether a peer's last send is older than the heartbeat cadence.
	 */
	bool IsHeartbeatDue(const FTransportPeerSlot& InSlot, const Core::TimePointMilliseconds InNowMilliseconds) const noexcept
	{
		return ElapsedSince(InNowMilliseconds, InSlot.LastSendMilliseconds) >= Config.HeartbeatIntervalMilliseconds;
	}

	/**
	 * Motivation: Lets internal callers branch on whether this host admits remote peers.
	 * Responsibilities: Report true for the ListenServer and DedicatedServer roles.
	 */
	constexpr bool IsServer() const noexcept { return Mode == ENetworkMode::ListenServer || Mode == ENetworkMode::DedicatedServer; }

	/**
	 * Motivation: Locates an active peer by address for admission and dispatch.
	 * Responsibilities: Return the active peer index at the address, or MaxPeers when none matches.
	 */
	std::size_t FindActivePeerIndexByAddress(const Core::FDeviceAddress& InAddress) const noexcept
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

	/**
	 * Motivation: Locates a reusable slot for a new peer.
	 * Responsibilities: Return the lowest free peer slot, or MaxPeers when the table is full.
	 */
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

	/**
	 * Motivation: Builds the generation-checked identity a caller holds for a slot.
	 * Responsibilities: Pair the index with its current generation into an FPeerId.
	 */
	FPeerId MakePeerId(const std::size_t InIndex) const noexcept { return FPeerId{static_cast<std::uint8_t>(InIndex), Peers[InIndex].Generation}; }

	/**
	 * Motivation: Validates a caller-supplied peer id against live slot state before use.
	 * Responsibilities: Return the matching live slot, or nullptr for an unknown or stale id.
	 */
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

	/**
	 * Motivation: Frees a slot and invalidates outstanding ids in one step.
	 * Responsibilities: Mark the slot inactive, bump its generation, and clear its address.
	 */
	void EvictPeer(const std::size_t InIndex) noexcept
	{
		FTransportPeerSlot& Slot = Peers[InIndex];
		Slot.bActive = false;
		// Generation is u8, so it wraps after 256 evictions of this slot; a stale
		// id from exactly 256 evictions ago would re-match -- an accepted,
		// practically-unreachable window.
		Slot.Generation = static_cast<std::uint8_t>(Slot.Generation + 1);
		Slot.Address = Core::FDeviceAddress{};
	}

	/**
	 * Motivation: Restores a client to the connecting state when its server peer is lost.
	 * Responsibilities: Move a client whose server slot was lost back to Connecting and force the next greet.
	 */
	void OnPeerLost(const std::size_t InIndex) noexcept
	{
		if (Mode == ENetworkMode::Client && InIndex == ServerPeerSlotIndex)
		{
			State = ETransportHostState::Connecting;
			bHelloDue = true;
		}
	}

	/**
	 * Motivation: Reaps dead peers each receive pump so liveness stays bounded to the timeout window.
	 * Responsibilities: Evict every active peer whose last receive is older than the timeout window and notify loss.
	 */
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

	/**
	 * Motivation: Routes one inbound packet by channel, keeping control internal and dispatching application messages.
	 * Responsibilities: Drop a malformed packet or an application message from an unknown peer, refresh the sender's liveness, and
	 *   dispatch recognized application messages to the handler.
	 */
	void HandleInboundPacket(
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

	/**
	 * Motivation: Decodes a control payload and routes it to its handler by type.
	 * Responsibilities: Drop an unknown or malformed control message and dispatch Hello, Welcome, Heartbeat, and Bye.
	 */
	void HandleControlMessage(
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

	/**
	 * Motivation: Admits a client that greets the server.
	 * Responsibilities: Act only as a server, ignore a wrong-version Hello, admit or refresh the peer idempotently per address,
	 *   and reply with Welcome unless the table is full.
	 */
	void HandleHello(
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

	/**
	 * Motivation: Completes a client's admission on receiving its assigned identity.
	 * Responsibilities: Act only as a client, ignore a wrong-version Welcome, record the server as the single peer, store the
	 *   assigned identity, and enter Connected.
	 */
	void HandleWelcome(
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

	/**
	 * Motivation: Keeps a known peer alive on Heartbeat.
	 * Responsibilities: Refresh a known peer's liveness and ignore heartbeats from strangers.
	 */
	void HandleHeartbeat(const Core::FDeviceAddress& InFrom, const Core::TimePointMilliseconds InNowMilliseconds) noexcept
	{
		const std::size_t Index = FindActivePeerIndexByAddress(InFrom);
		if (Index == MaxPeers)
		{
			MW_LOG_MSG(Log, "TransportHost", "ignored heartbeat from unknown peer");
			return;
		}
		Peers[Index].LastReceiveMilliseconds = InNowMilliseconds;
	}

	/**
	 * Motivation: Removes a peer on Bye and returns a client to connecting if its server departed.
	 * Responsibilities: Evict the matching peer and notify loss.
	 */
	void HandleBye(const Core::FDeviceAddress& InFrom) noexcept
	{
		const std::size_t Index = FindActivePeerIndexByAddress(InFrom);
		if (Index == MaxPeers)
		{
			return;
		}
		EvictPeer(Index);
		OnPeerLost(Index);
	}

	/**
	 * Motivation: Replies to an admitted client with the identity it must use.
	 * Responsibilities: Queue a Welcome carrying the assigned index and generation, logging if the queue is full.
	 */
	void SendWelcome(const std::size_t InPeerIndex, const Core::FDeviceAddress& InTo) noexcept
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

	/**
	 * Motivation: Greets the configured server during connection.
	 * Responsibilities: Queue a Hello to the configured server address, logging if the queue is full.
	 */
	void QueueHello() noexcept
	{
		FControlMessage Hello{};
		Hello.Type = EControlMessageType::Hello;
		Hello.ProtocolVersion = Config.ProtocolVersion;
		if (QueueControl(Config.ServerAddress, Hello) != Core::ETransportResult::Success)
		{
			MW_LOG_MSG(Warning, "TransportHost", "Hello not queued: outbound queue full");
		}
	}

	/**
	 * Motivation: Frames and queues a control message in one step so callers avoid the writer directly.
	 * Responsibilities: Encode the control message into a fixed buffer and queue it to InTo; return the framing or queue result.
	 */
	Core::ETransportResult QueueControl(const Core::FDeviceAddress& InTo, const FControlMessage& InControl) noexcept
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

	/**
	 * Motivation: Frames and queues an application message in one step so callers avoid the writer directly.
	 * Responsibilities: Encode the message into a fixed buffer and queue it to InTo; return the framing or queue result.
	 */
	Core::ETransportResult QueueAppMessage(
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

	/**
	 * Motivation: Delivers one application message to every registered handler.
	 * Responsibilities: Broadcast to the multicast delegate and best-effort ignore a dispatch failure.
	 */
	void DispatchToHandler(const FPeerId InFrom, const std::uint8_t InChannel, Core::TSpan<const std::uint8_t> InPayload) noexcept
	{
		(void)MessageHandler.Broadcast(InFrom, InChannel, InPayload);
	}

	/**
	 * Motivation: Drains queued packets in one bounded pass so a pump cannot loop forever.
	 * Responsibilities: Send FIFO entries until it empties or the device stops accepting, bounded by SendQueueDepth.
	 */
	void DrainOutbound() noexcept
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

	/**
	 * Motivation: Bounds inbound work per receive pump so a flood cannot monopolize one tick.
	 * Responsibilities: Receive up to MaxPeers + PumpSlackPackets packets this pump, routing each to HandleInboundPacket.
	 */
	void DrainInboundPackets(const Core::TimePointMilliseconds InNowMilliseconds) noexcept
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

	/**
	 * Motivation: Paces client Hello retries so a connecting client re-greets on a fixed cadence.
	 * Responsibilities: Greet on the first connecting pump and on each heartbeat interval afterward.
	 */
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

	/**
	 * Motivation: Keeps every active peer alive on a fixed cadence.
	 * Responsibilities: Queue a heartbeat to every active peer whose last send is older than the heartbeat interval.
	 */
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

	/**
	 * Motivation: Notifies peers of shutdown so they can evict promptly.
	 * Responsibilities: Best-effort queue a Bye to every active peer, then drain the outbound FIFO without waiting for physical
	 *   transmission.
	 */
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

	/**
	 * Motivation: Tears down every peer so outstanding ids go stale on shutdown.
	 * Responsibilities: Evict every active peer, bumping each generation.
	 */
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

	/**
	 * Motivation: Short-circuits a send to the listen server's local peer so it skips the device entirely.
	 * Responsibilities: Dispatch directly to the handler in ListenServer mode and return Invalid in any other.
	 */
	Core::ETransportResult SendToLocalPeer(const std::uint8_t InChannel, Core::TSpan<const std::uint8_t> InPayload) noexcept
	{
		if (Mode != ENetworkMode::ListenServer)
		{
			return Core::ETransportResult::Invalid;
		}
		DispatchToHandler(GetLocalPeer(), InChannel, InPayload);
		return Core::ETransportResult::Success;
	}

	/**
	 * Motivation: Adds or refreshes a peer on Hello so admission is idempotent per address.
	 * Responsibilities: Find this address's peer or allocate a free slot, refresh liveness, and return MaxPeers when the table is full.
	 */
	std::size_t AdmitPeer(const Core::FDeviceAddress& InFrom, const Core::TimePointMilliseconds InNowMilliseconds) noexcept
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

	/** Motivation: Borrows the device for one host lifetime; pending physical transmission progresses after each outbound pump. */
	Core::ITransportDevice& Device;

	/** Motivation: Owns the outbound packet bytes, lengths, and destinations for the FIFO. */
	TTransportPacketStorage<SendQueueDepth, MaxPacketBytes> OutboundStorage{};

	/** Motivation: Owns the outbound FIFO over the device, reused rather than re-implementing queue mechanics. */
	TTransportManager<SendQueueDepth, MaxPacketBytes> OutboundManager;

	/** Motivation: Dispatches application messages to every registered handler. */
	FMessageHandler MessageHandler{};

	/** Motivation: Holds the fixed table of remote peer slots. */
	std::array<FTransportPeerSlot, MaxPeers> Peers{};

	/** Motivation: Carries the session timing and identity set by Configure. */
	FTransportHostConfig Config{};

	/** Motivation: Stores the configured role. */
	ENetworkMode Mode{ENetworkMode::Standalone};

	/** Motivation: Stores the observable session state. */
	ETransportHostState State{ETransportHostState::Idle};

	/** Motivation: Stores the identity a Welcome assigned to this client within the server's table. */
	FPeerId AssignedPeer{};

	/** Motivation: Records the time the client last sent Hello; paces connecting retries. */
	Core::TimePointMilliseconds LastHelloSendMilliseconds{0};

	/** Motivation: Forces the next connecting PumpSend to greet immediately after start or reconnect. */
	bool bHelloDue{false};
};

} // namespace MicroWorld::Transport
