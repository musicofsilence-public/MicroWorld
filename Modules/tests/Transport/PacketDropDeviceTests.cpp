#include "TestSupport.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Transport/HostLoopback.h>
#include <MicroWorld/Transport/PacketDropDevice.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::Core::ETransportResult;
using MicroWorld::Core::FDeviceAddress;
using MicroWorld::Core::FReceiveResult;
using MicroWorld::Core::MakeLoopbackAddress;
using MicroWorld::Core::TSpan;
using MicroWorld::Transport::FPacketDropDevice;
using MicroWorld::Transport::THostLoopback;

/** Motivation: Loopback template parameters every packet-drop case binds: two ports, deep enough mailboxes, one-word packets. */
constexpr std::size_t LoopbackPortCount = 2;
constexpr std::size_t LoopbackMailboxDepth = 16;
constexpr std::size_t LoopbackPacketBytes = 4;

/** Motivation: Loopback port index that owns the sender side in every packet-drop case. */
constexpr std::uint8_t SenderPortIndex = 0;
/** Motivation: Loopback port index that owns the receiver mailbox in every packet-drop case. */
constexpr std::uint8_t ReceiverPortIndex = 1;

/** Motivation: Drop interval that drops every send, proving Success is still reported to the caller. */
constexpr std::uint32_t DropEverySendInterval = 1;
/** Motivation: Drop interval that drops exactly every third send while delivering the rest. */
constexpr std::uint32_t DropEveryThirdInterval = 3;
/** Motivation: Drop interval that disables loss injection so every send is forwarded. */
constexpr std::uint32_t DropNeverInterval = 0;

/** Motivation: Number of sends the drop-every-third case drives so exactly three are dropped and six delivered. */
constexpr std::size_t DropEveryThirdSendCount = 9;
/** Motivation: Number of sends the drop-every-third case must deliver after dropping three. */
constexpr std::size_t DropEveryThirdDeliveredCount = 6;
/** Motivation: Number of sends the drop-every-third case must count as dropped. */
constexpr std::uint32_t DropEveryThirdDroppedCount = 3;
/** Motivation: Markers the drop-every-third case sends in order, so delivered order skips 3, 6, and 9. */
constexpr std::uint8_t DropEveryThirdMarkers[DropEveryThirdSendCount] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
/** Motivation: Delivered markers expected after the drop-every-third case drops 3, 6, and 9. */
constexpr std::uint8_t DropEveryThirdExpected[DropEveryThirdDeliveredCount] = {1, 2, 4, 5, 7, 8};

/** Motivation: Number of sends the no-loss case drives to prove every one is forwarded. */
constexpr std::size_t NoLossSendCount = 5;
/** Motivation: Markers the no-loss case sends, so each delivered packet can be checked for an unmodified marker. */
constexpr std::uint8_t NoLossMarkers[NoLossSendCount] = {10, 20, 30, 40, 50};

/** Motivation: Single-byte payload the N=1 case sends and proves never reaches the inner device. */
constexpr std::uint8_t DroppedSendPayloadByte = 0x42;
/** Motivation: Two payload bytes the passthrough case threads through the receiver unchanged. */
constexpr std::uint8_t PassthroughPacketBytes[2] = {0xAA, 0xBB};

/**
 * Motivation: Records transport progress without requiring a real transport, isolating the decorator forwarding
 *   contract.
 * Responsibilities: Honour the contract in Motivation and own no behaviour beyond it.
 * Example:
 *   // Construct and exercise the type in one behavior test.
 */
class FAdvanceRecordingDevice final : public MicroWorld::Core::ITransportDevice
{
public:
	/**
	 * Motivation: Records the bounded pre-advance turn that a wrapping decorator must preserve.
	 * Responsibilities: Perform only the documented
	 * mutation and leave unrelated state untouched.
	 */
	void PreAdvance(MicroWorld::Core::TimePointMilliseconds) noexcept override { ++AdvanceCount; }

	/**
	 * Motivation: This fake only observes transport progress.
	 * Responsibilities: Remains inert.
	 */
	ETransportResult TrySend(const FDeviceAddress&, TSpan<const std::uint8_t>) noexcept override { return ETransportResult::Unavailable; }

	/**
	 * Motivation: This fake only observes transport progress.
	 * Responsibilities: Remains inert.
	 */
	ETransportResult TryReceive(FDeviceAddress&, TSpan<std::uint8_t>, FReceiveResult&) noexcept override { return ETransportResult::Unavailable; }

	/**
	 * Motivation: Supplies a valid fixed capacity for the complete device contract.
	 * Responsibilities: Return the stored value and touch nothing else.
	 */
	std::size_t MaxPacketBytes() const noexcept override { return 1; }

	/** Motivation: Makes forwarded progress directly observable to this focused test. */
	std::size_t AdvanceCount{0};
};

/**
 * Motivation: Wrap an inner recording device with a drop-every-send decorator, then run its pre-advance turn.
 * Responsibilities: Transport progress
 * reaches the wrapped device even when every logical send drops.
 */
MW_TEST_CASE(PacketDropDevice_ForwardsPendingTransmitProgress)
{
	// Arrange
	FAdvanceRecordingDevice InnerDevice;
	FPacketDropDevice Dropper(InnerDevice, DropEverySendInterval);

	// Act
	Dropper.PreAdvance(0);

	// Assert
	MW_EXPECT_EQ(
		Test,
		static_cast<std::size_t>(1),
		InnerDevice.AdvanceCount,
		"Transport progress must reach the wrapped device even when every logical send drops");
}

/**
 * Motivation: Send nine sequential markers through a drop-every-third decorator, then drain the receiver.
 * Responsibilities: Every send reports Success whether or not it was dropped; exactly three of nine are dropped and the
 *   six delivered markers skip 3, 6, and.
 */
MW_TEST_CASE(PacketDropDevice_DropsEveryThirdSendDeliveringTheRest)
{
	// Arrange
	THostLoopback<LoopbackPortCount, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	FPacketDropDevice Dropper(Loopback.Port(SenderPortIndex), DropEveryThirdInterval);
	const FDeviceAddress ReceiverAddress = MakeLoopbackAddress(ReceiverPortIndex);

	// Act - send every marker; each call must report Success whether or not it was dropped.
	for (const std::uint8_t Marker : DropEveryThirdMarkers)
	{
		const std::array<std::uint8_t, 1> Packet{Marker};
		MW_EXPECT_EQ(
			Test,
			ETransportResult::Success,
			Dropper.TrySend(ReceiverAddress, TSpan<const std::uint8_t>(Packet.data(), Packet.size())),
			"Every send, dropped or not, must report Success");
	}
	MW_EXPECT_EQ(Test, DropEveryThirdDroppedCount, Dropper.DroppedSendCount(), "Nine sends with N=3 must drop exactly three");

	// Act - drain the receiver, bounding writes to the array capacity regardless of how many arrive.
	std::array<std::uint8_t, DropEveryThirdSendCount> DeliveredMarkers{};
	std::size_t DeliveredCount = 0;
	std::array<std::uint8_t, LoopbackPacketBytes> Destination{};
	FReceiveResult ReceiveResult{};
	FDeviceAddress ReceiveFrom{};
	while (DeliveredCount < DeliveredMarkers.size())
	{
		const ETransportResult ReceiveOutcome =
			Loopback.Port(ReceiverPortIndex).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination.data(), Destination.size()), ReceiveResult);
		if (ReceiveOutcome != ETransportResult::Success)
		{
			break;
		}
		DeliveredMarkers[DeliveredCount] = Destination[0];
		++DeliveredCount;
	}

	// Assert
	MW_EXPECT_EQ(Test, DropEveryThirdDeliveredCount, DeliveredCount, "Exactly six of nine sends must be delivered");
	for (std::size_t Index = 0; Index < DropEveryThirdDeliveredCount; ++Index)
	{
		MW_EXPECT_EQ(Test, DropEveryThirdExpected[Index], DeliveredMarkers[Index], "Delivered markers must skip 3, 6, and 9 in order");
	}
}

/**
 * Motivation: Send five markers through a zero drop interval decorator, then drain the receiver.
 * Responsibilities: A zero interval forwards every send to the receiver mailbox with its original marker unmodified and
 *   never counts a drop.
 */
MW_TEST_CASE(PacketDropDevice_ZeroIntervalForwardsEverySend)
{
	// Arrange
	THostLoopback<LoopbackPortCount, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	FPacketDropDevice Dropper(Loopback.Port(SenderPortIndex), DropNeverInterval);
	const FDeviceAddress ReceiverAddress = MakeLoopbackAddress(ReceiverPortIndex);

	// Act - send every marker; with N=0 none are dropped.
	for (const std::uint8_t Marker : NoLossMarkers)
	{
		const std::array<std::uint8_t, 1> Packet{Marker};
		MW_EXPECT_EQ(
			Test,
			ETransportResult::Success,
			Dropper.TrySend(ReceiverAddress, TSpan<const std::uint8_t>(Packet.data(), Packet.size())),
			"Every send with N=0 must succeed");
	}
	// Assert
	MW_EXPECT_EQ(Test, static_cast<std::uint32_t>(DropNeverInterval), Dropper.DroppedSendCount(), "N=0 must never drop");
	MW_EXPECT_EQ(Test, NoLossSendCount, Loopback.QueuedCount(ReceiverPortIndex), "N=0 must deliver every send to the receiver mailbox");

	std::array<std::uint8_t, LoopbackPacketBytes> Destination{};
	FReceiveResult ReceiveResult{};
	FDeviceAddress ReceiveFrom{};
	// Act / Assert - each delivered packet must carry its original marker unmodified.
	for (const std::uint8_t ExpectedMarker : NoLossMarkers)
	{
		MW_EXPECT_EQ(
			Test,
			ETransportResult::Success,
			Loopback.Port(ReceiverPortIndex).TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination.data(), Destination.size()), ReceiveResult),
			"Each queued packet must be receivable");
		MW_EXPECT_EQ(Test, ExpectedMarker, Destination[0], "Each delivered packet must carry its original marker unmodified");
	}
}

/**
 * Motivation: Send one packet through a drop-every-send decorator and observe the receiver mailbox.
 * Responsibilities: N=1 drops every send yet still reports Success, and the dropped send never reaches the inner
 *   device's wire.
 */
MW_TEST_CASE(PacketDropDevice_DroppedSendReturnsSuccessWithoutReachingInner)
{
	// Arrange
	THostLoopback<LoopbackPortCount, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	FPacketDropDevice Dropper(Loopback.Port(SenderPortIndex), DropEverySendInterval);
	const FDeviceAddress ReceiverAddress = MakeLoopbackAddress(ReceiverPortIndex);

	const std::array<std::uint8_t, 1> Packet{DroppedSendPayloadByte};
	// Act
	const ETransportResult SendResult = Dropper.TrySend(ReceiverAddress, TSpan<const std::uint8_t>(Packet.data(), Packet.size()));

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, SendResult, "N=1 drops every send yet still reports Success");
	MW_EXPECT_EQ(Test, DropEverySendInterval, Dropper.DroppedSendCount(), "The one send under N=1 must be counted as dropped");
	MW_EXPECT_EQ(Test, true, Loopback.IsEmpty(ReceiverPortIndex), "A dropped send must never reach the inner device's wire");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Loopback.QueuedCount(ReceiverPortIndex), "A dropped send must leave the receiver mailbox empty");
}

/**
 * Motivation: Queue one packet for the receiver, receive through the drop decorator, then receive again on the
 *   empty queue.
 * Responsibilities: The receive path is a bit-identical passthrough matching the inner device's own Success and
 *   Unavailable semantics, and a receive never.
 */
MW_TEST_CASE(PacketDropDevice_ReceivePathIsBitIdenticalPassthrough)
{
	// Arrange
	THostLoopback<LoopbackPortCount, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	const FDeviceAddress SenderAddress = MakeLoopbackAddress(SenderPortIndex);
	const FDeviceAddress ReceiverAddress = MakeLoopbackAddress(ReceiverPortIndex);
	FPacketDropDevice Dropper(Loopback.Port(ReceiverPortIndex), DropEveryThirdInterval);

	const std::array<std::uint8_t, 2> SentPacket{PassthroughPacketBytes[0], PassthroughPacketBytes[1]};
	// Act - setup send queues one packet for the receiver.
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Loopback.Port(SenderPortIndex).TrySend(ReceiverAddress, TSpan<const std::uint8_t>(SentPacket.data(), SentPacket.size())),
		"Setup send must queue one packet for the receiver");

	std::array<std::uint8_t, LoopbackPacketBytes> Destination{};
	FReceiveResult ReceiveResult{};
	FDeviceAddress ReceiveFrom{};
	// Act
	const ETransportResult ReceiveOutcome =
		Dropper.TryReceive(ReceiveFrom, TSpan<std::uint8_t>(Destination.data(), Destination.size()), ReceiveResult);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, ReceiveOutcome, "A queued packet must receive as Success, exactly like the inner device");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), ReceiveResult.BytesReceived, "BytesReceived must match the inner device's own report");
	MW_EXPECT_EQ(Test, PassthroughPacketBytes[0], Destination[0], "Received bytes must be bit-identical to the sent packet");
	MW_EXPECT_EQ(Test, PassthroughPacketBytes[1], Destination[1], "Received bytes must be bit-identical to the sent packet");
	MW_EXPECT_EQ(Test, true, ReceiveFrom == SenderAddress, "Sender address must pass through unchanged");
	MW_EXPECT_EQ(Test, static_cast<std::uint32_t>(DropNeverInterval), Dropper.DroppedSendCount(), "A receive must never change the drop count");

	FReceiveResult SecondReceiveResult{};
	FDeviceAddress SecondReceiveFrom{};
	// Act - a second receive finds an empty queue.
	const ETransportResult EmptyOutcome =
		Dropper.TryReceive(SecondReceiveFrom, TSpan<std::uint8_t>(Destination.data(), Destination.size()), SecondReceiveResult);
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Unavailable, EmptyOutcome, "An empty queue must forward Unavailable, exactly like the inner device");
	MW_EXPECT_EQ(
		Test, static_cast<std::uint32_t>(DropNeverInterval), Dropper.DroppedSendCount(), "A second receive must still never change the drop count");
}

/**
 * Motivation: Query MaxPacketBytes on the drop decorator and the wrapped loopback port.
 * Responsibilities: MaxPacketBytes forwards the wrapped port's reported capacity unchanged.
 */
MW_TEST_CASE(PacketDropDevice_MaxPacketBytesForwardsInner)
{
	// Arrange
	THostLoopback<LoopbackPortCount, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	FPacketDropDevice Dropper(Loopback.Port(SenderPortIndex), DropEveryThirdInterval);

	// Assert
	MW_EXPECT_EQ(Test, LoopbackPacketBytes, Dropper.MaxPacketBytes(), "MaxPacketBytes must match the loopback's template packet capacity");
	MW_EXPECT_EQ(
		Test, Loopback.Port(SenderPortIndex).MaxPacketBytes(), Dropper.MaxPacketBytes(), "MaxPacketBytes must forward the wrapped port's capacity");
}

} // namespace
