#include "TestSupport.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Transport/ByteWriter.h>

#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::Core::ETransportResult;
using MicroWorld::Core::TSpan;
using MicroWorld::Transport::FByteWriter;

/** Motivation: Value the value-initialized storage bytes hold before any write, so a write is observable. */
constexpr std::uint8_t UntouchedStorageByte = 0x00;

/** Motivation: Distinct payload bytes the ordered-write tests thread through the writer. */
constexpr std::uint8_t WriteByte01 = 0x01;
constexpr std::uint8_t WriteByte02 = 0x02;
constexpr std::uint8_t WriteByte03 = 0x03;
constexpr std::uint8_t WriteByte04 = 0x04;
constexpr std::uint8_t WriteByte10 = 0x10;
constexpr std::uint8_t WriteByte11 = 0x11;
constexpr std::uint8_t WriteByte20 = 0x20;
constexpr std::uint8_t WriteByte22 = 0x22;
constexpr std::uint8_t WriteByte33 = 0x33;
constexpr std::uint8_t WriteByte42 = 0x42;
constexpr std::uint8_t WriteByte44 = 0x44;
constexpr std::uint8_t WriteByte55 = 0x55;
constexpr std::uint8_t WriteByte77 = 0x77;
constexpr std::uint8_t WriteByte99 = 0x99;
constexpr std::uint8_t WriteByteAA = 0xAA;
constexpr std::uint8_t WriteByteBB = 0xBB;
constexpr std::uint8_t WriteByteCC = 0xCC;

/** Motivation: Storage/span byte counts the capacity and boundary tests exercise. */
constexpr std::size_t FourByteBufferCount = 4;
constexpr std::size_t ThreeByteBufferCount = 3;
constexpr std::size_t TwoByteBufferCount = 2;
constexpr std::size_t OneByteBufferCount = 1;

/**
 * Motivation: Construct a fresh byte writer over a four-byte buffer.
 * Responsibilities: The writer reports its observed capacity, zero position, full remaining capacity, and an empty
 *   written prefix.
 */
MW_TEST_CASE(ByteWriterStartsEmptyWithObservedCapacity)
{
	// Arrange
	std::uint8_t Storage[FourByteBufferCount] = {};
	FByteWriter Writer(TSpan<std::uint8_t>(Storage, FourByteBufferCount));

	// Assert
	MW_EXPECT_EQ(Test, FourByteBufferCount, Writer.Capacity(), "Capacity must match the observed buffer");
	MW_EXPECT_EQ(Test, std::size_t{0}, Writer.Position(), "An empty writer must report zero position");
	MW_EXPECT_EQ(Test, FourByteBufferCount, Writer.Remaining(), "Remaining must equal capacity before any write");
	MW_EXPECT_EQ(Test, std::size_t{0}, Writer.Position(), "Position must be zero before any write");
	MW_EXPECT_EQ(Test, true, Writer.WrittenBytes().IsEmpty(), "Written prefix must be empty before any write");
}

/**
 * Motivation: Write two single bytes in sequence into a three-byte buffer.
 * Responsibilities: Each byte is appended in storage order, the cursor advances by exactly one byte per write, and the
 *   trailing byte stays untouched.
 */
MW_TEST_CASE(ByteWriterAppendsOrderedBytes)
{
	// Arrange
	std::uint8_t Storage[ThreeByteBufferCount] = {};
	FByteWriter Writer(TSpan<std::uint8_t>(Storage, ThreeByteBufferCount));

	// Act
	MW_EXPECT_EQ(Test, ETransportResult::Success, Writer.WriteByte(WriteByte10), "First byte write must succeed");
	MW_EXPECT_EQ(Test, ETransportResult::Success, Writer.WriteByte(WriteByte20), "Second byte write must succeed");
	MW_EXPECT_EQ(Test, TwoByteBufferCount, Writer.Position(), "Two writes must advance the cursor by two");

	// Assert
	MW_EXPECT_EQ(Test, WriteByte10, Storage[0], "First storage byte must match the first write");
	MW_EXPECT_EQ(Test, WriteByte20, Storage[1], "Second storage byte must match the second write");
	MW_EXPECT_EQ(Test, UntouchedStorageByte, Storage[2], "Third storage byte must remain untouched");
}

/**
 * Motivation: Write one byte, then append a two-byte span to the writer.
 * Responsibilities: The span is appended after the prior byte in source order, prior bytes are not altered, and the
 *   cursor advances by the accepted span.
 */
MW_TEST_CASE(ByteWriterAppendsOrderedSpan)
{
	// Arrange
	std::uint8_t Storage[FourByteBufferCount] = {};
	FByteWriter Writer(TSpan<std::uint8_t>(Storage, FourByteBufferCount));

	Writer.WriteByte(WriteByte01);
	const std::uint8_t SpanData[TwoByteBufferCount] = {WriteByte02, WriteByte03};
	// Act
	const ETransportResult SpanResult = Writer.Write(TSpan<const std::uint8_t>(SpanData, TwoByteBufferCount));

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, SpanResult, "Span write within remaining capacity must succeed");
	MW_EXPECT_EQ(Test, ThreeByteBufferCount, Writer.Position(), "Cursor must advance by the accepted span length");

	MW_EXPECT_EQ(Test, WriteByte01, Storage[0], "Prior byte must survive a later span write");
	MW_EXPECT_EQ(Test, WriteByte02, Storage[1], "First span byte must land after prior bytes");
	MW_EXPECT_EQ(Test, WriteByte03, Storage[2], "Second span byte must land after prior bytes");
	MW_EXPECT_EQ(Test, UntouchedStorageByte, Storage[3], "Untouched storage must remain zero");
}

/**
 * Motivation: Write two bytes to the exact capacity of a two-byte buffer, then attempt one more byte.
 * Responsibilities: The capacity-filling writes succeed; the overflow write returns Full, does not advance the cursor,
 *   and leaves accepted bytes intact.
 */
MW_TEST_CASE(ByteWriterAcceptsExactCapacityThenReportsFull)
{
	// Arrange
	std::uint8_t Storage[TwoByteBufferCount] = {};
	FByteWriter Writer(TSpan<std::uint8_t>(Storage, TwoByteBufferCount));

	// Act
	MW_EXPECT_EQ(Test, ETransportResult::Success, Writer.WriteByte(WriteByteAA), "First byte at a 2-byte buffer must succeed");
	MW_EXPECT_EQ(Test, ETransportResult::Success, Writer.WriteByte(WriteByteBB), "Second byte filling the buffer must succeed");
	// Assert
	MW_EXPECT_EQ(Test, std::size_t{0}, Writer.Remaining(), "Remaining must be zero at exact capacity");

	// Act
	const ETransportResult OverflowResult = Writer.WriteByte(WriteByteCC);
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Full, OverflowResult, "A byte write past capacity must return Full");
	MW_EXPECT_EQ(Test, TwoByteBufferCount, Writer.Position(), "Failed write must not advance the cursor");
	MW_EXPECT_EQ(Test, WriteByteAA, Storage[0], "Accepted bytes must survive a failed overflow write");
	MW_EXPECT_EQ(Test, WriteByteBB, Storage[1], "Accepted bytes must survive a failed overflow write");
}

/**
 * Motivation: Attempt a span write larger than the total capacity into a pre-filled buffer.
 * Responsibilities: The write returns Invalid, does not advance the cursor, and leaves the storage untouched.
 */
MW_TEST_CASE(ByteWriterSpanLargerThanTotalCapacityReturnsInvalid)
{
	// Arrange
	std::uint8_t Storage[ThreeByteBufferCount] = {UntouchedStorageByte, UntouchedStorageByte, UntouchedStorageByte};
	FByteWriter Writer(TSpan<std::uint8_t>(Storage, ThreeByteBufferCount));
	const std::size_t PositionBefore = Writer.Position();

	const std::uint8_t TooLargeForTotal[FourByteBufferCount] = {WriteByte22, WriteByte33, WriteByte44, WriteByte55};
	// Act
	const ETransportResult OversizedResult = Writer.Write(TSpan<const std::uint8_t>(TooLargeForTotal, FourByteBufferCount));

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, OversizedResult, "A span larger than total capacity must return Invalid");
	MW_EXPECT_EQ(Test, PositionBefore, Writer.Position(), "Oversized span must not advance the cursor");
	MW_EXPECT_EQ(Test, UntouchedStorageByte, Storage[0], "Storage must remain untouched by an oversized span");
	MW_EXPECT_EQ(Test, UntouchedStorageByte, Storage[2], "Storage must remain untouched by an oversized span");
}

/**
 * Motivation: Write one byte into a three-byte buffer, then attempt a three-byte span that fits total capacity but
 *   exceeds remaining.
 * Responsibilities: The write returns Full, does not advance the cursor, makes no partial progress, and leaves both the
 *   accepted prefix and the untouched.
 */
MW_TEST_CASE(ByteWriterSpanExceedingRemainingReturnsFullWithoutPartialProgress)
{
	// Arrange
	std::uint8_t Storage[ThreeByteBufferCount] = {UntouchedStorageByte, UntouchedStorageByte, UntouchedStorageByte};
	FByteWriter Writer(TSpan<std::uint8_t>(Storage, ThreeByteBufferCount));
	Writer.WriteByte(WriteByte11);
	const std::size_t PositionBefore = Writer.Position();

	// A 3-byte span fits the total capacity but only 2 bytes remain.
	const std::uint8_t FitsTotal[ThreeByteBufferCount] = {WriteByte22, WriteByte33, WriteByte44};
	// Act
	const ETransportResult FullResult = Writer.Write(TSpan<const std::uint8_t>(FitsTotal, ThreeByteBufferCount));

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Full, FullResult, "A span exceeding remaining but fitting total must return Full");
	MW_EXPECT_EQ(Test, PositionBefore, Writer.Position(), "Full must not advance the cursor");
	MW_EXPECT_EQ(Test, WriteByte11, Storage[0], "Accepted prefix must survive Full");
	MW_EXPECT_EQ(Test, UntouchedStorageByte, Storage[1], "Untouched tail must survive Full");
	MW_EXPECT_EQ(Test, UntouchedStorageByte, Storage[2], "Untouched tail must survive Full");
}

/**
 * Motivation: Write one byte, then attempt a span write with a null data pointer and nonzero length.
 * Responsibilities: The write returns Invalid, does not advance the cursor, and leaves accepted bytes intact.
 */
MW_TEST_CASE(ByteWriterRejectsNullSourceWithNonzeroLength)
{
	// Arrange
	std::uint8_t Storage[FourByteBufferCount] = {};
	FByteWriter Writer(TSpan<std::uint8_t>(Storage, FourByteBufferCount));
	Writer.WriteByte(WriteByte77);

	// Act
	const ETransportResult NullResult = Writer.Write(TSpan<const std::uint8_t>(nullptr, ThreeByteBufferCount));
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, NullResult, "Null data with nonzero length must return Invalid");
	MW_EXPECT_EQ(Test, OneByteBufferCount, Writer.Position(), "Invalid write must not advance the cursor");
	MW_EXPECT_EQ(Test, WriteByte77, Storage[0], "Accepted bytes must survive an invalid write");
}

/**
 * Motivation: Write one byte, then attempt empty-span writes with both non-null and null data pointers.
 * Responsibilities: Each empty span is a valid no-op that does not advance the cursor.
 */
MW_TEST_CASE(ByteWriterAcceptsEmptySpanAsNoOp)
{
	// Arrange
	std::uint8_t Storage[TwoByteBufferCount] = {};
	FByteWriter Writer(TSpan<std::uint8_t>(Storage, TwoByteBufferCount));
	Writer.WriteByte(WriteByte42);

	// Act
	const ETransportResult EmptyDataResult = Writer.Write(TSpan<const std::uint8_t>(Storage, 0));
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, EmptyDataResult, "An empty span with non-null data must be a valid no-op");
	MW_EXPECT_EQ(Test, OneByteBufferCount, Writer.Position(), "Empty span must not advance the cursor");

	// Act
	const ETransportResult NullEmptyResult = Writer.Write(TSpan<const std::uint8_t>(nullptr, 0));
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, NullEmptyResult, "An empty span with null data must be a valid no-op");
	MW_EXPECT_EQ(Test, OneByteBufferCount, Writer.Position(), "Empty null span must not advance the cursor");
}

/**
 * Motivation: Fill a two-byte buffer, reset the writer, then write one byte again.
 * Responsibilities: Reset returns the cursor to zero and restores remaining to capacity, and the rewrite overwrites the
 *   first byte while leaving the second.
 */
MW_TEST_CASE(ByteWriterResetAllowsBufferReuse)
{
	// Arrange
	std::uint8_t Storage[TwoByteBufferCount] = {};
	FByteWriter Writer(TSpan<std::uint8_t>(Storage, TwoByteBufferCount));
	Writer.WriteByte(WriteByte01);
	Writer.WriteByte(WriteByte02);

	// Act
	Writer.Reset();
	// Assert
	MW_EXPECT_EQ(Test, std::size_t{0}, Writer.Position(), "Reset must return the cursor to zero");
	MW_EXPECT_EQ(Test, TwoByteBufferCount, Writer.Remaining(), "Reset must restore remaining to capacity");

	// Act
	const ETransportResult RewriteResult = Writer.WriteByte(WriteByte99);
	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, RewriteResult, "A byte write after reset must succeed");
	MW_EXPECT_EQ(Test, WriteByte99, Storage[0], "Rewrite must overwrite the first storage byte");
	MW_EXPECT_EQ(Test, WriteByte02, Storage[1], "Prior second byte must remain untouched by reset");
}

/**
 * Motivation: Write two bytes into a four-byte buffer, then read the written-bytes view.
 * Responsibilities: The view exposes exactly the accepted prefix length and bytes without exposing mutable storage.
 */
MW_TEST_CASE(ByteWriterReportsAcceptedPrefixView)
{
	// Arrange
	std::uint8_t Storage[FourByteBufferCount] = {};
	FByteWriter Writer(TSpan<std::uint8_t>(Storage, FourByteBufferCount));
	Writer.WriteByte(WriteByte10);
	Writer.WriteByte(WriteByte20);

	// Act
	const TSpan<const std::uint8_t> Accepted = Writer.WrittenBytes();
	// Assert
	MW_EXPECT_EQ(Test, TwoByteBufferCount, Accepted.Size(), "Written view must report the accepted prefix length");
	MW_EXPECT_EQ(Test, WriteByte10, Accepted[0], "Written view must expose the first accepted byte");
	MW_EXPECT_EQ(Test, WriteByte20, Accepted[1], "Written view must expose the second accepted byte");
}

/**
 * Motivation: Bind a writer to an invalid {nullptr, nonzero} buffer and exercise query, single-byte write, and
 *   span-write operations.
 * Responsibilities: Query operations report the observed configuration; mutating operations return Invalid without
 *   advancing the cursor or touching storage.
 */
MW_TEST_CASE(ByteWriterInvalidBackingBufferNeverDereferencesNull)
{
	// Arrange
	FByteWriter Writer(TSpan<std::uint8_t>(nullptr, FourByteBufferCount));

	// Assert - query operations must remain safely callable and report the invalid configuration.
	MW_EXPECT_EQ(Test, FourByteBufferCount, Writer.Capacity(), "Capacity reports the observed size even for an invalid buffer");
	MW_EXPECT_EQ(Test, std::size_t{0}, Writer.Position(), "An invalid buffer must start at zero position");
	MW_EXPECT_EQ(Test, FourByteBufferCount, Writer.Remaining(), "Remaining reports observed size minus zero position");
	MW_EXPECT_EQ(Test, true, Writer.WrittenBytes().IsEmpty(), "WrittenBytes must return an empty view for an invalid buffer");

	// Assert - mutating operations must return Invalid without advancing the cursor or touching storage.
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, Writer.WriteByte(WriteByte01), "WriteByte on an invalid buffer must return Invalid");
	MW_EXPECT_EQ(Test, std::size_t{0}, Writer.Position(), "Invalid buffer must never advance the cursor");

	const std::uint8_t Packet[TwoByteBufferCount] = {WriteByte02, WriteByte03};
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Invalid,
		Writer.Write(TSpan<const std::uint8_t>(Packet, TwoByteBufferCount)),
		"Write on an invalid buffer must return Invalid");
	MW_EXPECT_EQ(Test, std::size_t{0}, Writer.Position(), "Invalid buffer must never advance the cursor on span write");
}

/**
 * Motivation: Bind a writer to a valid empty {nullptr, 0} buffer, then attempt an empty span, a single byte, and a
 *   span larger than total capacity.
 * Responsibilities: The writer reports zero capacity and empty written bytes, accepts the empty span as a no-op, and
 *   returns Full for the single byte and.
 */
MW_TEST_CASE(ByteWriterValidEmptyBufferAcceptsOnlyEmptySpans)
{
	// Arrange
	FByteWriter Writer(TSpan<std::uint8_t>(nullptr, 0));

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{0}, Writer.Capacity(), "A zero-capacity buffer reports zero capacity");
	MW_EXPECT_EQ(Test, std::size_t{0}, Writer.Remaining(), "A zero-capacity buffer reports zero remaining");
	MW_EXPECT_EQ(Test, true, Writer.WrittenBytes().IsEmpty(), "WrittenBytes is empty for a zero-capacity buffer");

	// Act / Assert - an empty span is the only accepted write.
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Success,
		Writer.Write(TSpan<const std::uint8_t>(nullptr, 0)),
		"An empty span is a valid no-op on a zero-capacity buffer");
	MW_EXPECT_EQ(Test, ETransportResult::Full, Writer.WriteByte(WriteByte01), "WriteByte on a zero-capacity buffer must return Full");
	const std::uint8_t OneByte[OneByteBufferCount] = {WriteByte02};
	MW_EXPECT_EQ(
		Test,
		ETransportResult::Invalid,
		Writer.Write(TSpan<const std::uint8_t>(OneByte, OneByteBufferCount)),
		"A span larger than total capacity must return Invalid");
}

} // namespace
