#include "TestSupport.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Net/HostLoopback.h>
#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetDriver.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/Net/PacketDropDriver.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::ENetResult;
using MicroWorld::FNetAddress;
using MicroWorld::FNetReceiveResult;
using MicroWorld::FPacketDropDriver;
using MicroWorld::MakeLoopbackAddress;
using MicroWorld::THostLoopback;
using MicroWorld::TSpan;

/** Proves N=3 drops exactly the 3rd, 6th, and 9th sends while every TrySend call still reports Success. */
MW_TEST_CASE(PacketDropDriver_DropsEveryThirdSendDeliveringTheRest)
{
	THostLoopback<2, 16, 4> Loopback;
	FPacketDropDriver Dropper(Loopback.Port(0), 3);
	const FNetAddress ReceiverAddress = MakeLoopbackAddress(1);

	const std::array<std::uint8_t, 9> Markers{1, 2, 3, 4, 5, 6, 7, 8, 9};
	for (const std::uint8_t Marker : Markers)
	{
		const std::array<std::uint8_t, 1> Packet{Marker};
		MW_EXPECT_EQ(
			Test,
			ENetResult::Success,
			Dropper.TrySend(ReceiverAddress, TSpan<const std::uint8_t>(Packet.data(), Packet.size())),
			"Every send, dropped or not, must report Success");
	}
	MW_EXPECT_EQ(Test, static_cast<std::uint32_t>(3), Dropper.DroppedSendCount(), "Nine sends with N=3 must drop exactly three");

	// Drain the receiver, bounding writes to the array capacity regardless of how many arrive.
	std::array<std::uint8_t, Markers.size()> DeliveredMarkers{};
	std::size_t DeliveredCount = 0;
	std::array<std::uint8_t, 4> Destination{};
	FNetReceiveResult ReceiveResult{};
	FNetAddress ReceiveFrom{};
	while (DeliveredCount < DeliveredMarkers.size())
	{
		const ENetResult ReceiveOutcome =
			Loopback.Port(1).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination.data(), Destination.size()), ReceiveResult);
		if (ReceiveOutcome != ENetResult::Success)
		{
			break;
		}
		DeliveredMarkers[DeliveredCount] = Destination[0];
		++DeliveredCount;
	}

	const std::array<std::uint8_t, 6> ExpectedMarkers{1, 2, 4, 5, 7, 8};
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(6), DeliveredCount, "Exactly six of nine sends must be delivered");
	for (std::size_t Index = 0; Index < ExpectedMarkers.size(); ++Index)
	{
		MW_EXPECT_EQ(Test, ExpectedMarkers[Index], DeliveredMarkers[Index], "Delivered markers must skip 3, 6, and 9 in order");
	}
}

/** Proves a zero drop interval forwards every send and never counts a drop. */
MW_TEST_CASE(PacketDropDriver_ZeroIntervalForwardsEverySend)
{
	THostLoopback<2, 16, 4> Loopback;
	FPacketDropDriver Dropper(Loopback.Port(0), 0);
	const FNetAddress ReceiverAddress = MakeLoopbackAddress(1);

	const std::array<std::uint8_t, 5> Markers{10, 20, 30, 40, 50};
	for (const std::uint8_t Marker : Markers)
	{
		const std::array<std::uint8_t, 1> Packet{Marker};
		MW_EXPECT_EQ(
			Test,
			ENetResult::Success,
			Dropper.TrySend(ReceiverAddress, TSpan<const std::uint8_t>(Packet.data(), Packet.size())),
			"Every send with N=0 must succeed");
	}
	MW_EXPECT_EQ(Test, static_cast<std::uint32_t>(0), Dropper.DroppedSendCount(), "N=0 must never drop");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(5), Loopback.QueuedCount(1), "N=0 must deliver every send to the receiver mailbox");

	std::array<std::uint8_t, 4> Destination{};
	FNetReceiveResult ReceiveResult{};
	FNetAddress ReceiveFrom{};
	for (const std::uint8_t ExpectedMarker : Markers)
	{
		MW_EXPECT_EQ(
			Test,
			ENetResult::Success,
			Loopback.Port(1).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination.data(), Destination.size()), ReceiveResult),
			"Each queued packet must be receivable");
		MW_EXPECT_EQ(Test, ExpectedMarker, Destination[0], "Each delivered packet must carry its original marker unmodified");
	}
}

/** Proves a dropped send under N=1 reports Success while never reaching the inner driver's wire. */
MW_TEST_CASE(PacketDropDriver_DroppedSendReturnsSuccessWithoutReachingInner)
{
	THostLoopback<2, 16, 4> Loopback;
	FPacketDropDriver Dropper(Loopback.Port(0), 1);
	const FNetAddress ReceiverAddress = MakeLoopbackAddress(1);

	const std::array<std::uint8_t, 1> Packet{0x42};
	const ENetResult SendResult = Dropper.TrySend(ReceiverAddress, TSpan<const std::uint8_t>(Packet.data(), Packet.size()));

	MW_EXPECT_EQ(Test, ENetResult::Success, SendResult, "N=1 drops every send yet still reports Success");
	MW_EXPECT_EQ(Test, static_cast<std::uint32_t>(1), Dropper.DroppedSendCount(), "The one send under N=1 must be counted as dropped");
	MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(1), "A dropped send must never reach the inner driver's wire");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Loopback.QueuedCount(1), "A dropped send must leave the receiver mailbox empty");
}

/** Proves the receive path is a bit-identical passthrough, matching the inner driver's own Success and Unavailable semantics. */
MW_TEST_CASE(PacketDropDriver_ReceivePathIsBitIdenticalPassthrough)
{
	THostLoopback<2, 16, 4> Loopback;
	const FNetAddress SenderAddress = MakeLoopbackAddress(0);
	const FNetAddress ReceiverAddress = MakeLoopbackAddress(1);
	FPacketDropDriver Dropper(Loopback.Port(1), 3);

	const std::array<std::uint8_t, 2> SentPacket{0xAA, 0xBB};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Loopback.Port(0).TrySend(ReceiverAddress, TSpan<const std::uint8_t>(SentPacket.data(), SentPacket.size())),
		"Setup send must queue one packet for the receiver");

	std::array<std::uint8_t, 4> Destination{};
	FNetReceiveResult ReceiveResult{};
	FNetAddress ReceiveFrom{};
	const ENetResult ReceiveOutcome = Dropper.TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination.data(), Destination.size()), ReceiveResult);

	MW_EXPECT_EQ(Test, ENetResult::Success, ReceiveOutcome, "A queued packet must receive as Success, exactly like the inner driver");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), ReceiveResult.BytesReceived, "BytesReceived must match the inner driver's own report");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xAA), Destination[0], "Received bytes must be bit-identical to the sent packet");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xBB), Destination[1], "Received bytes must be bit-identical to the sent packet");
	MW_EXPECT_EQ(Test, true, ReceiveFrom == SenderAddress, "Sender address must pass through unchanged");
	MW_EXPECT_EQ(Test, static_cast<std::uint32_t>(0), Dropper.DroppedSendCount(), "A receive must never change the drop count");

	FNetReceiveResult SecondReceiveResult{};
	FNetAddress SecondReceiveFrom{};
	const ENetResult EmptyOutcome =
		Dropper.TryReceive(SecondReceiveFrom, TSpan<std::uint8_t>(Destination.data(), Destination.size()), SecondReceiveResult);
	MW_EXPECT_EQ(Test, ENetResult::Unavailable, EmptyOutcome, "An empty queue must forward Unavailable, exactly like the inner driver");
	MW_EXPECT_EQ(Test, static_cast<std::uint32_t>(0), Dropper.DroppedSendCount(), "A second receive must still never change the drop count");
}

/** Proves MaxPacketBytes forwards the wrapped port's reported capacity unchanged. */
MW_TEST_CASE(PacketDropDriver_MaxPacketBytesForwardsInner)
{
	THostLoopback<2, 16, 4> Loopback;
	FPacketDropDriver Dropper(Loopback.Port(0), 3);

	MW_EXPECT_EQ(Test, static_cast<std::size_t>(4), Dropper.MaxPacketBytes(), "MaxPacketBytes must match the loopback's template packet capacity");
	MW_EXPECT_EQ(Test, Loopback.Port(0).MaxPacketBytes(), Dropper.MaxPacketBytes(), "MaxPacketBytes must forward the wrapped port's capacity");
}

} // namespace
