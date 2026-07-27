#include "TestSupport.h"

#include <MicroWorld/Net/E32Lora.h>
#include <MicroWorld/Net/FrameCodec.h>

#include <cstddef>
#include <cstdint>

namespace
{

/** Proves a normal node id survives the shared E32 address encoding. */
MW_TEST_CASE(E32LoraAddressRoundTripsNodeId)
{
	constexpr std::uint8_t NodeId = 42;

	const MicroWorld::FNetAddress Address = MicroWorld::MakeLoraAddress(NodeId);

	MW_EXPECT_TRUE(Test, MicroWorld::IsLoraAddress(Address), "Encoded E32 address must use the one-byte shape");
	MW_EXPECT_EQ(Test, NodeId, MicroWorld::LoraAddressNodeId(Address), "Encoded E32 address must preserve the node id");
}

/** Proves the one-byte encoding preserves both uint8 node-id boundaries. */
MW_TEST_CASE(E32LoraAddressPreservesBoundaryNodeIds)
{
	const MicroWorld::FNetAddress LowestAddress = MicroWorld::MakeLoraAddress(std::uint8_t{0});
	const MicroWorld::FNetAddress HighestAddress = MicroWorld::MakeLoraAddress(std::uint8_t{255});

	MW_EXPECT_EQ(Test, std::uint8_t{0}, MicroWorld::LoraAddressNodeId(LowestAddress), "Node id zero must round-trip");
	MW_EXPECT_EQ(Test, std::uint8_t{255}, MicroWorld::LoraAddressNodeId(HighestAddress), "Node id 255 must round-trip");
}

/** Proves address recognition accepts only the E32 driver's one-byte shape. */
MW_TEST_CASE(E32LoraAddressRejectsOtherActiveLengths)
{
	MicroWorld::FNetAddress EmptyAddress{};
	MicroWorld::FNetAddress TwoByteAddress{};
	TwoByteAddress.Size = 2;

	MW_EXPECT_TRUE(Test, !MicroWorld::IsLoraAddress(EmptyAddress), "An empty address must not match the E32 shape");
	MW_EXPECT_TRUE(Test, !MicroWorld::IsLoraAddress(TwoByteAddress), "A two-byte address must not match the E32 shape");
}

/** Locks the E32 payload bound to one 64-byte transparent frame including MicroWorld framing. */
MW_TEST_CASE(E32LoraPayloadBoundMatchesTransparentFrame)
{
	constexpr std::size_t E32TransparentFrameBytes = 64;

	MW_EXPECT_EQ(
		Test,
		E32TransparentFrameBytes,
		MicroWorld::E32MaxPayloadBytes + MicroWorld::FrameOverheadBytes,
		"E32 payload plus MicroWorld framing must fit one transparent frame");
}

} // namespace
