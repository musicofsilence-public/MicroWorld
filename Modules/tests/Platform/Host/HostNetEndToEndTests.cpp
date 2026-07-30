#include "TestSupport.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Transport/NetAddress.h>
#include <MicroWorld/Transport/NetDriver.h>
#include <MicroWorld/Transport/NetHost.h>
#include <MicroWorld/Transport/NetResult.h>
#include <MicroWorld/Platform/Host/HostTimeSource.h>
#include <MicroWorld/Platform/Host/HostUdpDriver.h>
#include <MicroWorld/Platform/Host/UdpAddress.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Core/Delegates/Delegate.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

using namespace MicroWorld;

/** Records the last application message the server handler observed. */
struct FServerCapture
{
	/** Number of messages the handler has observed; zero means it never ran. */
	std::size_t Count{0};

	/** Sender identity from the most recent dispatch. */
	FPeerId From{};

	/** Channel from the most recent dispatch. */
	std::uint8_t Channel{0};

	/** First payload byte from the most recent dispatch, or zero for an empty payload. */
	std::uint8_t FirstByte{0};
};

/** Host loopback octet prefix reused by every endpoint address in the demo. */
constexpr std::uint8_t OctetA = 127;
constexpr std::uint8_t OctetB = 0;
constexpr std::uint8_t OctetC = 0;
constexpr std::uint8_t OctetD = 1;

/** TNetHost peer capacity shared by the client and server fixtures in the end-to-end demo. */
constexpr std::size_t NetHostPeerCapacity = 4;

/** TNetHost per-message scratch capacity the demo's short application payload must stay within. */
constexpr std::size_t NetHostScratchBytes = 256;

/** Application payload delivered after the handshake; kept short so the host's 256-byte scratch is never exceeded. */
const std::array<std::uint8_t, 4> AppPayload = {0x10, 0x20, 0x30, 0x40};

/** Upper bound on handshake pump iterations before the test gives up waiting for Connected. */
constexpr int HandshakeIterationCap = 20;

/** Milliseconds `PollReadable` blocks waiting for a readable datagram during the handshake. */
constexpr int HandshakePollTimeoutMilliseconds = 500;

/** Distinct byte values carried by the four-byte application payload. */
constexpr std::uint8_t AppPayloadByte0 = 0x10;
constexpr std::uint8_t AppPayloadByte1 = 0x20;

/** Application-message channel the client uses to address the server's handler. */
constexpr std::uint8_t ApplicationChannel = 1;

/** Drives one client and one server through the Hello/Welcome handshake over UDP, bounded by the iteration cap. */
void PumpHandshake(
	FHostUdpDriver& ServerDriver,
	FHostUdpDriver& ClientDriver,
	TNetHost<NetHostPeerCapacity, NetHostScratchBytes>& Server,
	TNetHost<NetHostPeerCapacity, NetHostScratchBytes>& Client,
	const TimePointMilliseconds Now) noexcept
{
	for (int Iteration = 0; Iteration < HandshakeIterationCap; ++Iteration)
	{
		(void)Client.PumpSend(Now);
		const bool bServerReadable = ServerDriver.PollReadable(HandshakePollTimeoutMilliseconds);
		if (bServerReadable)
		{
			(void)Server.PumpReceive(Now);
		}
		(void)Server.PumpSend(Now);
		const bool bClientReadable = ClientDriver.PollReadable(HandshakePollTimeoutMilliseconds);
		if (bClientReadable)
		{
			(void)Client.PumpReceive(Now);
		}
		const bool bClientConnected = Client.GetState() == ENetHostState::Connected;
		if (bClientConnected)
		{
			break;
		}
	}
}

} // namespace

/**
 * Scenario: Drive a TNetHost client and dedicated server through the Hello/Welcome handshake over real UDP localhost, then send one application
 * message. Expected: The client reaches Connected; the server admits exactly one peer; the server handler observes one message on the requested
 * channel carrying the sent payload's first byte.
 */
MW_TEST_CASE(HostNetHandshakeAndApplicationMessageCrossRealUdp)
{
	// Arrange
	FHostUdpDriver ServerDriver(0);
	FHostUdpDriver ClientDriver(0);
	MW_EXPECT_TRUE(Test, ServerDriver.IsOpen(), "The server UDP driver opened");
	MW_EXPECT_TRUE(Test, ClientDriver.IsOpen(), "The client UDP driver opened");

	TNetHost<NetHostPeerCapacity, NetHostScratchBytes> Server(ServerDriver);
	TNetHost<NetHostPeerCapacity, NetHostScratchBytes> Client(ClientDriver);
	FNetHostConfig ServerConfig{};
	MW_EXPECT_EQ(Test, ENetResult::Success, Server.Configure(ENetMode::DedicatedServer, ServerConfig), "The server configures as dedicated");
	FNetHostConfig ClientConfig{};
	ClientConfig.ServerAddress = MakeUdpAddress(OctetA, OctetB, OctetC, OctetD, ServerDriver.BoundPort());
	MW_EXPECT_EQ(
		Test, ENetResult::Success, Client.Configure(ENetMode::Client, ClientConfig), "The client configures against the server's UDP address");

	FHostTimeSource Clock;
	const TimePointMilliseconds Now = Clock.Now();
	MW_EXPECT_EQ(Test, ENetResult::Success, Server.Start(Now), "The server starts listening");
	MW_EXPECT_EQ(Test, ENetResult::Success, Client.Start(Now), "The client starts connecting");

	FServerCapture Capture{};
	TNetHost<NetHostPeerCapacity, NetHostScratchBytes>::FMessageHandlerBinding Binding;
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
	PumpHandshake(ServerDriver, ClientDriver, Server, Client, Now);
	MW_EXPECT_EQ(Test, ENetHostState::Connected, Client.GetState(), "The client reached Connected over UDP");
	MW_EXPECT_EQ(Test, std::size_t{1}, Server.ActivePeerCount(), "The server admitted exactly one peer");

	const FPeerId ServerPeer = Client.GetServerPeer();
	MW_EXPECT_TRUE(Test, ServerPeer.IsValid(), "The client resolves its server peer after connecting");
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Client.SendTo(ServerPeer, ApplicationChannel, TSpan<const std::uint8_t>(AppPayload.data(), AppPayload.size())),
		"The client queues one channel-1 message to the server");
	(void)Client.PumpSend(Now);
	const bool bServerDelivered = ServerDriver.PollReadable(HandshakePollTimeoutMilliseconds);
	if (bServerDelivered)
	{
		(void)Server.PumpReceive(Now);
	}

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{1}, Capture.Count, "The server handler observed exactly one message");
	MW_EXPECT_EQ(Test, ApplicationChannel, Capture.Channel, "The message arrived on the requested channel");
	MW_EXPECT_EQ(Test, AppPayloadByte0, Capture.FirstByte, "The message carried the sent payload's first byte");
}
