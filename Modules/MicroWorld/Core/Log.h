#pragma once

#include <cstdint>

/**
 * Motivation: Gives the whole process one logging facade that gates levels in the preprocessor,
 *   formats without a heap, and routes everything through one process-global output device.
 * Responsibilities: Strip below-floor call sites to nothing, format each enabled record into a
 *   fixed-size caller-stack buffer with no exceptions or hidden clock, and forward only to the
 *   single installed device (a null device disables logging).
 */

// Compile-time severity ranks. Lower value = more important. The preprocessor
// uses these to strip below-floor call sites; the enum mirrors them for the
// output-device signature.
#define MW_LOG_LEVEL_Error 0
#define MW_LOG_LEVEL_Warning 1
#define MW_LOG_LEVEL_Log 2
#define MW_LOG_LEVEL_Verbose 3

// Compile-time floor: call sites whose level rank is greater than this value are
// stripped entirely. Override on the command line (e.g.
// -DMW_LOG_MIN_LEVEL=MW_LOG_LEVEL_Verbose) before including this header.
#ifndef MW_LOG_MIN_LEVEL
#define MW_LOG_MIN_LEVEL MW_LOG_LEVEL_Log
#endif

// Maximum formatted-message length, including the terminating null, written to
// the per-call stack buffer. Longer messages are safely truncated by vsnprintf.
// Override to trade stack footprint against message length.
#ifndef MW_LOG_MESSAGE_CAPACITY
#define MW_LOG_MESSAGE_CAPACITY 128
#endif

// Per-level enable flags resolved at preprocessing time. A level is enabled when
// its rank is at least as important as the configured floor.
#if MW_LOG_LEVEL_Error <= MW_LOG_MIN_LEVEL
#define MW_LOG_ENABLED_Error 1
#else
#define MW_LOG_ENABLED_Error 0
#endif

#if MW_LOG_LEVEL_Warning <= MW_LOG_MIN_LEVEL
#define MW_LOG_ENABLED_Warning 1
#else
#define MW_LOG_ENABLED_Warning 0
#endif

#if MW_LOG_LEVEL_Log <= MW_LOG_MIN_LEVEL
#define MW_LOG_ENABLED_Log 1
#else
#define MW_LOG_ENABLED_Log 0
#endif

#if MW_LOG_LEVEL_Verbose <= MW_LOG_MIN_LEVEL
#define MW_LOG_ENABLED_Verbose 1
#else
#define MW_LOG_ENABLED_Verbose 0
#endif

// printf-format checking on the variadic dispatch helper, where available.
#if defined(__GNUC__) || defined(__clang__)
#define MW_LOG_PRINTF_FORMAT __attribute__((format(printf, 3, 4)))
#else
#define MW_LOG_PRINTF_FORMAT
#endif

namespace MicroWorld::Core
{

/**
 * Motivation: Ranks each log record by importance so a compile-time floor can strip the rest.
 * Responsibilities: Carry one severity rank the preprocessor and output device both understand.
 * Example:
 *   ELogLevel Level = ELogLevel::Warning;
 */
enum class ELogLevel : std::uint8_t
{
	Error = MW_LOG_LEVEL_Error,		///< Motivation: Reports an unrecoverable fault the caller must handle.
	Warning = MW_LOG_LEVEL_Warning, ///< Motivation: Reports a recoverable anomaly worth surfacing.
	Log = MW_LOG_LEVEL_Log,			///< Motivation: Reports ordinary operational milestones.
	Verbose = MW_LOG_LEVEL_Verbose, ///< Motivation: Reports fine-grained detail usually stripped in release.
};

/** Motivation: Names the plain function pointer that receives one fully formed record from the one installed output device. */
using FOutputDeviceFunction = void (*)(ELogLevel InLevel, const char* InCategory, const char* InMessage);

/**
 * Motivation: Lets startup install the one process-global output device that all dispatch uses.
 * Responsibilities: Store InOutputDevice, accepting nullptr to disable logging.
 */
void SetOutputDevice(FOutputDeviceFunction InOutputDevice) noexcept;

/**
 * Motivation: Lets a caller forward an already-formed message without printf interpretation.
 * Responsibilities: Deliver the record to the installed device, doing nothing when none is set.
 */
void DispatchLogMessage(ELogLevel InLevel, const char* InCategory, const char* InMessage) noexcept;

/**
 * Motivation: Lets a caller format a printf-style record into a bounded buffer before delivery.
 * Responsibilities: Format into a fixed-size stack buffer and forward to the device, skipping the work when none is set.
 */
void DispatchLogFormatted(ELogLevel InLevel, const char* InCategory, const char* InFormat, ...) noexcept MW_LOG_PRINTF_FORMAT;

} // namespace MicroWorld::Core

// Two-step paste so the level's enable flag expands before it selects an emitter.
#define MW_LOG_CONCAT_(Prefix, Suffix) Prefix##Suffix
#define MW_LOG_CONCAT(Prefix, Suffix) MW_LOG_CONCAT_(Prefix, Suffix)

// A below-floor call drops its arguments UNEVALUATED; an enabled one dispatches.
#define MW_LOG_EMIT_FORMATTED_0(Level, Category, ...) ((void)0)
#define MW_LOG_EMIT_FORMATTED_1(Level, Category, ...) \
	::MicroWorld::Core::DispatchLogFormatted(::MicroWorld::Core::ELogLevel::Level, (Category), __VA_ARGS__)

#define MW_LOG_EMIT_MESSAGE_0(Level, Category, Message) ((void)0)
#define MW_LOG_EMIT_MESSAGE_1(Level, Category, Message) \
	::MicroWorld::Core::DispatchLogMessage(::MicroWorld::Core::ELogLevel::Level, (Category), (Message))

/**
 * Motivation: Lets a call site emit one printf-style record at a level and category while the
 *   preprocessor decides whether it survives the configured floor.
 * Responsibilities: Expand an enabled level to a DispatchLogFormatted call and a below-floor level to nothing,
 *   never evaluating its arguments when stripped.
 */
#define MW_LOG(Level, Category, ...) MW_LOG_CONCAT(MW_LOG_EMIT_FORMATTED_, MW_LOG_CONCAT(MW_LOG_ENABLED_, Level))(Level, Category, __VA_ARGS__)

/**
 * Motivation: Lets a call site emit an already-formed message string without printf interpretation.
 * Responsibilities: Expand an enabled level to a DispatchLogMessage call and a below-floor level to nothing,
 *   never evaluating its arguments when stripped.
 */
#define MW_LOG_MSG(Level, Category, Message) MW_LOG_CONCAT(MW_LOG_EMIT_MESSAGE_, MW_LOG_CONCAT(MW_LOG_ENABLED_, Level))(Level, Category, Message)
