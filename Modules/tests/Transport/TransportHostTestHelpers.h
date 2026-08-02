#pragma once

#include "TransportAllocationCounters.h"
#include "TestSupport.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/ReceiveResult.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Transport/HostLoopback.h>
#include <MicroWorld/Transport/NetworkMode.h>
#include <MicroWorld/Transport/PeerId.h>
#include <MicroWorld/Transport/TransportHost.h>
#include <MicroWorld/Transport/TransportHostConfig.h>
#include <MicroWorld/Transport/TransportHostState.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace MicroWorld::Tests
{

using MicroWorld::Core::ETransportResult;
using MicroWorld::Core::FDelegateHandle;
using MicroWorld::Core::FDeviceAddress;
using MicroWorld::Core::FReceiveResult;
using MicroWorld::Core::ITransportDevice;
using MicroWorld::Core::MakeLoopbackAddress;
using MicroWorld::Core::TimePointMilliseconds;
using MicroWorld::Core::TSpan;
using MicroWorld::Transport::ENetworkMode;
using MicroWorld::Transport::ETransportHostState;
using MicroWorld::Transport::FPeerId;
using MicroWorld::Transport::FTransportHostConfig;
using MicroWorld::Transport::THostLoopback;
using MicroWorld::Transport::TTransportHost;

/** Motivation: Heartbeat interval (ms) the deterministic host config stamps so timed cases advance in fixed steps. */
constexpr TimePointMilliseconds HeartbeatIntervalMs = 100;
/** Motivation: Peer timeout window (ms) after which a silent peer is evicted. */
constexpr TimePointMilliseconds PeerTimeoutMs = 500;
/** Motivation: Last heartbeat instant before the timeout window the keep-alive case reaches. */
constexpr TimePointMilliseconds KeepAliveWindowEndMs = 800;
/** Motivation: Instant past the timeout window where a silent peer is evicted. */
constexpr TimePointMilliseconds EvictionTimeoutMs = 1000;
/** Motivation: Instant at which the generation-bump case evicts the first client. */
constexpr TimePointMilliseconds GenerationBumpEvictMs = 2000;
/** Motivation: Instant past the client-side timeout window where the server is treated as gone. */
constexpr TimePointMilliseconds ClientServerTimeoutMs = 2000;
/** Motivation: Instant at which the repeated-Hello case re-greets after the first admission. */
constexpr TimePointMilliseconds RepeatedHelloResendMs = 100;
/** Motivation: Send timestamp (ms) for the single-session and broadcast cases. */
constexpr TimePointMilliseconds SessionPumpMs = 100;

/** Motivation: Loopback template parameter: server port index. */
constexpr std::uint8_t ServerPortIndex = 0;
/** Motivation: Loopback template parameter: first client port index. */
constexpr std::uint8_t FirstClientPortIndex = 1;
/** Motivation: Loopback template parameter: second client port index. */
constexpr std::uint8_t SecondClientPortIndex = 2;
/** Motivation: Loopback template parameter: third client port index. */
constexpr std::uint8_t ThirdClientPortIndex = 3;
/** Motivation: FFloodDevice sender port index stamped into OutFrom. */
constexpr std::uint8_t FloodSenderPortIndex = 9;
/** Motivation: Channel the host-to-host send cases address. */
constexpr std::uint8_t SendChannel = 1;
/** Motivation: Channel the broadcast cases address. */
constexpr std::uint8_t BroadcastChannel = 1;
/** Motivation: Channel the SendTo single-target case addresses. */
constexpr std::uint8_t SendToChannel = 2;
/** Motivation: Channel the local-peer dispatch case addresses. */
constexpr std::uint8_t LocalDispatchChannel = 3;
/** Motivation: Protocol version the matched-protocol handshake cases use. */
constexpr std::uint8_t MatchedProtocolVersion = 1;
/** Motivation: Protocol version the mismatched-Hello case uses on the client side. */
constexpr std::uint8_t MismatchedProtocolVersion = 2;
/** Motivation: Loopback mailbox depth every host config case uses. */
constexpr std::size_t LoopbackMailboxDepth = 8;
/** Motivation: Loopback per-packet byte capacity every host config case uses. */
constexpr std::size_t LoopbackPacketBytes = 64;
/** Motivation: Per-host packet byte capacity every TTransportHost instantiation uses. */
constexpr std::size_t HostPacketBytes = 64;
/** Motivation: Number of remote peers the rejection-when-full case admits (matches MaxPeers). */
constexpr std::size_t FullPeerCount = 2;
/** Motivation: Peer count reported by the single-peer cases after a successful handshake. */
constexpr std::size_t AdmittedPeerCount = 1;
/** Motivation: Peer count reported by the no-peer cases. */
constexpr std::size_t EmptyPeerCount = 0;
/** Motivation: Peer count reported by the broadcast case after both clients connect. */
constexpr std::size_t BroadcastPeerCount = 2;
/** Motivation: Number of receives one bounded pump is permitted to call under flood. */
constexpr std::size_t BoundedPumpReceiveCount = 7;
/** Motivation: Byte value the FFloodDevice writes into every header byte of its empty control frame. */
constexpr std::uint8_t EmptyControlFrameByte = 0;
/** Motivation: Byte count the FFloodDevice reports as received per call. */
constexpr std::size_t FloodReceivedByteCount = 4;

/** Motivation: Sentinel payload byte the generation-bump case sends to a stale and a fresh peer id. */
constexpr std::uint8_t GenerationBumpPayloadByte = 0x42;
/** Motivation: Broadcast payload byte the broadcast case delivers to every connected peer. */
constexpr std::uint8_t BroadcastPayloadByte = 0x5A;
/** Motivation: Single-byte payload the SendTo single-target case delivers. */
constexpr std::uint8_t SendToPayloadByte = 0x33;
/** Motivation: Single-byte payload the local-peer dispatch case delivers synchronously. */
constexpr std::uint8_t LocalDispatchPayloadByte = 0x77;
/** Motivation: First byte of the three-byte session payload the allocation case broadcasts. */
constexpr std::uint8_t SessionPayloadFirstByte = 0x01;
/** Motivation: Second byte of the three-byte session payload the allocation case broadcasts. */
constexpr std::uint8_t SessionPayloadSecondByte = 0x02;
/** Motivation: Third byte of the three-byte session payload the allocation case broadcasts. */
constexpr std::uint8_t SessionPayloadThirdByte = 0x03;
/** Motivation: Single-byte payload the standalone send-attempt case threads through SendTo/Broadcast. */
constexpr std::uint8_t StandalonePayloadByte = 0x01;
/** Motivation: Single-byte payload the dedicated-server local-dispatch case rejects. */
constexpr std::uint8_t DedicatedServerLocalPayloadByte = 0x11;

/** Motivation: Hand-assembled control frame whose payload byte names an undefined control type (0x09): channel 0,
 *   zero flags, declared payload length 1, and the single payload byte 0x09.
 */
constexpr std::uint8_t UnknownControlTypeFrame[5] = {0x00, 0x00, 0x01, 0x00, 0x09};

/**
 * Motivation: Records the last message a handler observed so a test can assert delivery.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FHandlerCapture
{
	/** Motivation: Number of messages the handler has observed; zero means it never ran. */
	std::size_t Count{0};

	/** Motivation: Sender identity from the most recent dispatch. */
	FPeerId From{};

	/** Motivation: Channel from the most recent dispatch. */
	std::uint8_t Channel{0};

	/** Motivation: First payload byte from the most recent dispatch, or zero for an empty payload. */
	std::uint8_t FirstByte{0};
};

/**
 * Motivation: Builds a fast-heartbeat host config with a short timeout window for deterministic tests.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
inline FTransportHostConfig MakeHostConfig(const std::uint8_t InProtocolVersion) noexcept
{
	FTransportHostConfig Config{};
	Config.HeartbeatIntervalMilliseconds = HeartbeatIntervalMs;
	Config.PeerTimeoutMilliseconds = PeerTimeoutMs;
	Config.ProtocolVersion = InProtocolVersion;
	return Config;
}

/**
 * Motivation: Builds a client config that greets the loopback port `InServerPort`.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
inline FTransportHostConfig MakeClientConfig(const std::uint8_t InProtocolVersion, const std::uint8_t InServerPort) noexcept
{
	FTransportHostConfig Config = MakeHostConfig(InProtocolVersion);
	Config.ServerAddress = MakeLoopbackAddress(InServerPort);
	return Config;
}

/**
 * Motivation: Binds one capturing handler into a host and returns its removal handle.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
template<typename HostType>
FDelegateHandle InstallCapture(HostType& InHost, FHandlerCapture& InCapture) noexcept
{
	typename HostType::FMessageHandlerBinding Binding;
	Binding.Bind(
		[&InCapture](const FPeerId InFrom, const std::uint8_t InChannel, TSpan<const std::uint8_t> InPayload) noexcept
		{
			++InCapture.Count;
			InCapture.From = InFrom;
			InCapture.Channel = InChannel;
			InCapture.FirstByte = InPayload.Size() > 0 ? InPayload[0] : std::uint8_t{0};
		});
	FDelegateHandle Handle{};
	(void)InHost.AddMessageHandler(std::move(Binding), Handle);
	return Handle;
}

/**
 * Motivation: Runs one full Hello->Welcome handshake round at `InNowMilliseconds`.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 */
template<typename ServerType, typename ClientType>
void RunHandshake(ServerType& InServer, ClientType& InClient, const TimePointMilliseconds InNowMilliseconds) noexcept
{
	InClient.PumpSend(InNowMilliseconds);
	InServer.PumpReceive(InNowMilliseconds);
	InServer.PumpSend(InNowMilliseconds);
	InClient.PumpReceive(InNowMilliseconds);
}

} // namespace MicroWorld::Tests
