#include "TestSupport.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetDriver.h>
#include <MicroWorld/Net/NetManager.h>
#include <MicroWorld/Net/NetPacketStorage.h>
#include <MicroWorld/Net/NetResult.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::ENetResult;
using MicroWorld::FNetAddress;
using MicroWorld::FNetManager;
using MicroWorld::FNetPacketStorage;
using MicroWorld::FNetReceiveResult;
using MicroWorld::INetDriver;
using MicroWorld::TSpan;

/** Builds a 1-byte destination address whose single byte is `Index`; keeps queue call sites concise. */
constexpr FNetAddress MakeDest(const std::uint8_t Index) noexcept
{
	return MicroWorld::MakeLoopbackAddress(Index);
}

/**
 * Records the exact bytes and destination address the manager passed to every driver send so FIFO order,
 * head retention, recovery, and per-packet routing can be proven across differently sized and valued packets.
 *
 * The driver returns a caller-chosen result on each send attempt and never touches a real transport, so
 * manager ordering and retention behavior stays deterministic.
 */
class FRecordingDriver final : public INetDriver
{
public:
	/** Defaulted so the driver can live in automatic storage without side effects. */
	~FRecordingDriver() noexcept override = default;

	/** Counts every attempt and records the destination address and bytes of every successful send so FIFO order of delivered packets is provable. */
	ENetResult TrySend(const FNetAddress& To, TSpan<const std::uint8_t> Packet) noexcept override
	{
		++SendCount;
		if (ForcedSendResult == ENetResult::Success && SuccessfulSendCount < MaxRecordedSends)
		{
			const std::size_t CopyLength = Packet.Size() <= MaxRecordedBytes ? Packet.Size() : MaxRecordedBytes;
			for (std::size_t Index = 0; Index < CopyLength; ++Index)
			{
				RecordedSendBytes[SuccessfulSendCount][Index] = Packet[Index];
			}
			RecordedSendLengths[SuccessfulSendCount] = Packet.Size();
			RecordedSendDestinations[SuccessfulSendCount] = To;
			++SuccessfulSendCount;
		}
		return ForcedSendResult;
	}

	/** Returns the forced result, fills the destination on success, and stamps a deterministic sender into OutFrom only on success. */
	ENetResult TryReceive(FNetAddress& OutFrom, TSpan<std::uint8_t> Destination, FNetReceiveResult& OutResult) noexcept override
	{
		++ReceiveAttemptCount;
		if (ForcedReceiveResult == ENetResult::Success)
		{
			const std::size_t CopyLength = ReceiveByteCount <= Destination.Size() ? ReceiveByteCount : Destination.Size();
			for (std::size_t Index = 0; Index < CopyLength; ++Index)
			{
				Destination[Index] = ReceiveFillerByte;
			}
			OutResult.BytesReceived = ReceiveByteCount;
			OutFrom = ReceiveSender;
		}
		return ForcedReceiveResult;
	}

	/** Reports a fixed per-packet byte capacity large enough for every test packet in this suite. */
	std::size_t MaxPacketBytes() const noexcept override { return DriverMaxPacketBytes; }

	/** The result the next TrySend call must return, regardless of packet contents. */
	ENetResult ForcedSendResult{ENetResult::Success};

	/** The result the next TryReceive call must return, regardless of destination. */
	ENetResult ForcedReceiveResult{ENetResult::Unavailable};

	/** The byte count a successful forced receive reports. */
	std::size_t ReceiveByteCount{0};

	/** The byte value written into every received byte so success is observable. */
	std::uint8_t ReceiveFillerByte{0xAB};

	/** The sender address a successful forced receive stamps into OutFrom. */
	FNetAddress ReceiveSender{};

	/** Counts every send attempt, including failures, so backpressure retention is observable. */
	std::size_t SendCount{0};

	/** Counts only successful sends so recorded slots map one-to-one to delivered packets. */
	std::size_t SuccessfulSendCount{0};

	/** Counts how many times the manager attempted a receive. */
	std::size_t ReceiveAttemptCount{0};

	static constexpr std::size_t MaxRecordedSends = 16;
	static constexpr std::size_t MaxRecordedBytes = 8;
	static constexpr std::size_t DriverMaxPacketBytes = 64;

	/** Records the exact bytes of each send so FIFO order is provable. */
	std::array<std::array<std::uint8_t, MaxRecordedBytes>, MaxRecordedSends> RecordedSendBytes{};

	/** Records the exact length of each send alongside its bytes. */
	std::array<std::size_t, MaxRecordedSends> RecordedSendLengths{};

	/** Records the destination address the manager passed with each send so per-packet routing is provable. */
	std::array<FNetAddress, MaxRecordedSends> RecordedSendDestinations{};
};

/** Proves the manager reports its fixed configuration and an empty FIFO at construction. */
MW_TEST_CASE(NetManagerStartsEmptyWithFixedConfiguration)
{
	FRecordingDriver Driver;
	FNetPacketStorage<2, 4> Storage;
	FNetManager<2, 4> Manager(Driver, Storage);

	MW_EXPECT_EQ(Test, true, Manager.IsEmpty(), "A fresh manager must report an empty FIFO");
	MW_EXPECT_EQ(Test, false, Manager.IsFull(), "A fresh manager must not report a full FIFO");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Manager.QueueCapacity(), "Queue capacity must match the template parameter");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(4), Manager.MaximumPacketBytes(), "Max packet bytes must match the template parameter");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Manager.QueuedCountValue(), "A fresh manager must report zero queued packets");
}

/** Proves an oversized packet is rejected as Invalid without partial queueing. */
MW_TEST_CASE(NetManagerRejectsOversizedPacketTransactionally)
{
	FRecordingDriver Driver;
	FNetPacketStorage<2, 2> Storage;
	FNetManager<2, 2> Manager(Driver, Storage);

	const std::uint8_t OversizedData[4] = {0x01, 0x02, 0x03, 0x04};
	const ENetResult OversizedResult = Manager.QueueSend(MakeDest(0), TSpan<const std::uint8_t>(OversizedData, 4));
	MW_EXPECT_EQ(Test, ENetResult::Invalid, OversizedResult, "A packet larger than MaximumPacketBytes must return Invalid");
	MW_EXPECT_EQ(Test, true, Manager.IsEmpty(), "Oversized queue must not enqueue a packet");
}

/** Proves a null packet with nonzero length is rejected as Invalid without queueing. */
MW_TEST_CASE(NetManagerRejectsNullPacketWithNonzeroLength)
{
	FRecordingDriver Driver;
	FNetPacketStorage<2, 4> Storage;
	FNetManager<2, 4> Manager(Driver, Storage);

	const ENetResult NullResult = Manager.QueueSend(MakeDest(0), TSpan<const std::uint8_t>(nullptr, 2));
	MW_EXPECT_EQ(Test, ENetResult::Invalid, NullResult, "Null data with nonzero length must return Invalid");
	MW_EXPECT_EQ(Test, true, Manager.IsEmpty(), "Invalid queue must not enqueue a packet");
}

/** Proves QueueSend preserves FIFO order and AdvanceSend sends differently sized and valued packets in that order. */
MW_TEST_CASE(NetManagerAdvanceSendsDifferentlySizedPacketsInFifoOrder)
{
	FRecordingDriver Driver;
	FNetPacketStorage<3, 4> Storage;
	FNetManager<3, 4> Manager(Driver, Storage);

	const std::uint8_t FirstPacket[2] = {0x10, 0x20};
	const std::uint8_t SecondPacket[3] = {0x30, 0x40, 0x50};
	const std::uint8_t ThirdPacket[1] = {0x60};
	MW_EXPECT_EQ(Test, ENetResult::Success, Manager.QueueSend(MakeDest(0), TSpan<const std::uint8_t>(FirstPacket, 2)), "First queue must succeed");
	MW_EXPECT_EQ(Test, ENetResult::Success, Manager.QueueSend(MakeDest(0), TSpan<const std::uint8_t>(SecondPacket, 3)), "Second queue must succeed");
	MW_EXPECT_EQ(Test, ENetResult::Success, Manager.QueueSend(MakeDest(0), TSpan<const std::uint8_t>(ThirdPacket, 1)), "Third queue must succeed");

	Manager.AdvanceSend();
	Manager.AdvanceSend();
	Manager.AdvanceSend();

	MW_EXPECT_EQ(Test, static_cast<std::size_t>(3), Driver.SendCount, "Three advances must call the driver exactly three times");

	// First send: 2 bytes {0x10, 0x20}
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Driver.RecordedSendLengths[0], "First send must carry the first packet length");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x10), Driver.RecordedSendBytes[0][0], "First send must carry the first packet first byte");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x20), Driver.RecordedSendBytes[0][1], "First send must carry the first packet second byte");

	// Second send: 3 bytes {0x30, 0x40, 0x50}
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(3), Driver.RecordedSendLengths[1], "Second send must carry the second packet length");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x30), Driver.RecordedSendBytes[1][0], "Second send must carry the second packet first byte");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x50), Driver.RecordedSendBytes[1][2], "Second send must carry the second packet third byte");

	// Third send: 1 byte {0x60}
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Driver.RecordedSendLengths[2], "Third send must carry the third packet length");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x60), Driver.RecordedSendBytes[2][0], "Third send must carry the third packet first byte");

	MW_EXPECT_EQ(Test, true, Manager.IsEmpty(), "Three successful advances must drain a three-packet FIFO");
}

/** Proves a full FIFO rejects a further queue without losing the accepted packets. */
MW_TEST_CASE(NetManagerFullFifoRejectsFurtherQueue)
{
	FRecordingDriver Driver;
	FNetPacketStorage<1, 4> Storage;
	FNetManager<1, 4> Manager(Driver, Storage);

	const std::uint8_t Accepted[2] = {0xAA, 0xBB};
	const std::uint8_t Rejected[2] = {0xCC, 0xDD};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Success,
		Manager.QueueSend(MakeDest(0), TSpan<const std::uint8_t>(Accepted, 2)),
		"First queue into an empty FIFO must succeed");
	const ENetResult OverflowResult = Manager.QueueSend(MakeDest(0), TSpan<const std::uint8_t>(Rejected, 2));
	MW_EXPECT_EQ(Test, ENetResult::Full, OverflowResult, "Queue into a full FIFO must return Full");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Manager.QueuedCountValue(), "Overflow must not change the queued count");

	// Prove the accepted head survives the rejected queue.
	Manager.AdvanceSend();
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Driver.RecordedSendLengths[0], "Retained head must carry the accepted packet length");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xAA), Driver.RecordedSendBytes[0][0], "Retained head must carry the accepted first byte");
}

/** Proves an empty AdvanceSend returns Unavailable without calling the driver. */
MW_TEST_CASE(NetManagerAdvanceEmptyReturnsUnavailableWithoutDriverCall)
{
	FRecordingDriver Driver;
	FNetPacketStorage<2, 4> Storage;
	FNetManager<2, 4> Manager(Driver, Storage);

	const ENetResult EmptyAdvanceResult = Manager.AdvanceSend();
	MW_EXPECT_EQ(Test, ENetResult::Unavailable, EmptyAdvanceResult, "Advance on an empty FIFO must return Unavailable");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Driver.SendCount, "Empty advance must not call the driver");
}

/** Proves one AdvanceSend attempts exactly one driver send and removes the head on success. */
MW_TEST_CASE(NetManagerAdvanceAttemptsOneSendAndRemovesHeadOnSuccess)
{
	FRecordingDriver Driver;
	FNetPacketStorage<2, 4> Storage;
	FNetManager<2, 4> Manager(Driver, Storage);

	const std::uint8_t HeadPacket[2] = {0x11, 0x22};
	Manager.QueueSend(MakeDest(0), TSpan<const std::uint8_t>(HeadPacket, 2));

	const ENetResult AdvanceResult = Manager.AdvanceSend();
	MW_EXPECT_EQ(Test, ENetResult::Success, AdvanceResult, "Advance with a successful driver must succeed");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Driver.SendCount, "Advance must call the driver exactly once");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Driver.RecordedSendLengths[0], "Advance must send the head packet length");
	MW_EXPECT_EQ(Test, true, Manager.IsEmpty(), "Successful advance must remove the head packet");
}

/** Proves driver Full retains the exact head packet contents for the next advance. */
MW_TEST_CASE(NetManagerDriverFullRetainsExactHeadContents)
{
	FRecordingDriver Driver;
	Driver.ForcedSendResult = ENetResult::Full;
	FNetPacketStorage<2, 4> Storage;
	FNetManager<2, 4> Manager(Driver, Storage);

	const std::uint8_t FirstPacket[3] = {0x01, 0x02, 0x03};
	const std::uint8_t SecondPacket[2] = {0x04, 0x05};
	Manager.QueueSend(MakeDest(0), TSpan<const std::uint8_t>(FirstPacket, 3));
	Manager.QueueSend(MakeDest(0), TSpan<const std::uint8_t>(SecondPacket, 2));

	const ENetResult AdvanceResult = Manager.AdvanceSend();
	MW_EXPECT_EQ(Test, ENetResult::Full, AdvanceResult, "Driver Full must propagate as Full");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Manager.QueuedCountValue(), "Driver Full must retain all queued packets");

	// Clear backpressure: the next advance must send the retained first packet, not the second.
	Driver.ForcedSendResult = ENetResult::Success;
	const ENetResult RecoveryAdvanceResult = Manager.AdvanceSend();
	MW_EXPECT_EQ(Test, ENetResult::Success, RecoveryAdvanceResult, "Recovery advance must succeed after backpressure clears");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(3), Driver.RecordedSendLengths[0], "Retained head must be the first packet length");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x01), Driver.RecordedSendBytes[0][0], "Retained head must carry the first packet first byte");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x03), Driver.RecordedSendBytes[0][2], "Retained head must carry the first packet third byte");

	// The next advance must send the second packet in FIFO order.
	Manager.AdvanceSend();
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Driver.RecordedSendLengths[1], "Second advance must send the second packet length");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x04), Driver.RecordedSendBytes[1][0], "Second advance must send the second packet first byte");
}

/** Proves driver Unavailable retains the exact head packet for a later retry. */
MW_TEST_CASE(NetManagerDriverUnavailableRetainsExactHead)
{
	FRecordingDriver Driver;
	Driver.ForcedSendResult = ENetResult::Unavailable;
	FNetPacketStorage<1, 4> Storage;
	FNetManager<1, 4> Manager(Driver, Storage);

	const std::uint8_t Packet[2] = {0x55, 0x66};
	Manager.QueueSend(MakeDest(0), TSpan<const std::uint8_t>(Packet, 2));

	const ENetResult AdvanceResult = Manager.AdvanceSend();
	MW_EXPECT_EQ(Test, ENetResult::Unavailable, AdvanceResult, "Driver Unavailable must propagate as Unavailable");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Manager.QueuedCountValue(), "Driver Unavailable must retain the head packet");

	Driver.ForcedSendResult = ENetResult::Success;
	Manager.AdvanceSend();
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Driver.RecordedSendLengths[0], "Retained head must carry its original length");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x55), Driver.RecordedSendBytes[0][0], "Retained head must carry its original first byte");
}

/** Proves driver Invalid retains the exact head packet without consuming it. */
MW_TEST_CASE(NetManagerDriverInvalidRetainsExactHead)
{
	FRecordingDriver Driver;
	Driver.ForcedSendResult = ENetResult::Invalid;
	FNetPacketStorage<1, 4> Storage;
	FNetManager<1, 4> Manager(Driver, Storage);

	const std::uint8_t Packet[2] = {0x07, 0x08};
	Manager.QueueSend(MakeDest(0), TSpan<const std::uint8_t>(Packet, 2));

	const ENetResult AdvanceResult = Manager.AdvanceSend();
	MW_EXPECT_EQ(Test, ENetResult::Invalid, AdvanceResult, "Driver Invalid must propagate as Invalid");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Manager.QueuedCountValue(), "Driver Invalid must retain the head packet");

	Driver.ForcedSendResult = ENetResult::Success;
	Manager.AdvanceSend();
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Driver.RecordedSendLengths[0], "Retained head must carry its original length");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x08), Driver.RecordedSendBytes[0][1], "Retained head must carry its original second byte");
}

/** Proves a queued head survives backpressure and is sent once the driver clears, before later packets. */
MW_TEST_CASE(NetManagerRecoverySendsRetainedHeadBeforeLaterPackets)
{
	FRecordingDriver Driver;
	Driver.ForcedSendResult = ENetResult::Full;
	FNetPacketStorage<2, 4> Storage;
	FNetManager<2, 4> Manager(Driver, Storage);

	const std::uint8_t HeadPacket[2] = {0x99, 0xAA};
	const std::uint8_t LaterPacket[1] = {0xBB};
	Manager.QueueSend(MakeDest(0), TSpan<const std::uint8_t>(HeadPacket, 2));
	Manager.QueueSend(MakeDest(0), TSpan<const std::uint8_t>(LaterPacket, 1));

	MW_EXPECT_EQ(Test, ENetResult::Full, Manager.AdvanceSend(), "First advance into a full driver must return Full");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Manager.QueuedCountValue(), "Backpressure must retain both packets");

	Driver.ForcedSendResult = ENetResult::Success;
	const ENetResult FirstRecovery = Manager.AdvanceSend();
	MW_EXPECT_EQ(Test, ENetResult::Success, FirstRecovery, "Recovery advance must succeed");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Driver.RecordedSendLengths[0], "Recovery must send the retained head, not the later packet");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x99), Driver.RecordedSendBytes[0][0], "Recovery must send the retained head first byte");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Manager.QueuedCountValue(), "Recovery must remove only the head");

	Manager.AdvanceSend();
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Driver.RecordedSendLengths[1], "Second advance must send the later packet");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xBB), Driver.RecordedSendBytes[1][0], "Second advance must send the later packet byte");
}

/** Proves caller-owned packet storage can be reused after wraparound and draining across many cycles. */
MW_TEST_CASE(NetManagerCallerStorageReusedAfterWraparoundAndDraining)
{
	FRecordingDriver Driver;
	FNetPacketStorage<2, 2> Storage;
	FNetManager<2, 2> Manager(Driver, Storage);

	const std::uint8_t CycleA[2] = {0xA0, 0xA1};
	const std::uint8_t CycleB[2] = {0xB0, 0xB1};

	// Cycle the FIFO more times than its capacity so head/tail indices wrap around repeatedly.
	for (std::size_t Cycle = 0; Cycle < 6; ++Cycle)
	{
		MW_EXPECT_EQ(
			Test, ENetResult::Success, Manager.QueueSend(MakeDest(0), TSpan<const std::uint8_t>(CycleA, 2)), "Queue A must succeed each cycle");
		MW_EXPECT_EQ(
			Test, ENetResult::Success, Manager.QueueSend(MakeDest(0), TSpan<const std::uint8_t>(CycleB, 2)), "Queue B must succeed each cycle");
		MW_EXPECT_EQ(Test, true, Manager.IsFull(), "Two queues must fill the two-slot FIFO each cycle");

		Manager.AdvanceSend();
		Manager.AdvanceSend();
		MW_EXPECT_EQ(Test, true, Manager.IsEmpty(), "Two advances must drain the FIFO each cycle");

		const std::size_t SendIndex = Cycle * 2;
		MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Driver.RecordedSendLengths[SendIndex], "Cycle A send must carry two bytes");
		MW_EXPECT_EQ(
			Test, static_cast<std::uint8_t>(0xA0), Driver.RecordedSendBytes[SendIndex][0], "Cycle A send must carry the A packet first byte");
		MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Driver.RecordedSendLengths[SendIndex + 1], "Cycle B send must carry two bytes");
		MW_EXPECT_EQ(
			Test, static_cast<std::uint8_t>(0xB1), Driver.RecordedSendBytes[SendIndex + 1][1], "Cycle B send must carry the B packet second byte");
	}

	MW_EXPECT_EQ(Test, static_cast<std::size_t>(12), Driver.SendCount, "Six cycles of two sends must call the driver exactly twelve times");
}

/**
 * Proves AdvanceSend delivers each queued packet to the destination address stored with it, in FIFO order,
 * so the addressed queue routes every head independently of the others.
 */
MW_TEST_CASE(NetManagerAdvanceSendsEachHeadToItsStoredDestination)
{
	FRecordingDriver Driver;
	FNetPacketStorage<3, 4> Storage;
	FNetManager<3, 4> Manager(Driver, Storage);

	const FNetAddress DestA = MakeDest(1);
	const FNetAddress DestB = MakeDest(2);
	const FNetAddress DestC = MakeDest(3);
	const std::uint8_t PacketA[2] = {0xA0, 0xA1};
	const std::uint8_t PacketB[2] = {0xB0, 0xB1};
	const std::uint8_t PacketC[2] = {0xC0, 0xC1};
	MW_EXPECT_EQ(Test, ENetResult::Success, Manager.QueueSend(DestA, TSpan<const std::uint8_t>(PacketA, 2)), "Queue to DestA must succeed");
	MW_EXPECT_EQ(Test, ENetResult::Success, Manager.QueueSend(DestB, TSpan<const std::uint8_t>(PacketB, 2)), "Queue to DestB must succeed");
	MW_EXPECT_EQ(Test, ENetResult::Success, Manager.QueueSend(DestC, TSpan<const std::uint8_t>(PacketC, 2)), "Queue to DestC must succeed");

	Manager.AdvanceSend();
	Manager.AdvanceSend();
	Manager.AdvanceSend();

	// Each recorded send must carry the exact destination stored with that packet, in FIFO order.
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(3), Driver.SendCount, "Three advances must call the driver exactly three times");
	MW_EXPECT_EQ(Test, true, Driver.RecordedSendDestinations[0] == DestA, "First advance must send to the first queued destination");
	MW_EXPECT_EQ(Test, true, Driver.RecordedSendDestinations[1] == DestB, "Second advance must send to the second queued destination");
	MW_EXPECT_EQ(Test, true, Driver.RecordedSendDestinations[2] == DestC, "Third advance must send to the third queued destination");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xA0), Driver.RecordedSendBytes[0][0], "First send must still carry the first packet bytes");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xC1), Driver.RecordedSendBytes[2][1], "Third send must still carry the third packet bytes");
	MW_EXPECT_EQ(Test, true, Manager.IsEmpty(), "Three successful advances must drain a three-packet FIFO");
}

/** Proves Receive performs at most one direct driver receive and propagates its transactional result, leaving OutFrom unchanged on failure. */
MW_TEST_CASE(NetManagerReceivePerformsOneDirectDriverReceive)
{
	FRecordingDriver Driver;
	Driver.ForcedReceiveResult = ENetResult::Unavailable;
	FNetPacketStorage<2, 4> Storage;
	FNetManager<2, 4> Manager(Driver, Storage);

	std::uint8_t Destination[4] = {0xFF, 0xFF, 0xFF, 0xFF};
	FNetReceiveResult ReceiveResult{std::size_t{0xEE}};
	FNetAddress ReceiveFrom{0x42};
	const ENetResult UnavailableResult = Manager.Receive(ReceiveFrom, TSpan<std::uint8_t>(Destination, 4), ReceiveResult);
	MW_EXPECT_EQ(Test, ENetResult::Unavailable, UnavailableResult, "Receive must propagate the driver result");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Driver.ReceiveAttemptCount, "Receive must call the driver exactly once");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0xEE), ReceiveResult.BytesReceived, "Unavailable receive must leave BytesReceived unchanged");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xFF), Destination[0], "Unavailable receive must not modify the destination");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x42), ReceiveFrom.Bytes[0], "Unavailable receive must leave OutFrom unchanged");
}

/** Proves Receive propagates Success, the driver-reported byte count, destination bytes, and sender address. */
MW_TEST_CASE(NetManagerReceivePropagatesSuccessAndByteCount)
{
	FRecordingDriver Driver;
	Driver.ForcedReceiveResult = ENetResult::Success;
	Driver.ReceiveByteCount = 3;
	Driver.ReceiveFillerByte = 0x7C;
	Driver.ReceiveSender = MakeDest(7);
	FNetPacketStorage<2, 4> Storage;
	FNetManager<2, 4> Manager(Driver, Storage);

	std::uint8_t Destination[4] = {0};
	FNetReceiveResult ReceiveResult{std::size_t{0xEE}};
	FNetAddress ReceiveFrom{0x42};
	const ENetResult SuccessResult = Manager.Receive(ReceiveFrom, TSpan<std::uint8_t>(Destination, 4), ReceiveResult);
	MW_EXPECT_EQ(Test, ENetResult::Success, SuccessResult, "Receive must propagate a successful driver result");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(3), ReceiveResult.BytesReceived, "Receive must propagate the driver byte count");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x7C), Destination[0], "Receive must propagate the driver destination bytes");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x7C), Destination[2], "Receive must fill exactly the reported byte count");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x00), Destination[3], "Receive must not write past the reported byte count");
	MW_EXPECT_EQ(Test, true, ReceiveFrom == Driver.ReceiveSender, "Receive must propagate the driver-reported sender address");
}

} // namespace
