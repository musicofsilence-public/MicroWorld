#include "TestSupport.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Net/ByteReader.h>
#include <MicroWorld/Net/NetResult.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::ENetResult;
using MicroWorld::FByteReader;
using MicroWorld::TSpan;

/** Proves a fresh reader reports its source length and zero consumed bytes. */
MW_TEST_CASE(ByteReaderStartsAtZeroConsumed)
{
	const std::array<std::uint8_t, 4> Source{0x10, 0x20, 0x30, 0x40};
	FByteReader Reader(TSpan<const std::uint8_t>(Source.data(), Source.size()));

	MW_EXPECT_EQ(Test, static_cast<std::size_t>(4), Reader.Capacity(), "Capacity must match the observed source length");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Reader.Position(), "A fresh reader must report zero position");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(4), Reader.Remaining(), "Remaining must equal capacity before any read");
}

/** Proves single-byte reads return ordered source bytes and advance exactly one byte. */
MW_TEST_CASE(ByteReaderReturnsOrderedBytes)
{
	const std::array<std::uint8_t, 3> Source{0xAA, 0xBB, 0xCC};
	FByteReader Reader(TSpan<const std::uint8_t>(Source.data(), Source.size()));

	std::uint8_t FirstByte = 0;
	std::uint8_t SecondByte = 0;
	MW_EXPECT_EQ(Test, ENetResult::Success, Reader.ReadByte(FirstByte), "First byte read must succeed");
	MW_EXPECT_EQ(Test, ENetResult::Success, Reader.ReadByte(SecondByte), "Second byte read must succeed");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Reader.Position(), "Two reads must advance the cursor by two");

	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xAA), FirstByte, "First read must return the first source byte");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xBB), SecondByte, "Second read must return the second source byte");
}

/** Proves a span read copies the complete span and advances the cursor exactly that far. */
MW_TEST_CASE(ByteReaderCopiesOrderedSpan)
{
	const std::array<std::uint8_t, 4> Source{0x01, 0x02, 0x03, 0x04};
	FByteReader Reader(TSpan<const std::uint8_t>(Source.data(), Source.size()));

	std::array<std::uint8_t, 2> Destination{0xFF, 0xFF};
	const ENetResult SpanResult = Reader.Read(TSpan<std::uint8_t>(Destination.data(), Destination.size()));

	MW_EXPECT_EQ(Test, ENetResult::Success, SpanResult, "Span read within remaining must succeed");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Reader.Position(), "Cursor must advance by the read length");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x01), Destination[0], "First destination byte must match source order");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x02), Destination[1], "Second destination byte must match source order");
}

/** Proves reading at the exact boundary succeeds and the next read returns Invalid (truncated). */
MW_TEST_CASE(ByteReaderAcceptsExactBoundaryThenReportsInvalid)
{
	const std::array<std::uint8_t, 2> Source{0x11, 0x22};
	FByteReader Reader(TSpan<const std::uint8_t>(Source.data(), Source.size()));

	std::uint8_t FirstByte = 0;
	std::uint8_t SecondByte = 0;
	MW_EXPECT_EQ(Test, ENetResult::Success, Reader.ReadByte(FirstByte), "Read at start must succeed");
	MW_EXPECT_EQ(Test, ENetResult::Success, Reader.ReadByte(SecondByte), "Read at exact boundary must succeed");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Reader.Remaining(), "Remaining must be zero at the boundary");

	std::uint8_t UnusedByte = 0xEE;
	const ENetResult OverflowResult = Reader.ReadByte(UnusedByte);
	MW_EXPECT_EQ(Test, ENetResult::Invalid, OverflowResult, "A read past the source must return Invalid (truncated request)");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xEE), UnusedByte, "Failed read must not modify the output parameter");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(2), Reader.Position(), "Failed read must not advance the cursor");
}

/** Proves a span read larger than the remaining source returns Invalid without modifying outputs. */
MW_TEST_CASE(ByteReaderTruncatedSpanReadLeavesCursorAndOutputUnchanged)
{
	const std::array<std::uint8_t, 2> Source{0x10, 0x20};
	FByteReader Reader(TSpan<const std::uint8_t>(Source.data(), Source.size()));

	std::array<std::uint8_t, 4> Destination{0xFF, 0xFF, 0xFF, 0xFF};
	const ENetResult TruncatedResult = Reader.Read(TSpan<std::uint8_t>(Destination.data(), Destination.size()));

	MW_EXPECT_EQ(Test, ENetResult::Invalid, TruncatedResult, "A read larger than remaining must return Invalid (truncated)");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Reader.Position(), "Truncated read must not advance the cursor");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xFF), Destination[0], "Truncated read must not modify the destination");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xFF), Destination[1], "Truncated read must not modify the destination");
}

/** Proves a null destination with nonzero length is rejected as Invalid without cursor movement. */
MW_TEST_CASE(ByteReaderRejectsNullDestinationWithNonzeroLength)
{
	const std::array<std::uint8_t, 4> Source{0x10, 0x20, 0x30, 0x40};
	FByteReader Reader(TSpan<const std::uint8_t>(Source.data(), Source.size()));

	const ENetResult NullResult = Reader.Read(TSpan<std::uint8_t>(nullptr, 2));
	MW_EXPECT_EQ(Test, ENetResult::Invalid, NullResult, "Null destination with nonzero length must return Invalid");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Reader.Position(), "Invalid read must not advance the cursor");
}

/** Proves an empty destination is a valid no-op whether or not its data pointer is null. */
MW_TEST_CASE(ByteReaderAcceptsEmptyDestinationAsNoOp)
{
	const std::array<std::uint8_t, 2> Source{0x10, 0x20};
	FByteReader Reader(TSpan<const std::uint8_t>(Source.data(), Source.size()));

	const ENetResult EmptyResult = Reader.Read(TSpan<std::uint8_t>(nullptr, 0));
	MW_EXPECT_EQ(Test, ENetResult::Success, EmptyResult, "An empty destination must be a valid no-op");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Reader.Position(), "Empty destination must not advance the cursor");
}

/** Proves PeekByte observes the next byte without advancing the cursor. */
MW_TEST_CASE(ByteReaderPeeksWithoutAdvancing)
{
	const std::array<std::uint8_t, 2> Source{0x42, 0x99};
	FByteReader Reader(TSpan<const std::uint8_t>(Source.data(), Source.size()));

	std::uint8_t Peeked = 0;
	const ENetResult PeekResult = Reader.PeekByte(Peeked);
	MW_EXPECT_EQ(Test, ENetResult::Success, PeekResult, "Peek at a non-empty source must succeed");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x42), Peeked, "Peek must return the first source byte");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Reader.Position(), "Peek must not advance the cursor");

	std::uint8_t PeekedAgain = 0;
	MW_EXPECT_EQ(Test, ENetResult::Success, Reader.PeekByte(PeekedAgain), "Second peek at the same cursor must succeed");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x42), PeekedAgain, "Second peek must return the same byte");
}

/** Proves PeekByte past the source returns Invalid without modifying its output. */
MW_TEST_CASE(ByteReaderPeekPastSourceReturnsInvalid)
{
	const std::array<std::uint8_t, 1> Source{0x07};
	FByteReader Reader(TSpan<const std::uint8_t>(Source.data(), Source.size()));

	std::uint8_t Consumed = 0;
	Reader.ReadByte(Consumed);

	std::uint8_t Peeked = 0xEE;
	const ENetResult PeekResult = Reader.PeekByte(Peeked);
	MW_EXPECT_EQ(Test, ENetResult::Invalid, PeekResult, "Peek past the source must return Invalid");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xEE), Peeked, "Failed peek must not modify its output");
}

/** Proves Reset returns the cursor to zero so the caller-owned source can be re-parsed. */
MW_TEST_CASE(ByteReaderResetAllowsSourceReparse)
{
	const std::array<std::uint8_t, 2> Source{0x55, 0x66};
	FByteReader Reader(TSpan<const std::uint8_t>(Source.data(), Source.size()));

	std::uint8_t First = 0;
	Reader.ReadByte(First);
	Reader.Reset();

	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Reader.Position(), "Reset must return the cursor to zero");
	std::uint8_t ReRead = 0;
	MW_EXPECT_EQ(Test, ENetResult::Success, Reader.ReadByte(ReRead), "Read after reset must succeed");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x55), ReRead, "Read after reset must return the first source byte again");
}

/** Proves RemainingBytes exposes the unconsumed suffix without copying or exposing mutable storage. */
MW_TEST_CASE(ByteReaderReportsRemainingSuffixView)
{
	const std::array<std::uint8_t, 4> Source{0x01, 0x02, 0x03, 0x04};
	FByteReader Reader(TSpan<const std::uint8_t>(Source.data(), Source.size()));

	std::uint8_t Consumed = 0;
	Reader.ReadByte(Consumed);

	const TSpan<const std::uint8_t> Remaining = Reader.RemainingBytes();
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(3), Remaining.Size(), "Remaining view must report the unconsumed suffix length");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x02), Remaining[0], "Remaining view must expose the next unconsumed byte");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0x04), Remaining[2], "Remaining view must expose the last unconsumed byte");
}

/** Proves a reader bound to an invalid {nullptr, nonzero} source never dereferences null. */
MW_TEST_CASE(ByteReaderInvalidBackingSourceNeverDereferencesNull)
{
	FByteReader Reader(TSpan<const std::uint8_t>(nullptr, 4));

	// Query operations must remain safely callable and report the invalid configuration.
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(4), Reader.Capacity(), "Capacity reports the observed size even for an invalid source");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Reader.Position(), "An invalid source must start at zero position");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(4), Reader.Remaining(), "Remaining reports observed size minus zero position");
	MW_EXPECT_EQ(Test, true, Reader.RemainingBytes().IsEmpty(), "RemainingBytes must return an empty view for an invalid source");
	MW_EXPECT_EQ(
		Test, true, Reader.RemainingBytes().Data() == nullptr, "RemainingBytes must not synthesize a non-null data pointer for an invalid source");

	// Consuming operations must return Invalid without advancing the cursor or modifying outputs.
	std::uint8_t OutByte = 0xEE;
	MW_EXPECT_EQ(Test, ENetResult::Invalid, Reader.ReadByte(OutByte), "ReadByte on an invalid source must return Invalid");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xEE), OutByte, "ReadByte must not modify its output on an invalid source");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Reader.Position(), "Invalid source must never advance the cursor");

	std::uint8_t Peeked = 0xEE;
	MW_EXPECT_EQ(Test, ENetResult::Invalid, Reader.PeekByte(Peeked), "PeekByte on an invalid source must return Invalid");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xEE), Peeked, "PeekByte must not modify its output on an invalid source");

	std::array<std::uint8_t, 2> Destination{0xFF, 0xFF};
	const ENetResult SpanResult = Reader.Read(TSpan<std::uint8_t>(Destination.data(), Destination.size()));
	MW_EXPECT_EQ(Test, ENetResult::Invalid, SpanResult, "Read on an invalid source must return Invalid");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xFF), Destination[0], "Read must not modify the destination on an invalid source");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xFF), Destination[1], "Read must not modify the destination on an invalid source");
}

/** Proves a valid empty {nullptr, 0} reader reports an empty suffix view without null pointer arithmetic. */
MW_TEST_CASE(ByteReaderValidEmptySourceReturnsEmptyRemainingBytes)
{
	FByteReader Reader(TSpan<const std::uint8_t>(nullptr, 0));

	// A valid empty reader must be observable without dereferencing null.
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Reader.Capacity(), "A valid empty source must report zero capacity");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Reader.Position(), "A valid empty source must start at zero position");
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), Reader.Remaining(), "A valid empty source must report zero remaining bytes");
	const TSpan<const std::uint8_t> EmptySuffix = Reader.RemainingBytes();
	MW_EXPECT_EQ(Test, static_cast<std::size_t>(0), EmptySuffix.Size(), "RemainingBytes must return an empty view for a valid empty source");
	MW_EXPECT_EQ(
		Test,
		true,
		EmptySuffix.Data() == nullptr,
		"RemainingBytes must report a null data pointer for a valid empty source, never a computed non-null base");

	// Consuming operations must still return Invalid because no byte remains, without dereferencing null.
	std::uint8_t OutByte = 0xEE;
	MW_EXPECT_EQ(Test, ENetResult::Invalid, Reader.ReadByte(OutByte), "ReadByte on a valid empty source must return Invalid");
	MW_EXPECT_EQ(Test, static_cast<std::uint8_t>(0xEE), OutByte, "ReadByte must not modify its output on a valid empty source");
}

} // namespace
