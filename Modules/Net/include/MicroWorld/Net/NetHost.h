#pragma once

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Delegates/Delegate.h>
#include <MicroWorld/Log.h>
#include <MicroWorld/Net/ByteWriter.h>
#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetDriver.h>
#include <MicroWorld/Net/NetManager.h>
#include <MicroWorld/Net/NetPacketStorage.h>
#include <MicroWorld/Net/NetProtocol.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/Time.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace MicroWorld
{

/** The UE5-style role this host plays; selects which session traffic it originates and accepts. */
enum class ENetMode : std::uint8_t
{
	/** Runs no driver traffic; every send reports `Unavailable`. */
	Standalone,

	/** Holds exactly one peer (the server) and sends `Hello` until admitted. */
	Client,

	/** Admits remote peers and additionally owns a directly dispatched local peer. */
	ListenServer,

	/** Admits remote peers with no local peer of its own. */
	DedicatedServer,
};

/** Observable session state, primarily meaningful to a `Client` tracking admission. */
enum class ENetHostState : std::uint8_t
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

/** Session timing and identity supplied once before `Start`. */
struct FNetHostConfig
{
	/** Interval between outgoing heartbeats (and client `Hello` retries while connecting). */
	DurationMilliseconds HeartbeatIntervalMilliseconds{1000};

	/** Silence window after which a peer is evicted; must exceed the heartbeat interval. */
	DurationMilliseconds PeerTimeoutMilliseconds{5000};

	/** Address the client greets with `Hello`; ignored by every non-client mode. */
	FNetAddress ServerAddress{};

	/** Protocol version advertised in `Hello`/`Welcome`; a mismatch is ignored, not admitted. */
	std::uint8_t ProtocolVersion{1};
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
constexpr bool operator==(const FPeerId Left, const FPeerId Right) noexcept
{
	return Left.Index == Right.Index && Left.Generation == Right.Generation;
}

/** Negates `operator==` so callers can test peer inequality directly. */
constexpr bool operator!=(const FPeerId Left, const FPeerId Right) noexcept
{
	return !(Left == Right);
}

/**
 * Bounded session host over one `INetDriver`, delivering the UE5 dedicated/listen/client roles.
 *
 * Owns a fixed peer table, an outbound FIFO, and one message handler; drives the
 * protocol only through explicit `PumpReceive`/`PumpSend` ticks so it samples no
 * clock and allocates nothing. Channel 0 is handled internally (admission,
 * heartbeats, timeout eviction); channels 1..255 dispatch to the registered handler.
 */
template<std::size_t MaxPeers, std::size_t MaxPacketBytes>
class TNetHost final
{
	static_assert(MaxPeers > 0, "TNetHost requires at least one peer slot.");
	static_assert(MaxPeers < 0xFE, "TNetHost reserves peer indices 0xFE (local) and 0xFF (invalid).");
	static_assert(
		MaxPacketBytes >= MessageHeaderBytes + MaxControlPayloadBytes, "TNetHost packets must fit the largest control frame (header + Welcome).");

public:
	/** Outbound FIFO depth: one full broadcast plus pending heartbeats plus slack. */
	static constexpr std::size_t SendQueueDepth = 2 * MaxPeers + 4;

	/** Fixed number of message-handler bindings; small because one dispatcher usually suffices. */
	static constexpr std::size_t MaxMessageHandlers = 4;

	/** Inline bytes reserved per handler callable, sized for a small capture. */
	static constexpr std::size_t MessageHandlerInlineBytes = 32;

	/** Reserved peer index that routes a message to the listen server's local peer. */
	static constexpr std::uint8_t LocalPeerIndex = 0xFE;

	/** Multicast dispatcher type for application (channel >= 1) messages. */
	using FMessageHandler = TMulticastDelegate<void(FPeerId, std::uint8_t, TSpan<const std::uint8_t>), MaxMessageHandlers, MessageHandlerInlineBytes>;

	/** One bindable handler callable matching `FMessageHandler`'s signature. */
	using FMessageHandlerBinding = TDelegate<void(FPeerId, std::uint8_t, TSpan<const std::uint8_t>), MessageHandlerInlineBytes>;

	/** Binds the host to one externally owned driver; mode and config follow via `Configure`. */
	explicit TNetHost(INetDriver& Driver) noexcept : OutboundManager(Driver, OutboundStorage) {}

	/** Prevents copying so one host value binds one driver, table, and handler. */
	TNetHost(const TNetHost&) = delete;

	/** Prevents copying so one host value binds one driver, table, and handler. */
	TNetHost& operator=(const TNetHost&) = delete;

	/** Prevents moving so the owned manager's driver reference and handler slots stay fixed. */
	TNetHost(TNetHost&&) = delete;

	/** Prevents moving so the owned manager's driver reference and handler slots stay fixed. */
	TNetHost& operator=(TNetHost&&) = delete;

	/** Defaulted; the host holds only fixed inline storage and no external resource. */
	~TNetHost() noexcept = default;

	/**
	 * Sets the role and session parameters before the host starts.
	 * Returns `Invalid` without changing anything when the host is not `Idle`, so a
	 * running session cannot be silently reconfigured.
	 */
	ENetResult Configure(const ENetMode NewMode, const FNetHostConfig& NewConfig) noexcept
	{
		if (State != ENetHostState::Idle)
		{
			return ENetResult::Invalid;
		}
		Mode = NewMode;
		Config = NewConfig;
		return ENetResult::Success;
	}

	/**
	 * Begins the session: a client enters `Connecting` (and greets on the next `PumpSend`),
	 * a server enters `Listening`, standalone stays `Idle`. Returns `Invalid` when already started.
	 */
	ENetResult Start(const TimePointMilliseconds NowMilliseconds) noexcept
	{
		if (State != ENetHostState::Idle)
		{
			return ENetResult::Invalid;
		}
		switch (Mode)
		{
			case ENetMode::Client:
				State = ENetHostState::Connecting;
				LastHelloSendMilliseconds = NowMilliseconds;
				bHelloDue = true;
				break;
			case ENetMode::ListenServer:
			case ENetMode::DedicatedServer:
				State = ENetHostState::Listening;
				break;
			case ENetMode::Standalone:
				break;
		}
		return ENetResult::Success;
	}

	/**
	 * Ends the session: best-effort `Bye` to every active peer, then evicts all and returns to `Idle`.
	 * The generation of each evicted slot is bumped so any outstanding `FPeerId` goes stale.
	 */
	void Stop() noexcept
	{
		if (Mode != ENetMode::Standalone)
		{
			FControlMessage Farewell{};
			Farewell.Type = EControlMessageType::Bye;
			for (std::size_t Index = 0; Index < MaxPeers; ++Index)
			{
				if (Peers[Index].bActive)
				{
					(void)QueueControl(Peers[Index].Address, Farewell);
				}
			}
			DrainOutbound();
		}
		for (std::size_t Index = 0; Index < MaxPeers; ++Index)
		{
			if (Peers[Index].bActive)
			{
				EvictPeer(Index);
			}
		}
		State = ENetHostState::Idle;
		bHelloDue = false;
	}

	/**
	 * Drains inbound packets (at most `MaxPeers + 4`), handling control internally and
	 * dispatching application messages, then evicts peers past the timeout window.
	 * A standalone host does no driver traffic and returns immediately.
	 */
	ENetResult PumpReceive(const TimePointMilliseconds NowMilliseconds) noexcept
	{
		if (Mode == ENetMode::Standalone)
		{
			return ENetResult::Success;
		}
		std::array<std::uint8_t, MaxPacketBytes> ReceiveBuffer{};
		const std::size_t MaxReceives = MaxPeers + 4;
		for (std::size_t Count = 0; Count < MaxReceives; ++Count)
		{
			FNetAddress From{};
			FNetReceiveResult Result{};
			const ENetResult ReceiveResult = OutboundManager.Receive(From, TSpan<std::uint8_t>(ReceiveBuffer.data(), ReceiveBuffer.size()), Result);
			if (ReceiveResult != ENetResult::Success)
			{
				// Unavailable means the transport is drained; any other failure cannot make progress now.
				break;
			}
			HandleInboundPacket(From, TSpan<const std::uint8_t>(ReceiveBuffer.data(), Result.BytesReceived), NowMilliseconds);
		}
		EvictTimedOutPeers(NowMilliseconds);
		return ENetResult::Success;
	}

	/**
	 * Emits due heartbeats (and client `Hello` retries), then drains the outbound FIFO.
	 * A standalone host does no driver traffic and returns immediately.
	 */
	ENetResult PumpSend(const TimePointMilliseconds NowMilliseconds) noexcept
	{
		if (Mode == ENetMode::Standalone)
		{
			return ENetResult::Success;
		}
		if (Mode == ENetMode::Client && State == ENetHostState::Connecting)
		{
			if (bHelloDue || ElapsedSince(NowMilliseconds, LastHelloSendMilliseconds) >= Config.HeartbeatIntervalMilliseconds)
			{
				QueueHello();
				LastHelloSendMilliseconds = NowMilliseconds;
				bHelloDue = false;
			}
		}
		for (std::size_t Index = 0; Index < MaxPeers; ++Index)
		{
			FNetPeerSlot& Slot = Peers[Index];
			if (Slot.bActive && ElapsedSince(NowMilliseconds, Slot.LastSendMilliseconds) >= Config.HeartbeatIntervalMilliseconds)
			{
				FControlMessage Beat{};
				Beat.Type = EControlMessageType::Heartbeat;
				(void)QueueControl(Slot.Address, Beat);
				Slot.LastSendMilliseconds = NowMilliseconds;
			}
		}
		DrainOutbound();
		return ENetResult::Success;
	}

	/**
	 * Queues one application message (channel 1..255) to a single peer.
	 * A message to the listen server's local peer dispatches directly to the handler
	 * without the driver. Returns `Unavailable` for a standalone host, `Invalid` for
	 * channel 0 or an unresolved peer, or the framing/queue result otherwise.
	 */
	ENetResult SendTo(const FPeerId Peer, const std::uint8_t Channel, TSpan<const std::uint8_t> Payload) noexcept
	{
		if (Mode == ENetMode::Standalone)
		{
			return ENetResult::Unavailable;
		}
		if (Channel == ControlChannel)
		{
			return ENetResult::Invalid;
		}
		if (Peer.Index == LocalPeerIndex)
		{
			if (Mode != ENetMode::ListenServer)
			{
				return ENetResult::Invalid;
			}
			DispatchToHandler(GetLocalPeer(), Channel, Payload);
			return ENetResult::Success;
		}
		const FNetPeerSlot* const Slot = ResolvePeer(Peer);
		if (Slot == nullptr)
		{
			return ENetResult::Invalid;
		}
		return QueueAppMessage(Slot->Address, Channel, Payload);
	}

	/**
	 * Queues one application message (channel 1..255) to every active peer.
	 * A listen server also dispatches to its local peer directly. Best-effort: returns
	 * `Success` when every active peer queued, otherwise the first failure result.
	 */
	ENetResult Broadcast(const std::uint8_t Channel, TSpan<const std::uint8_t> Payload) noexcept
	{
		if (Mode == ENetMode::Standalone)
		{
			return ENetResult::Unavailable;
		}
		if (Channel == ControlChannel)
		{
			return ENetResult::Invalid;
		}
		if (Mode == ENetMode::ListenServer)
		{
			DispatchToHandler(GetLocalPeer(), Channel, Payload);
		}
		ENetResult Outcome = ENetResult::Success;
		for (std::size_t Index = 0; Index < MaxPeers; ++Index)
		{
			if (!Peers[Index].bActive)
			{
				continue;
			}
			const ENetResult SlotResult = QueueAppMessage(Peers[Index].Address, Channel, Payload);
			if (SlotResult != ENetResult::Success && Outcome == ENetResult::Success)
			{
				Outcome = SlotResult;
			}
		}
		return Outcome;
	}

	/** Registers one message handler; forwards the multicast delegate's own result. */
	EDelegateResult AddMessageHandler(FMessageHandlerBinding&& Binding, FDelegateHandle& OutHandle) noexcept
	{
		return MessageHandler.Add(std::move(Binding), OutHandle);
	}

	/** Removes a previously registered message handler by its generation-checked handle. */
	EDelegateResult RemoveMessageHandler(const FDelegateHandle Handle) noexcept { return MessageHandler.Remove(Handle); }

	/** Reports the observable session state. */
	ENetHostState GetState() const noexcept { return State; }

	/** Reports the configured role. */
	ENetMode GetMode() const noexcept { return Mode; }

	/** Reports the listen server's local-peer identity; only meaningful in `ListenServer` mode. */
	constexpr FPeerId GetLocalPeer() const noexcept { return FPeerId{LocalPeerIndex, 1}; }

	/** Reports a connected client's server-peer identity, or an invalid id when not connected. */
	FPeerId GetServerPeer() const noexcept
	{
		if (Mode != ENetMode::Client || State != ENetHostState::Connected)
		{
			return FPeerId{};
		}
		return FPeerId{0, Peers[0].Generation};
	}

	/** Reports the identity a `Welcome` assigned to this client within the server's table. */
	FPeerId GetAssignedPeer() const noexcept { return AssignedPeer; }

	/** Reports how many remote peer slots are currently active. */
	std::size_t ActivePeerCount() const noexcept
	{
		std::size_t Total = 0;
		for (std::size_t Index = 0; Index < MaxPeers; ++Index)
		{
			if (Peers[Index].bActive)
			{
				++Total;
			}
		}
		return Total;
	}

private:
	/** One remote peer's address, liveness timestamps, and reuse generation. */
	struct FNetPeerSlot
	{
		/** Transport address to reach this peer; empty while the slot is free. */
		FNetAddress Address{};

		/** Time of the last packet received from this peer; drives timeout eviction. */
		TimePointMilliseconds LastReceiveMilliseconds{0};

		/** Time of the last packet sent to this peer; paces outgoing heartbeats. */
		TimePointMilliseconds LastSendMilliseconds{0};

		/** Reuse counter bumped on eviction so a stale `FPeerId` cannot match a later occupant. */
		std::uint8_t Generation{0};

		/** Distinguishes a live peer from a free, reusable slot. */
		bool bActive{false};
	};

	/** Returns monotonic elapsed milliseconds, clamped so a backward clock reads as zero elapsed. */
	static constexpr DurationMilliseconds ElapsedSince(
		const TimePointMilliseconds NowMilliseconds, const TimePointMilliseconds PastMilliseconds) noexcept
	{
		if (NowMilliseconds <= PastMilliseconds)
		{
			return 0;
		}
		const TimePointMilliseconds Delta = NowMilliseconds - PastMilliseconds;
		constexpr TimePointMilliseconds MaxDuration = 0xFFFFFFFFu;
		return Delta > MaxDuration ? static_cast<DurationMilliseconds>(MaxDuration) : static_cast<DurationMilliseconds>(Delta);
	}

	/** Reports whether this host admits remote peers. */
	constexpr bool IsServer() const noexcept { return Mode == ENetMode::ListenServer || Mode == ENetMode::DedicatedServer; }

	/** Finds the active peer at `Address`, or `MaxPeers` when none matches. */
	std::size_t FindActivePeerIndexByAddress(const FNetAddress& Address) const noexcept
	{
		for (std::size_t Index = 0; Index < MaxPeers; ++Index)
		{
			if (Peers[Index].bActive && Peers[Index].Address == Address)
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

	/** Builds the current generation-checked identity of the peer at `Index`. */
	FPeerId MakePeerId(const std::size_t Index) const noexcept { return FPeerId{static_cast<std::uint8_t>(Index), Peers[Index].Generation}; }

	/** Resolves a remote peer id to its live slot, or `nullptr` when unknown or stale. */
	const FNetPeerSlot* ResolvePeer(const FPeerId Peer) const noexcept
	{
		if (Peer.Index >= MaxPeers)
		{
			return nullptr;
		}
		const FNetPeerSlot& Slot = Peers[Peer.Index];
		if (!Slot.bActive || Slot.Generation != Peer.Generation)
		{
			return nullptr;
		}
		return &Slot;
	}

	/** Frees the slot at `Index` and bumps its generation so outstanding ids go stale. */
	void EvictPeer(const std::size_t Index) noexcept
	{
		FNetPeerSlot& Slot = Peers[Index];
		Slot.bActive = false;
		Slot.Generation = static_cast<std::uint8_t>(Slot.Generation + 1);
		Slot.Address = FNetAddress{};
	}

	/** Returns a disconnected client to `Connecting` so it re-greets the server on the next send. */
	void OnPeerLost(const std::size_t Index) noexcept
	{
		if (Mode == ENetMode::Client && Index == 0)
		{
			State = ENetHostState::Connecting;
			bHelloDue = true;
		}
	}

	/** Evicts every active peer whose last receive is older than the timeout window. */
	void EvictTimedOutPeers(const TimePointMilliseconds NowMilliseconds) noexcept
	{
		for (std::size_t Index = 0; Index < MaxPeers; ++Index)
		{
			FNetPeerSlot& Slot = Peers[Index];
			if (!Slot.bActive)
			{
				continue;
			}
			if (ElapsedSince(NowMilliseconds, Slot.LastReceiveMilliseconds) > Config.PeerTimeoutMilliseconds)
			{
				MW_LOG(Log, "NetHost", "evicting peer %u (timeout)", static_cast<unsigned>(Index));
				EvictPeer(Index);
				OnPeerLost(Index);
			}
		}
	}

	/** Parses one inbound packet, routing control internally and application messages to the handler. */
	void HandleInboundPacket(const FNetAddress& From, TSpan<const std::uint8_t> Packet, const TimePointMilliseconds NowMilliseconds) noexcept
	{
		FMessageHeader Header{};
		TSpan<const std::uint8_t> Payload{};
		if (ReadMessage(Packet, Header, Payload) != ENetResult::Success)
		{
			MW_LOG_MSG(Log, "NetHost", "dropped malformed inbound packet");
			return;
		}
		if (Header.Channel == ControlChannel)
		{
			HandleControlMessage(From, Payload, NowMilliseconds);
			return;
		}
		const std::size_t Index = FindActivePeerIndexByAddress(From);
		if (Index == MaxPeers)
		{
			MW_LOG_MSG(Log, "NetHost", "dropped application message from unknown peer");
			return;
		}
		Peers[Index].LastReceiveMilliseconds = NowMilliseconds;
		DispatchToHandler(MakePeerId(Index), Header.Channel, Payload);
	}

	/** Decodes one channel-0 control payload and dispatches it by type. */
	void HandleControlMessage(const FNetAddress& From, TSpan<const std::uint8_t> Payload, const TimePointMilliseconds NowMilliseconds) noexcept
	{
		FControlMessage Control{};
		if (ReadControlMessage(Payload, Control) != ENetResult::Success)
		{
			MW_LOG_MSG(Log, "NetHost", "dropped unknown or malformed control message");
			return;
		}
		switch (Control.Type)
		{
			case EControlMessageType::Hello:
				HandleHello(From, Control, NowMilliseconds);
				break;
			case EControlMessageType::Welcome:
				HandleWelcome(From, Control, NowMilliseconds);
				break;
			case EControlMessageType::Heartbeat:
				HandleHeartbeat(From, NowMilliseconds);
				break;
			case EControlMessageType::Bye:
				HandleBye(From);
				break;
		}
	}

	/** Admits a client on `Hello` (idempotent per address), or ignores it on wrong version or a full table. */
	void HandleHello(const FNetAddress& From, const FControlMessage& Control, const TimePointMilliseconds NowMilliseconds) noexcept
	{
		if (!IsServer())
		{
			return;
		}
		if (Control.ProtocolVersion != Config.ProtocolVersion)
		{
			MW_LOG(
				Warning,
				"NetHost",
				"ignored Hello: protocol version %u != %u",
				static_cast<unsigned>(Control.ProtocolVersion),
				static_cast<unsigned>(Config.ProtocolVersion));
			return;
		}
		std::size_t Index = FindActivePeerIndexByAddress(From);
		if (Index == MaxPeers)
		{
			Index = FindFreePeerSlot();
			if (Index == MaxPeers)
			{
				MW_LOG_MSG(Warning, "NetHost", "rejected Hello: peer table full");
				return;
			}
			FNetPeerSlot& Slot = Peers[Index];
			Slot.Address = From;
			Slot.LastReceiveMilliseconds = NowMilliseconds;
			Slot.LastSendMilliseconds = NowMilliseconds;
			Slot.bActive = true;
		}
		else
		{
			Peers[Index].LastReceiveMilliseconds = NowMilliseconds;
		}
		SendWelcome(Index, From);
	}

	/** Records the server as a connected client's single peer and enters `Connected`. */
	void HandleWelcome(const FNetAddress& From, const FControlMessage& Control, const TimePointMilliseconds NowMilliseconds) noexcept
	{
		if (Mode != ENetMode::Client)
		{
			return;
		}
		if (Control.ProtocolVersion != Config.ProtocolVersion)
		{
			MW_LOG(
				Warning,
				"NetHost",
				"ignored Welcome: server version %u != %u",
				static_cast<unsigned>(Control.ProtocolVersion),
				static_cast<unsigned>(Config.ProtocolVersion));
			return;
		}
		FNetPeerSlot& Server = Peers[0];
		Server.Address = From;
		Server.LastReceiveMilliseconds = NowMilliseconds;
		Server.LastSendMilliseconds = NowMilliseconds;
		Server.bActive = true;
		AssignedPeer = FPeerId{Control.PeerIndex, Control.PeerGeneration};
		State = ENetHostState::Connected;
	}

	/** Refreshes a known peer's liveness on `Heartbeat`; ignores heartbeats from strangers. */
	void HandleHeartbeat(const FNetAddress& From, const TimePointMilliseconds NowMilliseconds) noexcept
	{
		const std::size_t Index = FindActivePeerIndexByAddress(From);
		if (Index == MaxPeers)
		{
			MW_LOG_MSG(Log, "NetHost", "ignored heartbeat from unknown peer");
			return;
		}
		Peers[Index].LastReceiveMilliseconds = NowMilliseconds;
	}

	/** Evicts a peer on `Bye`; a client whose server departs returns to `Connecting`. */
	void HandleBye(const FNetAddress& From) noexcept
	{
		const std::size_t Index = FindActivePeerIndexByAddress(From);
		if (Index == MaxPeers)
		{
			return;
		}
		EvictPeer(Index);
		OnPeerLost(Index);
	}

	/** Queues a `Welcome` carrying the assigned index and generation to a newly admitted client. */
	void SendWelcome(const std::size_t PeerIndex, const FNetAddress& To) noexcept
	{
		FControlMessage Welcome{};
		Welcome.Type = EControlMessageType::Welcome;
		Welcome.ProtocolVersion = Config.ProtocolVersion;
		Welcome.PeerIndex = static_cast<std::uint8_t>(PeerIndex);
		Welcome.PeerGeneration = Peers[PeerIndex].Generation;
		if (QueueControl(To, Welcome) != ENetResult::Success)
		{
			MW_LOG_MSG(Warning, "NetHost", "Welcome not queued: outbound queue full");
		}
	}

	/** Queues a client `Hello` to the configured server address. */
	void QueueHello() noexcept
	{
		FControlMessage Hello{};
		Hello.Type = EControlMessageType::Hello;
		Hello.ProtocolVersion = Config.ProtocolVersion;
		if (QueueControl(Config.ServerAddress, Hello) != ENetResult::Success)
		{
			MW_LOG_MSG(Warning, "NetHost", "Hello not queued: outbound queue full");
		}
	}

	/** Frames one control message and queues it to `To`; returns the framing or queue result. */
	ENetResult QueueControl(const FNetAddress& To, const FControlMessage& Control) noexcept
	{
		std::array<std::uint8_t, MessageHeaderBytes + MaxControlPayloadBytes> FrameBuffer{};
		FByteWriter Writer(TSpan<std::uint8_t>(FrameBuffer.data(), FrameBuffer.size()));
		const ENetResult WriteResult = WriteControlMessage(Writer, Control);
		if (WriteResult != ENetResult::Success)
		{
			return WriteResult;
		}
		return OutboundManager.QueueSend(To, Writer.WrittenBytes());
	}

	/** Frames one application message and queues it to `To`; returns the framing or queue result. */
	ENetResult QueueAppMessage(const FNetAddress& To, const std::uint8_t Channel, TSpan<const std::uint8_t> Payload) noexcept
	{
		std::array<std::uint8_t, MaxPacketBytes> FrameBuffer{};
		FByteWriter Writer(TSpan<std::uint8_t>(FrameBuffer.data(), FrameBuffer.size()));
		const ENetResult WriteResult = WriteMessage(Writer, Channel, Payload);
		if (WriteResult != ENetResult::Success)
		{
			return WriteResult;
		}
		return OutboundManager.QueueSend(To, Writer.WrittenBytes());
	}

	/** Delivers one application message to every registered handler; a dispatch failure is best-effort ignored. */
	void DispatchToHandler(const FPeerId From, const std::uint8_t Channel, TSpan<const std::uint8_t> Payload) noexcept
	{
		(void)MessageHandler.Broadcast(From, Channel, Payload);
	}

	/** Sends outbound FIFO entries until it empties or the driver stops accepting; bounded by depth. */
	void DrainOutbound() noexcept
	{
		for (std::size_t Count = 0; Count < SendQueueDepth; ++Count)
		{
			if (OutboundManager.AdvanceSend() != ENetResult::Success)
			{
				// Unavailable means the FIFO is empty; a driver failure retains the head for a later drain.
				break;
			}
		}
	}

	/** Owns the outbound packet bytes, lengths, and destinations for the FIFO. */
	FNetPacketStorage<SendQueueDepth, MaxPacketBytes> OutboundStorage{};

	/** Owns the outbound FIFO over the driver; reused rather than re-implementing queue mechanics. */
	FNetManager<SendQueueDepth, MaxPacketBytes> OutboundManager;

	/** Dispatches application messages to every registered handler. */
	FMessageHandler MessageHandler{};

	/** Fixed table of remote peer slots. */
	std::array<FNetPeerSlot, MaxPeers> Peers{};

	/** Session timing and identity set by `Configure`. */
	FNetHostConfig Config{};

	/** Configured role. */
	ENetMode Mode{ENetMode::Standalone};

	/** Observable session state. */
	ENetHostState State{ENetHostState::Idle};

	/** Identity a `Welcome` assigned to this client within the server's table. */
	FPeerId AssignedPeer{};

	/** Time the client last sent `Hello`; paces connecting retries. */
	TimePointMilliseconds LastHelloSendMilliseconds{0};

	/** Forces the next connecting `PumpSend` to greet immediately after start or reconnect. */
	bool bHelloDue{false};
};

} // namespace MicroWorld
