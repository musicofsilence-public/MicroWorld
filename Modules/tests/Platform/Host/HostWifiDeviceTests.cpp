#include "TestSupport.h"

#include <MicroWorld/Transport/DeviceAddress.h>
#include <MicroWorld/Transport/Device.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Platform/Host/HostWifiDevice.h>
#include <MicroWorld/Platform/Host/UdpAddress.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Core/Containers/Span.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

using namespace MicroWorld::Core;
using namespace MicroWorld::Transport;
using namespace MicroWorld::Transport::Address;
using namespace MicroWorld::Transport::Device;
using MicroWorld::Platform::Host::FHostWifiDevice;

/** Motivation: Loopback prefix reused by every test's target address. */
constexpr std::uint8_t OctetA = 127;
constexpr std::uint8_t OctetB = 0;
constexpr std::uint8_t OctetC = 0;
constexpr std::uint8_t OctetD = 1;

/** Motivation: One ready byte sequence that proves the full datagram round trips unchanged. */
const std::array<std::uint8_t, 8> SamplePayload = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};

/** Motivation: Sentinel sender-address length proving a no-receive path leaves the caller output untouched. */
constexpr std::uint8_t SentinelAddressSize = 0x42;

/** Motivation: Sentinel byte count proving a no-receive path leaves the receive-result struct untouched. */
constexpr std::size_t SentinelByteCount = 0xEE;

/** Motivation: Pre-fill pattern proving a Full receive leaves every caller-owned destination byte untouched. */
constexpr std::uint8_t FullReceiveFillByte = 0xAB;

/** Motivation: Documented per-datagram byte bound reported by the host UDP device. */
constexpr std::size_t HostUdpMaxPacketBytes = 1200;

/** Motivation: One byte beyond the documented UDP bound, proving oversize sends are rejected. */
constexpr std::size_t OversizePacketBytes = HostUdpMaxPacketBytes + 1;

/** Motivation: Distinct port that does not collide with any device's ephemeral binding, proving address validation. */
constexpr std::uint16_t UnusedFixedPort = 9;

/**
 * Motivation: Waits up to ~1s for a datagram to be readable, then asserts success of that wait.
 * Responsibilities: Perform only the documented mutation and leave unrelated state untouched.
 */
void ExpectReadable(MicroWorld::Tests::FTestContext& Test, const FHostWifiDevice& Device, const char* const Message) noexcept
{
	bool Ready = false;
	for (int Attempt = 0; Attempt < 20; ++Attempt)
	{
		if (Device.PollReadable(50))
		{
			Ready = true;
			break;
		}
	}
	MW_EXPECT_TRUE(Test, Ready, Message);
}

} // namespace

/**
 * Motivation: Open two UDP devices each requesting an ephemeral port.
 * Responsibilities: Both devices open usable sockets; their bound ports are distinct and nonzero; MaxPacketBytes reports
 *   the documented UDP bound.
 */
MW_TEST_CASE(HostWifiDeviceOpensTwoDistinctEphemeralSockets)
{
	// Arrange
	FHostWifiDevice DeviceA(0);
	FHostWifiDevice DeviceB(0);

	// Assert
	MW_EXPECT_TRUE(Test, DeviceA.IsOpen(), "DeviceA opened a usable socket");
	MW_EXPECT_TRUE(Test, DeviceB.IsOpen(), "DeviceB opened a usable socket");
	MW_EXPECT_TRUE(Test, DeviceA.BoundPort() != 0, "DeviceA reports a nonzero bound port");
	MW_EXPECT_TRUE(Test, DeviceB.BoundPort() != 0, "DeviceB reports a nonzero bound port");
	MW_EXPECT_TRUE(Test, DeviceA.BoundPort() != DeviceB.BoundPort(), "Two devices bind distinct ports");
	MW_EXPECT_EQ(Test, HostUdpMaxPacketBytes, DeviceA.MaxPacketBytes(), "MaxPacketBytes reports the documented UDP bound");
}

/**
 * Motivation: Send one datagram from one bound socket to another's bound port, then receive on the second.
 * Responsibilities: The received bytes match the sent payload; the reported byte count matches the payload length; the
 *   sender address encodes the first.
 */
MW_TEST_CASE(HostWifiDeviceDeliversOnePacketBetweenTwoSockets)
{
	// Arrange
	FHostWifiDevice DeviceA(0);
	FHostWifiDevice DeviceB(0);
	const FDeviceAddress ToB = MakeUdpAddress(OctetA, OctetB, OctetC, OctetD, DeviceB.BoundPort());

	// Act
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		DeviceA.TrySend(ToB, TSpan<const std::uint8_t>(SamplePayload.data(), SamplePayload.size())),
		"Sending to B's bound port succeeds");
	ExpectReadable(Test, DeviceB, "B observes a readable datagram within a bounded wait");

	std::array<std::uint8_t, 256> Destination{};
	for (std::size_t Index = 0; Index < Destination.size(); ++Index)
	{
		Destination[Index] = static_cast<std::uint8_t>(0xFF - Index);
	}
	FDeviceAddress OutFrom{SentinelAddressSize};
	FReceiveResult OutResult{SentinelByteCount};

	// Assert
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		DeviceB.TryReceive(OutFrom, TSpan<std::uint8_t>(Destination.data(), Destination.size()), OutResult),
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
	MW_EXPECT_EQ(Test, MakeUdpAddress(OctetA, OctetB, OctetC, OctetD, DeviceA.BoundPort()), OutFrom, "The sender address encodes A's bound port");
}

/**
 * Motivation: Call TryReceive on a device whose queue is empty, with caller outputs pre-set to sentinel values.
 * Responsibilities: TryReceive returns Unavailable immediately without blocking; both caller-owned sentinels are left
 *   unchanged.
 */
MW_TEST_CASE(HostWifiDeviceReceiveOnEmptyQueueIsUnavailable)
{
	// Arrange
	FHostWifiDevice Device(0);
	std::array<std::uint8_t, 32> Destination{};
	FDeviceAddress OutFrom{};
	OutFrom.Size = SentinelAddressSize;
	FReceiveResult OutResult{SentinelByteCount};

	// Act
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Unavailable,
		Device.TryReceive(OutFrom, TSpan<std::uint8_t>(Destination.data(), Destination.size()), OutResult),
		"An empty queue reports Unavailable immediately");

	// Assert
	MW_EXPECT_EQ(Test, SentinelAddressSize, OutFrom.Size, "Unavailable leaves the sender sentinel unchanged");
	MW_EXPECT_EQ(Test, SentinelByteCount, OutResult.BytesReceived, "Unavailable leaves the byte-count sentinel unchanged");
}

/**
 * Motivation: Call TrySend with a null span of nonzero length, an oversize packet, and a non-UDP destination
 *   address.
 * Responsibilities: Each invalid argument is rejected as Invalid without sending.
 */
MW_TEST_CASE(HostWifiDeviceTrySendRejectsInvalidArguments)
{
	// Arrange
	FHostWifiDevice Device(0);
	const FDeviceAddress ToB = MakeUdpAddress(OctetA, OctetB, OctetC, OctetD, UnusedFixedPort);
	std::array<std::uint8_t, OversizePacketBytes> Oversize{};

	// Act and Assert
	MW_EXPECT_EQ(
		Test, ETransportResult::Invalid, Device.TrySend(ToB, TSpan<const std::uint8_t>(nullptr, 4)), "A null span with nonzero length is Invalid");
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Invalid,
		Device.TrySend(ToB, TSpan<const std::uint8_t>(Oversize.data(), Oversize.size())),
		"A packet larger than MaxPacketBytes is Invalid");
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Invalid,
		Device.TrySend(MakeLoopbackAddress(3), TSpan<const std::uint8_t>(SamplePayload.data(), SamplePayload.size())),
		"A non-UDP address is Invalid");
}

/**
 * Motivation: Send a datagram larger than a too-small receive destination, then retry with a larger destination.
 * Responsibilities: The small receive reports Full with every caller output untouched; the larger retry then delivers
 *   the queued datagram intact.
 */
MW_TEST_CASE(HostWifiDeviceFullReceiveStaysTransactional)
{
	// Arrange
	FHostWifiDevice DeviceA(0);
	FHostWifiDevice DeviceB(0);
	const FDeviceAddress ToB = MakeUdpAddress(OctetA, OctetB, OctetC, OctetD, DeviceB.BoundPort());
	std::array<std::uint8_t, 16> LargePayload{};
	for (std::size_t Index = 0; Index < LargePayload.size(); ++Index)
	{
		LargePayload[Index] = static_cast<std::uint8_t>(Index + 1);
	}

	// Act
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		DeviceA.TrySend(ToB, TSpan<const std::uint8_t>(LargePayload.data(), LargePayload.size())),
		"Sending the oversized-for-small-dest datagram succeeds");
	ExpectReadable(Test, DeviceB, "B observes the queued datagram");

	// Pre-fill the too-small destination with a known sentinel pattern so a Full
	// result can be proven to leave every caller-owned byte untouched.
	std::array<std::uint8_t, 4> SmallDestination{};
	for (std::size_t Index = 0; Index < SmallDestination.size(); ++Index)
	{
		SmallDestination[Index] = FullReceiveFillByte;
	}
	FDeviceAddress OutFrom{};
	OutFrom.Size = SentinelAddressSize;
	FReceiveResult OutResult{SentinelByteCount};

	// Assert the Full path leaves caller outputs untouched.
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Full,
		DeviceB.TryReceive(OutFrom, TSpan<std::uint8_t>(SmallDestination.data(), SmallDestination.size()), OutResult),
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
	FDeviceAddress OutFromSecond{};
	FReceiveResult OutResultSecond{SentinelByteCount};
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		DeviceB.TryReceive(OutFromSecond, TSpan<std::uint8_t>(LargeDestination.data(), LargeDestination.size()), OutResultSecond),
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
