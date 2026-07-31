#include "LoraPayloadRegression.h"
#include "TestSupport.h"

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace
{

/**
 * Motivation: Confirms the shared helper reports the exact payload boundary for each regression case.
 * Responsibilities: Query empty, typical, and maximum boundaries and assert the wire contract byte counts.
 */
MW_TEST_CASE(LoraPayloadRegressionUsesExactPayloadBoundaries)
{
	// Act
	const std::size_t EmptyPayloadBytes = MicroWorld::Example17::PayloadRegressionByteCount(MicroWorld::Example17::EPayloadRegressionCase::Empty);
	const std::size_t TypicalPayloadBytes = MicroWorld::Example17::PayloadRegressionByteCount(MicroWorld::Example17::EPayloadRegressionCase::Typical);
	const std::size_t MaximumPayloadBytes = MicroWorld::Example17::PayloadRegressionByteCount(MicroWorld::Example17::EPayloadRegressionCase::Maximum);

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{0}, EmptyPayloadBytes, "Empty payload boundary must remain exactly zero bytes");
	MW_EXPECT_EQ(Test, std::size_t{5}, TypicalPayloadBytes, "Typical payload boundary must remain exactly five bytes");
	MW_EXPECT_EQ(Test, std::size_t{58}, MaximumPayloadBytes, "Maximum payload boundary must remain exactly 58 bytes");
}

/**
 * Motivation: Confirms the canonical fill produces a stable maximum-size pattern for cross-board comparison.
 * Responsibilities: Fill a maximum payload and assert representative first, middle, and final bytes.
 */
MW_TEST_CASE(LoraPayloadRegressionFillsCanonicalMaximumBytes)
{
	// Arrange
	std::uint8_t Payload[MicroWorld::Transport::E32MaxPayloadBytes]{};

	// Act
	MicroWorld::Example17::FillCanonicalPayload(MicroWorld::Example17::EPayloadRegressionCase::Maximum, Payload);

	// Assert
	const std::uint8_t FirstPayloadByte = Payload[0];
	const std::uint8_t MiddlePayloadByte = Payload[29];
	const std::uint8_t FinalPayloadByte = Payload[57];
	MW_EXPECT_EQ(Test, std::uint8_t{0xA5}, FirstPayloadByte, "First maximum payload byte must match the canonical pattern");
	MW_EXPECT_EQ(Test, std::uint8_t{0xEE}, MiddlePayloadByte, "Middle maximum payload byte must match the canonical pattern");
	MW_EXPECT_EQ(Test, std::uint8_t{0x1A}, FinalPayloadByte, "Final maximum payload byte must match the canonical pattern");
}

/**
 * Motivation: Confirms the shared validator accepts an intact canonical maximum payload at the exact wire length.
 * Responsibilities: Fill a maximum payload and assert the validator accepts the pattern both roles use.
 */
MW_TEST_CASE(LoraPayloadRegressionAcceptsIntactMaximumPayload)
{
	// Arrange
	std::uint8_t Payload[MicroWorld::Transport::E32MaxPayloadBytes]{};
	MicroWorld::Example17::FillCanonicalPayload(MicroWorld::Example17::EPayloadRegressionCase::Maximum, Payload);

	// Act
	const bool bIsCanonicalPayload =
		MicroWorld::Example17::IsCanonicalPayload(MicroWorld::Example17::EPayloadRegressionCase::Maximum, Payload, std::size(Payload));

	// Assert
	MW_EXPECT_TRUE(Test, bIsCanonicalPayload, "Intact 58-byte payload must pass canonical validation");
}

/**
 * Motivation: Confirms the validator rejects canonical payloads whose wire length is one byte off any boundary.
 * Responsibilities: Exercise every boundary with an underlength and overlength packet and assert rejection.
 */
MW_TEST_CASE(LoraPayloadRegressionRejectsCanonicalPayloadsWithMismatchedLengths)
{
	/**
	 * Motivation: Pairs one payload shape with an invalid wire length so each mismatch case carries its own
	 *   diagnostic contract.
	 * Responsibilities: Hold the case, the invalid received byte count, and the failure message together.
	 * Example:
	 *   FLengthMismatchCase Case{EPayloadRegressionCase::Typical, 4, "four-byte typical must fail"};
	 */
	struct FLengthMismatchCase
	{
		MicroWorld::Example17::EPayloadRegressionCase PayloadCase;
		std::size_t ReceivedBytes;
		const char* FailureMessage;
	};

	// Exercise every supported payload boundary without maintaining separate mutable fixtures.
	constexpr FLengthMismatchCase LengthMismatchCases[] = {
		{MicroWorld::Example17::EPayloadRegressionCase::Empty, std::size_t{1}, "One-byte empty payload must fail canonical validation"},
		{MicroWorld::Example17::EPayloadRegressionCase::Typical, std::size_t{4}, "Four-byte typical payload must fail canonical validation"},
		{MicroWorld::Example17::EPayloadRegressionCase::Typical, std::size_t{6}, "Six-byte typical payload must fail canonical validation"},
		{MicroWorld::Example17::EPayloadRegressionCase::Maximum, std::size_t{57}, "57-byte maximum payload must fail canonical validation"},
	};
	std::uint8_t Payload[MicroWorld::Transport::E32MaxPayloadBytes]{};

	for (const FLengthMismatchCase& LengthMismatchCase : LengthMismatchCases)
	{
		// Arrange
		MicroWorld::Example17::FillCanonicalPayload(LengthMismatchCase.PayloadCase, Payload);

		// Act
		const bool bLengthMismatchRejected =
			!MicroWorld::Example17::IsCanonicalPayload(LengthMismatchCase.PayloadCase, Payload, LengthMismatchCase.ReceivedBytes);

		// Assert
		MW_EXPECT_TRUE(Test, bLengthMismatchRejected, LengthMismatchCase.FailureMessage);
	}
}

/**
 * Motivation: Confirms the validator rejects a maximum payload after one canonical byte is corrupted.
 * Responsibilities: Flip one byte of a canonical maximum payload and assert the validator rejects it.
 */
MW_TEST_CASE(LoraPayloadRegressionRejectsCorruptedMaximumPayload)
{
	// Arrange
	std::uint8_t Payload[MicroWorld::Transport::E32MaxPayloadBytes]{};
	MicroWorld::Example17::FillCanonicalPayload(MicroWorld::Example17::EPayloadRegressionCase::Maximum, Payload);
	Payload[29] ^= std::uint8_t{0x01};

	// Act
	const bool bCorruptedPayloadRejected =
		!MicroWorld::Example17::IsCanonicalPayload(MicroWorld::Example17::EPayloadRegressionCase::Maximum, Payload, std::size(Payload));

	// Assert
	MW_EXPECT_TRUE(Test, bCorruptedPayloadRejected, "Corrupted maximum payload must fail canonical validation");
}

} // namespace
