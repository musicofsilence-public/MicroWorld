#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Delegates/Delegate.h>
#include <MicroWorld/Messaging/Message.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace MicroWorld::Messaging
{

/**
 * Motivation: Names, independent of any transport, which peers a wired channel sends to so a binding can be configured
 *   without naming the transport's own send-direction vocabulary.
 * Responsibilities: Distinguish the single connected-server target from the every-active-peer target.
 * Example:
 *   EChannelSendTarget Target = EChannelSendTarget::AllPeers;
 */
enum class EChannelSendTarget : std::uint8_t
{
	Server, ///< Motivation: Sends only to the peer this host is connected to as a client.

	AllPeers, ///< Motivation: Sends to every currently active peer (and the local peer, for a listen server).
};

/**
 * Motivation: Adapts one TTransportHost wire channel into a two-way IMessageChannel binding so the engine can reach a
 *   transport without depending on it, duck-typed on THost exactly like THostPlaySystem so this header names no
 *   Transport type and microworld-engine keeps zero dependency on microworld-transport.
 * Responsibilities: Forward outbound sends to the host (SendTo for a Server target, Broadcast for AllPeers) with the
 *   transport result mapped onto EMessageResult, route only the matching wire-channel byte inbound to the sink, and
 *   register and remove one inbound handler whose lifetime stays inside Host's.
 * Example:
 *   TMessageChannelBinding<TTransportHost> Binding(Host, WireByte, ChannelId, EChannelSendTarget::Server, Router);
 *   if (Binding.IsAttached()) { Router.AddChannel(Binding); }
 */
template<typename THost>
class TMessageChannelBinding final : public IMessageChannel
{
public:
	/**
	 * Motivation: Wires this binding to its host and sink at construction so a caller gets one ready channel per object.
	 * Responsibilities: Register the inbound handler with Host; record attachment in IsAttached() so a binding that
	 *   failed to register never receives inbound traffic.
	 */
	TMessageChannelBinding(
		THost& InHost,
		const std::uint8_t InWireChannelByte,
		const FMessageChannelId InChannelId,
		const EChannelSendTarget InSendTarget,
		IEncodedMessageSink& InSink) noexcept
		: Host(InHost), WireChannelByte(InWireChannelByte), ChannelId(InChannelId), SendTarget(InSendTarget), Sink(InSink)
	{
		typename THost::FMessageHandlerBinding InboundHandler;
		const Core::EDelegateResult BindResult =
			InboundHandler.Bind([this](auto /*Peer*/, const std::uint8_t Channel, const Core::TSpan<const std::uint8_t> Payload) noexcept
								{ this->OnWireBytesReceived(Channel, Payload); });
		// AddMessageHandler clears InboundHandle to invalid on any failure, so a short-circuited
		// ternary (skipping the call entirely when Bind already failed) needs no separate cleanup.
		const Core::EDelegateResult AddResult =
			(BindResult == Core::EDelegateResult::Success) ? Host.AddMessageHandler(std::move(InboundHandler), InboundHandle) : BindResult;
		bAttached = (AddResult == Core::EDelegateResult::Success);
	}

	/**
	 * Motivation: Keeps Host from invoking a handler whose captured `this` is gone, since this binding's scope is always
	 *   nested inside Host's.
	 * Responsibilities: Remove the inbound handler through its handle only while Host is still alive, leaving no callback
	 *   that could reach a destroyed binding.
	 */
	~TMessageChannelBinding() noexcept override
	{
		if (bAttached)
		{
			(void)Host.RemoveMessageHandler(InboundHandle);
		}
	}

	// Host is held by reference and captured by `this` inside the registered inbound handler;
	// neither may relocate, so copy and move are deleted (matching TMessageRouter's fixed-identity rule).
	/**
	 * Motivation: Stops copy construction from duplicating the `this` pointer the registered inbound handler captured.
	 * Responsibilities: Reject copy construction outright so the binding stays the single owner of its registered handler.
	 */
	TMessageChannelBinding(const TMessageChannelBinding&) = delete;
	/**
	 * Motivation: Stops copy assignment from rebinding the captured `this` pointer to a different location.
	 * Responsibilities: Reject copy assignment outright so the binding keeps one fixed identity.
	 */
	TMessageChannelBinding& operator=(const TMessageChannelBinding&) = delete;
	/**
	 * Motivation: Stops move construction from relocating the binding the registered inbound handler points at.
	 * Responsibilities: Reject move construction outright so the captured `this` stays valid.
	 */
	TMessageChannelBinding(TMessageChannelBinding&&) = delete;
	/**
	 * Motivation: Stops move assignment from relocating the binding the registered inbound handler points at.
	 * Responsibilities: Reject move assignment outright so the captured `this` stays valid.
	 */
	TMessageChannelBinding& operator=(TMessageChannelBinding&&) = delete;

	/**
	 * Motivation: Lets a caller tell whether the constructor's inbound-handler registration took hold before relying on it.
	 * Responsibilities: Report exactly the success of that one registration call.
	 */
	bool IsAttached() const noexcept { return bAttached; }

	/**
	 * Motivation: Lets a caller observe how many inbound payloads the sink could not accept, since the binding absorbs
	 *   those rejections silently.
	 * Responsibilities: Report the count of inbound payloads the sink rejected after passing the channel filter.
	 */
	std::uint32_t DroppedInboundCount() const noexcept { return DroppedInbound; }

	/**
	 * Motivation: Lets the router identify this binding by the id it was configured with.
	 * Responsibilities: Return the router-facing channel id supplied at construction.
	 */
	FMessageChannelId GetChannelId() const noexcept override { return ChannelId; }

	/**
	 * Motivation: Lets a caller size an encoded message against the host's packet budget before sending.
	 * Responsibilities: Return THost's MaxMessageBytes, the largest payload one send can carry.
	 */
	std::size_t MaxEncodedMessageBytes() const noexcept override { return THost::MaxMessageBytes; }

	/**
	 * Motivation: Lets the router send one already-encoded message over the wire through the chosen peer direction.
	 * Responsibilities: For a Server target require a connected server peer and report Unavailable otherwise so the
	 *   router retains the message and retries later; for AllPeers broadcast to every active peer, mapping the host's
	 *   transport result onto EMessageResult.
	 */
	EMessageResult TrySendEncodedMessage(const Core::TSpan<const std::uint8_t> InEncoded) noexcept override
	{
		if (SendTarget == EChannelSendTarget::Server)
		{
			const auto ServerPeer = Host.GetServerPeer(); // auto: never names the Transport peer-id type
			if (!ServerPeer.IsValid())
			{
				return EMessageResult::Unavailable;
			}
			return MapTransportSendResult(Host.SendTo(ServerPeer, WireChannelByte, InEncoded));
		}
		return MapTransportSendResult(Host.Broadcast(WireChannelByte, InEncoded));
	}

private:
	/**
	 * Motivation: Lets a duck-typed THost transport result be reported to the router in EMessageResult terms without this
	 *   engine header naming the transport's own result enum.
	 * Responsibilities: Map Success, Full, and Invalid onto their messaging equivalents and every other value onto
	 *   Unavailable, relying on dependent TTransportResult names resolved only at instantiation.
	 */
	template<typename TTransportResult>
	static EMessageResult MapTransportSendResult(const TTransportResult InResult) noexcept
	{
		if (InResult == TTransportResult::Success)
		{
			return EMessageResult::Success;
		}
		if (InResult == TTransportResult::Full)
		{
			return EMessageResult::CapacityExceeded;
		}
		if (InResult == TTransportResult::Invalid)
		{
			return EMessageResult::PayloadTooLarge;
		}
		return EMessageResult::Unavailable; // TTransportResult::Unavailable is the only value left.
	}

	/**
	 * Motivation: Lets the binding forward only the inbound payloads that belong to its wire channel, ignoring every
	 *   other wire-channel byte the same host may raise.
	 * Responsibilities: Skip a payload whose wire-channel byte differs from WireChannelByte, and count a sink rejection
	 *   against DroppedInbound rather than re-delivering it.
	 */
	void OnWireBytesReceived(const std::uint8_t InChannel, const Core::TSpan<const std::uint8_t> InPayload) noexcept
	{
		if (InChannel != WireChannelByte)
		{
			return;
		}
		if (Sink.ReceiveEncodedMessage(ChannelId, InPayload) != EMessageResult::Success)
		{
			++DroppedInbound;
		}
	}

	/** Motivation: Holds the externally owned network host this binding registers with and sends through. */
	THost& Host;

	/** Motivation: Holds the wire-level channel byte this binding reads and writes on the host. */
	std::uint8_t WireChannelByte;

	/** Motivation: Holds the router-facing channel id this binding is registered under. */
	FMessageChannelId ChannelId;

	/** Motivation: Holds whether TrySendEncodedMessage targets the connected server or every active peer. */
	EChannelSendTarget SendTarget;

	/** Motivation: Holds the externally owned sink that receives inbound payloads matching WireChannelByte. */
	IEncodedMessageSink& Sink;

	/** Motivation: Holds the handle that identifies the registered inbound handler so the destructor removes exactly it. */
	Core::FDelegateHandle InboundHandle;

	/** Motivation: Records whether the constructor's AddMessageHandler call succeeded. */
	bool bAttached{false};

	/** Motivation: Counts inbound payloads the sink rejected after passing the channel filter. */
	std::uint32_t DroppedInbound{0};
};

} // namespace MicroWorld::Messaging
