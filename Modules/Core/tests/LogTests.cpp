#include "TestSupport.h"

#include <MicroWorld/Log.h>

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

/** Proves a message-only record forwards its level, category, and text verbatim. */
MW_TEST_CASE(Log_MessageOnlyOutputDeviceReceivesLevelCategoryAndText)
{
	ResetCapture();
	SetOutputDevice(&CaptureLogRecord);

	MW_LOG_MSG(Warning, "Boot", "ready");

	const bool bCategoryMatches = GCapture.Category != nullptr && std::strcmp(GCapture.Category, "Boot") == 0;
	const bool bMessageMatches = std::strcmp(GCapture.Message, "ready") == 0;
	MW_EXPECT_EQ(Test, 1, GCapture.CallCount, "One message-only call should route once");
	MW_EXPECT_EQ(Test, ELogLevel::Warning, GCapture.Level, "Output device should receive the call-site level");
	MW_EXPECT_TRUE(Test, bCategoryMatches, "Output device should receive the call-site category");
	MW_EXPECT_TRUE(Test, bMessageMatches, "Output device should receive the message unchanged");
}

/** Proves the printf-style macro expands its arguments into the routed message. */
MW_TEST_CASE(Log_FormattedRecordExpandsPrintfArguments)
{
	ResetCapture();
	SetOutputDevice(&CaptureLogRecord);

	MW_LOG(Warning, "Net", "peer %u timed out", 7u);

	const bool bMessageMatches = std::strcmp(GCapture.Message, "peer 7 timed out") == 0;
	MW_EXPECT_EQ(Test, 1, GCapture.CallCount, "One formatted call should route once");
	MW_EXPECT_TRUE(Test, bMessageMatches, "Formatted message should expand printf arguments");
}

/** Proves a null output device drops records without crashing and can be reinstalled. */
MW_TEST_CASE(Log_NullOutputDeviceDropsRecordsThenReinstallRoutes)
{
	ResetCapture();
	SetOutputDevice(nullptr);

	MW_LOG_MSG(Error, "Boot", "dropped");
	MW_LOG(Error, "Boot", "dropped %d", 1);

	MW_EXPECT_EQ(Test, 0, GCapture.CallCount, "Null output device should route nothing");

	SetOutputDevice(&CaptureLogRecord);
	MW_LOG_MSG(Error, "Boot", "kept");

	const bool bMessageMatches = std::strcmp(GCapture.Message, "kept") == 0;
	MW_EXPECT_EQ(Test, 1, GCapture.CallCount, "Reinstalled output device should route again");
	MW_EXPECT_TRUE(Test, bMessageMatches, "Reinstalled output device should receive the record");
}

/** Proves a below-floor printf call is stripped and never evaluates its arguments. */
MW_TEST_CASE(Log_BelowFloorFormattedCallStripsArgumentEvaluation)
{
	ResetCapture();
	SetOutputDevice(&CaptureLogRecord);

	MW_LOG(Verbose, "Detail", "value=%d", EvaluatedInteger());

	MW_EXPECT_EQ(Test, 0, GArgumentEvaluations, "Below-floor call must not evaluate its arguments");
	MW_EXPECT_EQ(Test, 0, GCapture.CallCount, "Below-floor call must not reach the output device");

	MW_LOG(Log, "Detail", "value=%d", EvaluatedInteger());

	const bool bMessageMatches = std::strcmp(GCapture.Message, "value=42") == 0;
	MW_EXPECT_EQ(Test, 1, GArgumentEvaluations, "At-floor call should evaluate its arguments once");
	MW_EXPECT_EQ(Test, 1, GCapture.CallCount, "At-floor call should reach the output device");
	MW_EXPECT_TRUE(Test, bMessageMatches, "At-floor call should format the evaluated argument");
}

/** Proves a below-floor message call is stripped and never evaluates its argument. */
MW_TEST_CASE(Log_BelowFloorMessageCallStripsArgumentEvaluation)
{
	ResetCapture();
	SetOutputDevice(&CaptureLogRecord);

	MW_LOG_MSG(Verbose, "Detail", EvaluatedMessage());

	MW_EXPECT_EQ(Test, 0, GArgumentEvaluations, "Below-floor message call must not evaluate its argument");
	MW_EXPECT_EQ(Test, 0, GCapture.CallCount, "Below-floor message call must not reach the output device");

	MW_LOG_MSG(Log, "Detail", EvaluatedMessage());

	const bool bMessageMatches = std::strcmp(GCapture.Message, "probe") == 0;
	MW_EXPECT_EQ(Test, 1, GArgumentEvaluations, "At-floor message call should evaluate its argument once");
	MW_EXPECT_EQ(Test, 1, GCapture.CallCount, "At-floor message call should reach the output device");
	MW_EXPECT_TRUE(Test, bMessageMatches, "At-floor message call should route the evaluated string");
}

/** Proves every at-or-above-floor level routes while the below-floor level is stripped. */
MW_TEST_CASE(Log_FloorRoutesImportantLevelsAndStripsVerbose)
{
	ResetCapture();
	SetOutputDevice(&CaptureLogRecord);

	MW_LOG_MSG(Error, "Level", "error");
	MW_LOG_MSG(Warning, "Level", "warning");
	MW_LOG_MSG(Log, "Level", "log");
	MW_LOG_MSG(Verbose, "Level", "verbose");

	const bool bLastMessageMatches = std::strcmp(GCapture.Message, "log") == 0;
	MW_EXPECT_EQ(Test, 3, GCapture.CallCount, "Error, Warning, and Log route; Verbose is stripped");
	MW_EXPECT_EQ(Test, ELogLevel::Log, GCapture.Level, "Last routed record should be the Log-level call");
	MW_EXPECT_TRUE(Test, bLastMessageMatches, "Stripped Verbose call should not overwrite the last record");
}

} // namespace MicroWorld::Tests
