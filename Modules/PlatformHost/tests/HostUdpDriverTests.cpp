#include "TestSupport.h"

#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetDriver.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/PlatformHost/HostUdpDriver.h>
#include <MicroWorld/PlatformHost/UdpAddress.h>
#include <MicroWorld/Time.h>
#include <MicroWorld/Containers/Span.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

using namespace MicroWorld;

/** Loopback prefix reused by every test's target address. */
constexpr std::uint8_t OctetA = 127;
constexpr std::uint8_t OctetB = 0;
constexpr std::uint8_t OctetC = 0;
constexpr std::uint8_t OctetD = 1;

/** One ready byte sequence that proves the full datagram round trips unchanged. */
const std::array<std::uint8_t, 8> SamplePayload = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};

/** Waits up to ~1s for a datagram to be readable, then asserts success of that wait. */
void ExpectReadable(MicroWorld::Tests::FTestContext& Test, const FHostUdpDriver& Driver, const char* const Message) noexcept
{
	bool Ready = false;
	for (int Attempt = 0; Attempt < 20; ++Attempt)
	{
		if (Driver.PollReadable(50))
		{
			Ready = true;
			break;
		}
	}
	MW_EXPECT_TRUE(Test, Ready, Message);
}

} // namespace

/** Two ephemeral-port drivers open, are distinct, and both report a nonzero bound port. */
MW_TEST_CASE(HostUdpDriverOpensTwoDistinctEphemeralSockets)
{
	FHostUdpDriver DriverA(0);
	FHostUdpDriver DriverB(0);
	MW_EXPECT_TRUE(Test, DriverA.IsOpen(), "DriverA opened a usable socket");
	MW_EXPECT_TRUE(Test, DriverB.IsOpen(), "DriverB opened a usable socket");
	MW_EXPECT_TRUE(Test, DriverA.BoundPort() != 0, "DriverA reports a nonzero bound port");
	MW_EXPECT_TRUE(Test, DriverB.BoundPort() != 0, "DriverB reports a nonzero bound port");
	MW_EXPECT_TRUE(Test, DriverA.BoundPort() != DriverB.BoundPort(), "Two drivers bind distinct ports");
	MW_EXPECT_EQ(Test, std::size_t{1200}, DriverA.MaxPacketBytes(), "MaxPacketBytes reports the documented UDP bound");
}

/** A one-packet send from A to B round trips the bytes and the sender address. */
MW_TEST_CASE(HostUdpDriverDeliversOnePacketBetweenTwoSockets)
{
	FHostUdpDriver DriverA(0);
	FHostUdpDriver DriverB(0);
	const FNetAddress ToB = MakeUdpAddress(OctetA, OctetB, OctetC, OctetD, DriverB.BoundPort());
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		DriverA.TrySend(ToB, TSpan<const std::uint8_t>(SamplePayload.data(), SamplePayload.size())),
		"Sending to B's bound port succeeds");
	ExpectReadable(Test, DriverB, "B observes a readable datagram within a bounded wait");

	std::array<std::uint8_t, 256> Destination{};
	for (std::size_t Index = 0; Index < Destination.size(); ++Index)
	{
		Destination[Index] = static_cast<std::uint8_t>(0xFF - Index);
	}
	FNetAddress OutFrom{0x42};
	FNetReceiveResult OutResult{0xEE};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		DriverB.TryReceive(OutFrom, TSpan<std::uint8_t>(Destination.data(), Destination.size()), OutResult),
		"Receiving the queued datagram succeeds");
	MW_EXPECT_EQ(Test, SamplePayload.size(), OutResult.BytesReceived, "The received byte count matches the sent payload");
	bool BytesMatch = OutResult.BytesReceived == SamplePayload.size();
	for (std::size_t Index = 0; BytesMatch && Index < SamplePayload.size(); ++Index)
	{
		if (Destination[Index] != SamplePayload[Index])
		{
			BytesMatch = false;
		}
	}
	MW_EXPECT_TRUE(Test, BytesMatch, "The received bytes match the sent payload");
	MW_EXPECT_EQ(Test, MakeUdpAddress(OctetA, OctetB, OctetC, OctetD, DriverA.BoundPort()), OutFrom, "The sender address encodes A's bound port");
}

/** A receive on an empty queue reports Unavailable without waiting. */
MW_TEST_CASE(HostUdpDriverReceiveOnEmptyQueueIsUnavailable)
{
	FHostUdpDriver Driver(0);
	std::array<std::uint8_t, 32> Destination{};
	FNetAddress OutFrom{};
	OutFrom.Size = 0x42;
	FNetReceiveResult OutResult{0xEE};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Unavailable,
		Driver.TryReceive(OutFrom, TSpan<std::uint8_t>(Destination.data(), Destination.size()), OutResult),
		"An empty queue reports Unavailable immediately");
	MW_EXPECT_EQ(Test, std::uint8_t{0x42}, OutFrom.Size, "Unavailable leaves the sender sentinel unchanged");
	MW_EXPECT_EQ(Test, std::size_t{0xEE}, OutResult.BytesReceived, "Unavailable leaves the byte-count sentinel unchanged");
}

/** TrySend rejects a null span with nonzero length, an oversize packet, and a non-UDP address. */
MW_TEST_CASE(HostUdpDriverTrySendRejectsInvalidArguments)
{
	FHostUdpDriver Driver(0);
	const FNetAddress ToB = MakeUdpAddress(OctetA, OctetB, OctetC, OctetD, 9);
	MW_EXPECT_EQ(Test, ENetResult::Invalid, Driver.TrySend(ToB, TSpan<const std::uint8_t>(nullptr, 4)), "A null span with nonzero length is Invalid");
	std::array<std::uint8_t, 1201> Oversize{};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Invalid,
		Driver.TrySend(ToB, TSpan<const std::uint8_t>(Oversize.data(), Oversize.size())),
		"A packet larger than MaxPacketBytes is Invalid");
	MW_EXPECT_EQ(
		Test,
		ENetResult::Invalid,
		Driver.TrySend(MakeLoopbackAddress(3), TSpan<const std::uint8_t>(SamplePayload.data(), SamplePayload.size())),
		"A non-UDP address is Invalid");
}

/** A receive into a too-small destination reports Full and leaves the datagram queued for a larger read. */
MW_TEST_CASE(HostUdpDriverFullReceiveStaysTransactional)
{
	FHostUdpDriver DriverA(0);
	FHostUdpDriver DriverB(0);
	const FNetAddress ToB = MakeUdpAddress(OctetA, OctetB, OctetC, OctetD, DriverB.BoundPort());
	std::array<std::uint8_t, 16> LargePayload{};
	for (std::size_t Index = 0; Index < LargePayload.size(); ++Index)
	{
		LargePayload[Index] = static_cast<std::uint8_t>(Index + 1);
	}
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		DriverA.TrySend(ToB, TSpan<const std::uint8_t>(LargePayload.data(), LargePayload.size())),
		"Sending the oversized-for-small-dest datagram succeeds");
	ExpectReadable(Test, DriverB, "B observes the queued datagram");

	// Pre-fill the too-small destination with a known sentinel pattern so a Full
	// result can be proven to leave every caller-owned byte untouched.
	std::array<std::uint8_t, 4> SmallDestination{};
	for (std::size_t Index = 0; Index < SmallDestination.size(); ++Index)
	{
		SmallDestination[Index] = 0xAB;
	}
	FNetAddress OutFrom{};
	OutFrom.Size = 0x42;
	FNetReceiveResult OutResult{0xEE};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Full,
		DriverB.TryReceive(OutFrom, TSpan<std::uint8_t>(SmallDestination.data(), SmallDestination.size()), OutResult),
		"A too-small destination reports Full");
	MW_EXPECT_EQ(Test, std::uint8_t{0x42}, OutFrom.Size, "Full leaves the sender sentinel unchanged");
	MW_EXPECT_EQ(Test, std::size_t{0xEE}, OutResult.BytesReceived, "Full leaves the byte-count sentinel unchanged");
	bool DestinationUntouched = true;
	for (std::size_t Index = 0; Index < SmallDestination.size(); ++Index)
	{
		if (SmallDestination[Index] != 0xAB)
		{
			DestinationUntouched = false;
		}
	}
	MW_EXPECT_TRUE(Test, DestinationUntouched, "Full leaves every caller-owned destination byte unchanged");

	std::array<std::uint8_t, 256> LargeDestination{};
	FNetAddress OutFromSecond{};
	FNetReceiveResult OutResultSecond{0xEE};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		DriverB.TryReceive(OutFromSecond, TSpan<std::uint8_t>(LargeDestination.data(), LargeDestination.size()), OutResultSecond),
		"A larger destination receives the datagram that Full did not consume");
	MW_EXPECT_EQ(Test, LargePayload.size(), OutResultSecond.BytesReceived, "The queued datagram delivered its full length");
	bool BytesMatch = OutResultSecond.BytesReceived == LargePayload.size();
	for (std::size_t Index = 0; BytesMatch && Index < LargePayload.size(); ++Index)
	{
		if (LargeDestination[Index] != LargePayload[Index])
		{
			BytesMatch = false;
		}
	}
	MW_EXPECT_TRUE(Test, BytesMatch, "The queued datagram delivered the exact sent bytes");
}
