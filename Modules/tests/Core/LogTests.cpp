#include "TestSupport.h"

#include <MicroWorld/Core/Log.h>

#include <cstdio>
#include <cstring>

// These tests run under the default compile-time floor (MW_LOG_MIN_LEVEL == Log),
// so Error/Warning/Log call sites are live and Verbose call sites are stripped.

namespace MicroWorld::Tests
{

namespace
{

	/** Capacity of the bounded captured-message copy in bytes. */
	constexpr std::size_t CapturedMessageByteCount = 64;

	/** Marker value the evaluated-integer probe returns, so the formatted-message test can assert it. */
	constexpr int EvaluatedIntegerMarker = 42;

	/** Captures the last record the output device received without dynamic storage. */
	struct FLogCapture
	{
		/** Counts output device invocations so stripped or dropped calls are observable. */
		int CallCount{0};

		/** Records the level of the most recent routed record. */
		ELogLevel Level{ELogLevel::Log};

		/** Records the category pointer of the most recent routed record. */
		const char* Category{nullptr};

		/** Owns a bounded copy of the message, since formatting buffers are transient. */
		char Message[CapturedMessageByteCount]{};
	};

	/** Holds the single capture the function-pointer output device writes into. */
	FLogCapture GCapture{};

	/** Counts side effects in log arguments so stripped calls prove non-evaluation. */
	int GArgumentEvaluations{0};

	/** Records one routed log record into the shared capture. */
	void CaptureLogRecord(ELogLevel InLevel, const char* InCategory, const char* InMessage)
	{
		++GCapture.CallCount;
		GCapture.Level = InLevel;
		GCapture.Category = InCategory;
		std::snprintf(GCapture.Message, sizeof(GCapture.Message), "%s", InMessage);
	}

	/** Returns a marker integer while recording that the argument was evaluated. */
	int EvaluatedInteger()
	{
		++GArgumentEvaluations;
		return EvaluatedIntegerMarker;
	}

	/** Returns a marker string while recording that the argument was evaluated. */
	const char* EvaluatedMessage()
	{
		++GArgumentEvaluations;
		return "probe";
	}

	/** Clears shared capture and evaluation counters before one test observes them. */
	void ResetCapture() noexcept
	{
		GCapture = FLogCapture{};
		GArgumentEvaluations = 0;
	}

} // namespace

/**
 * Scenario: Route a message-only Warning record through the installed output device.
 * Expected: The device receives the call once, with the matching level, category, and unchanged text.
 */
MW_TEST_CASE(Log_MessageOnlyOutputDeviceReceivesLevelCategoryAndText)
{
	// Arrange
	ResetCapture();
	SetOutputDevice(&CaptureLogRecord);

	// Act
	MW_LOG_MSG(Warning, "Boot", "ready");
	const bool bCategoryMatches = GCapture.Category != nullptr && std::strcmp(GCapture.Category, "Boot") == 0;
	const bool bMessageMatches = std::strcmp(GCapture.Message, "ready") == 0;

	// Assert
	MW_EXPECT_EQ(Test, 1, GCapture.CallCount, "One message-only call should route once");
	MW_EXPECT_EQ(Test, ELogLevel::Warning, GCapture.Level, "Output device should receive the call-site level");
	MW_EXPECT_TRUE(Test, bCategoryMatches, "Output device should receive the call-site category");
	MW_EXPECT_TRUE(Test, bMessageMatches, "Output device should receive the message unchanged");
}

/**
 * Scenario: Route a printf-style Warning call carrying one unsigned argument through the output device.
 * Expected: The device receives one record with the argument expanded into the formatted message.
 */
MW_TEST_CASE(Log_FormattedRecordExpandsPrintfArguments)
{
	// Arrange
	ResetCapture();
	SetOutputDevice(&CaptureLogRecord);

	// Act
	MW_LOG(Warning, "Net", "peer %u timed out", 7u);
	const bool bMessageMatches = std::strcmp(GCapture.Message, "peer 7 timed out") == 0;

	// Assert
	MW_EXPECT_EQ(Test, 1, GCapture.CallCount, "One formatted call should route once");
	MW_EXPECT_TRUE(Test, bMessageMatches, "Formatted message should expand printf arguments");
}

/**
 * Scenario: Log under a null output device, then reinstall a capturing device and log again.
 * Expected: The null device drops both records without crashing; the reinstalled device routes the kept record once.
 */
MW_TEST_CASE(Log_NullOutputDeviceDropsRecordsThenReinstallRoutes)
{
	// Arrange
	ResetCapture();
	SetOutputDevice(nullptr);

	// Act
	MW_LOG_MSG(Error, "Boot", "dropped");
	MW_LOG(Error, "Boot", "dropped %d", 1);

	// Assert
	MW_EXPECT_EQ(Test, 0, GCapture.CallCount, "Null output device should route nothing");

	// Act
	SetOutputDevice(&CaptureLogRecord);
	MW_LOG_MSG(Error, "Boot", "kept");
	const bool bMessageMatches = std::strcmp(GCapture.Message, "kept") == 0;

	// Assert
	MW_EXPECT_EQ(Test, 1, GCapture.CallCount, "Reinstalled output device should route again");
	MW_EXPECT_TRUE(Test, bMessageMatches, "Reinstalled output device should receive the record");
}

/**
 * Scenario: Issue a below-floor formatted call and then an at-floor formatted call, both using a side-effecting argument.
 * Expected: The below-floor call is stripped and evaluates nothing; the at-floor call evaluates its argument once and routes the formatted record.
 */
MW_TEST_CASE(Log_BelowFloorFormattedCallStripsArgumentEvaluation)
{
	// Arrange
	ResetCapture();
	SetOutputDevice(&CaptureLogRecord);

	// Act
	MW_LOG(Verbose, "Detail", "value=%d", EvaluatedInteger());

	// Assert
	MW_EXPECT_EQ(Test, 0, GArgumentEvaluations, "Below-floor call must not evaluate its arguments");
	MW_EXPECT_EQ(Test, 0, GCapture.CallCount, "Below-floor call must not reach the output device");

	// Act
	MW_LOG(Log, "Detail", "value=%d", EvaluatedInteger());
	const bool bMessageMatches = std::strcmp(GCapture.Message, "value=42") == 0;

	// Assert
	MW_EXPECT_EQ(Test, 1, GArgumentEvaluations, "At-floor call should evaluate its arguments once");
	MW_EXPECT_EQ(Test, 1, GCapture.CallCount, "At-floor call should reach the output device");
	MW_EXPECT_TRUE(Test, bMessageMatches, "At-floor call should format the evaluated argument");
}

/**
 * Scenario: Issue a below-floor message call and then an at-floor message call, both using a side-effecting argument.
 * Expected: The below-floor call is stripped and evaluates nothing; the at-floor call evaluates its argument once and routes the evaluated string.
 */
MW_TEST_CASE(Log_BelowFloorMessageCallStripsArgumentEvaluation)
{
	// Arrange
	ResetCapture();
	SetOutputDevice(&CaptureLogRecord);

	// Act
	MW_LOG_MSG(Verbose, "Detail", EvaluatedMessage());

	// Assert
	MW_EXPECT_EQ(Test, 0, GArgumentEvaluations, "Below-floor message call must not evaluate its argument");
	MW_EXPECT_EQ(Test, 0, GCapture.CallCount, "Below-floor message call must not reach the output device");

	// Act
	MW_LOG_MSG(Log, "Detail", EvaluatedMessage());
	const bool bMessageMatches = std::strcmp(GCapture.Message, "probe") == 0;

	// Assert
	MW_EXPECT_EQ(Test, 1, GArgumentEvaluations, "At-floor message call should evaluate its argument once");
	MW_EXPECT_EQ(Test, 1, GCapture.CallCount, "At-floor message call should reach the output device");
	MW_EXPECT_TRUE(Test, bMessageMatches, "At-floor message call should route the evaluated string");
}

/**
 * Scenario: Log at Error, Warning, Log, and Verbose levels in sequence under the compile-time floor.
 * Expected: The at-or-above-floor levels route once each and the below-floor Verbose level is stripped.
 */
MW_TEST_CASE(Log_FloorRoutesImportantLevelsAndStripsVerbose)
{
	// Arrange
	ResetCapture();
	SetOutputDevice(&CaptureLogRecord);

	// Act
	MW_LOG_MSG(Error, "Level", "error");
	MW_LOG_MSG(Warning, "Level", "warning");
	MW_LOG_MSG(Log, "Level", "log");
	MW_LOG_MSG(Verbose, "Level", "verbose");
	const bool bLastMessageMatches = std::strcmp(GCapture.Message, "log") == 0;

	// Assert
	MW_EXPECT_EQ(Test, 3, GCapture.CallCount, "Error, Warning, and Log route; Verbose is stripped");
	MW_EXPECT_EQ(Test, ELogLevel::Log, GCapture.Level, "Last routed record should be the Log-level call");
	MW_EXPECT_TRUE(Test, bLastMessageMatches, "Stripped Verbose call should not overwrite the last record");
}

} // namespace MicroWorld::Tests
