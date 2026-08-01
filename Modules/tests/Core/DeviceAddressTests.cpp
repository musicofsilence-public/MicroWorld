#include "TestSupport.h"

#include <MicroWorld/Core/IO/DeviceAddress.h>

#include <cstdint>

namespace
{

using MicroWorld::Core::FDeviceAddress;
using MicroWorld::Core::MakeBroadcastAddress;
using MicroWorld::Core::MakeLoopbackAddress;

static_assert(MakeBroadcastAddress().Size == 0);
static_assert(MakeLoopbackAddress(7).Size == 1);
static_assert(MakeLoopbackAddress(7).Bytes[0] == 7);

/**
 * Motivation: Proves empty address values supply a stable default route value before any medium assigns bytes.
 * Responsibilities: Confirm two default-constructed addresses compare equal and their inequality remains false.
 */
MW_TEST_CASE(DefaultConstructedDeviceAddressesCompareEqual)
{
	// Arrange
	const FDeviceAddress FirstAddress{};
	const FDeviceAddress SecondAddress{};

	// Act
	const bool bAreEqual = FirstAddress == SecondAddress;
	const bool bAreNotEqual = FirstAddress != SecondAddress;

	// Assert
	MW_EXPECT_TRUE(Test, bAreEqual, "Default-constructed addresses should compare equal");
	MW_EXPECT_TRUE(Test, !bAreNotEqual, "Equal default-constructed addresses should not compare unequal");
}

/**
 * Motivation: Lets a loopback medium route by its port index without inventing a device-specific address type.
 * Responsibilities: Confirm matching port indices compare equal and distinct port indices compare unequal.
 */
MW_TEST_CASE(LoopbackAddressesCompareByPortIndex)
{
	// Arrange
	const FDeviceAddress FirstMatchingAddress = MakeLoopbackAddress(3);
	const FDeviceAddress SecondMatchingAddress = MakeLoopbackAddress(3);
	const FDeviceAddress DifferentAddress = MakeLoopbackAddress(4);

	// Act
	const bool bMatchingAddressesAreEqual = FirstMatchingAddress == SecondMatchingAddress;
	const bool bDifferentAddressesAreNotEqual = FirstMatchingAddress != DifferentAddress;

	// Assert
	MW_EXPECT_TRUE(Test, bMatchingAddressesAreEqual, "Loopback addresses with the same port index should compare equal");
	MW_EXPECT_TRUE(Test, bDifferentAddressesAreNotEqual, "Loopback addresses with different port indices should compare unequal");
}

/**
 * Motivation: Makes the empty value usable as a medium's default route without a separate broadcast flag.
 * Responsibilities: Confirm the broadcast value has no active bytes and differs from a loopback address.
 */
MW_TEST_CASE(BroadcastAddressUsesAnEmptyActiveLength)
{
	// Arrange
	const FDeviceAddress BroadcastAddress = MakeBroadcastAddress();
	const FDeviceAddress LoopbackAddress = MakeLoopbackAddress(0);

	// Act
	const std::uint8_t BroadcastSize = BroadcastAddress.Size;
	const bool bBroadcastDiffersFromLoopback = BroadcastAddress != LoopbackAddress;

	// Assert
	MW_EXPECT_EQ(Test, std::uint8_t{0}, BroadcastSize, "Broadcast address should have no active bytes");
	MW_EXPECT_TRUE(Test, bBroadcastDiffersFromLoopback, "Broadcast address should differ from every loopback address");
}

/**
 * Motivation: Keeps fixed storage opaque so device implementations can ignore unused capacity safely.
 * Responsibilities: Confirm bytes after the active address length do not affect equality.
 */
MW_TEST_CASE(DeviceAddressesIgnoreInactiveTrailingBytes)
{
	// Arrange
	const FDeviceAddress FirstAddress = MakeLoopbackAddress(5);
	FDeviceAddress SecondAddress = MakeLoopbackAddress(5);
	SecondAddress.Bytes[1] = 99;

	// Act
	const bool bAreEqual = FirstAddress == SecondAddress;

	// Assert
	MW_EXPECT_TRUE(Test, bAreEqual, "Inactive trailing bytes should not affect address equality");
}

/**
 * Motivation: Gives callers a direct inequality test whose result cannot drift from equality semantics.
 * Responsibilities: Confirm operator!= is the exact negation of operator== for equal and unequal addresses.
 */
MW_TEST_CASE(DeviceAddressInequalityExactlyNegatesEquality)
{
	// Arrange
	const FDeviceAddress FirstAddress = MakeLoopbackAddress(8);
	const FDeviceAddress MatchingAddress = MakeLoopbackAddress(8);
	const FDeviceAddress DifferentAddress = MakeLoopbackAddress(9);

	// Act
	const bool bMatchingEquality = FirstAddress == MatchingAddress;
	const bool bMatchingInequality = FirstAddress != MatchingAddress;
	const bool bDifferentEquality = FirstAddress == DifferentAddress;
	const bool bDifferentInequality = FirstAddress != DifferentAddress;

	// Assert
	MW_EXPECT_EQ(Test, !bMatchingEquality, bMatchingInequality, "Matching address inequality should negate equality");
	MW_EXPECT_EQ(Test, !bDifferentEquality, bDifferentInequality, "Different address inequality should negate equality");
}

} // namespace
