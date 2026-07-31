#include "TestSupport.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Transport/DeviceAddress.h>
#include <MicroWorld/Transport/Device.h>
#include <MicroWorld/Transport/TransportHost.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Platform/Host/HostTimeSource.h>
#include <MicroWorld/Platform/Host/HostWifiDevice.h>
#include <MicroWorld/Platform/Host/UdpAddress.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Core/Delegates/Delegate.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

using namespace MicroWorld::Core;
using namespace MicroWorld::Transport;
using namespace MicroWorld::Transport::Address;
using namespace MicroWorld::Transport::Device;
using MicroWorld::Platform::Host::FHostTimeSource;
using MicroWorld::Platform::Host::FHostWifiDevice;

/**
 * Motivation: Records the last application message the server handler observed.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
struct FServerCapture
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

/** Motivation: Host loopback octet prefix reused by every endpoint address in the demo. */
constexpr std::uint8_t OctetA = 127;
constexpr std::uint8_t OctetB = 0;
constexpr std::uint8_t OctetC = 0;
constexpr std::uint8_t OctetD = 1;

/** Motivation: TTransportHost peer capacity shared by the client and server fixtures in the end-to-end demo. */
constexpr std::size_t TransportHostPeerCapacity = 4;

/** Motivation: TTransportHost per-message scratch capacity the demo's short application payload must stay within. */
constexpr std::size_t TransportHostScratchBytes = 256;

/** Motivation: Application payload delivered after the handshake; kept short so the host's 256-byte scratch is never exceeded. */
const std::array<std::uint8_t, 4> AppPayload = {0x10, 0x20, 0x30, 0x40};

/** Motivation: Upper bound on handshake pump iterations before the test gives up waiting for Connected. */
constexpr int HandshakeIterationCap = 20;

/** Motivation: Milliseconds `PollReadable` blocks waiting for a readable datagram during the handshake. */
constexpr int HandshakePollTimeoutMilliseconds = 500;

/** Motivation: Distinct byte values carried by the four-byte application payload. */
constexpr std::uint8_t AppPayloadByte0 = 0x10;
constexpr std::uint8_t AppPayloadByte1 = 0x20;

/** Motivation: Application-message channel the client uses to address the server's handler. */
constexpr std::uint8_t ApplicationChannel = 1;

/**
 * Motivation: Drives one client and one server through the Hello/Welcome handshake over UDP, bounded by the
 *   iteration cap.
 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
 */
void PumpHandshake(
	FHostWifiDevice& ServerDevice,
	FHostWifiDevice& ClientDevice,
	TTransportHost<TransportHostPeerCapacity, TransportHostScratchBytes>& Server,
	TTransportHost<TransportHostPeerCapacity, TransportHostScratchBytes>& Client,
	const TimePointMilliseconds Now) noexcept
{
	for (int Iteration = 0; Iteration < HandshakeIterationCap; ++Iteration)
	{
		(void)Client.PumpSend(Now);
		const bool bServerReadable = ServerDevice.PollReadable(HandshakePollTimeoutMilliseconds);
		if (bServerReadable)
		{
			(void)Server.PumpReceive(Now);
		}
		(void)Server.PumpSend(Now);
		const bool bClientReadable = ClientDevice.PollReadable(HandshakePollTimeoutMilliseconds);
		if (bClientReadable)
		{
			(void)Client.PumpReceive(Now);
		}
		const bool bClientConnected = Client.GetState() == ETransportHostState::Connected;
		if (bClientConnected)
		{
			break;
		}
	}
}

} // namespace

/**
 * Motivation: Scenario: Drive a TTransportHost client and dedicated server through the Hello/Welcome handshake
 *   over real UDP localhost, then send one application message.
 * Responsibilities: Expected: The client reaches Connected; the server admits exactly one peer; the server handler
 *   observes one message on the requested channel carrying the sent payload's first byte.
 */
MW_TEST_CASE(HostTransportHandshakeAndApplicationMessageCrossRealUdp)
{
	// Arrange
	FHostWifiDevice ServerDevice(0);
	FHostWifiDevice ClientDevice(0);
	MW_EXPECT_TRUE(Test, ServerDevice.IsOpen(), "The server UDP device opened");
	MW_EXPECT_TRUE(Test, ClientDevice.IsOpen(), "The client UDP device opened");

	TTransportHost<TransportHostPeerCapacity, TransportHostScratchBytes> Server(ServerDevice);
	TTransportHost<TransportHostPeerCapacity, TransportHostScratchBytes> Client(ClientDevice);
	FTransportHostConfig ServerConfig{};
	MW_EXPECT_EQ(
		Test, ETransportResult::Success, Server.Configure(ENetworkMode::DedicatedServer, ServerConfig), "The server configures as dedicated");
	FTransportHostConfig ClientConfig{};
	ClientConfig.ServerAddress = MakeUdpAddress(OctetA, OctetB, OctetC, OctetD, ServerDevice.BoundPort());
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Client.Configure(ENetworkMode::Client, ClientConfig),
		"The client configures against the server's UDP address");

	FHostTimeSource Clock;
	const TimePointMilliseconds Now = Clock.Now();
	MW_EXPECT_EQ(Test, ETransportResult::Success, Server.Start(Now), "The server starts listening");
	MW_EXPECT_EQ(Test, ETransportResult::Success, Client.Start(Now), "The client starts connecting");

	FServerCapture Capture{};
	TTransportHost<TransportHostPeerCapacity, TransportHostScratchBytes>::FMessageHandlerBinding Binding;
	Binding.Bind(
		[&Capture](const FPeerId From, const std::uint8_t Channel, TSpan<const std::uint8_t> Payload) noexcept
		{
			++Capture.Count;
			Capture.From = From;
			Capture.Channel = Channel;
			Capture.FirstByte = Payload.Size() > 0 ? Payload[0] : std::uint8_t{0};
		});
	FDelegateHandle ServerHandle{};
	MW_EXPECT_EQ(Test, EDelegateResult::Success, Server.AddMessageHandler(std::move(Binding), ServerHandle), "The server handler binds");

	// Act
	PumpHandshake(ServerDevice, ClientDevice, Server, Client, Now);
	MW_EXPECT_EQ(Test, ETransportHostState::Connected, Client.GetState(), "The client reached Connected over UDP");
	MW_EXPECT_EQ(Test, std::size_t{1}, Server.ActivePeerCount(), "The server admitted exactly one peer");

	const FPeerId ServerPeer = Client.GetServerPeer();
	MW_EXPECT_TRUE(Test, ServerPeer.IsValid(), "The client resolves its server peer after connecting");
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Client.SendTo(ServerPeer, ApplicationChannel, TSpan<const std::uint8_t>(AppPayload.data(), AppPayload.size())),
		"The client queues one channel-1 message to the server");
	(void)Client.PumpSend(Now);
	const bool bServerDelivered = ServerDevice.PollReadable(HandshakePollTimeoutMilliseconds);
	if (bServerDelivered)
	{
		(void)Server.PumpReceive(Now);
	}

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{1}, Capture.Count, "The server handler observed exactly one message");
	MW_EXPECT_EQ(Test, ApplicationChannel, Capture.Channel, "The message arrived on the requested channel");
	MW_EXPECT_EQ(Test, AppPayloadByte0, Capture.FirstByte, "The message carried the sent payload's first byte");
}
