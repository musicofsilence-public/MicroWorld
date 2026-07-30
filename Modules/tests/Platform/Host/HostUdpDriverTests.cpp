#include "TestSupport.h"

#include <MicroWorld/Transport/NetAddress.h>
#include <MicroWorld/Transport/NetDriver.h>
#include <MicroWorld/Transport/NetResult.h>
#include <MicroWorld/Platform/Host/HostUdpDriver.h>
#include <MicroWorld/Platform/Host/UdpAddress.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Core/Containers/Span.h>

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

/** Sentinel sender-address length proving a no-receive path leaves the caller output untouched. */
constexpr std::uint8_t SentinelAddressSize = 0x42;

/** Sentinel byte count proving a no-receive path leaves the receive-result struct untouched. */
constexpr std::size_t SentinelByteCount = 0xEE;

/** Pre-fill pattern proving a Full receive leaves every caller-owned destination byte untouched. */
constexpr std::uint8_t FullReceiveFillByte = 0xAB;

/** Documented per-datagram byte bound reported by the host UDP driver. */
constexpr std::size_t HostUdpMaxPacketBytes = 1200;

/** One byte beyond the documented UDP bound, proving oversize sends are rejected. */
constexpr std::size_t OversizePacketBytes = HostUdpMaxPacketBytes + 1;

/** Distinct port that does not collide with any driver's ephemeral binding, proving address validation. */
constexpr std::uint16_t UnusedFixedPort = 9;

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

/**
 * Scenario: Open two UDP drivers each requesting an ephemeral port.
 * Expected: Both drivers open usable sockets; their bound ports are distinct and nonzero; MaxPacketBytes reports the documented UDP bound.
 */
MW_TEST_CASE(HostUdpDriverOpensTwoDistinctEphemeralSockets)
{
	// Arrange
	FHostUdpDriver DriverA(0);
	FHostUdpDriver DriverB(0);

	// Assert
	MW_EXPECT_TRUE(Test, DriverA.IsOpen(), "DriverA opened a usable socket");
	MW_EXPECT_TRUE(Test, DriverB.IsOpen(), "DriverB opened a usable socket");
	MW_EXPECT_TRUE(Test, DriverA.BoundPort() != 0, "DriverA reports a nonzero bound port");
	MW_EXPECT_TRUE(Test, DriverB.BoundPort() != 0, "DriverB reports a nonzero bound port");
	MW_EXPECT_TRUE(Test, DriverA.BoundPort() != DriverB.BoundPort(), "Two drivers bind distinct ports");
	MW_EXPECT_EQ(Test, HostUdpMaxPacketBytes, DriverA.MaxPacketBytes(), "MaxPacketBytes reports the documented UDP bound");
}

/**
 * Scenario: Send one datagram from one bound socket to another's bound port, then receive on the second.
 * Expected: The received bytes match the sent payload; the reported byte count matches the payload length; the sender address encodes the first
 * socket's bound port.
 */
MW_TEST_CASE(HostUdpDriverDeliversOnePacketBetweenTwoSockets)
{
	// Arrange
	FHostUdpDriver DriverA(0);
	FHostUdpDriver DriverB(0);
	const FNetAddress ToB = MakeUdpAddress(OctetA, OctetB, OctetC, OctetD, DriverB.BoundPort());

	// Act
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
	FNetAddress OutFrom{SentinelAddressSize};
	FNetReceiveResult OutResult{SentinelByteCount};

	// Assert
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

/**
 * Scenario: Call TryReceive on a driver whose queue is empty, with caller outputs pre-set to sentinel values.
 * Expected: TryReceive returns Unavailable immediately without blocking; both caller-owned sentinels are left unchanged.
 */
MW_TEST_CASE(HostUdpDriverReceiveOnEmptyQueueIsUnavailable)
{
	// Arrange
	FHostUdpDriver Driver(0);
	std::array<std::uint8_t, 32> Destination{};
	FNetAddress OutFrom{};
	OutFrom.Size = SentinelAddressSize;
	FNetReceiveResult OutResult{SentinelByteCount};

	// Act
	MW_EXPECT_EQ(
		Test,
		ENetResult::Unavailable,
		Driver.TryReceive(OutFrom, TSpan<std::uint8_t>(Destination.data(), Destination.size()), OutResult),
		"An empty queue reports Unavailable immediately");

	// Assert
	MW_EXPECT_EQ(Test, SentinelAddressSize, OutFrom.Size, "Unavailable leaves the sender sentinel unchanged");
	MW_EXPECT_EQ(Test, SentinelByteCount, OutResult.BytesReceived, "Unavailable leaves the byte-count sentinel unchanged");
}

/**
 * Scenario: Call TrySend with a null span of nonzero length, an oversize packet, and a non-UDP destination address.
 * Expected: Each invalid argument is rejected as Invalid without sending.
 */
MW_TEST_CASE(HostUdpDriverTrySendRejectsInvalidArguments)
{
	// Arrange
	FHostUdpDriver Driver(0);
	const FNetAddress ToB = MakeUdpAddress(OctetA, OctetB, OctetC, OctetD, UnusedFixedPort);
	std::array<std::uint8_t, OversizePacketBytes> Oversize{};

	// Act and Assert
	MW_EXPECT_EQ(Test, ENetResult::Invalid, Driver.TrySend(ToB, TSpan<const std::uint8_t>(nullptr, 4)), "A null span with nonzero length is Invalid");
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

/**
 * Scenario: Send a datagram larger than a too-small receive destination, then retry with a larger destination.
 * Expected: The small receive reports Full with every caller output untouched; the larger retry then delivers the queued datagram intact.
 */
MW_TEST_CASE(HostUdpDriverFullReceiveStaysTransactional)
{
	// Arrange
	FHostUdpDriver DriverA(0);
	FHostUdpDriver DriverB(0);
	const FNetAddress ToB = MakeUdpAddress(OctetA, OctetB, OctetC, OctetD, DriverB.BoundPort());
	std::array<std::uint8_t, 16> LargePayload{};
	for (std::size_t Index = 0; Index < LargePayload.size(); ++Index)
	{
		LargePayload[Index] = static_cast<std::uint8_t>(Index + 1);
	}

	// Act
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
		SmallDestination[Index] = FullReceiveFillByte;
	}
	FNetAddress OutFrom{};
	OutFrom.Size = SentinelAddressSize;
	FNetReceiveResult OutResult{SentinelByteCount};

	// Assert the Full path leaves caller outputs untouched.
	MW_EXPECT_EQ(
		Test,
		ENetResult::Full,
		DriverB.TryReceive(OutFrom, TSpan<std::uint8_t>(SmallDestination.data(), SmallDestination.size()), OutResult),
		"A too-small destination reports Full");
	MW_EXPECT_EQ(Test, SentinelAddressSize, OutFrom.Size, "Full leaves the sender sentinel unchanged");
	MW_EXPECT_EQ(Test, SentinelByteCount, OutResult.BytesReceived, "Full leaves the byte-count sentinel unchanged");
	bool DestinationUntouched = true;
	for (std::size_t Index = 0; Index < SmallDestination.size(); ++Index)
	{
		if (SmallDestination[Index] != FullReceiveFillByte)
		{
			DestinationUntouched = false;
		}
	}
	MW_EXPECT_TRUE(Test, DestinationUntouched, "Full leaves every caller-owned destination byte unchanged");

	// Assert the queued datagram survives Full for a larger retry.
	std::array<std::uint8_t, 256> LargeDestination{};
	FNetAddress OutFromSecond{};
	FNetReceiveResult OutResultSecond{SentinelByteCount};
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
