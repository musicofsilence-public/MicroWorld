#include "TestSupport.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/ReliableChannel.h>
#include <MicroWorld/Time.h>

#include <cstddef>
#include <cstdint>

namespace
{
using MicroWorld::DurationMilliseconds;
using MicroWorld::EMessageResult;
using MicroWorld::EReliablePacketKind;
using MicroWorld::FMessageChannelId;
using MicroWorld::FReliableChannelConfig;
using MicroWorld::HighByteShift;
using MicroWorld::IEncodedMessageSink;
using MicroWorld::IMessageChannel;
using MicroWorld::LocalChannelId;
using MicroWorld::LowByteMask;
using MicroWorld::ReliableHeaderBytes;
using MicroWorld::TimePointMilliseconds;
using MicroWorld::TReliableChannel;
using MicroWorld::TSpan;

/** Fixed channel id FRecordingInnerChannel reports, distinct from LocalChannelId. */
constexpr FMessageChannelId InnerChannelId = 7;

/** Fixed per-send budget FRecordingInnerChannel reports, generously above every test payload. */
constexpr std::size_t InnerChannelBudget = 64;

/** Pending-table capacity shared by every case in this suite. */
constexpr std::size_t TestMaxPendingMessages = 4;

/** Per-slot wrapped-packet byte budget shared by every case in this suite. */
constexpr std::size_t TestMaxMessageBytes = 96;

/** Retry interval used by every case's config, distinct so cases 3/4's counts stay unambiguous. */
constexpr DurationMilliseconds TestRetryIntervalMilliseconds = 250;

/** Attempt ceiling used by every case's config, distinct so case 4's counts stay unambiguous. */
constexpr std::uint8_t TestMaxSendAttempts = 4;

/** Distinct payload bytes the wrapping and forwarding cases thread through the reliable channel. */
constexpr std::uint8_t PayloadByte01 = 0x01;
constexpr std::uint8_t PayloadByte11 = 0x11;
constexpr std::uint8_t PayloadByte22 = 0x22;
constexpr std::uint8_t PayloadByte33 = 0x33;
constexpr std::uint8_t PayloadByte77 = 0x77;
constexpr std::uint8_t PayloadByteAA = 0xAA;
constexpr std::uint8_t PayloadByteBB = 0xBB;
constexpr std::uint8_t PayloadByteCC = 0xCC;
constexpr std::uint8_t PayloadByteA1 = 0xA1;
constexpr std::uint8_t PayloadByteA2 = 0xA2;

/** Sequence number the wrapping case assigns to its first outbound Data packet. */
constexpr std::uint16_t FirstAssignedSequence = 1;

/** Sequence number the duplicate-forward case threads through its single inbound Data packet. */
constexpr std::uint16_t SequenceFive = 5;

/** Sequence number the window-edge case uses for its second (one-window-newer) Data packet. */
constexpr std::uint16_t SequenceThirtyThree = 33;

/** Wall-clock baseline the retry cases establish before advancing toward the retry interval. */
constexpr TimePointMilliseconds BaselineTime = 1000;

/** One-byte payload count shared by every one-byte payload and ack-handling span in this suite. */
constexpr std::size_t OneBytePayloadCount = 1;

/** Three-byte payload count the wrapping case sends and its matching ack packet both use. */
constexpr std::size_t ThreeBytePayloadCount = 3;

/** Four-byte wire-packet count the inbound Data cases feed to ReceiveEncodedMessage. */
constexpr std::size_t FourByteWirePacketCount = 4;

/** Wrapped Data packet length for a three-byte payload: ReliableHeaderBytes plus the payload. */
constexpr std::size_t WrappedThreeByteDataLength = ReliableHeaderBytes + ThreeBytePayloadCount;

/** The reliable channel profile under test in this suite. */
using FTestReliableChannel = TReliableChannel<TestMaxPendingMessages, TestMaxMessageBytes>;

/** Builds the shared retry config every case constructs its channel with. */
FReliableChannelConfig MakeTestConfig() noexcept
{
	FReliableChannelConfig Config{};
	Config.RetryIntervalMilliseconds = TestRetryIntervalMilliseconds;
	Config.MaxSendAttempts = TestMaxSendAttempts;
	return Config;
}

/**
 * Records every TrySendEncodedMessage call's full bytes and a running count so a case can assert
 * the wrapper's outbound wire format byte-for-byte and count how many sends the inner channel observed.
 */
class FRecordingInnerChannel final : public IMessageChannel
{
public:
	/** Bounds the copied bytes of the most recent send so this fixture stays fixed-size. */
	static constexpr std::size_t MaxRecordedBytes = 32;

	/** Reports how many TrySendEncodedMessage calls this stub has observed. */
	std::size_t SendCallCount() const noexcept { return CallCount; }

	/** Reports the byte length of the most recently recorded send. */
	std::size_t LastSendLength() const noexcept { return LastLength; }

	/** Accesses one byte of the most recently recorded send; the caller must keep Index < LastSendLength(). */
	std::uint8_t LastSendByte(const std::size_t InIndex) const noexcept { return LastBytes[InIndex]; }

	/** Returns the fixed test channel id this stub was configured with. */
	FMessageChannelId GetChannelId() const noexcept override { return InnerChannelId; }

	/** Returns the fixed test budget this stub was configured with. */
	std::size_t MaxEncodedMessageBytes() const noexcept override { return InnerChannelBudget; }

	/** Records the call and its bytes, then always reports Success. */
	EMessageResult TrySendEncodedMessage(const TSpan<const std::uint8_t> InEncoded) noexcept override
	{
		++CallCount;
		LastLength = InEncoded.Size() < MaxRecordedBytes ? InEncoded.Size() : MaxRecordedBytes;
		for (std::size_t Index = 0; Index < LastLength; ++Index)
		{
			LastBytes[Index] = InEncoded.Data()[Index];
		}
		return EMessageResult::Success;
	}

private:
	/** Total TrySendEncodedMessage calls observed so far. */
	std::size_t CallCount{0};

	/** Byte length of the most recently recorded send. */
	std::size_t LastLength{0};

	/** Copy of the most recently recorded send's bytes, truncated to MaxRecordedBytes. */
	std::uint8_t LastBytes[MaxRecordedBytes]{};
};

/** Records every forwarded payload's byte count and bytes so a case can assert "forwarded once" and inspect the delivered bytes. */
class FRecordingForwardSink final : public IEncodedMessageSink
{
public:
	/** Bounds the copied bytes of the most recent forward so this fixture stays fixed-size. */
	static constexpr std::size_t MaxRecordedBytes = 32;

	/** Reports how many ReceiveEncodedMessage calls this stub has observed. */
	std::size_t ForwardedCallCount() const noexcept { return CallCount; }

	/** Records the call and its bytes, then always reports Success. */
	EMessageResult ReceiveEncodedMessage(const FMessageChannelId InArrivedOnChannelId, const TSpan<const std::uint8_t> InEncoded) noexcept override
	{
		(void)InArrivedOnChannelId;
		++CallCount;
		LastLength = InEncoded.Size() < MaxRecordedBytes ? InEncoded.Size() : MaxRecordedBytes;
		for (std::size_t Index = 0; Index < LastLength; ++Index)
		{
			LastBytes[Index] = InEncoded.Data()[Index];
		}
		return EMessageResult::Success;
	}

private:
	/** Total ReceiveEncodedMessage calls observed so far. */
	std::size_t CallCount{0};

	/** Byte length of the most recently recorded forward. */
	std::size_t LastLength{0};

	/** Copy of the most recently recorded forward's bytes, truncated to MaxRecordedBytes. */
	std::uint8_t LastBytes[MaxRecordedBytes]{};
};

/** An outbound send must be wrapped as [Data][Sequence=1 LE][original payload] and left pending until acknowledged. */
MW_TEST_CASE(EngineReliableChannel_DataIsWrappedWithKindAndSequence)
{
	FRecordingForwardSink ForwardSink;
	FRecordingInnerChannel InnerChannel;
	FTestReliableChannel Reliable(ForwardSink, MakeTestConfig());
	Reliable.SetInnerChannel(InnerChannel);

	const std::uint8_t Payload[ThreeBytePayloadCount] = {PayloadByte11, PayloadByte22, PayloadByte33};
	MW_EXPECT_EQ(
		Test,
		EMessageResult::Success,
		Reliable.TrySendEncodedMessage(TSpan<const std::uint8_t>(Payload, ThreeBytePayloadCount)),
		"A reliable channel with its inner channel set must accept a well-formed send");

	MW_EXPECT_EQ(Test, std::size_t{1}, InnerChannel.SendCallCount(), "The wrapped Data packet must reach the inner channel exactly once");
	MW_EXPECT_EQ(
		Test,
		WrappedThreeByteDataLength,
		InnerChannel.LastSendLength(),
		"The wrapped packet must be ReliableHeaderBytes (3) plus the 3-byte payload");
	MW_EXPECT_EQ(
		Test, static_cast<std::uint8_t>(EReliablePacketKind::Data), InnerChannel.LastSendByte(0), "Byte 0 must be EReliablePacketKind::Data (1)");
	MW_EXPECT_EQ(
		Test,
		static_cast<std::uint8_t>(FirstAssignedSequence & LowByteMask),
		InnerChannel.LastSendByte(1),
		"Byte 1 must be the low byte of the first assigned sequence (1)");
	MW_EXPECT_EQ(
		Test,
		static_cast<std::uint8_t>((FirstAssignedSequence >> HighByteShift) & LowByteMask),
		InnerChannel.LastSendByte(2),
		"Byte 2 must be the high byte of sequence 1");
	MW_EXPECT_EQ(Test, PayloadByte11, InnerChannel.LastSendByte(3), "Byte 3 must be the original payload's first byte");
	MW_EXPECT_EQ(Test, PayloadByte22, InnerChannel.LastSendByte(4), "Byte 4 must be the original payload's second byte");
	MW_EXPECT_EQ(Test, PayloadByte33, InnerChannel.LastSendByte(5), "Byte 5 must be the original payload's third byte");
	MW_EXPECT_EQ(Test, std::size_t{1}, Reliable.PendingCount(), "The sent message must remain pending until acknowledged");
}

/** An inbound Acknowledgement for the outstanding sequence must clear its pending slot and stop further resends. */
MW_TEST_CASE(EngineReliableChannel_AckClearsPending)
{
	FRecordingForwardSink ForwardSink;
	FRecordingInnerChannel InnerChannel;
	FTestReliableChannel Reliable(ForwardSink, MakeTestConfig());
	Reliable.SetInnerChannel(InnerChannel);

	const std::uint8_t Payload[OneBytePayloadCount] = {PayloadByteAA};
	MW_EXPECT_EQ(
		Test,
		EMessageResult::Success,
		Reliable.TrySendEncodedMessage(TSpan<const std::uint8_t>(Payload, OneBytePayloadCount)),
		"The send must be accepted");
	MW_EXPECT_EQ(Test, std::size_t{1}, Reliable.PendingCount(), "The sent message must be pending before any ack arrives");

	// [Acknowledgement][Sequence=1 LE]
	const std::uint8_t AckBytes[ReliableHeaderBytes] = {
		static_cast<std::uint8_t>(EReliablePacketKind::Acknowledgement),
		static_cast<std::uint8_t>(FirstAssignedSequence & LowByteMask),
		static_cast<std::uint8_t>((FirstAssignedSequence >> HighByteShift) & LowByteMask)};
	MW_EXPECT_EQ(
		Test,
		EMessageResult::Success,
		Reliable.ReceiveEncodedMessage(InnerChannelId, TSpan<const std::uint8_t>(AckBytes, ReliableHeaderBytes)),
		"An ack naming the outstanding sequence must be accepted");
	MW_EXPECT_EQ(Test, std::size_t{0}, Reliable.PendingCount(), "The ack must clear the matching pending slot");

	Reliable.PostAdvance(0);
	Reliable.PostAdvance(TestRetryIntervalMilliseconds);
	MW_EXPECT_EQ(Test, std::size_t{1}, InnerChannel.SendCallCount(), "No resend may occur once the pending slot has been acked and cleared");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, Reliable.ResentCount(), "ResentCount must stay zero once the message was acked before any retry was due");
}

/** An unacknowledged send must resend exactly once at the retry interval, never before it. */
MW_TEST_CASE(EngineReliableChannel_NoAckResendsAfterExactlyRetryInterval)
{
	FRecordingForwardSink ForwardSink;
	FRecordingInnerChannel InnerChannel;
	FTestReliableChannel Reliable(ForwardSink, MakeTestConfig());
	Reliable.SetInnerChannel(InnerChannel);

	const std::uint8_t Payload[OneBytePayloadCount] = {PayloadByteBB};
	MW_EXPECT_EQ(
		Test,
		EMessageResult::Success,
		Reliable.TrySendEncodedMessage(TSpan<const std::uint8_t>(Payload, OneBytePayloadCount)),
		"The send must be accepted");
	MW_EXPECT_EQ(Test, std::size_t{1}, InnerChannel.SendCallCount(), "The initial send must reach the inner channel once");

	Reliable.PostAdvance(BaselineTime);
	MW_EXPECT_EQ(
		Test, std::size_t{1}, InnerChannel.SendCallCount(), "The first PostAdvance after a send only establishes the retry baseline, never resends");

	Reliable.PostAdvance(BaselineTime + TestRetryIntervalMilliseconds - 1);
	MW_EXPECT_EQ(Test, std::size_t{1}, InnerChannel.SendCallCount(), "A flush before the retry interval elapses must not resend");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, Reliable.ResentCount(), "ResentCount must stay zero before the retry interval elapses");

	Reliable.PostAdvance(BaselineTime + TestRetryIntervalMilliseconds);
	MW_EXPECT_EQ(Test, std::size_t{2}, InnerChannel.SendCallCount(), "A flush at exactly the retry interval must resend exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, Reliable.ResentCount(), "Exactly one resend must be counted at the retry interval");
}

/** A never-acknowledged send must resend MaxSendAttempts - 1 times, then drop and count as lost, sending no further attempts. */
MW_TEST_CASE(EngineReliableChannel_DropsAfterMaxSendAttempts)
{
	FRecordingForwardSink ForwardSink;
	FRecordingInnerChannel InnerChannel;
	FTestReliableChannel Reliable(ForwardSink, MakeTestConfig());
	Reliable.SetInnerChannel(InnerChannel);

	const std::uint8_t Payload[OneBytePayloadCount] = {PayloadByteCC};
	MW_EXPECT_EQ(
		Test,
		EMessageResult::Success,
		Reliable.TrySendEncodedMessage(TSpan<const std::uint8_t>(Payload, OneBytePayloadCount)),
		"The send must be accepted");

	TimePointMilliseconds Now = BaselineTime;
	Reliable.PostAdvance(Now); // Establishes the retry baseline; the initial send already counts as attempt 1.

	for (int RetryIndex = 0; RetryIndex < TestMaxSendAttempts - 1; ++RetryIndex)
	{
		Now += TestRetryIntervalMilliseconds;
		Reliable.PostAdvance(Now);
	}
	MW_EXPECT_EQ(
		Test,
		std::uint32_t{TestMaxSendAttempts - 1},
		Reliable.ResentCount(),
		"Exactly MaxSendAttempts - 1 resends must occur before the slot exhausts its attempts");
	MW_EXPECT_EQ(Test, std::size_t{1}, Reliable.PendingCount(), "The slot must still be pending after its last allowed resend");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, Reliable.LostCount(), "The slot must not yet be counted lost while attempts remain");

	Now += TestRetryIntervalMilliseconds;
	Reliable.PostAdvance(Now);
	MW_EXPECT_EQ(Test, std::size_t{0}, Reliable.PendingCount(), "The slot must be dropped once MaxSendAttempts is exhausted");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, Reliable.LostCount(), "Exactly one message must be counted lost");

	const std::size_t SendCountAtDrop = InnerChannel.SendCallCount();
	Now += TestRetryIntervalMilliseconds;
	Reliable.PostAdvance(Now);
	MW_EXPECT_EQ(Test, SendCountAtDrop, InnerChannel.SendCallCount(), "No further inner sends may occur once the slot has been dropped");
}

/** The same Data sequence delivered twice must be forwarded only once, while both deliveries still ack and the second counts as a duplicate. */
MW_TEST_CASE(EngineReliableChannel_DuplicateDataForwardedOnceAckedTwice)
{
	FRecordingForwardSink ForwardSink;
	FRecordingInnerChannel InnerChannel;
	FTestReliableChannel Reliable(ForwardSink, MakeTestConfig());
	Reliable.SetInnerChannel(InnerChannel);

	// [Data][Sequence=5 LE][payload=0x77]
	const std::uint8_t DataBytes[FourByteWirePacketCount] = {
		static_cast<std::uint8_t>(EReliablePacketKind::Data),
		static_cast<std::uint8_t>(SequenceFive & LowByteMask),
		static_cast<std::uint8_t>((SequenceFive >> HighByteShift) & LowByteMask),
		PayloadByte77};
	const TSpan<const std::uint8_t> DataView(DataBytes, FourByteWirePacketCount);

	MW_EXPECT_EQ(
		Test,
		EMessageResult::Success,
		Reliable.ReceiveEncodedMessage(InnerChannelId, DataView),
		"The first delivery of a fresh sequence must be accepted");
	MW_EXPECT_EQ(Test, std::size_t{1}, ForwardSink.ForwardedCallCount(), "A fresh Data message must be forwarded once");
	MW_EXPECT_EQ(Test, std::size_t{1}, InnerChannel.SendCallCount(), "The first delivery must send exactly one ack");

	MW_EXPECT_EQ(
		Test,
		EMessageResult::Success,
		Reliable.ReceiveEncodedMessage(InnerChannelId, DataView),
		"A duplicate delivery of the same sequence must still be accepted (acked)");
	MW_EXPECT_EQ(Test, std::size_t{1}, ForwardSink.ForwardedCallCount(), "The duplicate must not be forwarded a second time");
	MW_EXPECT_EQ(
		Test, std::size_t{2}, InnerChannel.SendCallCount(), "The duplicate must still be acked, since the sender's first ack may have been lost");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, Reliable.DuplicateDroppedCount(), "Exactly one duplicate must be counted");
}

/**
 * A fresh sequence exactly one duplicate-window width (32) above the highest must still leave the
 * displaced old highest recognized as seen: a later re-delivery of it must be dropped, not re-forwarded.
 */
MW_TEST_CASE(EngineReliableChannel_WindowEdgeJumpStillDropsOldHighestDuplicate)
{
	FRecordingForwardSink ForwardSink;
	FRecordingInnerChannel InnerChannel;
	FTestReliableChannel Reliable(ForwardSink, MakeTestConfig());
	Reliable.SetInnerChannel(InnerChannel);

	// [Data][Sequence=1 LE][payload]
	const std::uint8_t DataSequence1[FourByteWirePacketCount] = {
		static_cast<std::uint8_t>(EReliablePacketKind::Data),
		static_cast<std::uint8_t>(FirstAssignedSequence & LowByteMask),
		static_cast<std::uint8_t>((FirstAssignedSequence >> HighByteShift) & LowByteMask),
		PayloadByteA1};
	// [Data][Sequence=33 LE][payload], exactly 32 newer
	const std::uint8_t DataSequence33[FourByteWirePacketCount] = {
		static_cast<std::uint8_t>(EReliablePacketKind::Data),
		static_cast<std::uint8_t>(SequenceThirtyThree & LowByteMask),
		static_cast<std::uint8_t>((SequenceThirtyThree >> HighByteShift) & LowByteMask),
		PayloadByteA2};
	const TSpan<const std::uint8_t> Sequence1View(DataSequence1, FourByteWirePacketCount);
	const TSpan<const std::uint8_t> Sequence33View(DataSequence33, FourByteWirePacketCount);

	MW_EXPECT_EQ(
		Test, EMessageResult::Success, Reliable.ReceiveEncodedMessage(InnerChannelId, Sequence1View), "Sequence 1 is fresh and must be accepted");
	MW_EXPECT_EQ(
		Test,
		EMessageResult::Success,
		Reliable.ReceiveEncodedMessage(InnerChannelId, Sequence33View),
		"Sequence 33 is a fresh jump of exactly one window width and must be accepted");
	MW_EXPECT_EQ(Test, std::size_t{2}, ForwardSink.ForwardedCallCount(), "Both fresh sequences must be forwarded once each");

	MW_EXPECT_EQ(
		Test,
		EMessageResult::Success,
		Reliable.ReceiveEncodedMessage(InnerChannelId, Sequence1View),
		"The re-delivered sequence 1 must still be accepted (acked) after the window slid");
	MW_EXPECT_EQ(
		Test,
		std::size_t{2},
		ForwardSink.ForwardedCallCount(),
		"Sequence 1 sits exactly one window width below the highest and must be recognized as seen, not re-forwarded");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, Reliable.DuplicateDroppedCount(), "The re-delivered sequence 1 must count as exactly one duplicate");
}

/** Filling every pending slot then sending once more must report CapacityExceeded transactionally: no state change, no inner send. */
MW_TEST_CASE(EngineReliableChannel_PendingTableFullReportsCapacityExceededTransactionally)
{
	FRecordingForwardSink ForwardSink;
	FRecordingInnerChannel InnerChannel;
	FTestReliableChannel Reliable(ForwardSink, MakeTestConfig());
	Reliable.SetInnerChannel(InnerChannel);

	const std::uint8_t Payload[OneBytePayloadCount] = {PayloadByte01};
	for (std::size_t Index = 0; Index < TestMaxPendingMessages; ++Index)
	{
		MW_EXPECT_EQ(
			Test,
			EMessageResult::Success,
			Reliable.TrySendEncodedMessage(TSpan<const std::uint8_t>(Payload, OneBytePayloadCount)),
			"Filling every pending slot with an unacked send must succeed");
	}
	MW_EXPECT_EQ(Test, TestMaxPendingMessages, Reliable.PendingCount(), "Every pending slot must now be in use");

	const std::size_t SendCountBeforeRejection = InnerChannel.SendCallCount();
	MW_EXPECT_EQ(
		Test,
		EMessageResult::CapacityExceeded,
		Reliable.TrySendEncodedMessage(TSpan<const std::uint8_t>(Payload, OneBytePayloadCount)),
		"A send with every pending slot in use must report CapacityExceeded");
	MW_EXPECT_EQ(Test, TestMaxPendingMessages, Reliable.PendingCount(), "A rejected send must leave the pending table unchanged");
	MW_EXPECT_EQ(Test, SendCountBeforeRejection, InnerChannel.SendCallCount(), "A rejected send must never reach the inner channel (transactional)");
}

/** Before SetInnerChannel is called, sends must report Unavailable and the channel-identity queries must report their unset sentinels. */
MW_TEST_CASE(EngineReliableChannel_UnsetInnerChannelReportsUnavailable)
{
	FRecordingForwardSink ForwardSink;
	FTestReliableChannel Reliable(ForwardSink, MakeTestConfig());

	const std::uint8_t Payload[OneBytePayloadCount] = {PayloadByte01};
	MW_EXPECT_EQ(
		Test,
		EMessageResult::Unavailable,
		Reliable.TrySendEncodedMessage(TSpan<const std::uint8_t>(Payload, OneBytePayloadCount)),
		"A send before SetInnerChannel must report Unavailable");
	MW_EXPECT_EQ(Test, LocalChannelId, Reliable.GetChannelId(), "GetChannelId must report LocalChannelId (0) before SetInnerChannel");
	MW_EXPECT_EQ(Test, std::size_t{0}, Reliable.MaxEncodedMessageBytes(), "MaxEncodedMessageBytes must report 0 before SetInnerChannel");
}

} // namespace
