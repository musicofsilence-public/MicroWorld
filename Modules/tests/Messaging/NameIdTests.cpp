#include "TestSupport.h"

#include <MicroWorld/Messaging/NameId.h>

#include <cstdint>
#include <type_traits>

namespace
{

using MicroWorld::Messaging::FNameId;
using MicroWorld::Messaging::InvalidNameId;
using MicroWorld::Messaging::MakeNameId;

static_assert(MakeNameId("SensorA") == MakeNameId("SensorA"));
static_assert(std::is_constructible_v<FNameId, std::uint32_t>);
static_assert(!std::is_convertible_v<std::uint32_t, FNameId>);

/**
 * Motivation: Keeps string-literal call sites readable while preserving one canonical hash implementation.
 * Responsibilities: Confirm an implicitly converted literal equals the explicit hashing helper result.
 */
MW_TEST_CASE(NameId_ImplicitLiteralConversionMatchesMakeNameId)
{
	// Arrange
	const FNameId LiteralNameId = "SensorA";

	// Act
	const FNameId HashedNameId = MakeNameId("SensorA");

	// Assert
	MW_EXPECT_EQ(Test, HashedNameId, LiteralNameId, "A literal conversion should use MakeNameId's hash");
}

/**
 * Motivation: Gives default-constructed messaging values one explicit unset identity.
 * Responsibilities: Confirm the default FNameId equals the public invalid value.
 */
MW_TEST_CASE(NameId_DefaultValueEqualsInvalidNameId)
{
	// Arrange
	const FNameId DefaultNameId{};

	// Act
	const bool bIsInvalid = DefaultNameId == InvalidNameId;

	// Assert
	MW_EXPECT_TRUE(Test, bIsInvalid, "A default FNameId should equal InvalidNameId");
}

/**
 * Motivation: Lets independent call sites use the same readable name without coordinating stored ids.
 * Responsibilities: Confirm repeated calls for one name produce the same id through the public API.
 */
MW_TEST_CASE(NameId_ReturnsTheSameIdForRepeatedNames)
{
	// Arrange
	const char* const Name = "SensorA";

	// Act
	const FNameId FirstNameId = MakeNameId(Name);
	const FNameId SecondNameId = MakeNameId(Name);

	// Assert
	MW_EXPECT_EQ(Test, FirstNameId, SecondNameId, "Repeated calls for the same name should produce the same id");
}

/**
 * Motivation: Allows optional caller-supplied names without making null pointers a special runtime error path.
 * Responsibilities: Confirm null and empty names use and equal the FNV-1a offset basis through the public API.
 */
MW_TEST_CASE(NameId_NullAndEmptyNamesUseTheOffsetBasis)
{
	// Arrange
	constexpr FNameId ExpectedNameId{2166136261u};

	// Act
	const FNameId NullNameId = MakeNameId(nullptr);
	const FNameId EmptyNameId = MakeNameId("");

	// Assert
	MW_EXPECT_EQ(Test, ExpectedNameId, NullNameId, "A null name should use the FNV-1a offset basis");
	MW_EXPECT_EQ(Test, ExpectedNameId, EmptyNameId, "An empty name should use the FNV-1a offset basis");
	MW_EXPECT_EQ(Test, NullNameId, EmptyNameId, "Null and empty names should produce the same id");
}

/**
 * Motivation: Prevents related channels and messages from sharing an id when their names differ.
 * Responsibilities: Confirm the public API distinguishes SensorA from SensorB and SensorA_Temperature.
 */
MW_TEST_CASE(NameId_DistinguishesRelatedNames)
{
	// Arrange
	const char* const SensorA = "SensorA";
	const char* const SensorB = "SensorB";
	const char* const SensorATemperature = "SensorA_Temperature";

	// Act
	const FNameId SensorANameId = MakeNameId(SensorA);
	const FNameId SensorBNameId = MakeNameId(SensorB);
	const FNameId SensorATemperatureNameId = MakeNameId(SensorATemperature);

	// Assert
	MW_EXPECT_TRUE(Test, SensorANameId != SensorBNameId, "SensorA and SensorB should produce distinct ids");
	MW_EXPECT_TRUE(Test, SensorANameId != SensorATemperatureNameId, "SensorA and SensorA_Temperature should produce distinct ids");
	MW_EXPECT_TRUE(Test, SensorBNameId != SensorATemperatureNameId, "SensorB and SensorA_Temperature should produce distinct ids");
}

/**
 * Motivation: Keeps names case-sensitive so distinct protocol names cannot silently merge.
 * Responsibilities: Confirm the public API distinguishes sensora from SensorA.
 */
MW_TEST_CASE(NameId_DistinguishesCaseVariants)
{
	// Arrange
	const char* const LowercaseName = "sensora";
	const char* const MixedCaseName = "SensorA";

	// Act
	const FNameId LowercaseNameId = MakeNameId(LowercaseName);
	const FNameId MixedCaseNameId = MakeNameId(MixedCaseName);

	// Assert
	MW_EXPECT_TRUE(Test, LowercaseNameId != MixedCaseNameId, "sensora and SensorA should produce distinct ids");
}

/**
 * Motivation: Preserves distinct ids for long names that differ only at their final byte.
 * Responsibilities: Confirm the public API includes every byte before the null terminator in the computed id.
 */
MW_TEST_CASE(NameId_DistinguishesLongNamesThatDifferOnlyInTheFinalCharacter)
{
	// Arrange
	const char* const FirstLongName = "MicroWorldMessagingChannelForSensorTelemetryAndReliableUpdatesA";
	const char* const SecondLongName = "MicroWorldMessagingChannelForSensorTelemetryAndReliableUpdatesB";

	// Act
	const FNameId FirstLongNameId = MakeNameId(FirstLongName);
	const FNameId SecondLongNameId = MakeNameId(SecondLongName);

	// Assert
	MW_EXPECT_TRUE(Test, FirstLongNameId != SecondLongNameId, "Long names differing only in their final character should produce distinct ids");
}

} // namespace
