#include "TestSupport.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetDriver.h>
#include <MicroWorld/Net/NetHost.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/PlatformHost/HostTimeSource.h>
#include <MicroWorld/PlatformHost/HostUdpDriver.h>
#include <MicroWorld/PlatformHost/UdpAddress.h>
#include <MicroWorld/Time.h>
#include <MicroWorld/Delegates/Delegate.h>

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

/** Application payload delivered after the handshake; kept short so the host's 256-byte scratch is never exceeded. */
const std::array<std::uint8_t, 4> AppPayload = {0x10, 0x20, 0x30, 0x40};

/** Drives one client and one server through the Hello/Welcome handshake over UDP, bounded by the iteration cap. */
void PumpHandshake(
	FHostUdpDriver& ServerDriver,
	FHostUdpDriver& ClientDriver,
	TNetHost<4, 256>& Server,
	TNetHost<4, 256>& Client,
	const TimePointMilliseconds Now) noexcept
{
	for (int Iteration = 0; Iteration < 20; ++Iteration)
	{
		(void)Client.PumpSend(Now);
		if (ServerDriver.PollReadable(500))
		{
			(void)Server.PumpReceive(Now);
		}
		(void)Server.PumpSend(Now);
		if (ClientDriver.PollReadable(500))
		{
			(void)Client.PumpReceive(Now);
		}
		if (Client.GetState() == ENetHostState::Connected)
		{
			break;
		}
	}
}

} // namespace

/** A client and server exchange the TNetHost handshake and one app message over real UDP localhost. */
MW_TEST_CASE(HostNetHandshakeAndApplicationMessageCrossRealUdp)
{
	FHostUdpDriver ServerDriver(0);
	FHostUdpDriver ClientDriver(0);
	MW_EXPECT_TRUE(Test, ServerDriver.IsOpen(), "The server UDP driver opened");
	MW_EXPECT_TRUE(Test, ClientDriver.IsOpen(), "The client UDP driver opened");

	TNetHost<4, 256> Server(ServerDriver);
	TNetHost<4, 256> Client(ClientDriver);
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
	TNetHost<4, 256>::FMessageHandlerBinding Binding;
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

	PumpHandshake(ServerDriver, ClientDriver, Server, Client, Now);
	MW_EXPECT_EQ(Test, ENetHostState::Connected, Client.GetState(), "The client reached Connected over UDP");
	MW_EXPECT_EQ(Test, std::size_t{1}, Server.ActivePeerCount(), "The server admitted exactly one peer");

	const FPeerId ServerPeer = Client.GetServerPeer();
	MW_EXPECT_TRUE(Test, ServerPeer.IsValid(), "The client resolves its server peer after connecting");
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Client.SendTo(ServerPeer, 1, TSpan<const std::uint8_t>(AppPayload.data(), AppPayload.size())),
		"The client queues one channel-1 message to the server");
	(void)Client.PumpSend(Now);
	if (ServerDriver.PollReadable(500))
	{
		(void)Server.PumpReceive(Now);
	}
	MW_EXPECT_EQ(Test, std::size_t{1}, Capture.Count, "The server handler observed exactly one message");
	MW_EXPECT_EQ(Test, std::uint8_t{1}, Capture.Channel, "The message arrived on the requested channel");
	MW_EXPECT_EQ(Test, std::uint8_t{0x10}, Capture.FirstByte, "The message carried the sent payload's first byte");
}
