#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Delegates/Delegate.h>
#include <MicroWorld/Messaging/Message.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace MicroWorld::Messaging
{

/** Which peers a wired channel sends to (roadmap D4): a client's server, or every active peer. */
enum class EChannelSendTarget : std::uint8_t
{
	/** Sends only to the peer this host is connected to as a client. */
	Server,

	/** Sends to every currently active peer (and the local peer, for a listen server). */
	AllPeers,
};

/**
 * Two-way adapter binding one TTransportHost wire channel to one message sink.
 *
 * Outbound sends go to the host (SendTo for a Server target, Broadcast for AllPeers) with the
 * transport result mapped onto EMessageResult, while inbound payloads on the matching wire-channel
 * byte are handed to the sink and a foreign wire channel on the same host is ignored. Duck-typed on
 * THost exactly like THostPlaySystem (see Engine/EngineSystem.h), so this header names no Transport type and
 * microworld-engine keeps zero dependency on microworld-transport; the Transport types resolve only where a
 * caller instantiates this template against a concrete TTransportHost.
 */
template<typename THost>
class TMessageChannelBinding final : public IMessageChannel
{
public:
	/**
	 * Binds to Host and registers its inbound handler.
	 * Registration can fail (the host's handler table is already full); check IsAttached()
	 * afterward, since a binding that failed to attach never receives inbound traffic.
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
	 * Removes the inbound handler while Host is still alive: this binding is always constructed
	 * after Host and destroyed before it (scope and static destruction order), so Host can never
	 * later invoke a handler that captured a destroyed `this`.
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
	TMessageChannelBinding(const TMessageChannelBinding&) = delete;
	TMessageChannelBinding& operator=(const TMessageChannelBinding&) = delete;
	TMessageChannelBinding(TMessageChannelBinding&&) = delete;
	TMessageChannelBinding& operator=(TMessageChannelBinding&&) = delete;

	/** Reports whether the constructor's inbound-handler registration succeeded. */
	bool IsAttached() const noexcept { return bAttached; }

	/** Reports how many inbound messages Sink rejected (its own queue full) after passing the channel filter. */
	std::uint32_t DroppedInboundCount() const noexcept { return DroppedInbound; }

	/** Returns this binding's configured router-facing channel id. */
	FMessageChannelId GetChannelId() const noexcept override { return ChannelId; }

	/** Returns the largest encoded message THost's packet budget can carry in one send. */
	std::size_t MaxEncodedMessageBytes() const noexcept override { return THost::MaxMessageBytes; }

	/**
	 * Sends one already-encoded message over the wire.
	 * Server target requires a connected server peer, reporting Unavailable otherwise so the
	 * router retains the message and retries later; AllPeers broadcasts to every active peer.
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
	 * Maps a duck-typed THost transport result onto EMessageResult (roadmap §4.3's normative
	 * table) without naming the transport's result enum in this engine header: TTransportResult is this
	 * function template's own parameter, so `TTransportResult::Success` etc. are dependent names resolved
	 * only when a caller instantiates TMessageChannelBinding against a concrete THost.
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
	 * Forwards one inbound wire payload to Sink when it arrived on WireChannelByte.
	 * A different wire-channel byte on the same host belongs to some other binding and is silently
	 * ignored here; a Sink rejection (its own queue full) counts against DroppedInbound.
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

	/** Externally owned network host this binding registers with and sends through; never owned here. */
	THost& Host;

	/** Wire-level channel byte (TTransportHost channel 1..255) this binding reads and writes. */
	std::uint8_t WireChannelByte;

	/** Router-facing channel id this binding is registered under (IMessageChannel::GetChannelId). */
	FMessageChannelId ChannelId;

	/** Selects whether TrySendEncodedMessage targets the connected server or every active peer. */
	EChannelSendTarget SendTarget;

	/** Externally owned sink that receives inbound payloads matching WireChannelByte; never owned here. */
	IEncodedMessageSink& Sink;

	/** Identifies the registered inbound handler so the destructor removes exactly this one. */
	Core::FDelegateHandle InboundHandle;

	/** Reports whether the constructor's AddMessageHandler call succeeded. */
	bool bAttached{false};

	/** Counts inbound payloads Sink rejected after passing the channel filter. */
	std::uint32_t DroppedInbound{0};
};

} // namespace MicroWorld::Messaging
