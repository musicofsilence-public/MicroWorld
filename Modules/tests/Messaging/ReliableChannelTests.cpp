#include "TestSupport.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/ReliableChannel.h>
#include <MicroWorld/Core/Time.h>

#include <cstddef>
#include <cstdint>

namespace
{
using MicroWorld::DurationMilliseconds;
using MicroWorld::HighByteShift;
using MicroWorld::LowByteMask;
using MicroWorld::TimePointMilliseconds;
using MicroWorld::TSpan;
using MicroWorld::Messaging::EMessageResult;
using MicroWorld::Messaging::EReliablePacketKind;
using MicroWorld::Messaging::FMessageChannelId;
using MicroWorld::Messaging::FReliableChannelConfig;
using MicroWorld::Messaging::IEncodedMessageSink;
using MicroWorld::Messaging::IMessageChannel;
using MicroWorld::Messaging::LocalChannelId;
using MicroWorld::Messaging::ReliableHeaderBytes;
using MicroWorld::Messaging::ReliableSequenceFieldByteIndex;
using MicroWorld::Messaging::TReliableChannel;

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

/** Maximum value a 16-bit sequence field can hold; the value the wraparound case drains the sequence space down to. */
constexpr std::uint16_t MaxSequenceValue = 0xFFFFu;

/** Sequence value the allocator must reuse after the field wraps past its maximum, since 0 is reserved as "never sent". */
constexpr std::uint16_t FirstSequenceAfterWrap = MicroWorld::Messaging::FirstOutgoingSequence;

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

/**
 * Scenario: Send a three-byte payload through a reliable channel wired to a recording inner channel.
 * Expected: The send is accepted; the inner channel receives one packet wrapped as [Data][Sequence=1 LE][payload] and the message remains pending
 * until acknowledged.
 */
MW_TEST_CASE(EngineReliableChannel_DataIsWrappedWithKindAndSequence)
{
	// Arrange
	FRecordingForwardSink ForwardSink;
	FRecordingInnerChannel InnerChannel;
	FTestReliableChannel Reliable(ForwardSink, MakeTestConfig());
	Reliable.SetInnerChannel(InnerChannel);

	const std::uint8_t Payload[ThreeBytePayloadCount] = {PayloadByte11, PayloadByte22, PayloadByte33};

	// Act
	const EMessageResult SendResult = Reliable.TrySendEncodedMessage(TSpan<const std::uint8_t>(Payload, ThreeBytePayloadCount));

	// Assert
	MW_EXPECT_EQ(Test, EMessageResult::Success, SendResult, "A reliable channel with its inner channel set must accept a well-formed send");

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

/**
 * Scenario: Send a message, then receive an Acknowledgement naming its sequence, then advance past the retry interval.
 * Expected: The send is accepted and acked; the pending slot is cleared and no resend fires after the interval elapses.
 */
MW_TEST_CASE(EngineReliableChannel_AckClearsPending)
{
	// Arrange
	FRecordingForwardSink ForwardSink;
	FRecordingInnerChannel InnerChannel;
	FTestReliableChannel Reliable(ForwardSink, MakeTestConfig());
	Reliable.SetInnerChannel(InnerChannel);

	const std::uint8_t Payload[OneBytePayloadCount] = {PayloadByteAA};

	// Act
	const EMessageResult SendResult = Reliable.TrySendEncodedMessage(TSpan<const std::uint8_t>(Payload, OneBytePayloadCount));

	// Assert
	MW_EXPECT_EQ(Test, EMessageResult::Success, SendResult, "The send must be accepted");
	MW_EXPECT_EQ(Test, std::size_t{1}, Reliable.PendingCount(), "The sent message must be pending before any ack arrives");

	// Act: deliver an ack for the outstanding sequence.
	// [Acknowledgement][Sequence=1 LE]
	const std::uint8_t AckBytes[ReliableHeaderBytes] = {
		static_cast<std::uint8_t>(EReliablePacketKind::Acknowledgement),
		static_cast<std::uint8_t>(FirstAssignedSequence & LowByteMask),
		static_cast<std::uint8_t>((FirstAssignedSequence >> HighByteShift) & LowByteMask)};
	const EMessageResult AckResult = Reliable.ReceiveEncodedMessage(InnerChannelId, TSpan<const std::uint8_t>(AckBytes, ReliableHeaderBytes));

	// Assert
	MW_EXPECT_EQ(Test, EMessageResult::Success, AckResult, "An ack naming the outstanding sequence must be accepted");
	MW_EXPECT_EQ(Test, std::size_t{0}, Reliable.PendingCount(), "The ack must clear the matching pending slot");

	// Act: advance past the retry interval to prove no resend fires.
	Reliable.PostAdvance(0);
	Reliable.PostAdvance(TestRetryIntervalMilliseconds);
	// Assert
	MW_EXPECT_EQ(Test, std::size_t{1}, InnerChannel.SendCallCount(), "No resend may occur once the pending slot has been acked and cleared");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, Reliable.ResentCount(), "ResentCount must stay zero once the message was acked before any retry was due");
}

/**
 * Scenario: Send a message with no ack, then advance to one-before, then exactly at, the retry interval.
 * Expected: The initial send reaches the inner channel once; the baseline PostAdvance and a flush before the interval never resend; a flush at
 * exactly the interval resends exactly once.
 */
MW_TEST_CASE(EngineReliableChannel_NoAckResendsAfterExactlyRetryInterval)
{
	// Arrange
	FRecordingForwardSink ForwardSink;
	FRecordingInnerChannel InnerChannel;
	FTestReliableChannel Reliable(ForwardSink, MakeTestConfig());
	Reliable.SetInnerChannel(InnerChannel);

	const std::uint8_t Payload[OneBytePayloadCount] = {PayloadByteBB};

	// Act
	const EMessageResult SendResult = Reliable.TrySendEncodedMessage(TSpan<const std::uint8_t>(Payload, OneBytePayloadCount));

	// Assert
	MW_EXPECT_EQ(Test, EMessageResult::Success, SendResult, "The send must be accepted");
	MW_EXPECT_EQ(Test, std::size_t{1}, InnerChannel.SendCallCount(), "The initial send must reach the inner channel once");

	// Act
	Reliable.PostAdvance(BaselineTime);
	// Assert
	MW_EXPECT_EQ(
		Test, std::size_t{1}, InnerChannel.SendCallCount(), "The first PostAdvance after a send only establishes the retry baseline, never resends");

	// Act
	Reliable.PostAdvance(BaselineTime + TestRetryIntervalMilliseconds - 1);
	// Assert
	MW_EXPECT_EQ(Test, std::size_t{1}, InnerChannel.SendCallCount(), "A flush before the retry interval elapses must not resend");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, Reliable.ResentCount(), "ResentCount must stay zero before the retry interval elapses");

	// Act
	Reliable.PostAdvance(BaselineTime + TestRetryIntervalMilliseconds);
	// Assert
	MW_EXPECT_EQ(Test, std::size_t{2}, InnerChannel.SendCallCount(), "A flush at exactly the retry interval must resend exactly once");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, Reliable.ResentCount(), "Exactly one resend must be counted at the retry interval");
}

/**
 * Scenario: Send a message with no ack, then advance through MaxSendAttempts - 1 retries and one further interval, then advance once more.
 * Expected: Exactly MaxSendAttempts - 1 resends fire before the slot exhausts; one further interval drops the slot and counts it as lost; a later
 * flush sends nothing further.
 */
MW_TEST_CASE(EngineReliableChannel_DropsAfterMaxSendAttempts)
{
	// Arrange
	FRecordingForwardSink ForwardSink;
	FRecordingInnerChannel InnerChannel;
	FTestReliableChannel Reliable(ForwardSink, MakeTestConfig());
	Reliable.SetInnerChannel(InnerChannel);

	const std::uint8_t Payload[OneBytePayloadCount] = {PayloadByteCC};

	// Act
	const EMessageResult SendResult = Reliable.TrySendEncodedMessage(TSpan<const std::uint8_t>(Payload, OneBytePayloadCount));

	// Assert
	MW_EXPECT_EQ(Test, EMessageResult::Success, SendResult, "The send must be accepted");

	// Act: establish the retry baseline (the initial send already counts as attempt 1), then drive the allowed retries.
	TimePointMilliseconds Now = BaselineTime;
	Reliable.PostAdvance(Now);
	for (int RetryIndex = 0; RetryIndex < TestMaxSendAttempts - 1; ++RetryIndex)
	{
		Now += TestRetryIntervalMilliseconds;
		Reliable.PostAdvance(Now);
	}

	// Assert
	MW_EXPECT_EQ(
		Test,
		std::uint32_t{TestMaxSendAttempts - 1},
		Reliable.ResentCount(),
		"Exactly MaxSendAttempts - 1 resends must occur before the slot exhausts its attempts");
	MW_EXPECT_EQ(Test, std::size_t{1}, Reliable.PendingCount(), "The slot must still be pending after its last allowed resend");
	MW_EXPECT_EQ(Test, std::uint32_t{0}, Reliable.LostCount(), "The slot must not yet be counted lost while attempts remain");

	// Act: one more interval exhausts the attempt budget.
	Now += TestRetryIntervalMilliseconds;
	Reliable.PostAdvance(Now);
	// Assert
	MW_EXPECT_EQ(Test, std::size_t{0}, Reliable.PendingCount(), "The slot must be dropped once MaxSendAttempts is exhausted");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, Reliable.LostCount(), "Exactly one message must be counted lost");

	// Act: a further flush must not touch the dropped slot.
	const std::size_t SendCountAtDrop = InnerChannel.SendCallCount();
	Now += TestRetryIntervalMilliseconds;
	Reliable.PostAdvance(Now);
	// Assert
	MW_EXPECT_EQ(Test, SendCountAtDrop, InnerChannel.SendCallCount(), "No further inner sends may occur once the slot has been dropped");
}

/**
 * Scenario: Deliver the same Data sequence twice through a reliable channel wired to a recording forward sink and inner channel.
 * Expected: Both deliveries are accepted and both send an ack; the message is forwarded only once and the second delivery counts as exactly one
 * duplicate.
 */
MW_TEST_CASE(EngineReliableChannel_DuplicateDataForwardedOnceAckedTwice)
{
	// Arrange
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

	// Act: first delivery of a fresh sequence.
	const EMessageResult FirstResult = Reliable.ReceiveEncodedMessage(InnerChannelId, DataView);
	// Assert
	MW_EXPECT_EQ(Test, EMessageResult::Success, FirstResult, "The first delivery of a fresh sequence must be accepted");
	MW_EXPECT_EQ(Test, std::size_t{1}, ForwardSink.ForwardedCallCount(), "A fresh Data message must be forwarded once");
	MW_EXPECT_EQ(Test, std::size_t{1}, InnerChannel.SendCallCount(), "The first delivery must send exactly one ack");

	// Act: re-deliver the same sequence.
	const EMessageResult SecondResult = Reliable.ReceiveEncodedMessage(InnerChannelId, DataView);
	// Assert
	MW_EXPECT_EQ(Test, EMessageResult::Success, SecondResult, "A duplicate delivery of the same sequence must still be accepted (acked)");
	MW_EXPECT_EQ(Test, std::size_t{1}, ForwardSink.ForwardedCallCount(), "The duplicate must not be forwarded a second time");
	MW_EXPECT_EQ(
		Test, std::size_t{2}, InnerChannel.SendCallCount(), "The duplicate must still be acked, since the sender's first ack may have been lost");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, Reliable.DuplicateDroppedCount(), "Exactly one duplicate must be counted");
}

/**
 * Scenario: Deliver sequence 1, then jump exactly one window width to sequence 33, then re-deliver sequence 1.
 * Expected: Both fresh sequences are accepted and forwarded once each; the re-delivered sequence 1 is still acked but recognized as seen and counted
 * as exactly one duplicate, not re-forwarded.
 */
MW_TEST_CASE(EngineReliableChannel_WindowEdgeJumpStillDropsOldHighestDuplicate)
{
	// Arrange
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

	// Act: deliver sequence 1, then jump exactly one window width to sequence 33.
	const EMessageResult Seq1Result = Reliable.ReceiveEncodedMessage(InnerChannelId, Sequence1View);
	const EMessageResult Seq33Result = Reliable.ReceiveEncodedMessage(InnerChannelId, Sequence33View);
	// Assert
	MW_EXPECT_EQ(Test, EMessageResult::Success, Seq1Result, "Sequence 1 is fresh and must be accepted");
	MW_EXPECT_EQ(Test, EMessageResult::Success, Seq33Result, "Sequence 33 is a fresh jump of exactly one window width and must be accepted");
	MW_EXPECT_EQ(Test, std::size_t{2}, ForwardSink.ForwardedCallCount(), "Both fresh sequences must be forwarded once each");

	// Act: re-deliver sequence 1, now exactly one window width below the highest.
	const EMessageResult RedeliverResult = Reliable.ReceiveEncodedMessage(InnerChannelId, Sequence1View);
	// Assert
	MW_EXPECT_EQ(Test, EMessageResult::Success, RedeliverResult, "The re-delivered sequence 1 must still be accepted (acked) after the window slid");
	MW_EXPECT_EQ(
		Test,
		std::size_t{2},
		ForwardSink.ForwardedCallCount(),
		"Sequence 1 sits exactly one window width below the highest and must be recognized as seen, not re-forwarded");
	MW_EXPECT_EQ(Test, std::uint32_t{1}, Reliable.DuplicateDroppedCount(), "The re-delivered sequence 1 must count as exactly one duplicate");
}

/**
 * Scenario: Fill every pending slot with unacked sends, then attempt one more send.
 * Expected: The overflow send returns CapacityExceeded; the pending table is unchanged and the inner channel sees no further send.
 */
MW_TEST_CASE(EngineReliableChannel_PendingTableFullReportsCapacityExceededTransactionally)
{
	// Arrange
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

	// Act
	const EMessageResult OverflowResult = Reliable.TrySendEncodedMessage(TSpan<const std::uint8_t>(Payload, OneBytePayloadCount));
	// Assert
	MW_EXPECT_EQ(Test, EMessageResult::CapacityExceeded, OverflowResult, "A send with every pending slot in use must report CapacityExceeded");
	MW_EXPECT_EQ(Test, TestMaxPendingMessages, Reliable.PendingCount(), "A rejected send must leave the pending table unchanged");
	MW_EXPECT_EQ(Test, SendCountBeforeRejection, InnerChannel.SendCallCount(), "A rejected send must never reach the inner channel (transactional)");
}

/**
 * Scenario: Construct a reliable channel without calling SetInnerChannel, then issue a send.
 * Expected: The send returns Unavailable; GetChannelId reports LocalChannelId and MaxEncodedMessageBytes reports zero.
 */
MW_TEST_CASE(EngineReliableChannel_UnsetInnerChannelReportsUnavailable)
{
	// Arrange
	FRecordingForwardSink ForwardSink;
	FTestReliableChannel Reliable(ForwardSink, MakeTestConfig());

	const std::uint8_t Payload[OneBytePayloadCount] = {PayloadByte01};

	// Act
	const EMessageResult SendResult = Reliable.TrySendEncodedMessage(TSpan<const std::uint8_t>(Payload, OneBytePayloadCount));
	// Assert
	MW_EXPECT_EQ(Test, EMessageResult::Unavailable, SendResult, "A send before SetInnerChannel must report Unavailable");
	MW_EXPECT_EQ(Test, LocalChannelId, Reliable.GetChannelId(), "GetChannelId must report LocalChannelId (0) before SetInnerChannel");
	MW_EXPECT_EQ(Test, std::size_t{0}, Reliable.MaxEncodedMessageBytes(), "MaxEncodedMessageBytes must report 0 before SetInnerChannel");
}

/**
 * Scenario: Drain the entire 16-bit sequence space by sending and acking one message at a time, then send once more and ack the wrapped sequence.
 * Expected: The first send after the maximum wraps the sequence to its first value (never zero) as a Data packet and the wrapped send remains
 * ackable, clearing its pending slot.
 */
MW_TEST_CASE(EngineReliableChannel_SequenceWrapsAndWrappedSendRemainsAckable)
{
	// Arrange
	FRecordingForwardSink ForwardSink;
	FRecordingInnerChannel InnerChannel;
	FTestReliableChannel Reliable(ForwardSink, MakeTestConfig());
	Reliable.SetInnerChannel(InnerChannel);

	const std::uint8_t Payload[OneBytePayloadCount] = {PayloadByte01};

	// Act: drain every sequence the 16-bit field can hold (1..0xFFFF) by sending one message and acking it
	// before the next, so the single pending slot never fills. Each ack is built from the sequence the
	// recording inner channel observed on that send, so the matching is correct across the whole space.
	for (std::uint32_t Step = 0; Step < MaxSequenceValue; ++Step)
	{
		MW_EXPECT_EQ(
			Test,
			EMessageResult::Success,
			Reliable.TrySendEncodedMessage(TSpan<const std::uint8_t>(Payload, OneBytePayloadCount)),
			"Every send draining the sequence space must be accepted");

		const std::uint16_t ObservedSequence = static_cast<std::uint16_t>(
			InnerChannel.LastSendByte(ReliableSequenceFieldByteIndex)
			| (static_cast<std::uint16_t>(InnerChannel.LastSendByte(ReliableSequenceFieldByteIndex + 1)) << HighByteShift));

		// [Acknowledgement][ObservedSequence LE]
		const std::uint8_t AckBytes[ReliableHeaderBytes] = {
			static_cast<std::uint8_t>(EReliablePacketKind::Acknowledgement),
			static_cast<std::uint8_t>(ObservedSequence & LowByteMask),
			static_cast<std::uint8_t>((ObservedSequence >> HighByteShift) & LowByteMask)};
		MW_EXPECT_EQ(
			Test,
			EMessageResult::Success,
			Reliable.ReceiveEncodedMessage(InnerChannelId, TSpan<const std::uint8_t>(AckBytes, ReliableHeaderBytes)),
			"Every ack draining the sequence space must clear its pending slot");
	}
	MW_EXPECT_EQ(Test, std::size_t{0}, Reliable.PendingCount(), "Every drained sequence must have been acked before the wrap test begins");

	// Act: the first send after the field maximum is the one that proves the wraparound.
	const EMessageResult WrappedSendResult = Reliable.TrySendEncodedMessage(TSpan<const std::uint8_t>(Payload, OneBytePayloadCount));

	// Assert: the wrapped send still carries Kind=Data and reuses the first sequence number, never 0.
	MW_EXPECT_EQ(Test, EMessageResult::Success, WrappedSendResult, "A send after the sequence space wraps must still be accepted");
	MW_EXPECT_EQ(
		Test, static_cast<std::uint8_t>(EReliablePacketKind::Data), InnerChannel.LastSendByte(0), "The wrapped send must still be a Data packet");
	const std::uint16_t WrappedSequence = static_cast<std::uint16_t>(
		InnerChannel.LastSendByte(ReliableSequenceFieldByteIndex)
		| (static_cast<std::uint16_t>(InnerChannel.LastSendByte(ReliableSequenceFieldByteIndex + 1)) << HighByteShift));
	MW_EXPECT_EQ(
		Test,
		FirstSequenceAfterWrap,
		WrappedSequence,
		"The 16-bit sequence must wrap to its first value (1), never 0, after reaching the field maximum");
	MW_EXPECT_EQ(Test, std::size_t{1}, Reliable.PendingCount(), "The wrapped send must occupy a pending slot like any other");

	// Act: ack the wrapped sequence.
	// [Acknowledgement][WrappedSequence LE]
	const std::uint8_t WrappedAckBytes[ReliableHeaderBytes] = {
		static_cast<std::uint8_t>(EReliablePacketKind::Acknowledgement),
		static_cast<std::uint8_t>(WrappedSequence & LowByteMask),
		static_cast<std::uint8_t>((WrappedSequence >> HighByteShift) & LowByteMask)};
	const EMessageResult WrappedAckResult =
		Reliable.ReceiveEncodedMessage(InnerChannelId, TSpan<const std::uint8_t>(WrappedAckBytes, ReliableHeaderBytes));

	// Assert: the wrapped send remains ackable, proving sequence matching still works after the wrap.
	MW_EXPECT_EQ(Test, EMessageResult::Success, WrappedAckResult, "An ack for the wrapped sequence must be accepted");
	MW_EXPECT_EQ(Test, std::size_t{0}, Reliable.PendingCount(), "An ack for the wrapped sequence must clear its pending slot");
}

} // namespace
