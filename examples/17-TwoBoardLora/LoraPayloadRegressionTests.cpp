#include "LoraPayloadRegression.h"
#include "TestSupport.h"

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace
{

/**
 * Scenario: Query every payload-regression boundary through the shared helper.
 * Expected: The wire contract remains exactly empty zero bytes, typical five bytes, and maximum 58 bytes.
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
 * Scenario: Fill a maximum-size payload using the shared canonical pattern.
 * Expected: Representative first, middle, and final bytes remain stable for cross-board comparison.
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
 * Scenario: Validate an intact canonical maximum payload with the exact wire length.
 * Expected: The shared validator accepts the pattern used by both board roles.
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
 * Scenario: Validate canonical payload bytes with wire lengths one byte away from each supported boundary.
 * Expected: The shared validator rejects every underlength and overlength packet before reading its contents.
 */
MW_TEST_CASE(LoraPayloadRegressionRejectsCanonicalPayloadsWithMismatchedLengths)
{
	/** Pairs one payload shape with an invalid wire length and its diagnostic contract. */
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
 * Scenario: Validate a maximum payload after one canonical byte is changed.
 * Expected: The shared validator rejects corrupted wire content before protocol advancement.
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
