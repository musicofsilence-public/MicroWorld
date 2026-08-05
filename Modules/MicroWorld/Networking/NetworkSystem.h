#pragma once

#include <MicroWorld/Core/Containers/StaticVector.h>
#include <MicroWorld/Core/Delegates/MulticastDelegate.h>
#include <MicroWorld/Core/PlaySystem.h>
#include <MicroWorld/Messaging/MessagingRoute.h>
#include <MicroWorld/Messaging/MessagingSystem.h>
#include <MicroWorld/Networking/ConnectionState.h>
#include <MicroWorld/Networking/DisconnectReason.h>
#include <MicroWorld/Networking/NetworkResult.h>
#include <MicroWorld/Networking/NetworkSystemInformation.h>
#include <MicroWorld/Networking/PeerId.h>
#include <MicroWorld/Networking/RoutedMessage.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Networking
{

/**
 * Motivation: Restores bounded logical sessions above Messaging while keeping byte transports unaware of peers or roles.
 * Responsibilities: Own client/server admission, peer generations, liveness, route validation, and local application republishing; never access a
 * device.
 * Example: FNetworkSystem Network{Messaging, {ENetworkRole::Server}}; Network.Initialize();
 */
class FNetworkSystem final : public Core::IPlaySystem
{
public:
	/** Motivation: Fixes the maximum remote peers a server Network instance may admit. */
	static constexpr std::size_t MaxPeers = 4;

	/** Motivation: Bounds public event callbacks so Network notification never allocates. */
	static constexpr std::size_t MaxEventBindings = 4;

	/** Motivation: Reserves the non-reliable private Messaging channel used only for Network protocol traffic. */
	static constexpr Messaging::FNameId BestEffortWireChannelNameId = Messaging::MakeNameId("__NetworkBestEffort");

	/** Motivation: Reserves the reliable private Messaging channel used only for Network protocol traffic. */
	static constexpr Messaging::FNameId ReliableWireChannelNameId = Messaging::MakeNameId("__NetworkReliable");

	/** Motivation: Bounds Network event callback captures while preserving stack-only system construction. */
	static constexpr std::size_t EventCallableBytes = 24;

	/** Motivation: Publishes the application payload bound that remains safe on Network's reliable private wire channel. */
	static constexpr std::size_t MaxRoutedMessageBytes = FRoutedMessage::MaxPayloadBytes;

	/** Motivation: Publishes client lifecycle changes after state mutation. Responsibilities: Carry the new state only. */
	using FConnectionStateChangedDelegate = Core::TMulticastDelegate<void(EConnectionState), MaxEventBindings, EventCallableBytes>;

	/** Motivation: Publishes server peer admission without leaking Messaging routes. Responsibilities: Carry the assigned peer only. */
	using FPeerConnectedDelegate = Core::TMulticastDelegate<void(FPeerId), MaxEventBindings, EventCallableBytes>;

	/** Motivation: Publishes a retired peer after its generation becomes invalid. Responsibilities: Carry peer identity and observed reason. */
	using FPeerDisconnectedDelegate = Core::TMulticastDelegate<void(FPeerId, EDisconnectReason), MaxEventBindings, EventCallableBytes>;

	/**
	 * Motivation: Binds Network to its longer-lived Messaging system and immutable session policy.
	 * Responsibilities: Retain no device or route until connect/admission.
	 */
	explicit FNetworkSystem(Messaging::FMessagingSystem& InMessaging, const FNetworkSystemInformation& InInformation = {}) noexcept;

	/**
	 * Motivation: Ensures private Messaging subscriptions leave before the borrowed Messaging system dies.
	 * Responsibilities: Reverse Initialize on destruction.
	 */
	~FNetworkSystem() noexcept override;

	FNetworkSystem(const FNetworkSystem&) = delete;
	FNetworkSystem& operator=(const FNetworkSystem&) = delete;
	FNetworkSystem(FNetworkSystem&&) = delete;
	FNetworkSystem& operator=(FNetworkSystem&&) = delete;

	/**
	 * Motivation: Creates Network's private wire channels and subscriptions transactionally.
	 * Responsibilities: Return failure with no hidden created channel remaining.
	 */
	ENetworkResult Initialize() noexcept;

	/**
	 * Motivation: Releases private Network Messaging state before its owner is destroyed.
	 * Responsibilities: Unsubscribe then destroy channels in reverse creation order.
	 */
	void Shutdown() noexcept;

	/**
	 * Motivation: Lets applications select role-dependent calls without reading configuration internals.
	 * Responsibilities: Return the immutable configured role.
	 */
	ENetworkRole GetRole() const noexcept { return Information.Role; }

	/**
	 * Motivation: Lets clients observe only their public session lifecycle.
	 * Responsibilities: Return Disconnected for every server Network.
	 */
	EConnectionState GetConnectionState() const noexcept { return ConnectionState; }

	/**
	 * Motivation: Lets a connected client address its sole server without carrying routes in application code.
	 * Responsibilities: Return invalid outside a live client session.
	 */
	FPeerId GetServerPeer() const noexcept { return ServerPeer; }

	/**
	 * Motivation: Begins or replaces one client connection attempt on a registered Messaging route.
	 * Responsibilities: Reject wrong role, invalid routes, or exhausted attempts without mutation.
	 */
	ENetworkResult ConnectToServer(const Messaging::FMessagingRoute& InServerRoute, Core::TimePointMilliseconds InNowMilliseconds) noexcept;

	/**
	 * Motivation: Ends the live client session cleanly.
	 * Responsibilities: Send a best-effort disconnect only for a validated connected client, then invalidate it.
	 */
	ENetworkResult DisconnectFromServer() noexcept;

	/**
	 * Motivation: Lets a server remove one validated remote peer.
	 * Responsibilities: Send a best-effort disconnect, retire the generation, then publish the event.
	 */
	ENetworkResult DisconnectPeer(FPeerId InPeer) noexcept;

	/**
	 * Motivation: Sends one local application message to a client's sole server.
	 * Responsibilities: Enforce client role and current session before any Messaging send.
	 */
	ENetworkResult SendToServer(Messaging::FNameId InChannelNameId, const Messaging::FMessage& InMessage) noexcept;

	/**
	 * Motivation: Sends one local application message to one server-admitted peer.
	 * Responsibilities: Enforce server role and live generation-checked peer validation.
	 */
	ENetworkResult SendTo(FPeerId InPeer, Messaging::FNameId InChannelNameId, const Messaging::FMessage& InMessage) noexcept;

	/**
	 * Motivation: Fans one local application message out to every server-admitted peer.
	 * Responsibilities: Attempt each live peer once and report aggregate success.
	 */
	ENetworkResult Broadcast(Messaging::FNameId InChannelNameId, const Messaging::FMessage& InMessage) noexcept;

	/**
	 * Motivation: Recovers a validated live sender identity after Network locally republishes an inbound application message.
	 * Responsibilities: Reject absent, stale, or wrong-role source values.
	 */
	FPeerId ResolveSenderPeer(const Messaging::FMessage& InMessage) const noexcept;

	/**
	 * Motivation: Lets callers subscribe to state changes without owning Network lifecycle state.
	 * Responsibilities: Return the fixed event source by reference.
	 */
	FConnectionStateChangedDelegate& OnConnectionStateChanged() noexcept { return ConnectionStateChanged; }

	/**
	 * Motivation: Lets server callers observe peer admission without routes.
	 * Responsibilities: Return the fixed event source by reference.
	 */
	FPeerConnectedDelegate& OnPeerConnected() noexcept { return PeerConnected; }

	/**
	 * Motivation: Lets callers observe every peer retirement with a reason.
	 * Responsibilities: Return the fixed event source by reference.
	 */
	FPeerDisconnectedDelegate& OnPeerDisconnected() noexcept { return PeerDisconnected; }

	/**
	 * Motivation: Performs caller-timed liveness and heartbeat work before application advancement.
	 * Responsibilities: Visit no more than four peers and never advance devices.
	 */
	void PreAdvance(Core::TimePointMilliseconds InNowMilliseconds) noexcept override;

	/**
	 * Motivation: Completes the IPlaySystem lifecycle without a second device-driving phase.
	 * Responsibilities: Retain caller time for subsequent sends only.
	 */
	void PostAdvance(Core::TimePointMilliseconds InNowMilliseconds) noexcept override;

	/**
	 * Motivation: Invalidates all sessions before dependent world objects finish play.
	 * Responsibilities: Retire peers without accessing Messaging after shutdown.
	 */
	void EndPlay() noexcept override;

private:
	/** Motivation: Names the low byte of local Network source metadata that retains the peer slot index. */
	static constexpr std::uint64_t SourceIdPeerIndexMask = 0xFFu;
	/** Motivation: Names the bit offset of the peer generation in local Network source metadata. */
	static constexpr std::uint64_t SourceIdPeerGenerationShift = 8u;
	/** Motivation: Names the mask that bounds the 32-bit peer generation in local Network source metadata. */
	static constexpr std::uint64_t SourceIdPeerGenerationMask = 0xFFFFFFFFu;
	/** Motivation: Names the marker bit that distinguishes Network source metadata from other higher-layer sources. */
	static constexpr std::uint64_t SourceIdNetworkMarkerShift = 40u;
	/** Motivation: Marks an opaque Messaging source id as a Network peer encoding. */
	static constexpr std::uint64_t SourceIdNetworkMarkerMask = std::uint64_t{1} << SourceIdNetworkMarkerShift;
	/**
	 * Motivation: Retains one fixed server registry slot and its route/session validation facts.
	 * Responsibilities: Never expose its route to application code.
	 * Example: FPeerSlot Slot{};
	 */
	struct FPeerSlot final
	{
		/** Motivation: Marks a slot that currently represents one admitted remote peer. */
		bool bOccupied{false};
		/** Motivation: Prevents a generation-exhausted slot from ever aliasing an old peer id. */
		bool bRetired{false};
		/** Motivation: Carries the connection handle assigned when this slot was admitted. */
		FPeerId Peer{};
		/** Motivation: Keeps the complete Messaging route internal to Network send/validation paths. */
		Messaging::FMessagingRoute Route{};
		/** Motivation: Matches protocol traffic to the most recent client connect attempt. */
		std::uint32_t AttemptId{0};
		/** Motivation: Drives timeout policy with caller-supplied monotonic time. */
		Core::TimePointMilliseconds LastActivityMilliseconds{0};
		/** Motivation: Paces outbound heartbeats for this route. */
		Core::TimePointMilliseconds LastHeartbeatMilliseconds{0};
	};

	/**
	 * Motivation: Receives both private wire channels through one controlled policy gate.
	 * Responsibilities: Decode only recognized Network schemas and drop malformed traffic.
	 */
	void HandleWireMessage(const Messaging::FMessage& InMessage) noexcept;
	/**
	 * Motivation: Handles client requests at a server boundary.
	 * Responsibilities: Validate version, route, attempt, and capacity before admission.
	 */
	void HandleConnectRequest(const Messaging::FMessage& InMessage) noexcept;
	/**
	 * Motivation: Handles server admission at a client boundary.
	 * Responsibilities: Accept only the current attempt from the configured route.
	 */
	void HandleConnectAccepted(const Messaging::FMessage& InMessage) noexcept;
	/**
	 * Motivation: Handles server refusals at a client boundary.
	 * Responsibilities: Retire only the current pending attempt.
	 */
	void HandleConnectRejected(const Messaging::FMessage& InMessage) noexcept;
	/**
	 * Motivation: Refreshes liveness only for a route and session that match one live peer.
	 * Responsibilities: Ignore stale or mismatched traffic.
	 */
	void HandleHeartbeat(const Messaging::FMessage& InMessage) noexcept;
	/**
	 * Motivation: Retires a peer only when an explicit close identifies its live route and session.
	 * Responsibilities: Ignore stale closes.
	 */
	void HandleDisconnect(const Messaging::FMessage& InMessage) noexcept;
	/**
	 * Motivation: Republishes only a validated routed application message locally.
	 * Responsibilities: Hide route metadata and stamp a live sender source id.
	 */
	void HandleRoutedMessage(const Messaging::FMessage& InMessage) noexcept;

	/**
	 * Motivation: Converts current Messaging outcomes into Network's public policy result.
	 * Responsibilities: Preserve accepted, capacity, and invalid distinctions.
	 */
	static ENetworkResult MapMessagingResult(Messaging::EMessagingResult InResult) noexcept;
	/**
	 * Motivation: Keeps source metadata opaque while retaining exact peer information.
	 * Responsibilities: Encode one valid peer into a nonzero source id.
	 */
	static Messaging::FMessageSourceId MakeSourceId(FPeerId InPeer) noexcept;
	/**
	 * Motivation: Recovers a peer candidate from local-only source metadata.
	 * Responsibilities: Return invalid for absent or malformed source values.
	 */
	static FPeerId ReadSourceId(Messaging::FMessageSourceId InSourceId) noexcept;
	/**
	 * Motivation: Selects the reserved wire channel matching an eligible application's declared reliability.
	 * Responsibilities: Return InvalidNameId unless the channel exists, is local-only, and is not Network-reserved.
	 */
	Messaging::FNameId GetWireChannelNameId(Messaging::FNameId InApplicationChannelNameId) const noexcept;
	/**
	 * Motivation: Finds a live server peer by complete public identity.
	 * Responsibilities: Return null for stale, invalid, or client-only ids.
	 */
	FPeerSlot* FindPeer(FPeerId InPeer) noexcept;
	/**
	 * Motivation: Finds a live peer route and session from inbound Messaging metadata.
	 * Responsibilities: Return null when route is not admitted.
	 */
	FPeerSlot* FindPeerByRoute(const Messaging::FMessagingRoute& InRoute) noexcept;
	/**
	 * Motivation: Finds an idempotent or replacement admission slot.
	 * Responsibilities: Prefer matching route, then the first non-retired free slot.
	 */
	FPeerSlot* FindAdmissionSlot(const Messaging::FMessagingRoute& InRoute) noexcept;
	/**
	 * Motivation: Retires one server peer before callbacks can reuse its old handle.
	 * Responsibilities: Increment generation or permanently retire on exhaustion.
	 */
	void RetirePeer(FPeerSlot& InSlot, EDisconnectReason InReason) noexcept;
	/**
	 * Motivation: Retires the client server session before publishing its state change.
	 * Responsibilities: Clear route and peer identity atomically.
	 */
	void RetireServer(EConnectionState InNextState) noexcept;
	/**
	 * Motivation: Sends one schema only through its explicit private wire route.
	 * Responsibilities: Never deliver protocol traffic locally.
	 */
	template<typename MessageType>
	ENetworkResult SendProtocolMessage(
		const MessageType& InMessage, Messaging::FNameId InWireChannel, const Messaging::FMessagingRoute& InRoute) noexcept;
	/**
	 * Motivation: Sends a bounded application envelope to one already validated route.
	 * Responsibilities: Copy no more than FRoutedMessage::MaxPayloadBytes before remote-only Messaging send.
	 */
	ENetworkResult SendRoutedMessage(
		FPeerId InPeer, const Messaging::FMessagingRoute& InRoute, Messaging::FNameId InChannelNameId, const Messaging::FMessage& InMessage) noexcept;

	/** Motivation: Borrows the longer-lived Messaging service that owns all byte routing and subscriptions. */
	Messaging::FMessagingSystem& Messaging;
	/** Motivation: Retains immutable role and timing policy for this Network instance. */
	FNetworkSystemInformation Information{};
	/** Motivation: Stores four server peer records without dynamic allocation. */
	FPeerSlot PeerSlots[MaxPeers]{};
	/** Motivation: Stores the client-only configured server route while connecting or connected. */
	Messaging::FMessagingRoute ServerRoute{};
	/** Motivation: Stores the client-visible server connection identity. */
	FPeerId ServerPeer{};
	/** Motivation: Stores the client lifecycle state; server instances remain Disconnected. */
	EConnectionState ConnectionState{EConnectionState::Disconnected};
	/** Motivation: Associates all client traffic with the latest non-wrapping connect attempt. */
	std::uint32_t CurrentAttemptId{0};
	/** Motivation: Records the last client connect request or heartbeat time for bounded pacing. */
	Core::TimePointMilliseconds LastClientSendMilliseconds{0};
	/** Motivation: Records the last validated server traffic so local heartbeat sends cannot mask a silent server. */
	Core::TimePointMilliseconds LastServerActivityMilliseconds{0};
	/** Motivation: Retains the most recent caller-supplied time for immediate public sends. */
	Core::TimePointMilliseconds MostRecentTimeMilliseconds{0};
	/** Motivation: Retains the subscription for the best-effort private wire channel. */
	Messaging::FMessagingSystem::FSubscriptionHandle BestEffortSubscription{};
	/** Motivation: Retains the subscription for the reliable private wire channel. */
	Messaging::FMessagingSystem::FSubscriptionHandle ReliableSubscription{};
	/** Motivation: Prevents repeated initialization and makes reverse unwinding idempotent. */
	bool bInitialized{false};
	/** Motivation: Broadcasts client lifecycle state after mutation. */
	FConnectionStateChangedDelegate ConnectionStateChanged{};
	/** Motivation: Broadcasts server peer admission after slot state becomes live. */
	FPeerConnectedDelegate PeerConnected{};
	/** Motivation: Broadcasts peer retirement after its generation becomes stale. */
	FPeerDisconnectedDelegate PeerDisconnected{};
};

} // namespace MicroWorld::Networking
