#include "TestSupport.h"

#include <MicroWorld/Transport/E32Lora.h>
#include <MicroWorld/Transport/FrameCodec.h>

#include <cstddef>
#include <cstdint>

namespace
{

/**
 * Scenario: Encode a normal node id into the shared E32 address shape.
 * Expected: The encoded address uses the one-byte shape and preserves the original node id.
 */
MW_TEST_CASE(E32LoraAddressRoundTripsNodeId)
{
	// Arrange
	constexpr std::uint8_t NodeId = 42;

	// Act
	const MicroWorld::FNetAddress Address = MicroWorld::MakeLoraAddress(NodeId);

	// Assert
	MW_EXPECT_TRUE(Test, MicroWorld::IsLoraAddress(Address), "Encoded E32 address must use the one-byte shape");
	MW_EXPECT_EQ(Test, NodeId, MicroWorld::LoraAddressNodeId(Address), "Encoded E32 address must preserve the node id");
}

/**
 * Scenario: Encode both uint8 node-id boundaries (zero and 255) into the one-byte shape.
 * Expected: Each boundary node id round-trips through the encoding unchanged.
 */
MW_TEST_CASE(E32LoraAddressPreservesBoundaryNodeIds)
{
	// Arrange
	const MicroWorld::FNetAddress LowestAddress = MicroWorld::MakeLoraAddress(std::uint8_t{0});
	const MicroWorld::FNetAddress HighestAddress = MicroWorld::MakeLoraAddress(std::uint8_t{255});

	// Assert
	MW_EXPECT_EQ(Test, std::uint8_t{0}, MicroWorld::LoraAddressNodeId(LowestAddress), "Node id zero must round-trip");
	MW_EXPECT_EQ(Test, std::uint8_t{255}, MicroWorld::LoraAddressNodeId(HighestAddress), "Node id 255 must round-trip");
}

/**
 * Scenario: Offer both an empty address and a two-byte address to the E32 address recognizer.
 * Expected: Neither address matches the E32 driver's one-byte shape.
 */
MW_TEST_CASE(E32LoraAddressRejectsOtherActiveLengths)
{
	// Arrange
	MicroWorld::FNetAddress EmptyAddress{};
	MicroWorld::FNetAddress TwoByteAddress{};
	TwoByteAddress.Size = 2;

	// Assert
	MW_EXPECT_TRUE(Test, !MicroWorld::IsLoraAddress(EmptyAddress), "An empty address must not match the E32 shape");
	MW_EXPECT_TRUE(Test, !MicroWorld::IsLoraAddress(TwoByteAddress), "A two-byte address must not match the E32 shape");
}

/**
 * Scenario: Sum the E32 maximum payload bytes and the MicroWorld frame overhead.
 * Expected: The total exactly fills one 64-byte transparent frame.
 */
MW_TEST_CASE(E32LoraPayloadBoundMatchesTransparentFrame)
{
	// Arrange
	constexpr std::size_t E32TransparentFrameBytes = 64;

	// Assert
	MW_EXPECT_EQ(
		Test,
		E32TransparentFrameBytes,
		MicroWorld::E32MaxPayloadBytes + MicroWorld::FrameOverheadBytes,
		"E32 payload plus MicroWorld framing must fit one transparent frame");
}

} // namespace
