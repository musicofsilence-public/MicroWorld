#include "TestSupport.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Net/ByteWriter.h>
#include <MicroWorld/Net/NetResult.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::ENetResult;
using MicroWorld::FByteWriter;
using MicroWorld::TSpan;

/** Proves an empty writer reports its observed capacity and zero accepted bytes. */
MW_TEST_CASE(ByteWriterStartsEmptyWithObservedCapacity)
{
	std::array<std::uint8_t, 4> Storage{};
	FByteWriter Writer(TSpan<std::uint8_t>(Storage.data(), Storage.size()));

	MW_EXPECT_EQ(Test, static_cast<std::size_t>(4), Writer.Capacity(), "Capacity must match the observed buffer");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Writer.Position(), "An empty writer must report zero position");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(4), Writer.Remaining(), "Remaining must equal capacity before any write");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Writer.Written(), "Written must equal position before any write");
	MW_EXPECT_EQ(Test, true, Writer.WrittenBytes().IsEmpty(), "Written prefix must be empty before any write");
}

/** Proves single-byte writes append in order and advance the cursor exactly one byte. */
MW_TEST_CASE(ByteWriterAppendsOrderedBytes)
{
	std::array<std::uint8_t, 3> Storage{};
	FByteWriter Writer(TSpan<std::uint8_t>(Storage.data(), Storage.size()));

	MW_EXPECT_EQ(Test, ENetResult::Success, Writer.WriteByte(0x10), "First byte write must succeed");
	MW_EXPECT_EQ(Test, ENetResult::Success, Writer.WriteByte(0x20), "Second byte write must succeed");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Writer.Position(), "Two writes must advance the cursor by two");

	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x10), Storage[0], "First storage byte must match the first write");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x20), Storage[1], "Second storage byte must match the second write");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x00), Storage[2], "Third storage byte must remain untouched");
}

/** Proves a span write appends the complete span without altering prior bytes. */
MW_TEST_CASE(ByteWriterAppendsOrderedSpan)
{
	std::array<std::uint8_t, 4> Storage{};
	FByteWriter Writer(TSpan<std::uint8_t>(Storage.data(), Storage.size()));

	Writer.WriteByte(0x01);
	const std::array<std::uint8_t, 2> SpanData{0x02, 0x03};
	const ENetResult SpanResult = Writer.Write(TSpan<const std::uint8_t>(SpanData.data(), SpanData.size()));

	MW_EXPECT_EQ(Test, ENetResult::Success, SpanResult, "Span write within remaining capacity must succeed");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(3), Writer.Position(), "Cursor must advance by the accepted span length");

	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x01), Storage[0], "Prior byte must survive a later span write");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x02), Storage[1], "First span byte must land after prior bytes");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x03), Storage[2], "Second span byte must land after prior bytes");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x00), Storage[3], "Untouched storage must remain zero");
}

/** Proves a byte write at exact capacity succeeds and the next byte write returns Full. */
MW_TEST_CASE(ByteWriterAcceptsExactCapacityThenReportsFull)
{
	std::array<std::uint8_t, 2> Storage{};
	FByteWriter Writer(TSpan<std::uint8_t>(Storage.data(), Storage.size()));

	MW_EXPECT_EQ(Test, ENetResult::Success, Writer.WriteByte(0xAA), "First byte at a 2-byte buffer must succeed");
	MW_EXPECT_EQ(Test, ENetResult::Success, Writer.WriteByte(0xBB), "Second byte filling the buffer must succeed");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Writer.Remaining(), "Remaining must be zero at exact capacity");

	const ENetResult OverflowResult = Writer.WriteByte(0xCC);
	MW_EXPECT_EQ(Test, ENetResult::Full, OverflowResult, "A byte write past capacity must return Full");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Writer.Position(), "Failed write must not advance the cursor");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xAA), Storage[0], "Accepted bytes must survive a failed overflow write");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xBB), Storage[1], "Accepted bytes must survive a failed overflow write");
}

/** Proves a span larger than total capacity returns Invalid because it can never fit. */
MW_TEST_CASE(ByteWriterSpanLargerThanTotalCapacityReturnsInvalid)
{
	std::array<std::uint8_t, 3> Storage{0x00, 0x00, 0x00};
	FByteWriter Writer(TSpan<std::uint8_t>(Storage.data(), Storage.size()));
	const std::size_t PositionBefore = Writer.Position();

	const std::array<std::uint8_t, 4> TooLargeForTotal{0x22, 0x33, 0x44, 0x55};
	const ENetResult OversizedResult = Writer.Write(TSpan<const std::uint8_t>(TooLargeForTotal.data(), TooLargeForTotal.size()));

	MW_EXPECT_EQ(Test, ENetResult::Invalid, OversizedResult, "A span larger than total capacity must return Invalid");
	MW_EXPECT_EQ(Test, PositionBefore, Writer.Position(), "Oversized span must not advance the cursor");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x00), Storage[0], "Storage must remain untouched by an oversized span");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x00), Storage[2], "Storage must remain untouched by an oversized span");
}

/** Proves a span that fits total capacity but exceeds remaining returns Full without partial progress. */
MW_TEST_CASE(ByteWriterSpanExceedingRemainingReturnsFullWithoutPartialProgress)
{
	std::array<std::uint8_t, 3> Storage{0x00, 0x00, 0x00};
	FByteWriter Writer(TSpan<std::uint8_t>(Storage.data(), Storage.size()));
	Writer.WriteByte(0x11);
	const std::size_t PositionBefore = Writer.Position();

	// A 3-byte span fits the total capacity but only 2 bytes remain.
	const std::array<std::uint8_t, 3> FitsTotal{0x22, 0x33, 0x44};
	const ENetResult FullResult = Writer.Write(TSpan<const std::uint8_t>(FitsTotal.data(), FitsTotal.size()));

	MW_EXPECT_EQ(Test, ENetResult::Full, FullResult, "A span exceeding remaining but fitting total must return Full");
	MW_EXPECT_EQ(Test, PositionBefore, Writer.Position(), "Full must not advance the cursor");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x11), Storage[0], "Accepted prefix must survive Full");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x00), Storage[1], "Untouched tail must survive Full");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x00), Storage[2], "Untouched tail must survive Full");
}

/** Proves a null span with nonzero length is rejected as Invalid without cursor movement. */
MW_TEST_CASE(ByteWriterRejectsNullSourceWithNonzeroLength)
{
	std::array<std::uint8_t, 4> Storage{};
	FByteWriter Writer(TSpan<std::uint8_t>(Storage.data(), Storage.size()));
	Writer.WriteByte(0x77);

	const ENetResult NullResult = Writer.Write(TSpan<const std::uint8_t>(nullptr, 3));
	MW_EXPECT_EQ(Test, ENetResult::Invalid, NullResult, "Null data with nonzero length must return Invalid");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Writer.Position(), "Invalid write must not advance the cursor");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x77), Storage[0], "Accepted bytes must survive an invalid write");
}

/** Proves an empty span is a valid no-op whether or not its data pointer is null. */
MW_TEST_CASE(ByteWriterAcceptsEmptySpanAsNoOp)
{
	std::array<std::uint8_t, 2> Storage{};
	FByteWriter Writer(TSpan<std::uint8_t>(Storage.data(), Storage.size()));
	Writer.WriteByte(0x42);

	const ENetResult EmptyDataResult = Writer.Write(TSpan<const std::uint8_t>(Storage.data(), 0));
	MW_EXPECT_EQ(Test, ENetResult::Success, EmptyDataResult, "An empty span with non-null data must be a valid no-op");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Writer.Position(), "Empty span must not advance the cursor");

	const ENetResult NullEmptyResult = Writer.Write(TSpan<const std::uint8_t>(nullptr, 0));
	MW_EXPECT_EQ(Test, ENetResult::Success, NullEmptyResult, "An empty span with null data must be a valid no-op");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(1), Writer.Position(), "Empty null span must not advance the cursor");
}

/** Proves Reset returns the cursor to zero so the caller-owned buffer can be reused. */
MW_TEST_CASE(ByteWriterResetAllowsBufferReuse)
{
	std::array<std::uint8_t, 2> Storage{};
	FByteWriter Writer(TSpan<std::uint8_t>(Storage.data(), Storage.size()));
	Writer.WriteByte(0x01);
	Writer.WriteByte(0x02);

	Writer.Reset();
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Writer.Position(), "Reset must return the cursor to zero");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Writer.Remaining(), "Reset must restore remaining to capacity");

	const ENetResult RewriteResult = Writer.WriteByte(0x99);
	MW_EXPECT_EQ(Test, ENetResult::Success, RewriteResult, "A byte write after reset must succeed");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x99), Storage[0], "Rewrite must overwrite the first storage byte");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x02), Storage[1], "Prior second byte must remain untouched by reset");
}

/** Proves WrittenBytes exposes exactly the accepted prefix without exposing mutable storage. */
MW_TEST_CASE(ByteWriterReportsAcceptedPrefixView)
{
	std::array<std::uint8_t, 4> Storage{};
	FByteWriter Writer(TSpan<std::uint8_t>(Storage.data(), Storage.size()));
	Writer.WriteByte(0x10);
	Writer.WriteByte(0x20);

	const TSpan<const std::uint8_t> Accepted = Writer.WrittenBytes();
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Accepted.Size(), "Written view must report the accepted prefix length");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x10), Accepted[0], "Written view must expose the first accepted byte");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x20), Accepted[1], "Written view must expose the second accepted byte");
}

/** Proves a writer bound to an invalid {nullptr, nonzero} buffer never dereferences null. */
MW_TEST_CASE(ByteWriterInvalidBackingBufferNeverDereferencesNull)
{
	FByteWriter Writer(TSpan<std::uint8_t>(nullptr, 4));

	// Query operations must remain safely callable and report the invalid configuration.
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(4), Writer.Capacity(), "Capacity reports the observed size even for an invalid buffer");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Writer.Position(), "An invalid buffer must start at zero position");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(4), Writer.Remaining(), "Remaining reports observed size minus zero position");
	MW_EXPECT_EQ(Test, true, Writer.WrittenBytes().IsEmpty(), "WrittenBytes must return an empty view for an invalid buffer");

	// Mutating operations must return Invalid without advancing the cursor or touching storage.
	MW_EXPECT_EQ(Test, ENetResult::Invalid, Writer.WriteByte(0x01), "WriteByte on an invalid buffer must return Invalid");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Writer.Position(), "Invalid buffer must never advance the cursor");

	const std::array<std::uint8_t, 2> Packet{0x02, 0x03};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Invalid,
		Writer.Write(TSpan<const std::uint8_t>(Packet.data(), Packet.size())),
		"Write on an invalid buffer must return Invalid");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Writer.Position(), "Invalid buffer must never advance the cursor on span write");
}

/** Proves a writer bound to a valid empty {nullptr, 0} buffer accepts only empty spans and reports Full otherwise. */
MW_TEST_CASE(ByteWriterValidEmptyBufferAcceptsOnlyEmptySpans)
{
	FByteWriter Writer(TSpan<std::uint8_t>(nullptr, 0));

	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Writer.Capacity(), "A zero-capacity buffer reports zero capacity");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Writer.Remaining(), "A zero-capacity buffer reports zero remaining");
	MW_EXPECT_EQ(Test, true, Writer.WrittenBytes().IsEmpty(), "WrittenBytes is empty for a zero-capacity buffer");

	MW_EXPECT_EQ(
		Test, ENetResult::Success, Writer.Write(TSpan<const std::uint8_t>(nullptr, 0)), "An empty span is a valid no-op on a zero-capacity buffer");
	MW_EXPECT_EQ(Test, ENetResult::Full, Writer.WriteByte(0x01), "WriteByte on a zero-capacity buffer must return Full");
	const std::array<std::uint8_t, 1> OneByte{0x02};
	MW_EXPECT_EQ(
		Test,
		ENetResult::Invalid,
		Writer.Write(TSpan<const std::uint8_t>(OneByte.data(), OneByte.size())),
		"A span larger than total capacity must return Invalid");
}

} // namespace
